package brotli

import "encoding/binary"

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

const shortFillBitWindowRead = (8 >> 1)

var kBitMask = [33]uint32{
	0x00000000,
	0x00000001,
	0x00000003,
	0x00000007,
	0x0000000F,
	0x0000001F,
	0x0000003F,
	0x0000007F,
	0x000000FF,
	0x000001FF,
	0x000003FF,
	0x000007FF,
	0x00000FFF,
	0x00001FFF,
	0x00003FFF,
	0x00007FFF,
	0x0000FFFF,
	0x0001FFFF,
	0x0003FFFF,
	0x0007FFFF,
	0x000FFFFF,
	0x001FFFFF,
	0x003FFFFF,
	0x007FFFFF,
	0x00FFFFFF,
	0x01FFFFFF,
	0x03FFFFFF,
	0x07FFFFFF,
	0x0FFFFFFF,
	0x1FFFFFFF,
	0x3FFFFFFF,
	0x7FFFFFFF,
	0xFFFFFFFF,
}

func bitMask(n uint32) uint32 {
	return kBitMask[n]
}

type bitReader struct {
	val_      uint64
	bit_pos_  uint32
	input     []byte
	input_len uint
	byte_pos  uint
}

type bitReaderState struct {
	val_      uint64
	bit_pos_  uint32
	input     []byte
	input_len uint
	byte_pos  uint
}

func bitReaderSaveState(from *bitReader, to *bitReaderState) {
	to.val_ = from.val_
	to.bit_pos_ = from.bit_pos_
	to.input = from.input
	to.input_len = from.input_len
	to.byte_pos = from.byte_pos
}

func bitReaderRestoreState(to *bitReader, from *bitReaderState) {
	to.val_ = from.val_
	to.bit_pos_ = from.bit_pos_
	to.input = from.input
	to.input_len = from.input_len
	to.byte_pos = from.byte_pos
}

func getAvailableBits(br *bitReader) uint32 {
	return 64 - br.bit_pos_
}

func getRemainingBytes(br *bitReader) uint {
	return uint(uint32(br.input_len-br.byte_pos) + (getAvailableBits(br) >> 3))
}

func checkInputAmount(br *bitReader, num uint) bool {
	return br.input_len-br.byte_pos >= num
}

func fillBitWindow(br *bitReader, n_bits uint32) {
	if br.bit_pos_ >= 32 {
		br.val_ >>= 32
		br.bit_pos_ ^= 32
		br.val_ |= (uint64(binary.LittleEndian.Uint32(br.input[br.byte_pos:]))) << 32
		br.byte_pos += 4
	}
}

func fillBitWindow16(br *bitReader) {
	fillBitWindow(br, 17)
}

func pullByte(br *bitReader) bool {
	if br.byte_pos == br.input_len {
		return false
	}

	br.val_ >>= 8
	br.val_ |= (uint64(br.input[br.byte_pos])) << 56
	br.bit_pos_ -= 8
	br.byte_pos++
	return true
}

func getBitsUnmasked(br *bitReader) uint64 {
	return br.val_ >> br.bit_pos_
}

func get16BitsUnmasked(br *bitReader) uint32 {
	fillBitWindow(br, 16)
	return uint32(getBitsUnmasked(br))
}

func getBits(br *bitReader, n_bits uint32) uint32 {
	fillBitWindow(br, n_bits)
	return uint32(getBitsUnmasked(br)) & bitMask(n_bits)
}

func safeGetBits(br *bitReader, n_bits uint32, val *uint32) bool {
	for getAvailableBits(br) < n_bits {
		if !pullByte(br) {
			return false
		}
	}

	*val = uint32(getBitsUnmasked(br)) & bitMask(n_bits)
	return true
}

func dropBits(br *bitReader, n_bits uint32) {
	br.bit_pos_ += n_bits
}

func bitReaderUnload(br *bitReader) {
	var unused_bytes uint32 = getAvailableBits(br) >> 3
	var unused_bits uint32 = unused_bytes << 3
	br.byte_pos -= uint(unused_bytes)
	if unused_bits == 64 {
		br.val_ = 0
	} else {
		br.val_ <<= unused_bits
	}

	br.bit_pos_ += unused_bits
}

func takeBits(br *bitReader, n_bits uint32, val *uint32) {
	*val = uint32(getBitsUnmasked(br)) & bitMask(n_bits)
	dropBits(br, n_bits)
}

func readBits(br *bitReader, n_bits uint32) uint32 {
	var val uint32
	fillBitWindow(br, n_bits)
	takeBits(br, n_bits, &val)
	return val
}

func safeReadBits(br *bitReader, n_bits uint32, val *uint32) bool {
	for getAvailableBits(br) < n_bits {
		if !pullByte(br) {
			return false
		}
	}

	takeBits(br, n_bits, val)
	return true
}

func bitReaderJumpToByteBoundary(br *bitReader) bool {
	var pad_bits_count uint32 = getAvailableBits(br) & 0x7
	var pad_bits uint32 = 0
	if pad_bits_count != 0 {
		takeBits(br, pad_bits_count, &pad_bits)
	}

	return pad_bits == 0
}

func copyBytes(dest []byte, br *bitReader, num uint) {
	for getAvailableBits(br) >= 8 && num > 0 {
		dest[0] = byte(getBitsUnmasked(br))
		dropBits(br, 8)
		dest = dest[1:]
		num--
	}

	copy(dest, br.input[br.byte_pos:][:num])
	br.byte_pos += num
}

func initBitReader(br *bitReader) {
	br.val_ = 0
	br.bit_pos_ = 64
}

func warmupBitReader(br *bitReader) bool {

	if getAvailableBits(br) == 0 {
		if !pullByte(br) {
			return false
		}
	}

	return true
}
