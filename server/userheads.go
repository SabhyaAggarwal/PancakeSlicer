package main

import (
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
)

var userHeadID = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)

func userHeadDir(c Config) string { return filepath.Join(c.DataDir, "nonplanar", "heads") }

func (s *Server) listUserHeads(w http.ResponseWriter, r *http.Request) {
	entries, err := os.ReadDir(userHeadDir(s.cfg))
	out := []json.RawMessage{}
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		fail(w, 500, err.Error())
		return
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].Name() < entries[j].Name() })
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".json" {
			continue
		}
		if b, err := os.ReadFile(filepath.Join(userHeadDir(s.cfg), e.Name())); err == nil && json.Valid(b) {
			out = append(out, b)
		}
	}
	writeJSON(w, 200, out)
}

func (s *Server) saveUserHead(w http.ResponseWriter, r *http.Request) {
	b, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 1<<20))
	if err != nil {
		fail(w, 413, err.Error())
		return
	}
	var head struct {
		Schema string            `json:"schema"`
		ID     string            `json:"id"`
		Boxes  []json.RawMessage `json:"boxes"`
	}
	if json.Unmarshal(b, &head) != nil || head.Schema != "orca.head_envelope.v1" || len(head.Boxes) == 0 {
		fail(w, 400, "expected an orca.head_envelope.v1 head with boxes")
		return
	}
	if !userHeadID.MatchString(head.ID) {
		fail(w, 400, "Head id must contain only letters, digits, hyphens and underscores.")
		return
	}
	dir := userHeadDir(s.cfg)
	for _, existing := range []string{filepath.Join(s.cfg.Resources, "nonplanar", "heads", head.ID+".json"), filepath.Join(dir, head.ID+".json")} {
		if _, err := os.Stat(existing); err == nil {
			fail(w, 409, "A head with this id already exists. Choose a new user head id.")
			return
		}
	}
	if err := os.MkdirAll(dir, 0o755); err != nil {
		fail(w, 500, err.Error())
		return
	}
	tmp, err := os.CreateTemp(dir, ".put-*")
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	_, werr := tmp.Write(b)
	if err := errors.Join(werr, tmp.Close()); err != nil {
		os.Remove(tmp.Name())
		fail(w, 500, err.Error())
		return
	}
	defer os.Remove(tmp.Name())
	if err := os.Rename(tmp.Name(), filepath.Join(dir, head.ID+".json")); err != nil {
		fail(w, 500, err.Error())
		return
	}
	w.Write(b)
}

func copyUserHeads(c Config, datadir string) error {
	entries, err := os.ReadDir(userHeadDir(c))
	if errors.Is(err, os.ErrNotExist) || len(entries) == 0 {
		return nil
	} else if err != nil {
		return err
	}
	dst := filepath.Join(datadir, "nonplanar", "heads")
	if err := os.MkdirAll(dst, 0o755); err != nil {
		return err
	}
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".json" {
			continue
		}
		b, err := os.ReadFile(filepath.Join(userHeadDir(c), e.Name()))
		if err != nil {
			return err
		}
		if err := os.WriteFile(filepath.Join(dst, e.Name()), b, 0o644); err != nil {
			return err
		}
	}
	return nil
}
