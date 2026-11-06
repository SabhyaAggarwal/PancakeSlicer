package matchfinder

import "io"

type Match struct {
	Unmatched int
	Length    int
	Distance  int
}

type MatchFinder interface {
	FindMatches(dst []Match, src []byte) []Match

	Reset()
}

type Encoder interface {
	Encode(dst []byte, src []byte, matches []Match, lastBlock bool) []byte

	Reset()
}

type Writer struct {
	Dest        io.Writer
	MatchFinder MatchFinder
	Encoder     Encoder

	BlockSize int

	err     error
	inBuf   []byte
	outBuf  []byte
	matches []Match
}

func (w *Writer) Write(p []byte) (n int, err error) {
	if w.err != nil {
		return 0, w.err
	}

	if w.BlockSize == 0 {
		return w.writeBlock(p, false)
	}

	w.inBuf = append(w.inBuf, p...)
	var pos int
	for pos = 0; pos+w.BlockSize <= len(w.inBuf) && w.err == nil; pos += w.BlockSize {
		w.writeBlock(w.inBuf[pos:pos+w.BlockSize], false)
	}
	if pos > 0 {
		n := copy(w.inBuf, w.inBuf[pos:])
		w.inBuf = w.inBuf[:n]
	}

	return len(p), w.err
}

func (w *Writer) writeBlock(p []byte, lastBlock bool) (n int, err error) {
	w.outBuf = w.outBuf[:0]
	w.matches = w.MatchFinder.FindMatches(w.matches[:0], p)
	w.outBuf = w.Encoder.Encode(w.outBuf, p, w.matches, lastBlock)
	_, w.err = w.Dest.Write(w.outBuf)
	return len(p), w.err
}

func (w *Writer) Close() error {
	w.writeBlock(w.inBuf, true)
	w.inBuf = w.inBuf[:0]
	return w.err
}

func (w *Writer) Reset(newDest io.Writer) {
	w.MatchFinder.Reset()
	w.Encoder.Reset()
	w.err = nil
	w.inBuf = w.inBuf[:0]
	w.outBuf = w.outBuf[:0]
	w.matches = w.matches[:0]
	w.Dest = newDest
}
