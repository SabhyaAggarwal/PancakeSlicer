package brotli

import "encoding/binary"

/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

func (*h6) HashTypeLength() uint {
	return 8
}

func (*h6) StoreLookahead() uint {
	return 8
}

func hashBytesH6(data []byte, mask uint64, shift int) uint32 {
	var h uint64 = (binary.LittleEndian.Uint64(data) & mask) * kHashMul64Long

	return uint32(h >> uint(shift))
}

type h6 struct {
	hasherCommon
	bucket_size_ uint
	block_size_  uint
	hash_shift_  int
	hash_mask_   uint64
	block_mask_  uint32
	num          []uint16
	buckets      []uint32
}

func (h *h6) Initialize(params *encoderParams) {
	h.hash_shift_ = 64 - h.params.bucket_bits
	h.hash_mask_ = (^(uint64(0))) >> uint(64-8*h.params.hash_len)
	h.bucket_size_ = uint(1) << uint(h.params.bucket_bits)
	h.block_size_ = uint(1) << uint(h.params.block_bits)
	h.block_mask_ = uint32(h.block_size_ - 1)
	h.num = make([]uint16, h.bucket_size_)
	h.buckets = make([]uint32, h.block_size_*h.bucket_size_)
}

func (h *h6) Prepare(one_shot bool, input_size uint, data []byte) {
	var num []uint16 = h.num
	var partial_prepare_threshold uint = h.bucket_size_ >> 6

	if one_shot && input_size <= partial_prepare_threshold {
		var i uint
		for i = 0; i < input_size; i++ {
			var key uint32 = hashBytesH6(data[i:], h.hash_mask_, h.hash_shift_)
			num[key] = 0
		}
	} else {
		for i := 0; i < int(h.bucket_size_); i++ {
			num[i] = 0
		}
	}
}

func (h *h6) Store(data []byte, mask uint, ix uint) {
	var num []uint16 = h.num
	var key uint32 = hashBytesH6(data[ix&mask:], h.hash_mask_, h.hash_shift_)
	var minor_ix uint = uint(num[key]) & uint(h.block_mask_)
	var offset uint = minor_ix + uint(key<<uint(h.params.block_bits))
	h.buckets[offset] = uint32(ix)
	num[key]++
}

func (h *h6) StoreRange(data []byte, mask uint, ix_start uint, ix_end uint) {
	var i uint
	for i = ix_start; i < ix_end; i++ {
		h.Store(data, mask, i)
	}
}

func (h *h6) StitchToPreviousBlock(num_bytes uint, position uint, ringbuffer []byte, ringbuffer_mask uint) {
	if num_bytes >= h.HashTypeLength()-1 && position >= 3 {

		h.Store(ringbuffer, ringbuffer_mask, position-3)
		h.Store(ringbuffer, ringbuffer_mask, position-2)
		h.Store(ringbuffer, ringbuffer_mask, position-1)
	}
}

func (h *h6) PrepareDistanceCache(distance_cache []int) {
	prepareDistanceCache(distance_cache, h.params.num_last_distances_to_check)
}

func (h *h6) FindLongestMatch(dictionary *encoderDictionary, data []byte, ring_buffer_mask uint, distance_cache []int, cur_ix uint, max_length uint, max_backward uint, gap uint, max_distance uint, out *hasherSearchResult) {
	var num []uint16 = h.num
	var buckets []uint32 = h.buckets
	var cur_ix_masked uint = cur_ix & ring_buffer_mask
	var min_score uint = out.score
	var best_score uint = out.score
	var best_len uint = out.len
	var i uint
	var bucket []uint32

	out.len = 0

	out.len_code_delta = 0

	for i = 0; i < uint(h.params.num_last_distances_to_check); i++ {
		var backward uint = uint(distance_cache[i])
		var prev_ix uint = uint(cur_ix - backward)
		if prev_ix >= cur_ix {
			continue
		}

		if backward > max_backward {
			continue
		}

		prev_ix &= ring_buffer_mask

		if cur_ix_masked+best_len > ring_buffer_mask || prev_ix+best_len > ring_buffer_mask || data[cur_ix_masked+best_len] != data[prev_ix+best_len] {
			continue
		}
		{
			var len uint = findMatchLengthWithLimit(data[prev_ix:], data[cur_ix_masked:], max_length)
			if len >= 3 || (len == 2 && i < 2) {

				var score uint = backwardReferenceScoreUsingLastDistance(uint(len))
				if best_score < score {
					if i != 0 {
						score -= backwardReferencePenaltyUsingLastDistance(i)
					}
					if best_score < score {
						best_score = score
						best_len = uint(len)
						out.len = best_len
						out.distance = backward
						out.score = best_score
					}
				}
			}
		}
	}
	{
		var key uint32 = hashBytesH6(data[cur_ix_masked:], h.hash_mask_, h.hash_shift_)
		bucket = buckets[key<<uint(h.params.block_bits):]
		var down uint
		if uint(num[key]) > h.block_size_ {
			down = uint(num[key]) - h.block_size_
		} else {
			down = 0
		}
		for i = uint(num[key]); i > down; {
			var prev_ix uint
			i--
			prev_ix = uint(bucket[uint32(i)&h.block_mask_])
			var backward uint = cur_ix - prev_ix
			if backward > max_backward {
				break
			}

			prev_ix &= ring_buffer_mask
			if cur_ix_masked+best_len > ring_buffer_mask || prev_ix+best_len > ring_buffer_mask || data[cur_ix_masked+best_len] != data[prev_ix+best_len] {
				continue
			}
			{
				var len uint = findMatchLengthWithLimit(data[prev_ix:], data[cur_ix_masked:], max_length)
				if len >= 4 {

					var score uint = backwardReferenceScore(uint(len), backward)
					if best_score < score {
						best_score = score
						best_len = uint(len)
						out.len = best_len
						out.distance = backward
						out.score = best_score
					}
				}
			}
		}

		bucket[uint32(num[key])&h.block_mask_] = uint32(cur_ix)
		num[key]++
	}

	if min_score == out.score {
		searchInStaticDictionary(dictionary, h, data[cur_ix_masked:], max_length, max_backward+gap, max_distance, out, false)
	}
}
