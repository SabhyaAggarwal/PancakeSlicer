package matchfinder

type absoluteMatch struct {
	Start int

	End int

	Match int
}

type matchEmitter struct {
	Dst []Match

	NextEmit int
}

func (e *matchEmitter) emit(m absoluteMatch) {
	e.Dst = append(e.Dst, Match{
		Unmatched: m.Start - e.NextEmit,
		Length:    m.End - m.Start,
		Distance:  m.Start - m.Match,
	})
	e.NextEmit = m.End
}
