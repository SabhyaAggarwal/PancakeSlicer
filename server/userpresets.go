package main

import (
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"unicode"
)

type UserPreset struct {
	Name      string          `json:"name"`
	Type      string          `json:"type,omitempty"`
	Inherits  string          `json:"inherits,omitempty"`
	Overrides json.RawMessage `json:"overrides"`
	metadata  map[string]json.RawMessage
}

func (p *UserPreset) UnmarshalJSON(raw []byte) error {
	type wire UserPreset
	var value wire
	if err := json.Unmarshal(raw, &value); err != nil {
		return err
	}
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(raw, &fields); err != nil {
		return err
	}
	*p = UserPreset(value)
	p.metadata = fields
	return nil
}
func (p UserPreset) MarshalJSON() ([]byte, error) {
	type wire UserPreset
	raw, err := json.Marshal(wire(p))
	if err != nil {
		return nil, err
	}
	fields := map[string]json.RawMessage{}
	for key, value := range p.metadata {
		fields[key] = value
	}
	for _, key := range []string{"name", "type", "inherits", "overrides"} {
		delete(fields, key)
	}
	var known map[string]json.RawMessage
	if err := json.Unmarshal(raw, &known); err != nil {
		return nil, err
	}
	for key, value := range known {
		fields[key] = value
	}
	return json.Marshal(fields)
}

type UserPresetStore struct {
	dir string
	mu  sync.Mutex
}

func NewUserPresetStore(dir string) (*UserPresetStore, error) {
	return &UserPresetStore{dir: dir}, os.MkdirAll(dir, 0o755)
}

var errPresetNotFound = errors.New("user preset not found")

func validPresetName(name string) error {
	if name == "" || len(name) > 200 || strings.TrimSpace(name) != name {
		return errors.New("a user preset name is 1 to 200 characters without leading or trailing spaces")
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return errors.New("a user preset name has no control characters")
		}
	}
	return nil
}

func (s *UserPresetStore) path(typ, name string) string {
	if typ != "" {
		name = typ + "\x00" + name
	}
	return filepath.Join(s.dir, fmt.Sprintf("%x.json", sha256.Sum256([]byte(name))))
}

func (s *UserPresetStore) Get(name string) (*UserPreset, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	b, err := os.ReadFile(s.path("", name))
	if errors.Is(err, os.ErrNotExist) {
		return nil, errPresetNotFound
	} else if err != nil {
		return nil, err
	}
	var p UserPreset
	return &p, json.Unmarshal(b, &p)
}

func (s *UserPresetStore) Put(p *UserPreset) error {
	b, err := json.MarshalIndent(p, "", "  ")
	if err != nil {
		return err
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	tmp, err := os.CreateTemp(s.dir, ".put-*")
	if err != nil {
		return err
	}
	_, werr := tmp.Write(append(b, '\n'))
	if err := errors.Join(werr, tmp.Close()); err != nil {
		os.Remove(tmp.Name())
		return err
	}
	defer os.Remove(tmp.Name())
	return os.Rename(tmp.Name(), s.path(p.Type, p.Name))
}

func (s *UserPresetStore) Delete(name string) error { return s.DeleteTyped("", name) }

func (s *UserPresetStore) DeleteTyped(typ, name string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	err := os.Remove(s.path(typ, name))
	if errors.Is(err, os.ErrNotExist) {
		return errPresetNotFound
	}
	return err
}

func (s *UserPresetStore) List() ([]UserPreset, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	entries, err := os.ReadDir(s.dir)
	if err != nil {
		return nil, err
	}
	out := []UserPreset{}
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".json" {
			continue
		}
		b, err := os.ReadFile(filepath.Join(s.dir, e.Name()))
		if err != nil {
			return nil, err
		}
		var p UserPreset
		if err := json.Unmarshal(b, &p); err != nil {
			return nil, err
		}
		if validPresetType(p.Type) {
			out = append(out, p)
		}
	}
	sort.Slice(out, func(i, j int) bool {
		if out[i].Type != out[j].Type {
			return out[i].Type < out[j].Type
		}
		return out[i].Name < out[j].Name
	})
	return out, nil
}
