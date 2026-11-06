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
	transferMagic = "ORCAMT02"
	transferBlock = 16384
	matchRows     = 3
)

func transferBlockSize(k int) uint64 {
	return transferBlock >> max(0, 4-k)
}

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
	if headerSize < movesHeaderMin || stride == 0 || stride%4 != 0 || moves >= 1<<31 {
		return fmt.Errorf("move stream header (size %d, stride %d, %d moves) has no transfer form", headerSize, stride, moves)
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

	diffs := make([]uint32, int(moves)*words)
	rowHash := make([]uint64, moves)
	bits := 16
	for bits < 24 && uint64(1)<<bits < 2*moves {
		bits++
	}

	table := make([]int32, 1<<bits)
	key := func(i int) uint32 {
		h := rowHash[i]*0x9e3779b97f4a7c15 ^ rowHash[i+1]*0xc2b2ae3d27d4eb4f ^ rowHash[i+2]*0x165667b19e3779f9
		return uint32(h >> (64 - bits))
	}
	same := func(a, b int) bool {
		x, y := diffs[a*words:(a+1)*words], diffs[b*words:(b+1)*words]
		for w := range x {
			if x[w] != y[w] {
				return false
			}
		}
		return true
	}
	rows := make([]byte, transferBlock*stride)
	out := make([]byte, 0, 8+transferBlock*(stride+12))
	prev := make([]uint32, words)
	var runs, dists, lens, literals []uint32
	for first, k := 0, 0; first < int(moves); k++ {
		n := int(min(moves-uint64(first), transferBlockSize(k)))
		if _, err := io.ReadFull(r, rows[:n*stride]); err != nil {
			return fmt.Errorf("move stream moves: %w", err)
		}
		for i := 0; i < n; i++ {
			d := diffs[(first+i)*words : (first+i+1)*words]
			h := uint64(14695981039346656037)
			for w := range d {
				v := le.Uint32(rows[i*stride+w*4:])
				d[w] = v - prev[w]
				prev[w] = v
				h = (h ^ uint64(d[w])) * 1099511628211
			}
			rowHash[first+i] = h
		}
		runs, dists, lens, literals = runs[:0], dists[:0], lens[:0], literals[:0]
		end, run := first+n, uint32(0)
		for i := first; i < end; {
			if i+matchRows <= end {
				slot := key(i)
				j := int(table[slot]) - 1
				table[slot] = int32(i + 1)
				if j >= 0 && same(j, i) && same(j+1, i+1) && same(j+2, i+2) {
					l := matchRows
					for i+l < end && same(j+l, i+l) {
						l++
					}
					for p := i + 1; p < i+l && p+matchRows <= end; p++ {
						table[key(p)] = int32(p + 1)
					}
					runs, dists, lens = append(runs, run), append(dists, uint32(i-j)), append(lens, uint32(l))
					run = 0
					i += l
					continue
				}
			}
			literals = append(literals, uint32(i))
			run++
			i++
		}
		m, t := len(literals), len(runs)
		out = le.AppendUint32(le.AppendUint32(out[:0], uint32(m)), uint32(t))
		for _, column := range [][]uint32{runs, dists, lens} {
			for _, v := range column {
				out = le.AppendUint32(out, v)
			}
		}
		at := len(out)
		out = out[:at+m*stride]
		for w := 0; w < words; w++ {
			plane := out[at+w*4*m:]
			for li, i := range literals {
				d := diffs[int(i)*words+w]
				plane[li], plane[m+li], plane[2*m+li], plane[3*m+li] = byte(d), byte(d>>8), byte(d>>16), byte(d>>24)
			}
		}
		if err := writeAll(dst, out); err != nil {
			return err
		}
		if err := flush(); err != nil {
			return err
		}
		first += n
	}

	_, err := io.Copy(dst, r)
	return err
}

func serveMoves(w http.ResponseWriter, r *http.Request, path string, st os.FileInfo) {
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": filepath.Base(path)}))
	serveGzipped(w, r, path, st, ".mt2.gz", `"mt2-%x-%x"`, encodeMoveTransfer)
}
