package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"unicode"
)

type ProfileLibraryStore struct {
	dir string
	mu  sync.Mutex
}

func NewProfileLibraryStore(dir string) (*ProfileLibraryStore, error) {
	return &ProfileLibraryStore{dir: dir}, os.MkdirAll(dir, 0700)
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
	Digest   string `json:"digest"`
	Revision uint64 `json:"revision"`
}
type libraryEnvelope struct {
	Document   profileLibraryDocument            `json:"document"`
	Operations map[string]libraryReceipt         `json:"operations"`
	Adoptions  map[string]profileAdoptionReceipt `json:"adoptions,omitempty"`
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
	Adoption         *profileAdoption       `json:"adoption,omitempty"`
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
		return &libraryEnvelope{Document: emptyProfileLibrary(), Operations: map[string]libraryReceipt{}, Adoptions: map[string]profileAdoptionReceipt{}}, nil
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
	if envelope.Adoptions == nil {
		envelope.Adoptions = map[string]profileAdoptionReceipt{}
	}
	return &envelope, nil
}
func (s *Server) getProfileLibrary(w http.ResponseWriter, r *http.Request) {
	s.library.mu.Lock()
	defer s.library.mu.Unlock()
	value, err := s.library.readLibrary()
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "The profile library could not be read. Export your profiles before retrying storage.", "retry_storage", 0)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	writeJSON(w, 200, value.Document)
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
			var a struct {
				Schema     int      `json:"schema"`
				Mode       string   `json:"mode"`
				PrinterIDs []string `json:"printerIds"`
			}
			if json.Unmarshal(raw, &a) != nil || a.Schema != 1 || (a.Mode != "inherit" && a.Mode != "only") || a.Mode == "only" && a.PrinterIDs == nil {
				return nil, errors.New("applicability must be inherit or only with an explicit printerIds array")
			}
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
		var a struct {
			PrinterIDs []string `json:"printerIds"`
		}
		json.Unmarshal(p["applicability"], &a)
		for _, pid := range a.PrinterIDs {
			add(pid, id+" Works with")
		}
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
func (s *Server) putProfileLibrary(w http.ResponseWriter, r *http.Request) {
	raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 8<<20))
	if err != nil {
		libraryFailure(w, 413, "library_too_large", "The profile library exceeds 8 MiB.", "export_profiles", 0)
		return
	}
	var request libraryWrite
	if json.Unmarshal(raw, &request) != nil || request.OperationID == "" || len(request.OperationID) > 200 || request.Document == nil {
		libraryFailure(w, 400, "library_invalid", "operationId, expectedRevision and a complete document are required.", "repair_profiles", 0)
		return
	}
	if request.Adoption != nil {
		if request.OperationID != "browser-adoption:"+request.Adoption.Digest {
			libraryFailure(w, 400, "adoption_invalid", "Use the stable operationId browser-adoption:<source digest>.", "recapture_inventory", 0)
			return
		}
		if err := request.Adoption.validate(); err != nil {
			libraryFailure(w, 400, "adoption_invalid", err.Error(), "recapture_inventory", 0)
			return
		}
	}
	canonical, _ := json.Marshal(struct {
		Revision  uint64
		Document  profileLibraryDocument
		Inventory *libraryInventory
		Adoption  *profileAdoption `json:",omitempty"`
	}{request.ExpectedRevision, request.Document, request.Inventory, request.Adoption})
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
	if request.Adoption != nil {
		if receipt, ok := envelope.Adoptions[request.Adoption.Digest]; ok {
			if receipt.SourceID != request.Adoption.SourceID {
				libraryFailure(w, 409, "adoption_identity_conflict", "This capture was acknowledged for a different source identity. Keep the source and review its provenance.", "recapture_inventory", revision)
				return
			}

			writeJSON(w, 200, map[string]any{"revision": receipt.Revision, "operationId": receipt.OperationID, "adoption": receipt})
			return
		}
	}

	if receipt, ok := envelope.Operations[request.OperationID]; ok {
		if receipt.Digest != digestText {
			libraryFailure(w, 409, "operation_conflict", "This operation ID was already used for different profile changes.", "reload_reapply", revision)
			return
		}
		writeJSON(w, 200, map[string]any{"revision": receipt.Revision, "operationId": request.OperationID})
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
	envelope.Operations[request.OperationID] = libraryReceipt{digestText, revision + 1}
	if request.Adoption != nil {
		envelope.Adoptions[request.Adoption.Digest] = profileAdoptionReceipt{*request.Adoption, request.OperationID, revision + 1, true}
	}

	encoded, err := json.Marshal(envelope)
	if err == nil {
		err = s.library.writeLibrary(encoded)
	}
	if err != nil {
		libraryFailure(w, 507, "library_write_failed", "The profile library could not be saved. Free storage or export your profiles, then retry the same draft.", "export_profiles", revision)
		return
	}
	response := map[string]any{"revision": revision + 1, "operationId": request.OperationID}
	if request.Adoption != nil {
		response["adoption"] = envelope.Adoptions[request.Adoption.Digest]
	}
	writeJSON(w, 200, response)
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
