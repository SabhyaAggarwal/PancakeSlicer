package main

import (
	"bufio"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
)

const (
	transferMagic = "ORCAMT01"
	transferBlock = 16384
)

func encodeMoveTransfer(src io.Reader, dst io.Writer, flush func() error) error {
	r := bufio.NewReaderSize(src, 1<<20)
	head := make([]byte, movesHeaderMin)
	if _, err := io.ReadFull(r, head); err != nil {
		return fmt.Errorf("move stream header: %w", err)
	}
	if string(head[:8]) != "ORCAMV01" {
		return errors.New("not a move stream")
	}
	le := binary.LittleEndian
	headerSize, stride, stateStride := le.Uint32(head[8:]), int(le.Uint32(head[12:])), le.Uint32(head[16:])
	moves, states := le.Uint64(head[32:]), le.Uint32(head[40:])
	if headerSize < movesHeaderMin || stride == 0 || stride%4 != 0 {
		return fmt.Errorf("move stream header (size %d, stride %d) has no transfer form", headerSize, stride)
	}
	copy(head, transferMagic)
	le.PutUint64(head[56:], transferBlock)
	if err := writeAll(dst, head); err != nil {
		return err
	}
	if _, err := io.CopyN(dst, r, int64(headerSize-movesHeaderMin)+int64(states)*int64(stateStride)); err != nil {
		return err
	}
	if err := flush(); err != nil {
		return err
	}
	words := stride / 4
	rows := make([]byte, transferBlock*stride)
	planes := make([]byte, transferBlock*stride)
	for left := moves; left > 0; {
		n := int(min(left, transferBlock))
		if _, err := io.ReadFull(r, rows[:n*stride]); err != nil {
			return fmt.Errorf("move stream moves: %w", err)
		}
		for w := 0; w < words; w++ {
			plane := planes[w*4*n:]
			prev := uint32(0)
			for i := 0; i < n; i++ {
				v := le.Uint32(rows[i*stride+w*4:])
				d := v - prev
				prev = v
				plane[i], plane[n+i], plane[2*n+i], plane[3*n+i] = byte(d), byte(d>>8), byte(d>>16), byte(d>>24)
			}
		}
		if err := writeAll(dst, planes[:n*stride]); err != nil {
			return err
		}
		if err := flush(); err != nil {
			return err
		}
		left -= uint64(n)
	}
	_, err := io.Copy(dst, r)
	return err
}

func serveMoves(w http.ResponseWriter, r *http.Request, path string, st os.FileInfo) {
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": filepath.Base(path)}))
	serveGzipped(w, r, path, st, ".mt1.gz", `"mt1-%x-%x"`, encodeMoveTransfer)
}
