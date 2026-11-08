package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strings"
	"sync"
)

type libraryStore struct {
	dir string
	mu  sync.Mutex
}

type libraryV4Envelope struct {
	Document   map[string]json.RawMessage `json:"document"`
	Operations map[string]libraryReceipt  `json:"operations"`
}

const libraryV4File = "library.json"

func starterRecord(i int) map[string]any {
	s := starterProfiles[i]
	values := map[string]string{}
	for k, v := range starterValues {
		values[k] = v
	}
	for k, v := range s.values {
		values[k] = v
	}
	p := map[string]any{"id": "profile:starter-" + s.id, "type": "print", "name": s.name, "revision": 1, "values": values, "scales": defaultScales}
	speedSeed := map[string]string{"everyday": "standard", "strong": "strength", "fine-detail": "fine", "draft": "draft"}[s.id]
	p["speedModel"] = newSpeedModel(speedSeed)
	for _, f := range profileRules.SpeedFeatures {
		delete(values, f.Key)
	}
	if s.auto.PreferredHeight != nil {
		p["preferredHeight"] = *s.auto.PreferredHeight
	}
	return p
}

func seededLibraryV4(epoch string) map[string]json.RawMessage {
	profiles := []map[string]any{}
	for i := range starterProfiles {
		profiles = append(profiles, starterRecord(i))
	}
	doc := map[string]json.RawMessage{"schema": json.RawMessage(`4`), "revision": json.RawMessage(`1`), "setups": json.RawMessage(`{}`)}
	doc["profiles"], _ = json.Marshal(profiles)
	if epoch != "" {
		doc["epoch"], _ = json.Marshal(epoch)
	}
	return doc
}

func printCatalog() []map[string]any {
	out := []map[string]any{}
	for i, s := range starterProfiles {
		out = append(out, map[string]any{"id": "profile:starter-" + s.id, "name": s.name, "description": s.description, "layers": s.layers, "record": starterRecord(i)})
	}
	return out
}

type v4Record struct {
	ID             string                     `json:"id"`
	Type           string                     `json:"type"`
	Name           string                     `json:"name"`
	Revision       *uint64                    `json:"revision"`
	Archived       json.RawMessage            `json:"archived,omitempty"`
	Vendor         string                     `json:"vendor,omitempty"`
	Model          string                     `json:"model,omitempty"`
	Nozzle         float64                    `json:"nozzle,omitempty"`
	Machine        map[string]json.RawMessage `json:"machine,omitempty"`
	PrintOverrides map[string]json.RawMessage `json:"printOverrides,omitempty"`
	Family         string                     `json:"family,omitempty"`
	Brand          string                     `json:"brand,omitempty"`
	Material       string                     `json:"material,omitempty"`
	Colour         string                     `json:"colour,omitempty"`
	Settings       map[string]json.RawMessage `json:"settings,omitempty"`
	Fits           *struct {
		Models  []string  `json:"models,omitempty"`
		Nozzles []float64 `json:"nozzles,omitempty"`
	} `json:"fits,omitempty"`
	Values          map[string]json.RawMessage `json:"values,omitempty"`
	Scales          []autoScale                `json:"scales,omitempty"`
	PreferredHeight *float64                   `json:"preferredHeight,omitempty"`
	SpeedModel      *speedModel                `json:"speedModel,omitempty"`
}

func (r *v4Record) live() bool { return len(r.Archived) == 0 || string(r.Archived) == "null" }

func v4Profiles(doc map[string]json.RawMessage) (map[string]*v4Record, error) {
	var schema int
	if json.Unmarshal(doc["schema"], &schema) != nil || schema != 4 {
		return nil, errors.New("the library schema must be 4")
	}
	var raws []json.RawMessage
	if json.Unmarshal(doc["profiles"], &raws) != nil || raws == nil {
		return nil, errors.New("profiles must be an array")
	}
	out := map[string]*v4Record{}
	for _, raw := range raws {
		var r v4Record
		dec := json.NewDecoder(bytes.NewReader(raw))
		dec.DisallowUnknownFields()
		if err := dec.Decode(&r); err != nil {
			return nil, fmt.Errorf("a profile has an unknown or malformed field: %v", err)
		}
		bad := func(why string) error { return fmt.Errorf("%s: %s", r.Name, why) }
		if !strings.HasPrefix(r.ID, "profile:") || len(r.ID) <= 8 || out[r.ID] != nil {
			return nil, errors.New("every profile needs its own profile: ID")
		}
		if validProfileName(r.Name) != nil || r.Revision == nil || strings.Contains(r.Name, " @") {
			return nil, bad("needs a name (without a catalog \"@\" suffix) and a revision")
		}
		if !r.live() {
			var a struct {
				At          float64 `json:"at"`
				OperationID string  `json:"operationId"`
			}
			if json.Unmarshal(r.Archived, &a) != nil || a.At <= 0 || a.OperationID == "" {
				return nil, bad("archived keeps at and operationId")
			}
		}
		printerOnly := r.Vendor != "" || r.Model != "" || r.Nozzle != 0 || r.Machine != nil || r.PrintOverrides != nil
		filamentOnly := r.Family != "" || r.Brand != "" || r.Material != "" || r.Colour != "" || r.Settings != nil || r.Fits != nil
		printOnly := r.Values != nil || r.Scales != nil || r.PreferredHeight != nil || r.SpeedModel != nil
		switch r.Type {
		case "printer":
			if filamentOnly || printOnly || r.Vendor == "" || r.Model == "" || !(r.Nozzle > 0) {
				return nil, bad("a printer is {vendor, model, nozzle, machine?, printOverrides?}")
			}
			if err := validatePrintOverrides(map[string]json.RawMessage{"printOverrides": must(json.Marshal(r.PrintOverrides))}, "machine"); r.PrintOverrides != nil && err != nil {
				return nil, bad(err.Error())
			}
		case "filament":
			if printerOnly || printOnly || r.Family == "" || strings.Contains(r.Family, " @") {
				return nil, bad("a filament is {family, brand?, material?, colour?, settings?, fits?}")
			}
		case "print":
			if printerOnly || filamentOnly {
				return nil, bad("a print profile is {values?, scales?, preferredHeight?, speedModel?}")
			}
			if err := validateAuto(map[string]json.RawMessage{"auto": must(json.Marshal(autoProfile{Schema: 3, Scales: append([]autoScale{}, r.Scales...), PreferredHeight: r.PreferredHeight, SpeedModel: r.SpeedModel}))}, "process"); err != nil {
				return nil, bad(strings.TrimPrefix(err.Error(), "auto."))
			}
		default:
			return nil, bad("type is printer, filament or print")
		}
		out[r.ID] = &r
	}
	var setups map[string]json.RawMessage
	if json.Unmarshal(doc["setups"], &setups) != nil || setups == nil {
		return nil, errors.New("setups must be an object")
	}
	return out, nil
}

func (l *libraryStore) read() (*libraryV4Envelope, error) {
	raw, err := os.ReadFile(filepath.Join(l.dir, libraryV4File))
	if errors.Is(err, os.ErrNotExist) {
		return &libraryV4Envelope{Document: seededLibraryV4(""), Operations: map[string]libraryReceipt{}}, nil
	}
	var e libraryV4Envelope
	if err == nil {
		err = json.Unmarshal(raw, &e)
	}
	if err == nil && (e.Document == nil || e.Operations == nil) {
		err = errors.New("invalid stored library")
	}
	if err == nil {
		migrateProfiles(e.Document)
	}
	return &e, err
}

func migrateProfiles(doc map[string]json.RawMessage) {
	var profiles []map[string]json.RawMessage
	if json.Unmarshal(doc["profiles"], &profiles) != nil {
		return
	}
	changed := false
	for _, p := range profiles {
		if stringValue(p["type"]) != "print" {
			continue
		}
		if legacy, ok := p["intent"]; ok {
			values := map[string]json.RawMessage{}
			json.Unmarshal(legacy, &values)
			var owned map[string]json.RawMessage
			json.Unmarshal(p["values"], &owned)
			for k, v := range owned {
				values[k] = v
			}
			p["values"], _ = json.Marshal(values)
			delete(p, "intent")
			changed = true
		}
		var rows []autoScale
		json.Unmarshal(p["scales"], &rows)
		var legacy struct {
			Schema           int                `json:"schema"`
			Intent           string             `json:"intent"`
			Multipliers      map[string]float64 `json:"multipliers"`
			OuterWallCeiling float64            `json:"outerWallCeiling"`
		}
		json.Unmarshal(p["speedModel"], &legacy)
		if len(p["speedModel"]) == 0 && len(rows) > 0 {
			seed := "standard"
			switch stringValue(p["id"]) {
			case "profile:starter-fine-detail":
				seed = "fine"
			case "profile:starter-draft":
				seed = "draft"
			case "profile:starter-strong":
				seed = "strength"
			}
			p["speedModel"], _ = json.Marshal(newSpeedModel(seed))
			changed = true
		} else if legacy.Schema == 1 {
			if _, ok := profileRules.SpeedSeeds[legacy.Intent]; ok {
				model := newSpeedModel(legacy.Intent)
				for k, v := range legacy.Multipliers {
					model.Multipliers[k] = v
				}
				model.OuterWallCeiling = legacy.OuterWallCeiling
				p["speedModel"], _ = json.Marshal(model)
				changed = true
			}
		}
		kept := slices.DeleteFunc(slices.Clone(rows), func(r autoScale) bool { return slices.Contains(profileRules.NotScalable, r.Key) })
		if len(kept) != len(rows) {
			p["scales"], _ = json.Marshal(kept)
			changed = true
		}
	}
	if changed {
		doc["profiles"], _ = json.Marshal(profiles)
	}
}
func (l *libraryStore) write(e *libraryV4Envelope) error {
	raw, err := json.Marshal(e)
	if err != nil {
		return err
	}
	return writeFileAtomic(filepath.Join(l.dir, libraryV4File), append(raw, '\n'))
}
func (l *libraryStore) reset(epoch string) error {
	l.mu.Lock()
	defer l.mu.Unlock()
	return l.write(&libraryV4Envelope{Document: seededLibraryV4(epoch), Operations: map[string]libraryReceipt{}})
}

func (s *Server) libraryRoutes(mux *http.ServeMux) {
	l := s.libraryV4
	mux.HandleFunc("GET /api/print-catalog", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, 200, printCatalog())
	})
	mux.HandleFunc("GET /api/library", func(w http.ResponseWriter, r *http.Request) {
		l.mu.Lock()
		e, err := l.read()
		l.mu.Unlock()
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "Profiles could not be read.", "retry_storage", 0)
			return
		}
		w.Header().Set("Cache-Control", "no-store")
		writeJSON(w, 200, e.Document)
	})
	mux.HandleFunc("PUT /api/library", func(w http.ResponseWriter, r *http.Request) {
		raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 8<<20))
		var in struct {
			OperationID      string                     `json:"operationId"`
			ExpectedRevision uint64                     `json:"expectedRevision"`
			Document         map[string]json.RawMessage `json:"document"`
		}
		if err != nil || json.Unmarshal(raw, &in) != nil || in.OperationID == "" || len(in.OperationID) > 200 || in.Document == nil {
			libraryFailure(w, 400, "library_invalid", "operationId, expectedRevision and a complete document are required.", "reload", 0)
			return
		}
		digest := workspaceDigest([]any{in.ExpectedRevision, in.Document})
		l.mu.Lock()
		defer l.mu.Unlock()
		e, err := l.read()
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "The stored library could not be read; nothing was changed.", "retry_storage", 0)
			return
		}
		revision := libraryRevision(e.Document)
		if receipt, ok := e.Operations[in.OperationID]; ok {
			if receipt.Digest != digest {
				libraryFailure(w, 409, "operation_conflict", "This operation ID was already used for different changes.", "reload", revision)
				return
			}
			writeJSON(w, 200, map[string]any{"revision": receipt.Revision, "operationId": in.OperationID})
			return
		}
		if stringValue(in.Document["epoch"]) != stringValue(e.Document["epoch"]) {
			libraryFailure(w, 409, "library_reset", "Profiles were reset. Reload them.", "reload", revision)
			return
		}
		if in.ExpectedRevision != revision || libraryRevision(in.Document) != revision {
			libraryFailure(w, 409, "revision_conflict", "The library changed. Reload it and reapply this change.", "reload", revision)
			return
		}
		migrateProfiles(in.Document)
		next, err := v4Profiles(in.Document)
		if err != nil {
			libraryFailure(w, 400, "library_invalid", err.Error(), "repair_profiles", revision)
			return
		}
		previous, _ := v4Profiles(e.Document)
		setups := string(in.Document["setups"])
		for id, p := range previous {
			if next[id] == nil && strings.Contains(setups, `"`+id+`"`) {
				libraryFailure(w, 409, "profile_referenced", p.Name+" is still used by a printer setup. Archive it instead.", "archive_profile", revision)
				return
			}
		}
		in.Document["revision"] = json.RawMessage(fmt.Sprint(revision + 1))
		e.Document = in.Document
		e.Operations[in.OperationID] = libraryReceipt{Digest: digest, Revision: revision + 1}
		if err := l.write(e); err != nil {
			libraryFailure(w, 507, "library_write_failed", "Profiles could not be saved. Free storage and retry.", "retry_storage", revision)
			return
		}
		writeJSON(w, 200, map[string]any{"revision": revision + 1, "operationId": in.OperationID})
	})
}
