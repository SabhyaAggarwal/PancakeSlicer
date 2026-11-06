package matchfinder

import (
	"bytes"
	"encoding/binary"
	"math/bits"
	"runtime"
)

type M4 struct {
	MaxDistance int

	MinLength int

	HashLen int

	TableBits int

	ChainLength int

	DistanceBitCost int

	table []uint32
	chain []uint32

	history []byte
}

func (q *M4) Reset() {
	for i := range q.table {
		q.table[i] = 0
	}
	q.history = q.history[:0]
	q.chain = q.chain[:0]
}

func (q *M4) score(m absoluteMatch) int {
	return (m.End-m.Start)*256 + (bits.LeadingZeros32(uint32(m.Start-m.Match))-32)*q.DistanceBitCost
}

func (q *M4) FindMatches(dst []Match, src []byte) []Match {
	if q.MaxDistance == 0 {
		q.MaxDistance = 65535
	}
	if q.MinLength == 0 {
		q.MinLength = 4
	}
	if q.HashLen == 0 {
		q.HashLen = 6
	}
	if q.TableBits == 0 {
		q.TableBits = 17
	}
	if len(q.table) < 1<<q.TableBits {
		q.table = make([]uint32, 1<<q.TableBits)
	}

	e := matchEmitter{Dst: dst}

	if len(q.history) > q.MaxDistance*2 {

		delta := len(q.history) - q.MaxDistance
		copy(q.history, q.history[delta:])
		q.history = q.history[:q.MaxDistance]
		if q.ChainLength > 0 {
			copy(q.chain, q.chain[delta:])
			q.chain = q.chain[:q.MaxDistance]
		}

		for i, v := range q.table {
			newV := int(v) - delta
			if newV < 0 {
				newV = 0
			}
			q.table[i] = uint32(newV)
		}
	}

	e.NextEmit = len(q.history)
	q.history = append(q.history, src...)
	if q.ChainLength > 0 {
		q.chain = append(q.chain, make([]uint32, len(src))...)
	}
	src = q.history

	var matches [3]absoluteMatch
	for i := e.NextEmit; i < len(src)-7; i++ {
		if matches[0] != (absoluteMatch{}) && i >= matches[0].End {

			if matches[1] != (absoluteMatch{}) {
				if matches[1].End > matches[0].Start {
					matches[1].End = matches[0].Start
				}
				if matches[1].End-matches[1].Start >= q.MinLength && q.score(matches[1]) > 0 {
					e.emit(matches[1])
				}
			}
			e.emit(matches[0])
			matches = [3]absoluteMatch{}
		}

		if matches[0] == (absoluteMatch{}) && len(e.Dst) > 0 {
			prevDistance := e.Dst[len(e.Dst)-1].Distance
			if binary.LittleEndian.Uint32(src[i+1:]) == binary.LittleEndian.Uint32(src[i+1-prevDistance:]) {

				m := extendMatch2(src, i+1, i+1-prevDistance, e.NextEmit+1)
				if m.End-m.Start >= q.MinLength {
					matches[0] = m
				}
			}
		}

		h := ((binary.LittleEndian.Uint64(src[i:]) & (1<<(8*q.HashLen) - 1)) * hashMul64) >> (64 - q.TableBits)
		candidate := int(q.table[h])
		q.table[h] = uint32(i)
		if q.ChainLength > 0 && candidate != 0 {
			delta := i - candidate
			q.chain[i] = uint32(delta)
		}

		if i < matches[0].End && i != matches[0].End+2-q.HashLen {
			continue
		}
		if candidate == 0 || i-candidate > q.MaxDistance {
			continue
		}

		var currentMatch absoluteMatch

		if binary.LittleEndian.Uint32(src[candidate:]) == binary.LittleEndian.Uint32(src[i:]) {
			m := extendMatch2(src, i, candidate, e.NextEmit)
			if m.End-m.Start > q.MinLength && q.score(m) > 0 {
				currentMatch = m
			}
		}

		for j := 0; j < q.ChainLength; j++ {
			delta := q.chain[candidate]
			if delta == 0 {
				break
			}
			candidate -= int(delta)
			if candidate <= 0 || i-candidate > q.MaxDistance {
				break
			}
			if binary.LittleEndian.Uint32(src[candidate:]) == binary.LittleEndian.Uint32(src[i:]) {
				m := extendMatch2(src, i, candidate, e.NextEmit)
				if m.End-m.Start > q.MinLength && q.score(m) > q.score(currentMatch) {
					currentMatch = m
				}
			}
		}

		if currentMatch.End-currentMatch.Start < q.MinLength {
			continue
		}

		overlapPenalty := 0
		if matches[0] != (absoluteMatch{}) {
			overlapPenalty = 275
			if currentMatch.Start <= matches[1].End {

				overlapPenalty = 0
			}
		}

		if q.score(currentMatch) <= q.score(matches[0])+overlapPenalty {
			continue
		}

		matches = [3]absoluteMatch{
			currentMatch,
			matches[0],
			matches[1],
		}

		if matches[2] == (absoluteMatch{}) {
			continue
		}

		switch {
		case matches[0].Start < matches[2].End:

			matches = [3]absoluteMatch{
				matches[0],
				matches[2],
				absoluteMatch{},
			}

		case matches[0].Start < matches[2].End+q.MinLength:

			e.emit(matches[2])
			matches = [3]absoluteMatch{
				matches[0],
				absoluteMatch{},
				absoluteMatch{},
			}

		default:

			if matches[2].End > matches[1].Start {
				matches[2].End = matches[1].Start
				if q.ChainLength > 0 && matches[2].End-matches[2].Start >= q.MinLength {

					pos := matches[2].Start
					for {
						delta := int(q.chain[pos])
						if delta == 0 {
							break
						}
						pos -= delta
						if pos <= matches[2].Match {
							break
						}
						if bytes.Equal(src[matches[2].Start:matches[2].End], src[pos:pos+matches[2].End-matches[2].Start]) {
							matches[2].Match = pos
							break
						}
					}
				}
			}
			if matches[2].End-matches[2].Start >= q.MinLength && q.score(matches[2]) > 0 {
				e.emit(matches[2])
			}
			matches[2] = absoluteMatch{}
		}
	}

	if matches[1] != (absoluteMatch{}) {
		if matches[1].End > matches[0].Start {
			matches[1].End = matches[0].Start
		}
		if matches[1].End-matches[1].Start >= q.MinLength && q.score(matches[1]) > 0 {
			e.emit(matches[1])
		}
	}
	if matches[0] != (absoluteMatch{}) {
		e.emit(matches[0])
	}

	dst = e.Dst
	if e.NextEmit < len(src) {
		dst = append(dst, Match{
			Unmatched: len(src) - e.NextEmit,
		})
	}

	return dst
}

const hashMul64 = 0x1E35A7BD1E35A7BD

func extendMatch(src []byte, i, j int) int {
	switch runtime.GOARCH {
	case "amd64", "arm64":

		for j+8 < len(src) {
			iBytes := binary.LittleEndian.Uint64(src[i:])
			jBytes := binary.LittleEndian.Uint64(src[j:])
			if iBytes != jBytes {

				return j + bits.TrailingZeros64(iBytes^jBytes)>>3
			}
			i, j = i+8, j+8
		}
	case "386":

		for j+4 < len(src) {
			iBytes := binary.LittleEndian.Uint32(src[i:])
			jBytes := binary.LittleEndian.Uint32(src[j:])
			if iBytes != jBytes {
				return j + bits.TrailingZeros32(iBytes^jBytes)>>3
			}
			i, j = i+4, j+4
		}
	}
	for ; j < len(src) && src[i] == src[j]; i, j = i+1, j+1 {
	}
	return j
}

func extendMatch2(src []byte, start, candidate, min int) absoluteMatch {
	end := extendMatch(src, candidate+4, start+4)
	for start > min && candidate > 0 && src[start-1] == src[candidate-1] {
		start--
		candidate--
	}
	return absoluteMatch{
		Start: start,
		End:   end,
		Match: candidate,
	}
}
