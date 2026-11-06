// Copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package flate

import (
	"github.com/andybalholm/brotli/matchfinder"
)

const (
	offsetCodeCount = 30

	endBlockMarker = 256

	lengthCodesStart = 257

	codegenCodeCount = 19
	badCode          = 255

	maxNumLit         = 286
	maxStoreBlockSize = 65535
	baseMatchLength   = 3
	baseMatchOffset   = 1
)

var lengthExtraBits = []int8{
	0, 0, 0,
	0, 0, 0, 0, 0, 1, 1, 1, 1, 2,
	2, 2, 2, 3, 3, 3, 3, 4, 4, 4,
	4, 5, 5, 5, 5, 0,
}

var lengthBase = []int{
	0, 1, 2, 3, 4, 5, 6, 7, 8, 10,
	12, 14, 16, 20, 24, 28, 32, 40, 48, 56,
	64, 80, 96, 112, 128, 160, 192, 224, 255,
}

var offsetExtraBits = []int8{
	0, 0, 0, 0, 1, 1, 2, 2, 3, 3,
	4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
	9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
}

var offsetBase = []int{
	0x000000, 0x000001, 0x000002, 0x000003, 0x000004,
	0x000006, 0x000008, 0x00000c, 0x000010, 0x000018,
	0x000020, 0x000030, 0x000040, 0x000060, 0x000080,
	0x0000c0, 0x000100, 0x000180, 0x000200, 0x000300,
	0x000400, 0x000600, 0x000800, 0x000c00, 0x001000,
	0x001800, 0x002000, 0x003000, 0x004000, 0x006000,
}

var codegenOrder = []uint32{16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15}

type huffmanBitWriter struct {
	dst []byte

	bits            uint64
	nbits           uint
	codegenFreq     [codegenCodeCount]int32
	literalFreq     []int32
	offsetFreq      []int32
	codegen         []uint8
	literalEncoding *huffmanEncoder
	offsetEncoding  *huffmanEncoder
	codegenEncoding *huffmanEncoder
}

func NewEncoder() matchfinder.Encoder {
	return &huffmanBitWriter{
		literalFreq:     make([]int32, maxNumLit),
		offsetFreq:      make([]int32, offsetCodeCount),
		codegen:         make([]uint8, maxNumLit+offsetCodeCount+1),
		literalEncoding: newHuffmanEncoder(maxNumLit),
		codegenEncoding: newHuffmanEncoder(codegenCodeCount),
		offsetEncoding:  newHuffmanEncoder(offsetCodeCount),
	}
}

func (w *huffmanBitWriter) Reset() {
	w.bits, w.nbits = 0, 0
}

func (w *huffmanBitWriter) flush() {
	dst := w.dst
	for w.nbits != 0 {
		dst = append(dst, byte(w.bits))
		w.bits >>= 8
		if w.nbits > 8 {
			w.nbits -= 8
		} else {
			w.nbits = 0
		}
	}
	w.bits = 0
	w.dst = dst
}

func (w *huffmanBitWriter) writeBits(b int32, nb uint) {
	w.bits |= uint64(b) << w.nbits
	w.nbits += nb
	if w.nbits >= 48 {
		bits := w.bits
		w.bits >>= 48
		w.nbits -= 48
		w.dst = append(w.dst,
			byte(bits),
			byte(bits>>8),
			byte(bits>>16),
			byte(bits>>24),
			byte(bits>>32),
			byte(bits>>40),
		)
	}
}

func (w *huffmanBitWriter) writeBytes(bytes []byte) {
	if w.nbits&7 != 0 {
		panic("writeBytes with unfinished bits")
	}
	for w.nbits != 0 {
		w.dst = append(w.dst, byte(w.bits))
		w.bits >>= 8
		w.nbits -= 8
	}
	w.dst = append(w.dst, bytes...)
}

func (w *huffmanBitWriter) generateCodegen(numLiterals int, numOffsets int, litEnc, offEnc *huffmanEncoder) {
	for i := range w.codegenFreq {
		w.codegenFreq[i] = 0
	}

	codegen := w.codegen

	cgnl := codegen[:numLiterals]
	for i := range cgnl {
		cgnl[i] = uint8(litEnc.codes[i].len)
	}

	cgnl = codegen[numLiterals : numLiterals+numOffsets]
	for i := range cgnl {
		cgnl[i] = uint8(offEnc.codes[i].len)
	}
	codegen[numLiterals+numOffsets] = badCode

	size := codegen[0]
	count := 1
	outIndex := 0
	for inIndex := 1; size != badCode; inIndex++ {

		nextSize := codegen[inIndex]
		if nextSize == size {
			count++
			continue
		}

		if size != 0 {
			codegen[outIndex] = size
			outIndex++
			w.codegenFreq[size]++
			count--
			for count >= 3 {
				n := 6
				if n > count {
					n = count
				}
				codegen[outIndex] = 16
				outIndex++
				codegen[outIndex] = uint8(n - 3)
				outIndex++
				w.codegenFreq[16]++
				count -= n
			}
		} else {
			for count >= 11 {
				n := 138
				if n > count {
					n = count
				}
				codegen[outIndex] = 18
				outIndex++
				codegen[outIndex] = uint8(n - 11)
				outIndex++
				w.codegenFreq[18]++
				count -= n
			}
			if count >= 3 {

				codegen[outIndex] = 17
				outIndex++
				codegen[outIndex] = uint8(count - 3)
				outIndex++
				w.codegenFreq[17]++
				count = 0
			}
		}
		count--
		for ; count >= 0; count-- {
			codegen[outIndex] = size
			outIndex++
			w.codegenFreq[size]++
		}

		size = nextSize
		count = 1
	}

	codegen[outIndex] = badCode
}

func (w *huffmanBitWriter) dynamicSize(litEnc, offEnc *huffmanEncoder, extraBits int) (size, numCodegens int) {
	numCodegens = len(w.codegenFreq)
	for numCodegens > 4 && w.codegenFreq[codegenOrder[numCodegens-1]] == 0 {
		numCodegens--
	}
	header := 3 + 5 + 5 + 4 + (3 * numCodegens) +
		w.codegenEncoding.bitLength(w.codegenFreq[:]) +
		int(w.codegenFreq[16])*2 +
		int(w.codegenFreq[17])*3 +
		int(w.codegenFreq[18])*7
	size = header +
		litEnc.bitLength(w.literalFreq) +
		offEnc.bitLength(w.offsetFreq) +
		extraBits

	return size, numCodegens
}

func (w *huffmanBitWriter) fixedSize(extraBits int) int {
	return 3 +
		fixedLiteralEncoding.bitLength(w.literalFreq) +
		fixedOffsetEncoding.bitLength(w.offsetFreq) +
		extraBits
}

func (w *huffmanBitWriter) storedSize(in []byte) (int, bool) {
	if in == nil {
		return 0, false
	}
	if len(in) <= maxStoreBlockSize {
		return (len(in) + 5) * 8, true
	}
	return 0, false
}

func (w *huffmanBitWriter) writeCode(c hcode) {
	w.bits |= uint64(c.code) << w.nbits
	w.nbits += uint(c.len)
	if w.nbits >= 48 {
		bits := w.bits
		w.bits >>= 48
		w.nbits -= 48
		w.dst = append(w.dst,
			byte(bits),
			byte(bits>>8),
			byte(bits>>16),
			byte(bits>>24),
			byte(bits>>32),
			byte(bits>>40),
		)
	}
}

func (w *huffmanBitWriter) writeDynamicHeader(numLiterals int, numOffsets int, numCodegens int, isEof bool) {
	var firstBits int32 = 4
	if isEof {
		firstBits = 5
	}
	w.writeBits(firstBits, 3)
	w.writeBits(int32(numLiterals-257), 5)
	w.writeBits(int32(numOffsets-1), 5)
	w.writeBits(int32(numCodegens-4), 4)

	for i := 0; i < numCodegens; i++ {
		value := uint(w.codegenEncoding.codes[codegenOrder[i]].len)
		w.writeBits(int32(value), 3)
	}

	i := 0
	for {
		var codeWord int = int(w.codegen[i])
		i++
		if codeWord == badCode {
			break
		}
		w.writeCode(w.codegenEncoding.codes[uint32(codeWord)])

		switch codeWord {
		case 16:
			w.writeBits(int32(w.codegen[i]), 2)
			i++
		case 17:
			w.writeBits(int32(w.codegen[i]), 3)
			i++
		case 18:
			w.writeBits(int32(w.codegen[i]), 7)
			i++
		}
	}
}

func (w *huffmanBitWriter) writeStoredHeader(length int, isEof bool) {
	var flag int32
	if isEof {
		flag = 1
	}
	w.writeBits(flag, 3)
	w.flush()
	w.writeBits(int32(length), 16)
	w.writeBits(int32(^uint16(length)), 16)
}

func (w *huffmanBitWriter) writeFixedHeader(isEof bool) {

	var value int32 = 2
	if isEof {
		value = 3
	}
	w.writeBits(value, 3)
}

func (w *huffmanBitWriter) writeBlock(matches []matchfinder.Match, eof bool, input []byte) {
	numLiterals, numOffsets := w.makeStatistics(matches, input)

	var extraBits int
	storedSize, storable := w.storedSize(input)
	if storable {

		for lengthCode := lengthCodesStart + 8; lengthCode < numLiterals; lengthCode++ {

			extraBits += int(w.literalFreq[lengthCode]) * int(lengthExtraBits[lengthCode-lengthCodesStart])
		}
		for offsetCode := 4; offsetCode < numOffsets; offsetCode++ {

			extraBits += int(w.offsetFreq[offsetCode]) * int(offsetExtraBits[offsetCode])
		}
	}

	var literalEncoding = fixedLiteralEncoding
	var offsetEncoding = fixedOffsetEncoding
	var size = w.fixedSize(extraBits)

	var numCodegens int

	w.generateCodegen(numLiterals, numOffsets, w.literalEncoding, w.offsetEncoding)
	w.codegenEncoding.generate(w.codegenFreq[:], 7)
	dynamicSize, numCodegens := w.dynamicSize(w.literalEncoding, w.offsetEncoding, extraBits)

	if dynamicSize < size {
		size = dynamicSize
		literalEncoding = w.literalEncoding
		offsetEncoding = w.offsetEncoding
	}

	if storable && storedSize < size {
		w.writeStoredHeader(len(input), eof)
		w.writeBytes(input)
		return
	}

	if literalEncoding == fixedLiteralEncoding {
		w.writeFixedHeader(eof)
	} else {
		w.writeDynamicHeader(numLiterals, numOffsets, numCodegens, eof)
	}

	w.writeTokens(matches, input, literalEncoding.codes, offsetEncoding.codes)
	w.writeCode(literalEncoding.codes[endBlockMarker])
}

func (w *huffmanBitWriter) makeStatistics(matches []matchfinder.Match, input []byte) (numLiterals, numOffsets int) {
	for i := range w.literalFreq {
		w.literalFreq[i] = 0
	}
	for i := range w.offsetFreq {
		w.offsetFreq[i] = 0
	}

	pos := 0
	for _, m := range matches {
		for _, c := range input[pos : pos+m.Unmatched] {
			w.literalFreq[c]++
		}
		pos += m.Unmatched

		if m.Length == 0 {
			continue
		}
		w.literalFreq[lengthCodesStart+lengthCode(m.Length)]++
		w.offsetFreq[offsetCode(m.Distance)]++
		pos += m.Length
	}
	w.literalFreq[endBlockMarker]++

	numLiterals = len(w.literalFreq)
	for w.literalFreq[numLiterals-1] == 0 {
		numLiterals--
	}

	numOffsets = len(w.offsetFreq)
	for numOffsets > 0 && w.offsetFreq[numOffsets-1] == 0 {
		numOffsets--
	}
	if numOffsets == 0 {

		w.offsetFreq[0] = 1
		numOffsets = 1
	}
	w.literalEncoding.generate(w.literalFreq, 15)
	w.offsetEncoding.generate(w.offsetFreq, 15)
	return
}

func (w *huffmanBitWriter) writeTokens(matches []matchfinder.Match, input []byte, leCodes, oeCodes []hcode) {
	pos := 0
	for _, m := range matches {
		for _, c := range input[pos : pos+m.Unmatched] {
			w.writeCode(leCodes[c])
		}
		pos += m.Unmatched

		length := m.Length
		if length == 0 {
			continue
		}
		lengthCode := lengthCode(length)
		w.writeCode(leCodes[lengthCode+lengthCodesStart])
		extraLengthBits := uint(lengthExtraBits[lengthCode])
		if extraLengthBits > 0 {
			extraLength := int32(length - baseMatchLength - lengthBase[lengthCode])
			w.writeBits(extraLength, extraLengthBits)
		}

		offset := m.Distance
		offsetCode := offsetCode(offset)
		w.writeCode(oeCodes[offsetCode])
		extraOffsetBits := uint(offsetExtraBits[offsetCode])
		if extraOffsetBits > 0 {
			extraOffset := int32(offset - baseMatchOffset - offsetBase[offsetCode])
			w.writeBits(extraOffset, extraOffsetBits)
		}
		pos += m.Length
	}
}

func (w *huffmanBitWriter) Encode(dst []byte, src []byte, matches []matchfinder.Match, lastBlock bool) []byte {
	w.dst = dst

	w.writeBlock(matches, lastBlock, src)
	if lastBlock {
		w.flush()
	}

	dst = w.dst
	w.dst = nil
	return dst
}
