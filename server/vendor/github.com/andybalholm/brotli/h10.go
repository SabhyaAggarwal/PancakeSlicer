package brotli

import "encoding/binary"

/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

func (*h10) HashTypeLength() uint {
	return 4
}

func (*h10) StoreLookahead() uint {
	return 128
}

func hashBytesH10(data []byte) uint32 {
	var h uint32 = binary.LittleEndian.Uint32(data) * kHashMul32

	return h >> (32 - 17)
}

type h10 struct {
	hasherCommon
	window_mask_ uint
	buckets_     [1 << 17]uint32
	invalid_pos_ uint32
	forest       []uint32
}

func (h *h10) Initialize(params *encoderParams) {
	h.window_mask_ = (1 << params.lgwin) - 1
	h.invalid_pos_ = uint32(0 - h.window_mask_)
	var num_nodes uint = uint(1) << params.lgwin
	h.forest = make([]uint32, 2*num_nodes)
}

func (h *h10) Prepare(one_shot bool, input_size uint, data []byte) {
	var invalid_pos uint32 = h.invalid_pos_
	var i uint32
	for i = 0; i < 1<<17; i++ {
		h.buckets_[i] = invalid_pos
	}
}

func leftChildIndexH10(self *h10, pos uint) uint {
	return 2 * (pos & self.window_mask_)
}

func rightChildIndexH10(self *h10, pos uint) uint {
	return 2*(pos&self.window_mask_) + 1
}

func storeAndFindMatchesH10(self *h10, data []byte, cur_ix uint, ring_buffer_mask uint, max_length uint, max_backward uint, best_len *uint, matches []backwardMatch) []backwardMatch {
	var cur_ix_masked uint = cur_ix & ring_buffer_mask
	var max_comp_len uint = brotli_min_size_t(max_length, 128)
	var should_reroot_tree bool = (max_length >= 128)
	var key uint32 = hashBytesH10(data[cur_ix_masked:])
	var forest []uint32 = self.forest
	var prev_ix uint = uint(self.buckets_[key])
	var node_left uint = leftChildIndexH10(self, cur_ix)
	var node_right uint = rightChildIndexH10(self, cur_ix)
	var best_len_left uint = 0
	var best_len_right uint = 0
	var depth_remaining uint

	if should_reroot_tree {
		self.buckets_[key] = uint32(cur_ix)
	}

	for depth_remaining = 64; ; depth_remaining-- {
		var backward uint = cur_ix - prev_ix
		var prev_ix_masked uint = prev_ix & ring_buffer_mask
		if backward == 0 || backward > max_backward || depth_remaining == 0 {
			if should_reroot_tree {
				forest[node_left] = self.invalid_pos_
				forest[node_right] = self.invalid_pos_
			}

			break
		}
		{
			var cur_len uint = brotli_min_size_t(best_len_left, best_len_right)
			var len uint
			assert(cur_len <= 128)
			len = cur_len + findMatchLengthWithLimit(data[cur_ix_masked+cur_len:], data[prev_ix_masked+cur_len:], max_length-cur_len)
			if matches != nil && len > *best_len {
				*best_len = uint(len)
				initBackwardMatch(&matches[0], backward, uint(len))
				matches = matches[1:]
			}

			if len >= max_comp_len {
				if should_reroot_tree {
					forest[node_left] = forest[leftChildIndexH10(self, prev_ix)]
					forest[node_right] = forest[rightChildIndexH10(self, prev_ix)]
				}

				break
			}

			if data[cur_ix_masked+len] > data[prev_ix_masked+len] {
				best_len_left = uint(len)
				if should_reroot_tree {
					forest[node_left] = uint32(prev_ix)
				}

				node_left = rightChildIndexH10(self, prev_ix)
				prev_ix = uint(forest[node_left])
			} else {
				best_len_right = uint(len)
				if should_reroot_tree {
					forest[node_right] = uint32(prev_ix)
				}

				node_right = leftChildIndexH10(self, prev_ix)
				prev_ix = uint(forest[node_right])
			}
		}
	}

	return matches
}

func findAllMatchesH10(handle *h10, dictionary *encoderDictionary, data []byte, ring_buffer_mask uint, cur_ix uint, max_length uint, max_backward uint, gap uint, params *encoderParams, matches []backwardMatch) uint {
	var orig_matches []backwardMatch = matches
	var cur_ix_masked uint = cur_ix & ring_buffer_mask
	var best_len uint = 1
	var short_match_max_backward uint
	if params.quality != hqZopflificationQuality {
		short_match_max_backward = 16
	} else {
		short_match_max_backward = 64
	}
	var stop uint = cur_ix - short_match_max_backward
	var dict_matches [maxStaticDictionaryMatchLen + 1]uint32
	var i uint
	if cur_ix < short_match_max_backward {
		stop = 0
	}
	for i = cur_ix - 1; i > stop && best_len <= 2; i-- {
		var prev_ix uint = i
		var backward uint = cur_ix - prev_ix
		if backward > max_backward {
			break
		}

		prev_ix &= ring_buffer_mask
		if data[cur_ix_masked] != data[prev_ix] || data[cur_ix_masked+1] != data[prev_ix+1] {
			continue
		}
		{
			var len uint = findMatchLengthWithLimit(data[prev_ix:], data[cur_ix_masked:], max_length)
			if len > best_len {
				best_len = uint(len)
				initBackwardMatch(&matches[0], backward, uint(len))
				matches = matches[1:]
			}
		}
	}

	if best_len < max_length {
		matches = storeAndFindMatchesH10(handle, data, cur_ix, ring_buffer_mask, max_length, max_backward, &best_len, matches)
	}

	for i = 0; i <= maxStaticDictionaryMatchLen; i++ {
		dict_matches[i] = kInvalidMatch
	}
	{
		var minlen uint = brotli_max_size_t(4, best_len+1)
		if findAllStaticDictionaryMatches(dictionary, data[cur_ix_masked:], minlen, max_length, dict_matches[0:]) {
			var maxlen uint = brotli_min_size_t(maxStaticDictionaryMatchLen, max_length)
			var l uint
			for l = minlen; l <= maxlen; l++ {
				var dict_id uint32 = dict_matches[l]
				if dict_id < kInvalidMatch {
					var distance uint = max_backward + gap + uint(dict_id>>5) + 1
					if distance <= params.dist.max_distance {
						initDictionaryBackwardMatch(&matches[0], distance, l, uint(dict_id&31))
						matches = matches[1:]
					}
				}
			}
		}
	}

	return uint(-cap(matches) + cap(orig_matches))
}

func (h *h10) Store(data []byte, mask uint, ix uint) {
	var max_backward uint = h.window_mask_ - windowGap + 1

	storeAndFindMatchesH10(h, data, ix, mask, 128, max_backward, nil, nil)
}

func (h *h10) StoreRange(data []byte, mask uint, ix_start uint, ix_end uint) {
	var i uint = ix_start
	var j uint = ix_start
	if ix_start+63 <= ix_end {
		i = ix_end - 63
	}

	if ix_start+512 <= i {
		for ; j < i; j += 8 {
			h.Store(data, mask, j)
		}
	}

	for ; i < ix_end; i++ {
		h.Store(data, mask, i)
	}
}

func (h *h10) StitchToPreviousBlock(num_bytes uint, position uint, ringbuffer []byte, ringbuffer_mask uint) {
	if num_bytes >= h.HashTypeLength()-1 && position >= 128 {
		var i_start uint = position - 128 + 1
		var i_end uint = brotli_min_size_t(position, i_start+num_bytes)

		var i uint
		for i = i_start; i < i_end; i++ {

			var max_backward uint = h.window_mask_ - brotli_max_size_t(windowGap-1, position-i)

			storeAndFindMatchesH10(h, ringbuffer, i, ringbuffer_mask, 128, max_backward, nil, nil)
		}
	}
}

const maxNumMatchesH10 = 128

func (*h10) FindLongestMatch(dictionary *encoderDictionary, data []byte, ring_buffer_mask uint, distance_cache []int, cur_ix uint, max_length uint, max_backward uint, gap uint, max_distance uint, out *hasherSearchResult) {
	panic("unimplemented")
}

func (*h10) PrepareDistanceCache(distance_cache []int) {
	panic("unimplemented")
}
