package matchfinder

import (
	"encoding/binary"
	"math"
	"math/bits"
	"slices"
)

type Pathfinder struct {
	MaxDistance int

	MinLength int

	HashLen int

	TableBits int

	ChainLength int

	table []uint32
	chain []uint32

	history []byte

	arrivals     []arrival
	foundMatches []absoluteMatch
	matches      []Match
}

func (q *Pathfinder) Reset() {
	for i := range q.table {
		q.table[i] = 0
	}
	q.history = q.history[:0]
	q.chain = q.chain[:0]
}

type arrival struct {
	length   uint32
	distance uint32
	cost     float32
}

const (
	baseMatchCost float32 = 4
)

func (q *Pathfinder) FindMatches(dst []Match, src []byte) []Match {
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

	var histogram [256]uint32
	for _, b := range src {
		histogram[b]++
	}
	var byteCost [256]float32
	for b, n := range histogram {
		cost := max(math.Log2(float64(len(src))/float64(n)), 1)
		byteCost[b] = float32(cost)
	}

	arrivals := q.arrivals
	if len(arrivals) < len(src) {
		arrivals = make([]arrival, len(src))
		q.arrivals = arrivals
	} else {
		arrivals = arrivals[:len(src)]
		for i := range arrivals {
			arrivals[i] = arrival{}
		}
	}

	if len(q.history) > q.MaxDistance*2 {

		delta := len(q.history) - q.MaxDistance
		copy(q.history, q.history[delta:])
		q.history = q.history[:q.MaxDistance]
		q.chain = q.chain[:q.MaxDistance]

		for i, v := range q.table {
			newV := max(int(v)-delta, 0)
			q.table[i] = uint32(newV)
		}
	}

	historyLen := len(q.history)
	q.history = append(q.history, src...)
	q.chain = append(q.chain, make([]uint32, len(src))...)
	src = q.history

	for i := historyLen; i < len(src)-7; i++ {
		h := ((binary.LittleEndian.Uint64(src[i:]) & (1<<(8*q.HashLen) - 1)) * hashMul64) >> (64 - q.TableBits)
		candidate := int(q.table[h])
		q.table[h] = uint32(i)
		if candidate != 0 {
			delta := i - candidate
			q.chain[i] = uint32(delta)
		}
	}

	foundMatches := q.foundMatches[:0]
	var prevMatch absoluteMatch
	i := historyLen
	for i < len(src)-7 {
		delta := q.chain[i]
		if delta == 0 {
			i++
			continue
		}
		candidate := i - int(delta)
		if candidate <= 0 || i-candidate > q.MaxDistance {
			i++
			continue
		}

		var currentMatch absoluteMatch

		if i >= prevMatch.End && prevMatch != (absoluteMatch{}) {

			prevDistance := prevMatch.Start - prevMatch.Match
			if binary.LittleEndian.Uint32(src[i+1:]) == binary.LittleEndian.Uint32(src[i+1-prevDistance:]) {
				m := extendMatch2(src, i+1, i+1-prevDistance, i+1)
				if m.End-m.Start > q.MinLength {
					currentMatch = m
					foundMatches = append(foundMatches, m)
				}
			}
		}

		if binary.LittleEndian.Uint32(src[candidate:]) == binary.LittleEndian.Uint32(src[i:]) {
			m := extendMatch2(src, i, candidate, max(historyLen, prevMatch.Start))
			if m.End-m.Start > q.MinLength {
				currentMatch = m
				foundMatches = append(foundMatches, m)
			}
		}

		for range q.ChainLength {
			delta := q.chain[candidate]
			if delta == 0 {
				break
			}
			candidate -= int(delta)
			if candidate <= 0 || i-candidate > q.MaxDistance {
				break
			}
			if binary.LittleEndian.Uint32(src[candidate:]) == binary.LittleEndian.Uint32(src[i:]) {
				m := extendMatch2(src, i, candidate, max(historyLen, prevMatch.Start))
				if m.End-m.Start > q.MinLength && m.End-m.Start > currentMatch.End-currentMatch.Start {
					currentMatch = m
					foundMatches = append(foundMatches, m)
				}
			}
		}

		if i < prevMatch.End && currentMatch.End-currentMatch.Start <= prevMatch.End-prevMatch.Start {

			i = prevMatch.End
			continue
		}

		if currentMatch == (absoluteMatch{}) {

			i++
			continue
		}

		prevMatch = currentMatch
		i = currentMatch.End + 2 - q.HashLen
	}

	q.foundMatches = foundMatches

	slices.SortFunc(foundMatches, func(a, b absoluteMatch) int { return a.Start - b.Start })
	matchIndex := 0
	var pending absoluteMatch

	for i := historyLen; i < len(src); i++ {
		var arrivedHere arrival
		if i > historyLen {
			arrivedHere = arrivals[i-historyLen-1]
		}

		unmatched := 0
		if arrivedHere.distance == 0 {
			unmatched = int(arrivedHere.length)
		}
		prevDistance := 0
		if i-unmatched > historyLen {
			prevDistance = int(arrivals[i-historyLen-1-unmatched].distance)
		}

		literalCost := byteCost[src[i]]
		nextArrival := &arrivals[i-historyLen]
		if nextArrival.cost == 0 || arrivedHere.cost+literalCost < nextArrival.cost {
			*nextArrival = arrival{
				cost:   arrivedHere.cost + literalCost,
				length: uint32(unmatched + 1),
			}
		}

		for matchIndex < len(foundMatches) && foundMatches[matchIndex].Start == i {
			m := foundMatches[matchIndex]
			matchIndex++
			if m.End > pending.End {
				pending = m
			}
			matchCost := baseMatchCost + float32(bits.Len(uint(unmatched)))
			if m.Start-m.Match != prevDistance {
				matchCost += float32(bits.Len(uint(m.Start - m.Match)))
			}
			for j := m.Start + q.MinLength; j <= m.End; j++ {
				adjustedCost := matchCost
				if j-m.Start < 6 {

					adjustedCost += float32(6-(j-m.Start)) * 2
				}
				a := &arrivals[j-historyLen-1]
				if a.cost == 0 || arrivedHere.cost+adjustedCost < a.cost {
					*a = arrival{
						length:   uint32(j - m.Start),
						distance: uint32(m.Start - m.Match),
						cost:     arrivedHere.cost + adjustedCost,
					}
				}
			}
		}

		if unmatched == 0 && pending.Start != i && pending.End >= i+q.MinLength &&
			!(arrivedHere.length != 0 && arrivedHere.distance == uint32(pending.Start-pending.Match)) {
			matchCost := baseMatchCost + float32(bits.Len(uint(pending.Start-pending.Match)))
			for j := i + q.MinLength; j <= pending.End; j++ {
				adjustedCost := matchCost
				if j-i < 6 {

					adjustedCost += float32(6-(j-i)) * 2
				}
				a := &arrivals[j-historyLen-1]
				if a.cost == 0 || arrivedHere.cost+adjustedCost < a.cost {
					*a = arrival{
						length:   uint32(j - i),
						distance: uint32(pending.Start - pending.Match),
						cost:     arrivedHere.cost + adjustedCost,
					}
				}
			}
		}

		delta := q.chain[i]
		if delta == 0 {
			continue
		}
		candidate := i - int(delta)
		if candidate <= 0 || i-candidate > q.MaxDistance {
			continue
		}
	}

	matches := q.matches[:0]
	i = len(arrivals) - 1
	for i >= 0 {
		a := arrivals[i]
		if a.distance > 0 {
			matches = append(matches, Match{
				Length:   int(a.length),
				Distance: int(a.distance),
			})
			i -= int(a.length)
		} else {
			if len(matches) == 0 {
				matches = append(matches, Match{})
			}
			matches[len(matches)-1].Unmatched = int(a.length)
			i -= int(a.length)
		}
	}
	q.matches = matches

	slices.Reverse(matches)

	return append(dst, matches...)
}
