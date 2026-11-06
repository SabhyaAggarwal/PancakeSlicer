package brotli

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

type ringBuffer struct {
	size_       uint32
	mask_       uint32
	tail_size_  uint32
	total_size_ uint32
	cur_size_   uint32
	pos_        uint32
	data_       []byte
	buffer_     []byte
}

func ringBufferInit(rb *ringBuffer) {
	rb.pos_ = 0
}

func ringBufferSetup(params *encoderParams, rb *ringBuffer) {
	var window_bits int = computeRbBits(params)
	var tail_bits int = params.lgblock
	*(*uint32)(&rb.size_) = 1 << uint(window_bits)
	*(*uint32)(&rb.mask_) = (1 << uint(window_bits)) - 1
	*(*uint32)(&rb.tail_size_) = 1 << uint(tail_bits)
	*(*uint32)(&rb.total_size_) = rb.size_ + rb.tail_size_
}

const kSlackForEightByteHashingEverywhere uint = 7

func ringBufferInitBuffer(buflen uint32, rb *ringBuffer) {
	var new_data []byte
	var i uint
	size := 2 + int(buflen) + int(kSlackForEightByteHashingEverywhere)
	if cap(rb.data_) < size {
		new_data = make([]byte, size)
	} else {
		new_data = rb.data_[:size]
	}
	if rb.data_ != nil {
		copy(new_data, rb.data_[:2+rb.cur_size_+uint32(kSlackForEightByteHashingEverywhere)])
	}

	rb.data_ = new_data
	rb.cur_size_ = buflen
	rb.buffer_ = rb.data_[2:]
	rb.data_[1] = 0
	rb.data_[0] = rb.data_[1]
	for i = 0; i < kSlackForEightByteHashingEverywhere; i++ {
		rb.buffer_[rb.cur_size_+uint32(i)] = 0
	}
}

func ringBufferWriteTail(bytes []byte, n uint, rb *ringBuffer) {
	var masked_pos uint = uint(rb.pos_ & rb.mask_)
	if uint32(masked_pos) < rb.tail_size_ {

		var p uint = uint(rb.size_ + uint32(masked_pos))
		copy(rb.buffer_[p:], bytes[:brotli_min_size_t(n, uint(rb.tail_size_-uint32(masked_pos)))])
	}
}

func ringBufferWrite(bytes []byte, n uint, rb *ringBuffer) {
	if rb.pos_ == 0 && uint32(n) < rb.tail_size_ {

		rb.pos_ = uint32(n)

		ringBufferInitBuffer(rb.pos_, rb)
		copy(rb.buffer_, bytes[:n])
		return
	}

	if rb.cur_size_ < rb.total_size_ {

		ringBufferInitBuffer(rb.total_size_, rb)

		rb.buffer_[rb.size_-2] = 0

		rb.buffer_[rb.size_-1] = 0
	}
	{
		var masked_pos uint = uint(rb.pos_ & rb.mask_)

		ringBufferWriteTail(bytes, n, rb)

		if uint32(masked_pos+n) <= rb.size_ {

			copy(rb.buffer_[masked_pos:], bytes[:n])
		} else {

			copy(rb.buffer_[masked_pos:], bytes[:brotli_min_size_t(n, uint(rb.total_size_-uint32(masked_pos)))])

			copy(rb.buffer_, bytes[rb.size_-uint32(masked_pos):][:uint32(n)-(rb.size_-uint32(masked_pos))])
		}
	}
	{
		var not_first_lap bool = rb.pos_&(1<<31) != 0
		var rb_pos_mask uint32 = (1 << 31) - 1
		rb.data_[0] = rb.buffer_[rb.size_-2]
		rb.data_[1] = rb.buffer_[rb.size_-1]
		rb.pos_ = (rb.pos_ & rb_pos_mask) + uint32(uint32(n)&rb_pos_mask)
		if not_first_lap {

			rb.pos_ |= 1 << 31
		}
	}
}
