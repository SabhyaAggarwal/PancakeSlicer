package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"
)

type ProfileLibraryStore struct {
	dir      string
	mu       sync.Mutex
	response *cachedResponse
	stamp    [2]int64
}

func NewProfileLibraryStore(dir string) (*ProfileLibraryStore, error) {
	s := &ProfileLibraryStore{dir: dir}
	if err := os.MkdirAll(dir, 0700); err != nil {
		return nil, err
	}
	if err := s.migrateWorksWith(); err != nil {
		return nil, err
	}
	s.logLegacyProblems()
	return s, nil
}

func (s *ProfileLibraryStore) logLegacyProblems() {
	envelope, err := s.readLibrary()
	if err != nil {
		return
	}
	source, err := s.scanLegacyProfiles(envelope)
	if err != nil {
		log.Printf("earlier profiles: %v", err)
		return
	}
	for _, problem := range source.Problems {
		log.Printf("earlier profile file unreadable: %s", problem)
	}
}

func (s *ProfileLibraryStore) migrateWorksWith() error {
	envelope, err := s.readLibrary()
	if err != nil {
		return err
	}
	var profiles []map[string]json.RawMessage
	if json.Unmarshal(envelope.Document["profiles"], &profiles) != nil {
		return nil
	}
	changed := 0
	for _, p := range profiles {
		if raw, ok := p["applicability"]; ok {
			if rule, old := worksWithFromSchema1(raw); old {
				if rule == nil {
					delete(p, "applicability")
				} else {
					p["applicability"] = rule
				}
				changed++
			}
		}
	}
	if changed == 0 {
		return nil
	}
	if err := s.backup(time.Now().UTC().Format("20060102T150405Z"), "works-with-2", &legacySource{}); err != nil {
		return err
	}
	envelope.Document["profiles"], _ = json.Marshal(profiles)
	envelope.Document["revision"] = json.RawMessage(fmt.Sprint(libraryRevision(envelope.Document) + 1))
	encoded, err := json.Marshal(envelope)
	if err == nil {
		err = s.writeLibrary(encoded)
	}
	if err == nil {
		log.Printf("profile library: rewrote %d Works with associations as schema 2 rules", changed)
	}
	return err
}
func validProfileType(t string) bool { return t == "machine" || t == "process" || t == "filament" }
func validProfileName(name string) error {
	if name == "" || len(name) > 200 || strings.TrimSpace(name) != name {
		return errors.New("a profile name is 1 to 200 characters without leading or trailing spaces")
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return errors.New("a profile name has no control characters")
		}
	}
	return nil
}

type profileLibraryDocument map[string]json.RawMessage
type libraryReceipt struct {
	Digest   string          `json:"digest"`
	Revision uint64          `json:"revision"`
	Result   *recoveryResult `json:"result,omitempty"`
}

type libraryEnvelope struct {
	Document   profileLibraryDocument     `json:"document"`
	Operations map[string]libraryReceipt  `json:"operations"`
	Legacy     map[string]string          `json:"legacy,omitempty"`
	Adoptions  map[string]json.RawMessage `json:"adoptions,omitempty"`
}
type libraryInventory struct {
	Complete        bool   `json:"complete"`
	LibraryRevision uint64 `json:"libraryRevision"`
	ProjectRevision string `json:"projectRevision,omitempty"`
	References      map[string][]struct {
		Kind  string `json:"kind"`
		Label string `json:"label"`
		Owner string `json:"owner,omitempty"`
	} `json:"references"`
}
type libraryWrite struct {
	OperationID      string                 `json:"operationId"`
	ExpectedRevision uint64                 `json:"expectedRevision"`
	Document         profileLibraryDocument `json:"document"`
	Inventory        *libraryInventory      `json:"inventory,omitempty"`
}
type libraryError struct {
	Message  string `json:"error"`
	Code     string `json:"code"`
	Recovery string `json:"recovery"`
	Revision uint64 `json:"revision,omitempty"`
}

func libraryFailure(w http.ResponseWriter, status int, code, message, recovery string, revision uint64) {
	writeJSON(w, status, libraryError{message, code, recovery, revision})
}
func libraryRevision(doc profileLibraryDocument) uint64 {
	var revision uint64
	json.Unmarshal(doc["revision"], &revision)
	return revision
}
func emptyProfileLibrary() profileLibraryDocument {
	return profileLibraryDocument{"schema": json.RawMessage(`1`), "revision": json.RawMessage(`0`), "profiles": json.RawMessage(`[]`), "printers": json.RawMessage(`{"schema":1,"revision":0,"setups":{}}`)}
}
func (s *ProfileLibraryStore) readLibrary() (*libraryEnvelope, error) {
	raw, err := os.ReadFile(filepath.Join(s.dir, "profile-library.json"))
	if errors.Is(err, os.ErrNotExist) {
		return &libraryEnvelope{Document: emptyProfileLibrary(), Operations: map[string]libraryReceipt{}, Legacy: map[string]string{}}, nil
	}
	if err != nil {
		return nil, err
	}
	var envelope libraryEnvelope
	if err = json.Unmarshal(raw, &envelope); err != nil {
		return nil, err
	}
	if envelope.Document == nil || envelope.Operations == nil {
		return nil, errors.New("invalid stored profile library")
	}
	if envelope.Legacy == nil {
		envelope.Legacy = map[string]string{}
	}
	return &envelope, nil
}
func (s *Server) getProfileLibrary(w http.ResponseWriter, r *http.Request) {
	l := s.library
	l.mu.Lock()
	defer l.mu.Unlock()
	info, statErr := os.Stat(filepath.Join(l.dir, "profile-library.json"))
	response := l.response
	if response == nil || statErr != nil || fileStamp(info) != l.stamp {
		value, err := l.readLibrary()
		if err == nil {
			response, err = cacheJSON(value.Document)
		}
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "Profiles could not be read.", "retry_storage", 0)
			return
		}
		l.response = nil
		if statErr == nil {
			l.response, l.stamp = response, fileStamp(info)
		}
	}
	response.serve(w, r, "profile-library.json", "application/json", privateRevalidate, time.Time{})
}
func libraryProfiles(doc profileLibraryDocument) (map[string]map[string]json.RawMessage, error) {
	var schema int
	if json.Unmarshal(doc["schema"], &schema) != nil || schema != 1 {
		return nil, errors.New("profile library schema must be 1")
	}
	var revision uint64
	if json.Unmarshal(doc["revision"], &revision) != nil {
		return nil, errors.New("profile library revision is required")
	}
	var profiles []map[string]json.RawMessage
	if json.Unmarshal(doc["profiles"], &profiles) != nil || profiles == nil {
		return nil, errors.New("profiles must be an array")
	}
	ids := map[string]map[string]json.RawMessage{}
	for _, p := range profiles {
		id, name, typ := stringValue(p["id"]), stringValue(p["name"]), stringValue(p["type"])
		if !strings.HasPrefix(id, "profile:") || len(id) <= 8 || len(id) > 200 || strings.ContainsAny(id, "\r\n\x00") {
			return nil, errors.New("every profile needs its existing stable profile: ID")
		}
		if ids[id] != nil {
			return nil, errors.New("duplicate profile ID")
		}
		if validProfileName(name) != nil || !validProfileType(typ) {
			return nil, errors.New("every profile needs a valid name and type")
		}
		if kind := stringValue(p["kind"]); kind != "profile" && kind != "master" {
			return nil, errors.New("unknown profile kind")
		}
		var profileRevision uint64
		var anchor struct {
			Name    string `json:"name"`
			Version string `json:"version"`
		}
		var overlay map[string]json.RawMessage
		if json.Unmarshal(p["revision"], &profileRevision) != nil || json.Unmarshal(p["base"], &anchor) != nil || anchor.Name == "" || anchor.Version == "" || json.Unmarshal(p["overlay"], &overlay) != nil || overlay == nil {
			return nil, errors.New("every profile needs revision, base name/version and an overlay object in the canonical schema")
		}
		if raw, ok := p["applicability"]; ok {
			if _, err := parseWorksWith(raw, typ); err != nil {
				return nil, fmt.Errorf("%s: %w", name, err)
			}
		}
		if raw, ok := p["dynamic"]; ok {
			if _, err := parseDynamic(raw, typ); err != nil {
				return nil, fmt.Errorf("%s: %w", name, err)
			}
		}
		if raw, ok := p["parentId"]; ok && !strings.HasPrefix(stringValue(raw), "profile:") {
			return nil, errors.New("parentId names a profile: ID")
		}
		if raw, ok := p["baseRole"]; ok && stringValue(raw) != "base" && stringValue(raw) != "base-only" {
			return nil, errors.New("baseRole is base or base-only")
		}
		if raw, ok := p["mergedInto"]; ok && !strings.HasPrefix(stringValue(raw), "profile:") {
			return nil, errors.New("mergedInto names the dynamic profile's ID")
		}
		if raw, ok := p["archived"]; ok {
			var archive struct {
				At          float64 `json:"at"`
				OperationID string  `json:"operationId"`
			}
			if json.Unmarshal(raw, &archive) != nil || archive.At <= 0 || archive.OperationID == "" {
				return nil, errors.New("archived must retain at and operationId; omit it to restore")
			}
		}
		ids[id] = p
	}
	var printers struct {
		Schema   int                        `json:"schema"`
		Revision uint64                     `json:"revision"`
		Setups   map[string]json.RawMessage `json:"setups"`
	}
	if json.Unmarshal(doc["printers"], &printers) != nil || printers.Schema != 1 || printers.Setups == nil {
		return nil, errors.New("printers must contain schema 1 and a setups object")
	}
	return ids, nil
}

func libraryReferences(doc profileLibraryDocument, profiles map[string]map[string]json.RawMessage) map[string][]string {
	refs := map[string][]string{}
	add := func(id, label string) {
		if id != "" {
			refs[id] = append(refs[id], label)
		}
	}
	for id, p := range profiles {
		for _, key := range []string{"parentId", "parent", "printerId", "masterId"} {
			add(stringValue(p[key]), id+" "+key)
		}
		var a worksWithRule
		json.Unmarshal(p["applicability"], &a)
		if a.Printers != nil {
			for _, pid := range a.Printers.PrinterIDs {
				add(pid, id+" Works with")
			}
		}
		if a.Filaments != nil {
			for _, fid := range a.Filaments.ProfileIDs {
				add(fid, id+" Works with")
			}
		}
		if a.Prints != nil {
			for _, pid := range a.Prints.ProfileIDs {
				add(pid, id+" Works with")
			}
		}
		add(stringValue(p["mergedInto"]), id+" merged into")
	}
	var printers struct {
		Setups map[string]struct {
			PrinterID   string   `json:"printerId"`
			MaterialIDs []string `json:"materialIds"`
			Slots       []struct {
				MaterialID string `json:"materialId"`
			} `json:"slots"`
			Tuning map[string]json.RawMessage `json:"tuning"`
		} `json:"setups"`
	}
	json.Unmarshal(doc["printers"], &printers)
	for id, setup := range printers.Setups {
		add(id, "printer setup")
		add(setup.PrinterID, "printer setup")
		for _, mid := range setup.MaterialIDs {
			add(mid, id+" material")
		}
		for _, slot := range setup.Slots {
			add(slot.MaterialID, id+" slot")
		}
		for mid := range setup.Tuning {
			add(mid, id+" tuning")
		}
	}
	return refs
}

func (s *Server) worksWithNone(next, previous map[string]map[string]json.RawMessage) string {
	live := func(p map[string]json.RawMessage) bool {
		return len(p["archived"]) == 0 || string(p["archived"]) == "null"
	}
	printers := func(doc map[string]map[string]json.RawMessage) func() []printerFacts {
		var facts []printerFacts
		read := false
		return func() []printerFacts {
			if !read {
				for id, m := range doc {
					if stringValue(m["type"]) == "machine" && live(m) {
						facts = append(facts, s.printerFactsOf(id, m))
					}
				}
				read = true
			}
			return facts
		}
	}
	nextPrinters, previousPrinters := printers(next), printers(previous)
	none := func(facts func() []printerFacts, raw json.RawMessage, typ string) bool {
		rule, err := parseWorksWith(raw, typ)
		return err == nil && !slices.ContainsFunc(facts(), rule.matchesPrinter)
	}
	for id, p := range next {
		typ := stringValue(p["type"])
		raw, ruled := p["applicability"]
		if typ == "machine" || !live(p) || !ruled {
			continue
		}
		if old := previous[id]; old != nil {
			if was, ok := old["applicability"]; ok && (jsonEqual(was, raw) || none(previousPrinters, was, typ)) {
				continue
			}
		}
		if none(nextPrinters, raw, typ) {
			return stringValue(p["name"])
		}
	}
	return ""
}
func jsonEqual(a, b json.RawMessage) bool {
	var x, y any
	return json.Unmarshal(a, &x) == nil && json.Unmarshal(b, &y) == nil && fmt.Sprint(x) == fmt.Sprint(y)
}

func baseChainProblem(next, previous map[string]map[string]json.RawMessage) (int, string, string) {
	live := func(p map[string]json.RawMessage) bool {
		return p != nil && (len(p["archived"]) == 0 || string(p["archived"]) == "null")
	}
	moved := map[string]bool{}
	for id, p := range next {
		if old := previous[id]; old == nil || stringValue(old["parentId"]) != stringValue(p["parentId"]) {
			moved[id] = true
		}
	}
	messages := map[string]string{
		"profile_base_cycle":   "A profile cannot be based on itself through its bases",
		"profile_base_depth":   "A profile can be based on at most 4 of my profiles in a row",
		"profile_base_invalid": "A profile can only be based on an existing profile of the same type and system preset",
	}
	for id := range next {
		chain, code := baseChain(next, id)
		if code == "" || !slices.ContainsFunc(chain, func(p map[string]json.RawMessage) bool { return moved[stringValue(p["id"])] }) {
			continue
		}
		status := 422
		if code == "profile_base_cycle" {
			status = 409
		}
		return status, code, messages[code] + ": " + stringValue(next[id]["name"])
	}
	for id, old := range previous {
		if !live(old) || live(next[id]) {
			continue
		}
		for _, child := range next {
			if live(child) && stringValue(child["parentId"]) == id {
				return 409, "profile_base_children", "Re-parent the profiles based on " + stringValue(old["name"]) + " before removing it: " + stringValue(child["name"])
			}
		}
	}
	return 0, "", ""
}

func (s *Server) profileValue(p map[string]json.RawMessage, key string) json.RawMessage {
	var overlay map[string]json.RawMessage
	json.Unmarshal(p["overlay"], &overlay)
	if v, ok := overlay[key]; ok {
		return v
	}
	var base struct{ Name, Vendor, EngineName string }
	json.Unmarshal(p["base"], &base)
	if base.EngineName == "" {
		base.EngineName = base.Name
	}
	if s.presets == nil {
		return nil
	}
	vendor, ok := s.presets.Vendor(base.Vendor)
	if !ok {
		return nil
	}
	native := map[string]map[string]*Preset{"machine": vendor.machines, "process": vendor.processes, "filament": vendor.filaments}[stringValue(p["type"])][base.EngineName]
	if native == nil {
		return nil
	}
	return native.Config[key]
}
func (s *Server) printerFactsOf(id string, p map[string]json.RawMessage) printerFacts {
	nozzle, _ := strconv.ParseFloat(firstConfigText(s.profileValue(p, "nozzle_diameter")), 64)
	return printerFacts{ID: id, Model: firstConfigText(s.profileValue(p, "printer_model")), Nozzle: nozzle}
}

func firstConfigText(raw json.RawMessage) string {
	var list []any
	if json.Unmarshal(raw, &list) == nil {
		if len(list) == 0 {
			return ""
		}
		return strings.TrimSpace(fmt.Sprint(list[0]))
	}
	var scalar any
	json.Unmarshal(raw, &scalar)
	if scalar == nil {
		return ""
	}
	text, _, _ := strings.Cut(fmt.Sprint(scalar), ",")
	return strings.Trim(strings.TrimSpace(text), `"`)
}
func (s *Server) putProfileLibrary(w http.ResponseWriter, r *http.Request) {
	raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 8<<20))
	if err != nil {
		libraryFailure(w, 413, "library_too_large", "The profile library exceeds 8 MiB.", "", 0)
		return
	}
	var request libraryWrite
	if json.Unmarshal(raw, &request) != nil || request.OperationID == "" || len(request.OperationID) > 200 || request.Document == nil {
		libraryFailure(w, 400, "library_invalid", "operationId, expectedRevision and a complete document are required.", "repair_profiles", 0)
		return
	}
	canonical, _ := json.Marshal(struct {
		Revision  uint64
		Document  profileLibraryDocument
		Inventory *libraryInventory
	}{request.ExpectedRevision, request.Document, request.Inventory})
	var content any
	json.Unmarshal(canonical, &content)
	canonical, _ = json.Marshal(content)
	digest := sha256.Sum256(canonical)
	digestText := hex.EncodeToString(digest[:])
	s.library.mu.Lock()
	defer s.library.mu.Unlock()
	envelope, err := s.library.readLibrary()
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "The stored library could not be read; no profiles were changed.", "retry_storage", 0)
		return
	}
	revision := libraryRevision(envelope.Document)
	if receipt, ok := envelope.Operations[request.OperationID]; ok {
		if receipt.Digest != digestText {
			libraryFailure(w, 409, "operation_conflict", "This operation ID was already used for different profile changes.", "reload_reapply", revision)
			return
		}
		writeJSON(w, 200, map[string]any{"revision": receipt.Revision, "operationId": request.OperationID})
		return
	}
	if stringValue(request.Document["epoch"]) != stringValue(envelope.Document["epoch"]) {
		libraryFailure(w, 409, "library_reset", "Profiles were reset. Reload them.", "reload", revision)
		return
	}
	if request.ExpectedRevision != revision || libraryRevision(request.Document) != revision {
		libraryFailure(w, 409, "revision_conflict", "The profile library changed. Reload it and reapply this draft.", "reload_reapply", revision)
		return
	}
	next, err := libraryProfiles(request.Document)
	if err != nil {
		libraryFailure(w, 400, "library_invalid", err.Error(), "repair_profiles", revision)
		return
	}
	previous, err := libraryProfiles(envelope.Document)
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "The stored profile library needs recovery; no changes were written.", "repair_profiles", revision)
		return
	}
	convert := false
	for id, old := range previous {
		p := next[id]
		if p == nil {
			continue
		}
		if _, was := old["dynamic"]; was {
			if _, still := p["dynamic"]; !still {
				libraryFailure(w, 409, "profile_kind_locked", "A dynamic profile cannot become a normal profile. Use Split into normal profiles.", "reload", revision)
				return
			}
		} else if _, now := p["dynamic"]; now {
			convert = true
		}
		if _, merged := p["mergedInto"]; merged && old["mergedInto"] == nil {
			convert = true
		}
		if stringValue(p["parentId"]) != stringValue(old["parentId"]) || stringValue(p["baseRole"]) != stringValue(old["baseRole"]) {
			convert = true
		}
	}
	if status, code, message := baseChainProblem(next, previous); code != "" {
		libraryFailure(w, status, code, message, "edit_base", revision)
		return
	}
	if name := s.worksWithNone(next, previous); name != "" {
		libraryFailure(w, 422, "profile_works_with_none", "A profile must work with at least one printer: "+name, "edit_applicability", revision)
		return
	}
	if convert {
		if err := s.library.backup(request.OperationID, "convert", &legacySource{}); err != nil {
			libraryFailure(w, 507, "library_write_failed", "Profiles could not be backed up before converting. Free storage and retry.", "retry_storage", revision)
			return
		}
	}
	var workspace *workspaceData
	for id := range previous {
		if next[id] == nil {
			if s.workspace == nil {
				libraryFailure(w, 503, "inventory_unavailable", "Project reference storage is unavailable; no profile was purged.", "reload_references", revision)
				return
			}
			s.workspace.mu.Lock()
			defer s.workspace.mu.Unlock()
			workspace, err = s.workspace.read()
			if err != nil {
				libraryFailure(w, 503, "inventory_unavailable", "Project and recovery references could not be read; no profile was purged.", "reload_references", revision)
				return
			}
			break
		}
	}
	refs := libraryReferences(request.Document, next)
	for id, p := range previous {
		if next[id] == nil {
			if workspaceHasReference(workspace, id) {
				libraryFailure(w, 409, "profile_referenced", "A saved project, recovery draft or retained migration inventory still references this profile. Archive it to remove it from the normal library without losing that work.", "archive_profile", revision)
				return
			}
			inv := request.Inventory
			if inv == nil || !inv.Complete || inv.LibraryRevision != revision {
				libraryFailure(w, 409, "inventory_required", "Refresh the complete profile and project reference inventory before deleting a profile.", "reload_references", revision)
				return
			}
			if stringValue(p["kind"]) == "master" {
				var anchor map[string]json.RawMessage
				json.Unmarshal(p["base"], &anchor)
				for _, child := range next {
					if stringValue(child["kind"]) == "master" || stringValue(child["type"]) != stringValue(p["type"]) || stringValue(child["masterId"]) != "" {
						continue
					}
					var base map[string]json.RawMessage
					json.Unmarshal(child["base"], &base)
					if name := stringValue(anchor["name"]); name != "" && name == stringValue(base["name"]) && stringValue(anchor["vendor"]) == stringValue(base["vendor"]) {
						refs[id] = append(refs[id], stringValue(child["id"])+" implicit master")
					}
				}
			}
			known, exists := inv.References[id]
			if !exists || len(known) > 0 || len(refs[id]) > 0 || len(refs[stringValue(p["name"])]) > 0 {
				libraryFailure(w, 409, "profile_referenced", "This profile is still referenced. Reassign its slots, projects or dependent profiles before deleting it.", "reassign_references", revision)
				return
			}
		}
	}
	request.Document["revision"] = json.RawMessage(fmt.Sprint(revision + 1))
	envelope.Document = request.Document
	envelope.Operations[request.OperationID] = libraryReceipt{Digest: digestText, Revision: revision + 1}

	encoded, err := json.Marshal(envelope)
	if err == nil {
		err = s.library.writeLibrary(encoded)
	}
	if err != nil {
		libraryFailure(w, 507, "library_write_failed", "Profiles could not be saved. Free storage and retry.", "retry_storage", revision)
		return
	}
	writeJSON(w, 200, map[string]any{"revision": revision + 1, "operationId": request.OperationID})
}
func (s *ProfileLibraryStore) writeLibrary(raw []byte) error {
	root := s.dir
	file, err := os.CreateTemp(root, ".profile-library-*")
	if err != nil {
		return err
	}
	defer os.Remove(file.Name())
	_, err = file.Write(append(bytes.TrimSpace(raw), '\n'))
	if err == nil {
		err = file.Sync()
	}
	err = errors.Join(err, file.Close())
	if err != nil {
		return err
	}
	if err := os.Rename(file.Name(), filepath.Join(root, "profile-library.json")); err != nil {
		return err
	}
	return syncWorkspaceDirectory(root)
}
