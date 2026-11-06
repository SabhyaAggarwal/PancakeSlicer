package brotli

/* Copyright 2018 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

const kRollingHashMul32 uint32 = 69069

const kInvalidPosHashRolling uint32 = 0xffffffff

func (*hashRolling) HashTypeLength() uint {
	return 4
}

func (*hashRolling) StoreLookahead() uint {
	return 4
}

func (*hashRolling) HashByte(b byte) uint32 {
	return uint32(b) + 1
}

func (h *hashRolling) HashRollingFunctionInitial(state uint32, add byte, factor uint32) uint32 {
	return uint32(factor*state + h.HashByte(add))
}

func (h *hashRolling) HashRollingFunction(state uint32, add byte, rem byte, factor uint32, factor_remove uint32) uint32 {
	return uint32(factor*state + h.HashByte(add) - factor_remove*h.HashByte(rem))
}

type hashRolling struct {
	hasherCommon

	jump int

	state         uint32
	table         []uint32
	next_ix       uint
	factor        uint32
	factor_remove uint32
}

func (h *hashRolling) Initialize(params *encoderParams) {
	h.state = 0
	h.next_ix = 0

	h.factor = kRollingHashMul32

	h.factor_remove = 1

	for i := 0; i < 32; i += h.jump {
		h.factor_remove *= h.factor
	}

	h.table = make([]uint32, 16777216)
	for i := 0; i < 16777216; i++ {
		h.table[i] = kInvalidPosHashRolling
	}
}

func (h *hashRolling) Prepare(one_shot bool, input_size uint, data []byte) {

	if input_size < 32 {
		return
	}
	h.state = 0
	for i := 0; i < 32; i += h.jump {
		h.state = h.HashRollingFunctionInitial(h.state, data[i], h.factor)
	}
}

func (*hashRolling) Store(data []byte, mask uint, ix uint) {
}

func (*hashRolling) StoreRange(data []byte, mask uint, ix_start uint, ix_end uint) {
}

func (h *hashRolling) StitchToPreviousBlock(num_bytes uint, position uint, ringbuffer []byte, ring_buffer_mask uint) {
	var position_masked uint

	var available uint = num_bytes
	if position&uint(h.jump-1) != 0 {
		var diff uint = uint(h.jump) - (position & uint(h.jump-1))
		if diff > available {
			available = 0
		} else {
			available = available - diff
		}
		position += diff
	}

	position_masked = position & ring_buffer_mask

	if available > ring_buffer_mask-position_masked {
		available = ring_buffer_mask - position_masked
	}

	h.Prepare(false, available, ringbuffer[position&ring_buffer_mask:])
	h.next_ix = position
}

func (*hashRolling) PrepareDistanceCache(distance_cache []int) {
}

func (h *hashRolling) FindLongestMatch(dictionary *encoderDictionary, data []byte, ring_buffer_mask uint, distance_cache []int, cur_ix uint, max_length uint, max_backward uint, gap uint, max_distance uint, out *hasherSearchResult) {
	var cur_ix_masked uint = cur_ix & ring_buffer_mask
	var pos uint = h.next_ix

	if cur_ix&uint(h.jump-1) != 0 {
		return
	}

	if max_length < 32 {
		return
	}

	for pos = h.next_ix; pos <= cur_ix; pos += uint(h.jump) {
		var code uint32 = h.state & ((16777216 * 64) - 1)
		var rem byte = data[pos&ring_buffer_mask]
		var add byte = data[(pos+32)&ring_buffer_mask]
		var found_ix uint = uint(kInvalidPosHashRolling)

		h.state = h.HashRollingFunction(h.state, add, rem, h.factor, h.factor_remove)

		if code < 16777216 {
			found_ix = uint(h.table[code])
			h.table[code] = uint32(pos)
			if pos == cur_ix && uint32(found_ix) != kInvalidPosHashRolling {

				var backward uint = uint(uint32(cur_ix - found_ix))
				if backward <= max_backward {
					var found_ix_masked uint = found_ix & ring_buffer_mask
					var len uint = findMatchLengthWithLimit(data[found_ix_masked:], data[cur_ix_masked:], max_length)
					if len >= 4 && len > out.len {
						var score uint = backwardReferenceScore(uint(len), backward)
						if score > out.score {
							out.len = uint(len)
							out.distance = backward
							out.score = score
							out.len_code_delta = 0
						}
					}
				}
			}
		}
	}

	h.next_ix = cur_ix + uint(h.jump)
}
