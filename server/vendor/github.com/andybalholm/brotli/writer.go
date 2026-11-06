package brotli

import (
	"errors"
	"io"

	"github.com/andybalholm/brotli/matchfinder"
)

const (
	BestSpeed          = 0
	BestCompression    = 11
	DefaultCompression = 6
)

type WriterOptions struct {
	Quality int

	LGWin int
}

var (
	errEncode       = errors.New("brotli: encode error")
	errWriterClosed = errors.New("brotli: Writer is closed")
)

func NewWriter(dst io.Writer) *Writer {
	return NewWriterLevel(dst, DefaultCompression)
}

func NewWriterLevel(dst io.Writer, level int) *Writer {
	return NewWriterOptions(dst, WriterOptions{
		Quality: level,
	})
}

func NewWriterOptions(dst io.Writer, options WriterOptions) *Writer {
	w := new(Writer)
	w.options = options
	w.Reset(dst)
	return w
}

func (w *Writer) Reset(dst io.Writer) {
	encoderInitState(w)
	w.params.quality = w.options.Quality
	if w.options.LGWin > 0 {
		w.params.lgwin = uint(w.options.LGWin)
	}
	w.dst = dst
	w.err = nil
}

func (w *Writer) writeChunk(p []byte, op int) (n int, err error) {
	if w.dst == nil {
		return 0, errWriterClosed
	}
	if w.err != nil {
		return 0, w.err
	}

	for {
		availableIn := uint(len(p))
		nextIn := p
		success := encoderCompressStream(w, op, &availableIn, &nextIn)
		bytesConsumed := len(p) - int(availableIn)
		p = p[bytesConsumed:]
		n += bytesConsumed
		if !success {
			return n, errEncode
		}

		if len(p) == 0 || w.err != nil {
			return n, w.err
		}
	}
}

func (w *Writer) Flush() error {
	_, err := w.writeChunk(nil, operationFlush)
	return err
}

func (w *Writer) Close() error {

	_, err := w.writeChunk(nil, operationFinish)
	w.dst = nil
	return err
}

func (w *Writer) Write(p []byte) (n int, err error) {
	return w.writeChunk(p, operationProcess)
}

type nopCloser struct {
	io.Writer
}

func (nopCloser) Close() error { return nil }

func NewWriterV2(dst io.Writer, level int) *matchfinder.Writer {
	if level < 0 {
		level = 0
	} else if level > 9 {
		level = 9
	}
	var mf matchfinder.MatchFinder
	switch level {
	case 0, 1:
		mf = &matchfinder.ZFast{MaxDistance: 1 << 20}
	case 2:
		mf = &matchfinder.ZDFast{MaxDistance: 1 << 20}
	case 3:
		mf = &matchfinder.ZM{MaxDistance: 1 << 20}
	case 4:
		mf = &matchfinder.Trio{MaxDistance: 1 << 20}
	case 5, 6:
		mf = &matchfinder.Bargain1{MaxDistance: 1 << 20}
	case 7:
		mf = &matchfinder.Bargain2{MaxDistance: 1 << 20, Skip: true}
	case 8:
		mf = &matchfinder.Bargain2{MaxDistance: 1 << 20}
	case 9:
		mf = &matchfinder.Bargain3{MaxDistance: 1 << 20}
	}

	w := &matchfinder.Writer{
		Dest:        dst,
		MatchFinder: mf,
		Encoder:     &Encoder{},
		BlockSize:   1 << 16,
	}
	if level < 1 {
		w.Encoder = &FastEncoder{}
	}
	return w
}
