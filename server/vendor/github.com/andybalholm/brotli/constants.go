package brotli

/* Copyright 2016 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

const contextMapMaxRle = 16

const maxNumberOfBlockTypes = 256

const numLiteralSymbols = 256

const numCommandSymbols = 704

const numBlockLenSymbols = 26

const maxContextMapSymbols = (maxNumberOfBlockTypes + contextMapMaxRle)

const maxBlockTypeSymbols = (maxNumberOfBlockTypes + 2)

const repeatPreviousCodeLength = 16

const repeatZeroCodeLength = 17

const codeLengthCodes = (repeatZeroCodeLength + 1)

const initialRepeatedCodeLength = 8

const largeMaxDistanceBits = 62

const largeMinWbits = 10

const largeMaxWbits = 30

const numDistanceShortCodes = 16

const maxNpostfix = 3

const maxNdirect = 120

const maxDistanceBits = 24

func distanceAlphabetSize(NPOSTFIX uint, NDIRECT uint, MAXNBITS uint) uint {
	return numDistanceShortCodes + NDIRECT + uint(MAXNBITS<<(NPOSTFIX+1))
}

const numDistanceSymbols = 1128

const maxDistance = 0x3FFFFFC

const maxAllowedDistance = 0x7FFFFFFC

const literalContextBits = 6

const distanceContextBits = 2

const windowGap = 16

func maxBackwardLimit(W uint) uint {
	return (uint(1) << W) - windowGap
}
