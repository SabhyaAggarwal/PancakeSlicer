package matchfinder

import "fmt"

type TextEncoder struct{}

func (t TextEncoder) Reset() {}

func (t TextEncoder) Encode(dst []byte, src []byte, matches []Match, lastBlock bool) []byte {
	pos := 0
	for _, m := range matches {
		if m.Unmatched > 0 {
			dst = append(dst, src[pos:pos+m.Unmatched]...)
			pos += m.Unmatched
		}
		if m.Length > 0 {
			dst = append(dst, []byte(fmt.Sprintf("<%d,%d>", m.Length, m.Distance))...)
			pos += m.Length
		}
	}
	if pos < len(src) {
		dst = append(dst, src[pos:]...)
	}
	return dst
}

type NoMatchFinder struct{}

func (n NoMatchFinder) Reset() {}

func (n NoMatchFinder) FindMatches(dst []Match, src []byte) []Match {
	return append(dst, Match{
		Unmatched: len(src),
	})
}

type AutoReset struct {
	MatchFinder
}

func (a AutoReset) FindMatches(dst []Match, src []byte) []Match {
	a.Reset()
	return a.MatchFinder.FindMatches(dst, src)
}
