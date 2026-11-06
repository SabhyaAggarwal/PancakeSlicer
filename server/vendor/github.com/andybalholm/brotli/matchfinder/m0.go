package matchfinder

import (
	"encoding/binary"
)

type M0 struct {
	Lazy bool

	MaxDistance int
	MaxLength   int
}

func (M0) Reset() {}

const (
	m0HashLen = 5

	m0TableBits = 16
	m0TableSize = 1 << m0TableBits
	m0Shift     = 32 - m0TableBits

	m0TableMask = m0TableSize - 1
)

func (m M0) hash(data uint64) uint64 {
	hash := (data << (64 - 8*m0HashLen)) * hashMul64
	return hash >> (64 - m0TableBits)
}

func (m M0) FindMatches(dst []Match, src []byte) []Match {
	const inputMargin = 16 - 1
	const minNonLiteralBlockSize = 1 + 1 + inputMargin

	if len(src) < minNonLiteralBlockSize {
		dst = append(dst, Match{
			Unmatched: len(src),
		})
		return dst
	}
	if len(src) > 65536 {
		panic("block too long")
	}

	var table [m0TableSize]uint16

	sLimit := len(src) - inputMargin

	nextEmit := 0

	s := 1
	nextHash := m.hash(binary.LittleEndian.Uint64(src[s:]))

	for {

		skip := 32

		nextS := s
		candidate := 0
		for {
			s = nextS
			bytesBetweenHashLookups := skip >> 5
			nextS = s + bytesBetweenHashLookups
			skip += bytesBetweenHashLookups
			if nextS > sLimit {
				goto emitRemainder
			}
			candidate = int(table[nextHash&m0TableMask])
			table[nextHash&m0TableMask] = uint16(s)
			nextHash = m.hash(binary.LittleEndian.Uint64(src[nextS:]))
			if m.MaxDistance != 0 && s-candidate > m.MaxDistance {
				continue
			}
			if binary.LittleEndian.Uint32(src[s:]) == binary.LittleEndian.Uint32(src[candidate:]) {
				break
			}
		}

		base := s
		s = extendMatch(src, candidate+4, s+4)

		origBase := base
		if m.Lazy && base+1 < sLimit {
			newBase := base + 1
			h := m.hash(binary.LittleEndian.Uint64(src[newBase:]))
			newCandidate := int(table[h&m0TableMask])
			table[h&m0TableMask] = uint16(newBase)
			okDistance := true
			if m.MaxDistance != 0 && newBase-newCandidate > m.MaxDistance {
				okDistance = false
			}
			if okDistance && binary.LittleEndian.Uint32(src[newBase:]) == binary.LittleEndian.Uint32(src[newCandidate:]) {
				newS := extendMatch(src, newCandidate+4, newBase+4)
				if newS-newBase > s-base+1 {
					s = newS
					base = newBase
					candidate = newCandidate
				}
			}
		}

		if m.MaxLength != 0 && s-base > m.MaxLength {
			s = base + m.MaxLength
		}
		dst = append(dst, Match{
			Unmatched: base - nextEmit,
			Length:    s - base,
			Distance:  base - candidate,
		})
		nextEmit = s
		if s >= sLimit {
			goto emitRemainder
		}

		if m.Lazy {

			for i := origBase + 2; i < s-3; i++ {
				x := binary.LittleEndian.Uint64(src[i:])
				table[m.hash(x)&m0TableMask] = uint16(i)
			}
		} else {

			x := binary.LittleEndian.Uint64(src[base+1:])
			table[m.hash(x)&m0TableMask] = uint16(base + 1)
		}

		x := binary.LittleEndian.Uint64(src[s-3:])
		table[m.hash(x)&m0TableMask] = uint16(s - 3)
		table[m.hash(x>>8)&m0TableMask] = uint16(s - 2)
		prevHash := m.hash(x >> 16)
		table[prevHash&m0TableMask] = uint16(s - 1)
		nextHash = m.hash(x >> 24)
	}

emitRemainder:
	if nextEmit < len(src) {
		dst = append(dst, Match{
			Unmatched: len(src) - nextEmit,
		})
	}
	return dst
}
