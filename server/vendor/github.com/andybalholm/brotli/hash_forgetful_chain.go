package brotli

import "encoding/binary"

/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

func (*hashForgetfulChain) HashTypeLength() uint {
	return 4
}

func (*hashForgetfulChain) StoreLookahead() uint {
	return 4
}

func (h *hashForgetfulChain) HashBytes(data []byte) uint {
	var hash uint32 = binary.LittleEndian.Uint32(data) * kHashMul32

	return uint(hash >> (32 - h.bucketBits))
}

type slot struct {
	delta uint16
	next  uint16
}

type hashForgetfulChain struct {
	hasherCommon

	bucketBits              uint
	numBanks                uint
	bankBits                uint
	numLastDistancesToCheck int

	addr          []uint32
	head          []uint16
	tiny_hash     [65536]byte
	banks         [][]slot
	free_slot_idx []uint16
	max_hops      uint
}

func (h *hashForgetfulChain) Initialize(params *encoderParams) {
	var q uint
	if params.quality > 6 {
		q = 7
	} else {
		q = 8
	}
	h.max_hops = q << uint(params.quality-4)

	bankSize := 1 << h.bankBits
	bucketSize := 1 << h.bucketBits

	h.addr = make([]uint32, bucketSize)
	h.head = make([]uint16, bucketSize)
	h.banks = make([][]slot, h.numBanks)
	for i := range h.banks {
		h.banks[i] = make([]slot, bankSize)
	}
	h.free_slot_idx = make([]uint16, h.numBanks)
}

func (h *hashForgetfulChain) Prepare(one_shot bool, input_size uint, data []byte) {
	var partial_prepare_threshold uint = (1 << h.bucketBits) >> 6

	if one_shot && input_size <= partial_prepare_threshold {
		var i uint
		for i = 0; i < input_size; i++ {
			var bucket uint = h.HashBytes(data[i:])

			h.addr[bucket] = 0xCCCCCCCC

			h.head[bucket] = 0xCCCC
		}
	} else {

		for i := range h.addr {
			h.addr[i] = 0xCCCCCCCC
		}

		for i := range h.head {
			h.head[i] = 0
		}
	}

	h.tiny_hash = [65536]byte{}
	for i := range h.free_slot_idx {
		h.free_slot_idx[i] = 0
	}
}

func (h *hashForgetfulChain) Store(data []byte, mask uint, ix uint) {
	var key uint = h.HashBytes(data[ix&mask:])
	var bank uint = key & (h.numBanks - 1)
	idx := uint(h.free_slot_idx[bank]) & ((1 << h.bankBits) - 1)
	h.free_slot_idx[bank]++
	var delta uint = ix - uint(h.addr[key])
	h.tiny_hash[uint16(ix)] = byte(key)
	if delta > 0xFFFF {
		delta = 0xFFFF
	}
	h.banks[bank][idx].delta = uint16(delta)
	h.banks[bank][idx].next = h.head[key]
	h.addr[key] = uint32(ix)
	h.head[key] = uint16(idx)
}

func (h *hashForgetfulChain) StoreRange(data []byte, mask uint, ix_start uint, ix_end uint) {
	var i uint
	for i = ix_start; i < ix_end; i++ {
		h.Store(data, mask, i)
	}
}

func (h *hashForgetfulChain) StitchToPreviousBlock(num_bytes uint, position uint, ringbuffer []byte, ring_buffer_mask uint) {
	if num_bytes >= h.HashTypeLength()-1 && position >= 3 {

		h.Store(ringbuffer, ring_buffer_mask, position-3)
		h.Store(ringbuffer, ring_buffer_mask, position-2)
		h.Store(ringbuffer, ring_buffer_mask, position-1)
	}
}

func (h *hashForgetfulChain) PrepareDistanceCache(distance_cache []int) {
	prepareDistanceCache(distance_cache, h.numLastDistancesToCheck)
}

func (h *hashForgetfulChain) FindLongestMatch(dictionary *encoderDictionary, data []byte, ring_buffer_mask uint, distance_cache []int, cur_ix uint, max_length uint, max_backward uint, gap uint, max_distance uint, out *hasherSearchResult) {
	var cur_ix_masked uint = cur_ix & ring_buffer_mask
	var min_score uint = out.score
	var best_score uint = out.score
	var best_len uint = out.len
	var key uint = h.HashBytes(data[cur_ix_masked:])
	var tiny_hash byte = byte(key)

	out.len = 0

	out.len_code_delta = 0

	for i := 0; i < h.numLastDistancesToCheck; i++ {
		var backward uint = uint(distance_cache[i])
		var prev_ix uint = (cur_ix - backward)

		if i > 0 && h.tiny_hash[uint16(prev_ix)] != tiny_hash {
			continue
		}
		if prev_ix >= cur_ix || backward > max_backward {
			continue
		}

		prev_ix &= ring_buffer_mask
		{
			var len uint = findMatchLengthWithLimit(data[prev_ix:], data[cur_ix_masked:], max_length)
			if len >= 2 {
				var score uint = backwardReferenceScoreUsingLastDistance(uint(len))
				if best_score < score {
					if i != 0 {
						score -= backwardReferencePenaltyUsingLastDistance(uint(i))
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
		var bank uint = key & (h.numBanks - 1)
		var backward uint = 0
		var hops uint = h.max_hops
		var delta uint = cur_ix - uint(h.addr[key])
		var slot uint = uint(h.head[key])
		for {
			tmp6 := hops
			hops--
			if tmp6 == 0 {
				break
			}
			var prev_ix uint
			var last uint = slot
			backward += delta
			if backward > max_backward {
				break
			}
			prev_ix = (cur_ix - backward) & ring_buffer_mask
			slot = uint(h.banks[bank][last].next)
			delta = uint(h.banks[bank][last].delta)
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

		h.Store(data, ring_buffer_mask, cur_ix)
	}

	if out.score == min_score {
		searchInStaticDictionary(dictionary, h, data[cur_ix_masked:], max_length, max_backward+gap, max_distance, out, false)
	}
}
