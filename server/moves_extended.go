package main

import (
	"bufio"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
)

const (
	movesHeaderMin  = 64
	plainMoveStride = 52
	extMoveStride   = 60
)

func stripExtendedMoves(src, dst string) error {
	in, err := os.Open(src)
	if err != nil {
		return err
	}
	defer in.Close()
	r := bufio.NewReaderSize(in, 1<<20)
	head := make([]byte, movesHeaderMin)
	if _, err := io.ReadFull(r, head); err != nil {
		return fmt.Errorf("move stream header: %w", err)
	}
	if string(head[:8]) != "ORCAMV01" {
		return errors.New("not a move stream")
	}
	le := binary.LittleEndian
	headerSize, moveStride, stateStride := le.Uint32(head[8:]), le.Uint32(head[12:]), le.Uint32(head[16:])
	flags, moves, states := le.Uint32(head[28:]), le.Uint64(head[32:]), le.Uint32(head[40:])
	if headerSize < movesHeaderMin {
		return errors.New("move stream header too short")
	}
	tmp, err := os.CreateTemp(filepath.Dir(dst), filepath.Base(dst)+".*.tmp")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	w := bufio.NewWriterSize(tmp, 1<<20)
	if flags&1 == 0 || moveStride != extMoveStride {

		if _, err := w.Write(head); err == nil {
			_, err = io.Copy(w, r)
		}
	} else {
		le.PutUint32(head[12:], plainMoveStride)
		le.PutUint32(head[28:], flags&^1)
		err = writeAll(w, head)
		if err == nil {
			_, err = io.CopyN(w, r, int64(headerSize-movesHeaderMin)+int64(states)*int64(stateStride))
		}
		move := make([]byte, extMoveStride)
		for i := uint64(0); err == nil && i < moves; i++ {
			if _, err = io.ReadFull(r, move); err == nil {
				err = writeAll(w, move[:plainMoveStride])
			}
		}
		if err == nil {
			_, err = io.Copy(w, r)
		}
	}
	if err == nil {
		err = w.Flush()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err != nil {
		return err
	}
	return os.Rename(tmp.Name(), dst)
}

func writeAll(w io.Writer, b []byte) error {
	_, err := w.Write(b)
	return err
}
