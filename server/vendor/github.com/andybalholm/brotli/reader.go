package brotli

import (
	"errors"
	"io"
)

type decodeError int

func (err decodeError) Error() string {
	return "brotli: " + string(decoderErrorString(int(err)))
}

var errExcessiveInput = errors.New("brotli: excessive input")
var errInvalidState = errors.New("brotli: invalid state")

const readBufSize = 32 * 1024

func NewReader(src io.Reader) *Reader {
	r := new(Reader)
	r.Reset(src)
	return r
}

func (r *Reader) Reset(src io.Reader) error {
	if r.error_code < 0 {

		*r = Reader{buf: r.buf}
	}

	decoderStateInit(r)
	r.src = src
	if r.buf == nil {
		r.buf = make([]byte, readBufSize)
	}
	return nil
}

func (r *Reader) Read(p []byte) (n int, err error) {
	if !decoderHasMoreOutput(r) && len(r.in) == 0 {
		m, readErr := r.src.Read(r.buf)
		if m == 0 {
			if readErr == io.EOF && r.state != stateDone {
				readErr = io.ErrUnexpectedEOF
			}

			return 0, readErr
		}
		r.in = r.buf[:m]
	}

	if len(p) == 0 {
		return 0, nil
	}

	for {
		var written uint
		in_len := uint(len(r.in))
		out_len := uint(len(p))
		in_remaining := in_len
		out_remaining := out_len
		result := decoderDecompressStream(r, &in_remaining, &r.in, &out_remaining, &p)
		written = out_len - out_remaining
		n = int(written)

		switch result {
		case decoderResultSuccess:
			if len(r.in) > 0 {
				return n, errExcessiveInput
			}
			return n, nil
		case decoderResultError:
			return n, decodeError(decoderGetErrorCode(r))
		case decoderResultNeedsMoreOutput:
			if n == 0 {
				return 0, io.ErrShortBuffer
			}
			return n, nil
		case decoderNeedsMoreInput:
		}

		if len(r.in) != 0 {
			return 0, errInvalidState
		}

		if n > 0 {
			return n, nil
		}

		encN, err := r.src.Read(r.buf)
		if encN == 0 {

			if err == io.EOF {
				return 0, io.ErrUnexpectedEOF
			}
			return 0, err
		}
		r.in = r.buf[:encN]
	}
}
