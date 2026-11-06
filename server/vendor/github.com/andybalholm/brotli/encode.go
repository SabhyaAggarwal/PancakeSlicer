package brotli

import (
	"io"
	"math"
)

/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

const minWindowBits = 10

const maxWindowBits = 24

const largeMaxWindowBits = 30

const minInputBlockBits = 16

const maxInputBlockBits = 24

const minQuality = 0

const maxQuality = 11

const (
	modeGeneric = 0
	modeText    = 1
	modeFont    = 2
)

const defaultQuality = 11

const defaultWindow = 22

const defaultMode = modeGeneric

const (
	operationProcess      = 0
	operationFlush        = 1
	operationFinish       = 2
	operationEmitMetadata = 3
)

const (
	streamProcessing     = 0
	streamFlushRequested = 1
	streamFinished       = 2
	streamMetadataHead   = 3
	streamMetadataBody   = 4
)

type Writer struct {
	dst     io.Writer
	options WriterOptions
	err     error

	params              encoderParams
	hasher_             hasherHandle
	input_pos_          uint64
	ringbuffer_         ringBuffer
	commands            []command
	num_literals_       uint
	last_insert_len_    uint
	last_flush_pos_     uint64
	last_processed_pos_ uint64
	dist_cache_         [numDistanceShortCodes]int
	saved_dist_cache_   [4]int
	last_bytes_         uint16
	last_bytes_bits_    byte
	prev_byte_          byte
	prev_byte2_         byte
	storage             []byte
	small_table_        [1 << 10]int
	large_table_        []int
	large_table_size_   uint
	cmd_depths_         [128]byte
	cmd_bits_           [128]uint16
	cmd_code_           [512]byte
	cmd_code_numbits_   uint
	command_buf_        []uint32
	literal_buf_        []byte
	tiny_buf_           struct {
		u64 [2]uint64
		u8  [16]byte
	}
	remaining_metadata_bytes_ uint32
	stream_state_             int
	is_last_block_emitted_    bool
	is_initialized_           bool
}

func inputBlockSize(s *Writer) uint {
	return uint(1) << uint(s.params.lgblock)
}

func unprocessedInputSize(s *Writer) uint64 {
	return s.input_pos_ - s.last_processed_pos_
}

func remainingInputBlockSize(s *Writer) uint {
	var delta uint64 = unprocessedInputSize(s)
	var block_size uint = inputBlockSize(s)
	if delta >= uint64(block_size) {
		return 0
	}
	return block_size - uint(delta)
}

func wrapPosition(position uint64) uint32 {
	var result uint32 = uint32(position)
	var gb uint64 = position >> 30
	if gb > 2 {

		result = result&((1<<30)-1) | (uint32((gb-1)&1)+1)<<30
	}

	return result
}

func (s *Writer) getStorage(size int) []byte {
	if len(s.storage) < size {
		s.storage = make([]byte, size)
	}

	return s.storage
}

func hashTableSize(max_table_size uint, input_size uint) uint {
	var htsize uint = 256
	for htsize < max_table_size && htsize < input_size {
		htsize <<= 1
	}

	return htsize
}

func getHashTable(s *Writer, quality int, input_size uint, table_size *uint) []int {
	var max_table_size uint = maxHashTableSize(quality)
	var htsize uint = hashTableSize(max_table_size, input_size)

	var table []int
	assert(max_table_size >= 256)
	if quality == fastOnePassCompressionQuality {

		if htsize&0xAAAAA == 0 {
			htsize <<= 1
		}
	}

	if htsize <= uint(len(s.small_table_)) {
		table = s.small_table_[:]
	} else {
		if htsize > s.large_table_size_ {
			s.large_table_size_ = htsize
			s.large_table_ = nil
			s.large_table_ = make([]int, htsize)
		}

		table = s.large_table_
	}

	*table_size = htsize
	for i := 0; i < int(htsize); i++ {
		table[i] = 0
	}
	return table
}

func encodeWindowBits(lgwin int, large_window bool, last_bytes *uint16, last_bytes_bits *byte) {
	if large_window {
		*last_bytes = uint16((lgwin&0x3F)<<8 | 0x11)
		*last_bytes_bits = 14
	} else {
		if lgwin == 16 {
			*last_bytes = 0
			*last_bytes_bits = 1
		} else if lgwin == 17 {
			*last_bytes = 1
			*last_bytes_bits = 7
		} else if lgwin > 17 {
			*last_bytes = uint16((lgwin-17)<<1 | 0x01)
			*last_bytes_bits = 4
		} else {
			*last_bytes = uint16((lgwin-8)<<4 | 0x01)
			*last_bytes_bits = 7
		}
	}
}

var kStaticContextMapContinuation = [64]uint32{
	1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
}
var kStaticContextMapSimpleUTF8 = [64]uint32{
	0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
}

func chooseContextMap(quality int, bigram_histo []uint32, num_literal_contexts *uint, literal_context_map *[]uint32) {
	var monogram_histo = [3]uint32{0}
	var two_prefix_histo = [6]uint32{0}
	var total uint
	var i uint
	var dummy uint
	var entropy [4]float64
	for i = 0; i < 9; i++ {
		monogram_histo[i%3] += bigram_histo[i]
		two_prefix_histo[i%6] += bigram_histo[i]
	}

	entropy[1] = shannonEntropy(monogram_histo[:], 3, &dummy)
	entropy[2] = (shannonEntropy(two_prefix_histo[:], 3, &dummy) + shannonEntropy(two_prefix_histo[3:], 3, &dummy))
	entropy[3] = 0
	for i = 0; i < 3; i++ {
		entropy[3] += shannonEntropy(bigram_histo[3*i:], 3, &dummy)
	}

	total = uint(monogram_histo[0] + monogram_histo[1] + monogram_histo[2])
	assert(total != 0)
	entropy[0] = 1.0 / float64(total)
	entropy[1] *= entropy[0]
	entropy[2] *= entropy[0]
	entropy[3] *= entropy[0]

	if quality < minQualityForHqContextModeling {

		entropy[3] = entropy[1] * 10
	}

	if entropy[1]-entropy[2] < 0.2 && entropy[1]-entropy[3] < 0.2 {
		*num_literal_contexts = 1
	} else if entropy[2]-entropy[3] < 0.02 {
		*num_literal_contexts = 2
		*literal_context_map = kStaticContextMapSimpleUTF8[:]
	} else {
		*num_literal_contexts = 3
		*literal_context_map = kStaticContextMapContinuation[:]
	}
}

var kStaticContextMapComplexUTF8 = [64]uint32{
	11, 11, 12, 12,
	0, 0, 0, 0,
	1, 1, 9, 9,
	2, 2, 2, 2,
	1, 1, 1, 1,
	8, 3, 3, 3,
	1, 1, 1, 1,
	2, 2, 2, 2,
	8, 4, 4, 4,
	8, 7, 4, 4,
	8, 0, 0, 0,
	3, 3, 3, 3,
	5, 5, 10, 5,
	5, 5, 10, 5,
	6, 6, 6, 6,
	6, 6, 6, 6,
}

func shouldUseComplexStaticContextMap(input []byte, start_pos uint, length uint, mask uint, quality int, size_hint uint, num_literal_contexts *uint, literal_context_map *[]uint32) bool {

	if size_hint < 1<<20 {
		return false
	} else {
		var end_pos uint = start_pos + length
		var combined_histo = [32]uint32{0}
		var context_histo = [13][32]uint32{[32]uint32{0}}
		var total uint32 = 0
		var entropy [3]float64
		var dummy uint
		var i uint
		var utf8_lut contextLUT = getContextLUT(contextUTF8)

		for ; start_pos+64 <= end_pos; start_pos += 4096 {
			var stride_end_pos uint = start_pos + 64
			var prev2 byte = input[start_pos&mask]
			var prev1 byte = input[(start_pos+1)&mask]
			var pos uint

			for pos = start_pos + 2; pos < stride_end_pos; pos++ {
				var literal byte = input[pos&mask]
				var context byte = byte(kStaticContextMapComplexUTF8[getContext(prev1, prev2, utf8_lut)])
				total++
				combined_histo[literal>>3]++
				context_histo[context][literal>>3]++
				prev2 = prev1
				prev1 = literal
			}
		}

		entropy[1] = shannonEntropy(combined_histo[:], 32, &dummy)
		entropy[2] = 0
		for i = 0; i < 13; i++ {
			entropy[2] += shannonEntropy(context_histo[i][0:], 32, &dummy)
		}

		entropy[0] = 1.0 / float64(total)
		entropy[1] *= entropy[0]
		entropy[2] *= entropy[0]

		if entropy[2] > 3.0 || entropy[1]-entropy[2] < 0.2 {
			return false
		} else {
			*num_literal_contexts = 13
			*literal_context_map = kStaticContextMapComplexUTF8[:]
			return true
		}
	}
}

func decideOverLiteralContextModeling(input []byte, start_pos uint, length uint, mask uint, quality int, size_hint uint, num_literal_contexts *uint, literal_context_map *[]uint32) {
	if quality < minQualityForContextModeling || length < 64 {
		return
	} else if shouldUseComplexStaticContextMap(input, start_pos, length, mask, quality, size_hint, num_literal_contexts, literal_context_map) {
	} else {
		var end_pos uint = start_pos + length

		var bigram_prefix_histo = [9]uint32{0}
		for ; start_pos+64 <= end_pos; start_pos += 4096 {
			var lut = [4]int{0, 0, 1, 2}
			var stride_end_pos uint = start_pos + 64
			var prev int = lut[input[start_pos&mask]>>6] * 3
			var pos uint
			for pos = start_pos + 1; pos < stride_end_pos; pos++ {
				var literal byte = input[pos&mask]
				bigram_prefix_histo[prev+lut[literal>>6]]++
				prev = lut[literal>>6] * 3
			}
		}

		chooseContextMap(quality, bigram_prefix_histo[0:], num_literal_contexts, literal_context_map)
	}
}

func shouldCompress_encode(data []byte, mask uint, last_flush_pos uint64, bytes uint, num_literals uint, num_commands uint) bool {

	if bytes <= 2 {
		return false
	}
	if num_commands < (bytes>>8)+2 {
		if float64(num_literals) > 0.99*float64(bytes) {
			var literal_histo = [256]uint32{0}
			const kSampleRate uint32 = 13
			const kMinEntropy float64 = 7.92
			var bit_cost_threshold float64 = float64(bytes) * kMinEntropy / float64(kSampleRate)
			var t uint = uint((uint32(bytes) + kSampleRate - 1) / kSampleRate)
			var pos uint32 = uint32(last_flush_pos)
			var i uint
			for i = 0; i < t; i++ {
				literal_histo[data[pos&uint32(mask)]]++
				pos += kSampleRate
			}

			if bitsEntropy(literal_histo[:], 256) > bit_cost_threshold {
				return false
			}
		}
	}

	return true
}

func chooseContextMode(params *encoderParams, data []byte, pos uint, mask uint, length uint) int {

	if params.quality >= minQualityForHqBlockSplitting && !isMostlyUTF8(data, pos, mask, length, kMinUTF8Ratio) {
		return contextSigned
	}

	return contextUTF8
}

func writeMetaBlockInternal(data []byte, mask uint, last_flush_pos uint64, bytes uint, is_last bool, literal_context_mode int, params *encoderParams, prev_byte byte, prev_byte2 byte, num_literals uint, commands []command, saved_dist_cache []int, dist_cache []int, storage_ix *uint, storage []byte) {
	var wrapped_last_flush_pos uint32 = wrapPosition(last_flush_pos)
	var last_bytes uint16
	var last_bytes_bits byte
	var literal_context_lut contextLUT = getContextLUT(literal_context_mode)
	var block_params encoderParams = *params

	if bytes == 0 {

		writeBits(2, 3, storage_ix, storage)

		*storage_ix = (*storage_ix + 7) &^ 7
		return
	}

	if !shouldCompress_encode(data, mask, last_flush_pos, bytes, num_literals, uint(len(commands))) {

		copy(dist_cache, saved_dist_cache[:4])

		storeUncompressedMetaBlock(is_last, data, uint(wrapped_last_flush_pos), mask, bytes, storage_ix, storage)
		return
	}

	assert(*storage_ix <= 14)
	last_bytes = uint16(storage[1])<<8 | uint16(storage[0])
	last_bytes_bits = byte(*storage_ix)
	if params.quality <= maxQualityForStaticEntropyCodes {
		storeMetaBlockFast(data, uint(wrapped_last_flush_pos), bytes, mask, is_last, params, commands, storage_ix, storage)
	} else if params.quality < minQualityForBlockSplit {
		storeMetaBlockTrivial(data, uint(wrapped_last_flush_pos), bytes, mask, is_last, params, commands, storage_ix, storage)
	} else {
		mb := getMetaBlockSplit()
		if params.quality < minQualityForHqBlockSplitting {
			var num_literal_contexts uint = 1
			var literal_context_map []uint32 = nil
			if !params.disable_literal_context_modeling {
				decideOverLiteralContextModeling(data, uint(wrapped_last_flush_pos), bytes, mask, params.quality, params.size_hint, &num_literal_contexts, &literal_context_map)
			}

			buildMetaBlockGreedy(data, uint(wrapped_last_flush_pos), mask, prev_byte, prev_byte2, literal_context_lut, num_literal_contexts, literal_context_map, commands, mb)
		} else {
			buildMetaBlock(data, uint(wrapped_last_flush_pos), mask, &block_params, prev_byte, prev_byte2, commands, literal_context_mode, mb)
		}

		if params.quality >= minQualityForOptimizeHistograms {

			var num_effective_dist_codes uint32 = block_params.dist.alphabet_size
			if num_effective_dist_codes > numHistogramDistanceSymbols {
				num_effective_dist_codes = numHistogramDistanceSymbols
			}

			optimizeHistograms(num_effective_dist_codes, mb)
		}

		storeMetaBlock(data, uint(wrapped_last_flush_pos), bytes, mask, prev_byte, prev_byte2, is_last, &block_params, literal_context_mode, commands, mb, storage_ix, storage)
		freeMetaBlockSplit(mb)
	}

	if bytes+4 < *storage_ix>>3 {

		copy(dist_cache, saved_dist_cache[:4])

		storage[0] = byte(last_bytes)
		storage[1] = byte(last_bytes >> 8)
		*storage_ix = uint(last_bytes_bits)
		storeUncompressedMetaBlock(is_last, data, uint(wrapped_last_flush_pos), mask, bytes, storage_ix, storage)
	}
}

func chooseDistanceParams(params *encoderParams) {
	var distance_postfix_bits uint32 = 0
	var num_direct_distance_codes uint32 = 0

	if params.quality >= minQualityForNonzeroDistanceParams {
		var ndirect_msb uint32
		if params.mode == modeFont {
			distance_postfix_bits = 1
			num_direct_distance_codes = 12
		} else {
			distance_postfix_bits = params.dist.distance_postfix_bits
			num_direct_distance_codes = params.dist.num_direct_distance_codes
		}

		ndirect_msb = (num_direct_distance_codes >> distance_postfix_bits) & 0x0F
		if distance_postfix_bits > maxNpostfix || num_direct_distance_codes > maxNdirect || ndirect_msb<<distance_postfix_bits != num_direct_distance_codes {
			distance_postfix_bits = 0
			num_direct_distance_codes = 0
		}
	}

	initDistanceParams(params, distance_postfix_bits, num_direct_distance_codes)
}

func ensureInitialized(s *Writer) bool {
	if s.is_initialized_ {
		return true
	}

	s.last_bytes_bits_ = 0
	s.last_bytes_ = 0
	s.remaining_metadata_bytes_ = math.MaxUint32

	sanitizeParams(&s.params)
	s.params.lgblock = computeLgBlock(&s.params)
	chooseDistanceParams(&s.params)

	ringBufferSetup(&s.params, &s.ringbuffer_)

	{
		var lgwin int = int(s.params.lgwin)
		if s.params.quality == fastOnePassCompressionQuality || s.params.quality == fastTwoPassCompressionQuality {
			lgwin = brotli_max_int(lgwin, 18)
		}

		encodeWindowBits(lgwin, s.params.large_window, &s.last_bytes_, &s.last_bytes_bits_)
	}

	if s.params.quality == fastOnePassCompressionQuality {
		s.cmd_depths_ = [128]byte{
			0, 4, 4, 5, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,
			0, 0, 0, 4, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7,
			7, 7, 10, 10, 10, 10, 10, 10, 0, 4, 4, 5, 5, 5, 6, 6,
			7, 8, 8, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
			5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			6, 6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4,
			4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 7, 7, 7, 8, 10,
			12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
		}
		s.cmd_bits_ = [128]uint16{
			0, 0, 8, 9, 3, 35, 7, 71,
			39, 103, 23, 47, 175, 111, 239, 31,
			0, 0, 0, 4, 12, 2, 10, 6,
			13, 29, 11, 43, 27, 59, 87, 55,
			15, 79, 319, 831, 191, 703, 447, 959,
			0, 14, 1, 25, 5, 21, 19, 51,
			119, 159, 95, 223, 479, 991, 63, 575,
			127, 639, 383, 895, 255, 767, 511, 1023,
			14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			27, 59, 7, 39, 23, 55, 30, 1, 17, 9, 25, 5, 0, 8, 4, 12,
			2, 10, 6, 21, 13, 29, 3, 19, 11, 15, 47, 31, 95, 63, 127, 255,
			767, 2815, 1791, 3839, 511, 2559, 1535, 3583, 1023, 3071, 2047, 4095,
		}
		s.cmd_code_ = [512]byte{
			0xff, 0x77, 0xd5, 0xbf, 0xe7, 0xde, 0xea, 0x9e, 0x51, 0x5d, 0xde, 0xc6,
			0x70, 0x57, 0xbc, 0x58, 0x58, 0x58, 0xd8, 0xd8, 0x58, 0xd5, 0xcb, 0x8c,
			0xea, 0xe0, 0xc3, 0x87, 0x1f, 0x83, 0xc1, 0x60, 0x1c, 0x67, 0xb2, 0xaa,
			0x06, 0x83, 0xc1, 0x60, 0x30, 0x18, 0xcc, 0xa1, 0xce, 0x88, 0x54, 0x94,
			0x46, 0xe1, 0xb0, 0xd0, 0x4e, 0xb2, 0xf7, 0x04, 0x00,
		}
		s.cmd_code_numbits_ = 448
	}

	s.is_initialized_ = true
	return true
}

func encoderInitParams(params *encoderParams) {
	params.mode = defaultMode
	params.large_window = false
	params.quality = defaultQuality
	params.lgwin = defaultWindow
	params.lgblock = 0
	params.size_hint = 0
	params.disable_literal_context_modeling = false
	initEncoderDictionary(&params.dictionary)
	params.dist.distance_postfix_bits = 0
	params.dist.num_direct_distance_codes = 0
	params.dist.alphabet_size = uint32(distanceAlphabetSize(0, 0, maxDistanceBits))
	params.dist.max_distance = maxDistance
}

func encoderInitState(s *Writer) {
	encoderInitParams(&s.params)
	s.input_pos_ = 0
	s.commands = s.commands[:0]
	s.num_literals_ = 0
	s.last_insert_len_ = 0
	s.last_flush_pos_ = 0
	s.last_processed_pos_ = 0
	s.prev_byte_ = 0
	s.prev_byte2_ = 0
	if s.hasher_ != nil {
		s.hasher_.Common().is_prepared_ = false
	}
	s.cmd_code_numbits_ = 0
	s.stream_state_ = streamProcessing
	s.is_last_block_emitted_ = false
	s.is_initialized_ = false

	ringBufferInit(&s.ringbuffer_)

	s.dist_cache_[0] = 4

	s.dist_cache_[1] = 11
	s.dist_cache_[2] = 15
	s.dist_cache_[3] = 16

	copy(s.saved_dist_cache_[:], s.dist_cache_[:])
}

func copyInputToRingBuffer(s *Writer, input_size uint, input_buffer []byte) {
	var ringbuffer_ *ringBuffer = &s.ringbuffer_
	ringBufferWrite(input_buffer, input_size, ringbuffer_)
	s.input_pos_ += uint64(input_size)

	if ringbuffer_.pos_ <= ringbuffer_.mask_ {

		for i := 0; i < int(7); i++ {
			ringbuffer_.buffer_[ringbuffer_.pos_:][i] = 0
		}
	}
}

func updateLastProcessedPos(s *Writer) bool {
	var wrapped_last_processed_pos uint32 = wrapPosition(s.last_processed_pos_)
	var wrapped_input_pos uint32 = wrapPosition(s.input_pos_)
	s.last_processed_pos_ = s.input_pos_
	return wrapped_input_pos < wrapped_last_processed_pos
}

func extendLastCommand(s *Writer, bytes *uint32, wrapped_last_processed_pos *uint32) {
	var last_command *command = &s.commands[len(s.commands)-1]
	var data []byte = s.ringbuffer_.buffer_
	var mask uint32 = s.ringbuffer_.mask_
	var max_backward_distance uint64 = ((uint64(1)) << s.params.lgwin) - windowGap
	var last_copy_len uint64 = uint64(last_command.copy_len_) & 0x1FFFFFF
	var last_processed_pos uint64 = s.last_processed_pos_ - last_copy_len
	var max_distance uint64
	if last_processed_pos < max_backward_distance {
		max_distance = last_processed_pos
	} else {
		max_distance = max_backward_distance
	}
	var cmd_dist uint64 = uint64(s.dist_cache_[0])
	var distance_code uint32 = commandRestoreDistanceCode(last_command, &s.params.dist)
	if distance_code < numDistanceShortCodes || uint64(distance_code-(numDistanceShortCodes-1)) == cmd_dist {
		if cmd_dist <= max_distance {
			for *bytes != 0 && data[*wrapped_last_processed_pos&mask] == data[(uint64(*wrapped_last_processed_pos)-cmd_dist)&uint64(mask)] {
				last_command.copy_len_++
				(*bytes)--
				(*wrapped_last_processed_pos)++
			}
		}

		getLengthCode(uint(last_command.insert_len_), uint(int(last_command.copy_len_&0x1FFFFFF)+int(last_command.copy_len_>>25)), (last_command.dist_prefix_&0x3FF == 0), &last_command.cmd_prefix_)
	}
}

func encodeData(s *Writer, is_last bool, force_flush bool) bool {
	var delta uint64 = unprocessedInputSize(s)
	var bytes uint32 = uint32(delta)
	var wrapped_last_processed_pos uint32 = wrapPosition(s.last_processed_pos_)
	var data []byte
	var mask uint32
	var literal_context_mode int

	data = s.ringbuffer_.buffer_
	mask = s.ringbuffer_.mask_

	if s.is_last_block_emitted_ {
		return false
	}
	if is_last {
		s.is_last_block_emitted_ = true
	}

	if delta > uint64(inputBlockSize(s)) {
		return false
	}

	if s.params.quality == fastTwoPassCompressionQuality {
		if s.command_buf_ == nil || cap(s.command_buf_) < int(kCompressFragmentTwoPassBlockSize) {
			s.command_buf_ = make([]uint32, kCompressFragmentTwoPassBlockSize)
			s.literal_buf_ = make([]byte, kCompressFragmentTwoPassBlockSize)
		} else {
			s.command_buf_ = s.command_buf_[:kCompressFragmentTwoPassBlockSize]
			s.literal_buf_ = s.literal_buf_[:kCompressFragmentTwoPassBlockSize]
		}
	}

	if s.params.quality == fastOnePassCompressionQuality || s.params.quality == fastTwoPassCompressionQuality {
		var storage []byte
		var storage_ix uint = uint(s.last_bytes_bits_)
		var table_size uint
		var table []int

		if delta == 0 && !is_last {

			return true
		}

		storage = s.getStorage(int(2*bytes + 503))
		storage[0] = byte(s.last_bytes_)
		storage[1] = byte(s.last_bytes_ >> 8)
		table = getHashTable(s, s.params.quality, uint(bytes), &table_size)
		if s.params.quality == fastOnePassCompressionQuality {
			compressFragmentFast(data[wrapped_last_processed_pos&mask:], uint(bytes), is_last, table, table_size, s.cmd_depths_[:], s.cmd_bits_[:], &s.cmd_code_numbits_, s.cmd_code_[:], &storage_ix, storage)
		} else {
			compressFragmentTwoPass(data[wrapped_last_processed_pos&mask:], uint(bytes), is_last, s.command_buf_, s.literal_buf_, table, table_size, &storage_ix, storage)
		}

		s.last_bytes_ = uint16(storage[storage_ix>>3])
		s.last_bytes_bits_ = byte(storage_ix & 7)
		updateLastProcessedPos(s)
		s.writeOutput(storage[:storage_ix>>3])
		return true
	}
	{

		newsize := len(s.commands) + int(bytes)/2 + 1
		if newsize > cap(s.commands) {

			newsize += int(bytes/4) + 16

			new_commands := make([]command, len(s.commands), newsize)
			if s.commands != nil {
				copy(new_commands, s.commands)
			}

			s.commands = new_commands
		}
	}

	initOrStitchToPreviousBlock(&s.hasher_, data, uint(mask), &s.params, uint(wrapped_last_processed_pos), uint(bytes), is_last)

	literal_context_mode = chooseContextMode(&s.params, data, uint(wrapPosition(s.last_flush_pos_)), uint(mask), uint(s.input_pos_-s.last_flush_pos_))

	if len(s.commands) != 0 && s.last_insert_len_ == 0 {
		extendLastCommand(s, &bytes, &wrapped_last_processed_pos)
	}

	if s.params.quality == zopflificationQuality {
		assert(s.params.hasher.type_ == 10)
		createZopfliBackwardReferences(uint(bytes), uint(wrapped_last_processed_pos), data, uint(mask), &s.params, s.hasher_.(*h10), s.dist_cache_[:], &s.last_insert_len_, &s.commands, &s.num_literals_)
	} else if s.params.quality == hqZopflificationQuality {
		assert(s.params.hasher.type_ == 10)
		createHqZopfliBackwardReferences(uint(bytes), uint(wrapped_last_processed_pos), data, uint(mask), &s.params, s.hasher_, s.dist_cache_[:], &s.last_insert_len_, &s.commands, &s.num_literals_)
	} else {
		createBackwardReferences(uint(bytes), uint(wrapped_last_processed_pos), data, uint(mask), &s.params, s.hasher_, s.dist_cache_[:], &s.last_insert_len_, &s.commands, &s.num_literals_)
	}
	{
		var max_length uint = maxMetablockSize(&s.params)
		var max_literals uint = max_length / 8
		max_commands := int(max_length / 8)
		var processed_bytes uint = uint(s.input_pos_ - s.last_flush_pos_)
		var next_input_fits_metablock bool = (processed_bytes+inputBlockSize(s) <= max_length)
		var should_flush bool = (s.params.quality < minQualityForBlockSplit && s.num_literals_+uint(len(s.commands)) >= maxNumDelayedSymbols)

		if !is_last && !force_flush && !should_flush && next_input_fits_metablock && s.num_literals_ < max_literals && len(s.commands) < max_commands {

			if updateLastProcessedPos(s) {
				hasherReset(s.hasher_)
			}

			return true
		}
	}

	if s.last_insert_len_ > 0 {
		s.commands = append(s.commands, makeInsertCommand(s.last_insert_len_))
		s.num_literals_ += s.last_insert_len_
		s.last_insert_len_ = 0
	}

	if !is_last && s.input_pos_ == s.last_flush_pos_ {

		return true
	}

	assert(s.input_pos_ >= s.last_flush_pos_)
	assert(s.input_pos_ > s.last_flush_pos_ || is_last)
	assert(s.input_pos_-s.last_flush_pos_ <= 1<<24)
	{
		var metablock_size uint32 = uint32(s.input_pos_ - s.last_flush_pos_)
		var storage []byte = s.getStorage(int(2*metablock_size + 503))
		var storage_ix uint = uint(s.last_bytes_bits_)
		storage[0] = byte(s.last_bytes_)
		storage[1] = byte(s.last_bytes_ >> 8)
		writeMetaBlockInternal(data, uint(mask), s.last_flush_pos_, uint(metablock_size), is_last, literal_context_mode, &s.params, s.prev_byte_, s.prev_byte2_, s.num_literals_, s.commands, s.saved_dist_cache_[:], s.dist_cache_[:], &storage_ix, storage)
		s.last_bytes_ = uint16(storage[storage_ix>>3])
		s.last_bytes_bits_ = byte(storage_ix & 7)
		s.last_flush_pos_ = s.input_pos_
		if updateLastProcessedPos(s) {
			hasherReset(s.hasher_)
		}

		if s.last_flush_pos_ > 0 {
			s.prev_byte_ = data[(uint32(s.last_flush_pos_)-1)&mask]
		}

		if s.last_flush_pos_ > 1 {
			s.prev_byte2_ = data[uint32(s.last_flush_pos_-2)&mask]
		}

		s.commands = s.commands[:0]
		s.num_literals_ = 0

		copy(s.saved_dist_cache_[:], s.dist_cache_[:])

		s.writeOutput(storage[:storage_ix>>3])
		return true
	}
}

func writeMetadataHeader(s *Writer, block_size uint, header []byte) uint {
	storage_ix := uint(s.last_bytes_bits_)
	header[0] = byte(s.last_bytes_)
	header[1] = byte(s.last_bytes_ >> 8)
	s.last_bytes_ = 0
	s.last_bytes_bits_ = 0

	writeBits(1, 0, &storage_ix, header)
	writeBits(2, 3, &storage_ix, header)
	writeBits(1, 0, &storage_ix, header)
	if block_size == 0 {
		writeBits(2, 0, &storage_ix, header)
	} else {
		var nbits uint32
		if block_size == 1 {
			nbits = 0
		} else {
			nbits = log2FloorNonZero(uint(uint32(block_size)-1)) + 1
		}
		var nbytes uint32 = (nbits + 7) / 8
		writeBits(2, uint64(nbytes), &storage_ix, header)
		writeBits(uint(8*nbytes), uint64(block_size)-1, &storage_ix, header)
	}

	return (storage_ix + 7) >> 3
}

func injectBytePaddingBlock(s *Writer) {
	var seal uint32 = uint32(s.last_bytes_)
	var seal_bits uint = uint(s.last_bytes_bits_)
	s.last_bytes_ = 0
	s.last_bytes_bits_ = 0

	seal |= 0x6 << seal_bits

	seal_bits += 6

	destination := s.tiny_buf_.u8[:]

	destination[0] = byte(seal)
	if seal_bits > 8 {
		destination[1] = byte(seal >> 8)
	}
	if seal_bits > 16 {
		destination[2] = byte(seal >> 16)
	}
	s.writeOutput(destination[:(seal_bits+7)>>3])
}

func checkFlushComplete(s *Writer) {
	if s.stream_state_ == streamFlushRequested && s.err == nil {
		s.stream_state_ = streamProcessing
	}
}

func encoderCompressStreamFast(s *Writer, op int, available_in *uint, next_in *[]byte) bool {
	var block_size_limit uint = uint(1) << s.params.lgwin
	var buf_size uint = brotli_min_size_t(kCompressFragmentTwoPassBlockSize, brotli_min_size_t(*available_in, block_size_limit))
	var command_buf []uint32 = nil
	var literal_buf []byte = nil
	if s.params.quality != fastOnePassCompressionQuality && s.params.quality != fastTwoPassCompressionQuality {
		return false
	}

	if s.params.quality == fastTwoPassCompressionQuality {
		if s.command_buf_ == nil || cap(s.command_buf_) < int(buf_size) {
			s.command_buf_ = make([]uint32, buf_size)
			s.literal_buf_ = make([]byte, buf_size)
		} else {
			s.command_buf_ = s.command_buf_[:buf_size]
			s.literal_buf_ = s.literal_buf_[:buf_size]
		}

		command_buf = s.command_buf_
		literal_buf = s.literal_buf_
	}

	for {
		if s.stream_state_ == streamFlushRequested && s.last_bytes_bits_ != 0 {
			injectBytePaddingBlock(s)
			continue
		}

		if s.stream_state_ == streamProcessing && (*available_in != 0 || op != int(operationProcess)) {
			var block_size uint = brotli_min_size_t(block_size_limit, *available_in)
			var is_last bool = (*available_in == block_size) && (op == int(operationFinish))
			var force_flush bool = (*available_in == block_size) && (op == int(operationFlush))
			var max_out_size uint = 2*block_size + 503
			var storage []byte = nil
			var storage_ix uint = uint(s.last_bytes_bits_)
			var table_size uint
			var table []int

			if force_flush && block_size == 0 {
				s.stream_state_ = streamFlushRequested
				continue
			}

			storage = s.getStorage(int(max_out_size))

			storage[0] = byte(s.last_bytes_)
			storage[1] = byte(s.last_bytes_ >> 8)
			table = getHashTable(s, s.params.quality, block_size, &table_size)

			if s.params.quality == fastOnePassCompressionQuality {
				compressFragmentFast(*next_in, block_size, is_last, table, table_size, s.cmd_depths_[:], s.cmd_bits_[:], &s.cmd_code_numbits_, s.cmd_code_[:], &storage_ix, storage)
			} else {
				compressFragmentTwoPass(*next_in, block_size, is_last, command_buf, literal_buf, table, table_size, &storage_ix, storage)
			}

			*next_in = (*next_in)[block_size:]
			*available_in -= block_size
			var out_bytes uint = storage_ix >> 3
			s.writeOutput(storage[:out_bytes])

			s.last_bytes_ = uint16(storage[storage_ix>>3])
			s.last_bytes_bits_ = byte(storage_ix & 7)

			if force_flush {
				s.stream_state_ = streamFlushRequested
			}
			if is_last {
				s.stream_state_ = streamFinished
			}
			continue
		}

		break
	}

	checkFlushComplete(s)
	return true
}

func processMetadata(s *Writer, available_in *uint, next_in *[]byte) bool {
	if *available_in > 1<<24 {
		return false
	}

	if s.stream_state_ == streamProcessing {
		s.remaining_metadata_bytes_ = uint32(*available_in)
		s.stream_state_ = streamMetadataHead
	}

	if s.stream_state_ != streamMetadataHead && s.stream_state_ != streamMetadataBody {
		return false
	}

	for {
		if s.stream_state_ == streamFlushRequested && s.last_bytes_bits_ != 0 {
			injectBytePaddingBlock(s)
			continue
		}

		if s.input_pos_ != s.last_flush_pos_ {
			var result bool = encodeData(s, false, true)
			if !result {
				return false
			}
			continue
		}

		if s.stream_state_ == streamMetadataHead {
			n := writeMetadataHeader(s, uint(s.remaining_metadata_bytes_), s.tiny_buf_.u8[:])
			s.writeOutput(s.tiny_buf_.u8[:n])
			s.stream_state_ = streamMetadataBody
			continue
		} else {

			if s.remaining_metadata_bytes_ == 0 {
				s.remaining_metadata_bytes_ = math.MaxUint32
				s.stream_state_ = streamProcessing
				break
			}

			var c uint32 = brotli_min_uint32_t(s.remaining_metadata_bytes_, 16)
			copy(s.tiny_buf_.u8[:], (*next_in)[:c])
			*next_in = (*next_in)[c:]
			*available_in -= uint(c)
			s.remaining_metadata_bytes_ -= c
			s.writeOutput(s.tiny_buf_.u8[:c])

			continue
		}
	}

	return true
}

func updateSizeHint(s *Writer, available_in uint) {
	if s.params.size_hint == 0 {
		var delta uint64 = unprocessedInputSize(s)
		var tail uint64 = uint64(available_in)
		var limit uint32 = 1 << 30
		var total uint32
		if (delta >= uint64(limit)) || (tail >= uint64(limit)) || ((delta + tail) >= uint64(limit)) {
			total = limit
		} else {
			total = uint32(delta + tail)
		}

		s.params.size_hint = uint(total)
	}
}

func encoderCompressStream(s *Writer, op int, available_in *uint, next_in *[]byte) bool {
	if !ensureInitialized(s) {
		return false
	}

	if s.remaining_metadata_bytes_ != math.MaxUint32 {
		if uint32(*available_in) != s.remaining_metadata_bytes_ {
			return false
		}
		if op != int(operationEmitMetadata) {
			return false
		}
	}

	if op == int(operationEmitMetadata) {
		updateSizeHint(s, 0)
		return processMetadata(s, available_in, next_in)
	}

	if s.stream_state_ == streamMetadataHead || s.stream_state_ == streamMetadataBody {
		return false
	}

	if s.stream_state_ != streamProcessing && *available_in != 0 {
		return false
	}

	if s.params.quality == fastOnePassCompressionQuality || s.params.quality == fastTwoPassCompressionQuality {
		return encoderCompressStreamFast(s, op, available_in, next_in)
	}

	for {
		var remaining_block_size uint = remainingInputBlockSize(s)

		if remaining_block_size != 0 && *available_in != 0 {
			var copy_input_size uint = brotli_min_size_t(remaining_block_size, *available_in)
			copyInputToRingBuffer(s, copy_input_size, *next_in)
			*next_in = (*next_in)[copy_input_size:]
			*available_in -= copy_input_size
			continue
		}

		if s.stream_state_ == streamFlushRequested && s.last_bytes_bits_ != 0 {
			injectBytePaddingBlock(s)
			continue
		}

		if s.stream_state_ == streamProcessing {
			if remaining_block_size == 0 || op != int(operationProcess) {
				var is_last bool = ((*available_in == 0) && op == int(operationFinish))
				var force_flush bool = ((*available_in == 0) && op == int(operationFlush))
				var result bool
				updateSizeHint(s, *available_in)
				result = encodeData(s, is_last, force_flush)
				if !result {
					return false
				}
				if force_flush {
					s.stream_state_ = streamFlushRequested
				}
				if is_last {
					s.stream_state_ = streamFinished
				}
				continue
			}
		}

		break
	}

	checkFlushComplete(s)
	return true
}

func (w *Writer) writeOutput(data []byte) {
	if w.err != nil {
		return
	}

	_, w.err = w.dst.Write(data)
	if w.err == nil {
		checkFlushComplete(w)
	}
}
