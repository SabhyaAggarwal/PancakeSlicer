package brotli

import "encoding/binary"

/* Copyright 2010 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

func writeBits(n_bits uint, bits uint64, pos *uint, array []byte) {

	p := array[*pos>>3:]
	v := uint64(p[0])
	v |= bits << (*pos & 7)
	binary.LittleEndian.PutUint64(p, v)
	*pos += n_bits
}

func writeSingleBit(bit bool, pos *uint, array []byte) {
	if bit {
		writeBits(1, 1, pos, array)
	} else {
		writeBits(1, 0, pos, array)
	}
}

func writeBitsPrepareStorage(pos uint, array []byte) {
	assert(pos&7 == 0)
	array[pos>>3] = 0
}
