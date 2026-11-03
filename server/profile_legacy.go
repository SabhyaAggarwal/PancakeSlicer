package main

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

type legacyFile struct {
	Path   string
	SHA256 string
	Raw    []byte
}
type legacyProfile struct {
	Path   string
	Record map[string]json.RawMessage
}
type legacySource struct {
	Files      []legacyFile
	Profiles   []legacyProfile
	Unreadable int
	Nonregular []string
	Problems   []string
}

func sourceHash(raw []byte) string {
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:])
}

func handledLegacyFiles(envelope *libraryEnvelope) map[string]string {
	handled := map[string]string{}
	for path, sum := range envelope.Legacy {
		handled[path] = sum
	}
	for _, raw := range envelope.Adoptions {
		var receipt struct {
			Durable  bool   `json:"durable"`
			SourceID string `json:"sourceId"`
			Source   string `json:"source"`
		}
		var source struct {
			Files []struct {
				Path   string `json:"path"`
				SHA256 string `json:"sha256"`
			} `json:"files"`
		}
		if json.Unmarshal(raw, &receipt) == nil && receipt.Durable && strings.HasPrefix(receipt.SourceID, "service-profile-library:") && json.Unmarshal([]byte(receipt.Source), &source) == nil {
			for _, file := range source.Files {
				handled[file.Path] = file.SHA256
			}
		}
	}
	return handled
}

func (s *ProfileLibraryStore) scanLegacyProfiles(envelope *libraryEnvelope) (*legacySource, error) {
	root := filepath.Join(s.dir, "user-presets")
	source := &legacySource{}
	handled := handledLegacyFiles(envelope)
	var total int64
	err := filepath.WalkDir(root, func(full string, entry fs.DirEntry, err error) error {
		if errors.Is(err, fs.ErrNotExist) && full == root {
			return fs.SkipAll
		}
		if err != nil {
			return err
		}
		relative, _ := filepath.Rel(s.dir, full)
		relative = filepath.ToSlash(relative)
		isJSON := strings.EqualFold(filepath.Ext(relative), ".json")
		if entry.IsDir() {
			return nil
		}
		if !entry.Type().IsRegular() {
			if isJSON && handled[relative] == "" {
				source.Unreadable++
				source.Nonregular = append(source.Nonregular, relative)
				source.Problems = append(source.Problems, relative+": not a regular file")
			}
			return nil
		}
		info, err := entry.Info()
		if err != nil {
			return err
		}
		if total += info.Size(); total > 64<<20 {
			return errors.New("the earlier profile folder exceeds 64 MiB")
		}
		file, err := os.Open(full)
		if err != nil {
			return err
		}
		raw, err := io.ReadAll(file)
		file.Close()
		if err != nil {
			return err
		}
		sum := sourceHash(raw)
		if handled[relative] == sum {
			return nil
		}
		source.Files = append(source.Files, legacyFile{relative, sum, raw})
		if !isJSON {
			return nil
		}
		record, recognized, reason := legacyRecord(raw)
		if record != nil {
			source.Profiles = append(source.Profiles, legacyProfile{relative, record})
		} else if recognized {
			source.Unreadable++
			source.Problems = append(source.Problems, relative+": "+reason)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	sort.Slice(source.Files, func(i, j int) bool { return source.Files[i].Path < source.Files[j].Path })
	sort.Slice(source.Profiles, func(i, j int) bool { return source.Profiles[i].Path < source.Profiles[j].Path })
	return source, nil
}

func legacyRecord(raw []byte) (record map[string]json.RawMessage, recognized bool, reason string) {
	var fields map[string]json.RawMessage
	if !json.Valid(raw) {
		return nil, true, "invalid JSON"
	}
	if json.Unmarshal(raw, &fields) != nil || fields == nil {
		return nil, false, ""
	}
	if fields["name"] == nil && fields["type"] == nil && fields["id"] == nil && fields["overrides"] == nil && fields["overlay"] == nil {
		return nil, false, ""
	}
	record = map[string]json.RawMessage{}
	for key, value := range fields {
		if key != "inherits" && key != "overrides" {
			record[key] = value
		}
	}
	if record["overlay"] == nil {
		record["overlay"] = fields["overrides"]
	}
	var base map[string]json.RawMessage
	json.Unmarshal(fields["base"], &base)
	if base == nil {
		base = map[string]json.RawMessage{"name": fields["inherits"]}
	}
	if stringValue(base["version"]) == "" {
		base["version"] = json.RawMessage(`"legacy:unknown"`)
	}
	record["base"], _ = json.Marshal(base)
	if record["kind"] == nil {
		record["kind"] = json.RawMessage(`"profile"`)
	}
	if rule, old := worksWithFromSchema1(record["applicability"]); old {
		if rule == nil {
			delete(record, "applicability")
		} else {
			record["applicability"] = rule
		}
	}
	var revision uint64
	if json.Unmarshal(record["revision"], &revision) != nil {
		record["revision"] = json.RawMessage(`0`)
	}
	if record["id"] == nil {
		record["id"] = json.RawMessage(`"profile:legacy-check"`)
	}
	single := emptyProfileLibrary()
	single["profiles"], _ = json.Marshal([]map[string]json.RawMessage{record})
	if _, err := libraryProfiles(single); err != nil {
		return nil, true, err.Error()
	}
	if fields["id"] == nil {
		delete(record, "id")
	}
	return record, true, ""
}

func legacyAdditions(doc profileLibraryDocument, source *legacySource) ([]map[string]json.RawMessage, error) {
	profiles, err := libraryProfiles(doc)
	if err != nil {
		return nil, err
	}
	names := map[string]bool{}
	for _, p := range profiles {
		names[stringValue(p["type"])+"\x00"+stringValue(p["name"])] = true
	}
	var added []map[string]json.RawMessage
	for _, file := range source.Profiles {
		record := map[string]json.RawMessage{}
		for key, value := range file.Record {
			record[key] = value
		}
		if record["id"] == nil {
			record["id"], _ = json.Marshal("profile:legacy-" + sourceHash([]byte(file.Path))[:32])
		}
		key := stringValue(record["type"]) + "\x00" + stringValue(record["name"])
		if profiles[stringValue(record["id"])] != nil || names[key] {
			continue
		}
		profiles[stringValue(record["id"])], names[key] = record, true
		added = append(added, record)
	}
	return added, nil
}

type recoveryRequest struct {
	OperationID      string `json:"operationId"`
	ExpectedRevision uint64 `json:"expectedRevision"`
	Action           string `json:"action"`
}
type recoveryResult struct {
	OperationID string `json:"operationId"`
	Action      string `json:"action"`
	Revision    uint64 `json:"revision"`
	Saved       int    `json:"saved"`
	Unreadable  int    `json:"unreadable"`
}

func (s *Server) getProfileRecovery(w http.ResponseWriter, r *http.Request) {
	s.library.mu.Lock()
	defer s.library.mu.Unlock()
	w.Header().Set("Cache-Control", "no-store")
	envelope, err := s.library.readLibrary()
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "Profiles could not be read.", "retry_storage", 0)
		return
	}
	source, err := s.library.scanLegacyProfiles(envelope)
	if err != nil {
		libraryFailure(w, 503, "recovery_unavailable", "Earlier profiles could not be read.", "retry_storage", libraryRevision(envelope.Document))
		return
	}
	added, err := legacyAdditions(envelope.Document, source)
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "Profiles could not be read.", "retry_storage", libraryRevision(envelope.Document))
		return
	}
	writeJSON(w, 200, map[string]int{"count": len(added), "unreadable": source.Unreadable})
}

func (s *Server) postProfileRecovery(w http.ResponseWriter, r *http.Request) {
	var request recoveryRequest
	if json.NewDecoder(http.MaxBytesReader(w, r.Body, 64<<10)).Decode(&request) != nil || request.OperationID == "" || len(request.OperationID) > 200 || (request.Action != "save" && request.Action != "reset") {
		libraryFailure(w, 400, "recovery_invalid", "operationId, expectedRevision and action save or reset are required.", "", 0)
		return
	}
	canonical, _ := json.Marshal(request)
	digest := "recovery:" + sourceHash(canonical)
	s.library.mu.Lock()
	defer s.library.mu.Unlock()
	envelope, err := s.library.readLibrary()
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "Profiles could not be read. Nothing was changed.", "retry_storage", 0)
		return
	}
	revision := libraryRevision(envelope.Document)
	if receipt, ok := envelope.Operations[request.OperationID]; ok {
		if receipt.Digest != digest || receipt.Result == nil {
			libraryFailure(w, 409, "operation_conflict", "This action was already used for a different change.", "reload", revision)
			return
		}
		writeJSON(w, 200, receipt.Result)
		return
	}
	if request.ExpectedRevision != revision {
		libraryFailure(w, 409, "revision_conflict", "Profiles changed. Reload and try again.", "reload", revision)
		return
	}
	source, err := s.library.scanLegacyProfiles(envelope)
	if err != nil && request.Action == "save" {
		libraryFailure(w, 503, "recovery_unavailable", "Earlier profiles could not be read. Nothing was changed.", "retry_storage", revision)
		return
	}
	if err != nil {
		source = &legacySource{}
	}
	result := &recoveryResult{OperationID: request.OperationID, Action: request.Action, Revision: revision}
	next := envelope.Document
	if request.Action == "save" {
		added, err := legacyAdditions(envelope.Document, source)
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "The saved profiles need repair. Nothing was changed.", "reset", revision)
			return
		}
		result.Saved, result.Unreadable = len(added), source.Unreadable
		if len(added) > 0 {
			var profiles []json.RawMessage
			json.Unmarshal(envelope.Document["profiles"], &profiles)
			for _, record := range added {
				raw, _ := json.Marshal(record)
				profiles = append(profiles, raw)
			}
			next = profileLibraryDocument{}
			for key, value := range envelope.Document {
				next[key] = value
			}
			next["profiles"], _ = json.Marshal(profiles)
			result.Revision = revision + 1
		}
	} else {
		var printers struct {
			Revision uint64 `json:"revision"`
		}
		json.Unmarshal(envelope.Document["printers"], &printers)
		epoch := make([]byte, 16)
		if _, err := rand.Read(epoch); err != nil {
			libraryFailure(w, 503, "library_unavailable", "Profiles could not be reset. Nothing was changed.", "retry_storage", revision)
			return
		}
		next = emptyProfileLibrary()
		next["epoch"], _ = json.Marshal(hex.EncodeToString(epoch))
		next["printers"] = json.RawMessage(fmt.Sprintf(`{"schema":1,"revision":%d,"setups":{}}`, printers.Revision+1))
		result.Revision = revision + 1
	}
	if result.Revision != revision {
		if _, err := libraryProfiles(next); err != nil {
			libraryFailure(w, 400, "library_invalid", "Earlier profiles could not be combined. Nothing was changed.", "reset", revision)
			return
		}
		next["revision"] = json.RawMessage(fmt.Sprint(result.Revision))
	}
	if result.Revision != revision || len(source.Files) > 0 {
		if err := s.library.backup(request.OperationID, request.Action, source); err != nil {
			libraryFailure(w, 507, "library_write_failed", "The backup could not be written. Nothing was changed.", "retry_storage", revision)
			return
		}
	}
	for _, file := range source.Files {
		envelope.Legacy[file.Path] = file.SHA256
	}
	for _, path := range source.Nonregular {
		envelope.Legacy[path] = "nonregular"
	}
	envelope.Document = next
	envelope.Operations[request.OperationID] = libraryReceipt{Digest: digest, Revision: result.Revision, Result: result}
	encoded, err := json.Marshal(envelope)
	if err == nil {
		err = s.library.writeLibrary(encoded)
	}
	if err != nil {
		libraryFailure(w, 507, "library_write_failed", "Profiles could not be saved. Nothing was changed.", "retry_storage", revision)
		return
	}
	writeJSON(w, 200, result)
}

func (s *ProfileLibraryStore) backup(operationID, action string, source *legacySource) error {
	dir := filepath.Join(s.dir, "profile-backups", sourceHash([]byte(operationID))[:16]+"-"+action)
	if err := os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	write := func(path string, raw []byte) error {
		if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
			return err
		}
		file, err := os.OpenFile(path, os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0600)
		if err != nil {
			return err
		}
		_, err = file.Write(raw)
		if err == nil {
			err = file.Sync()
		}
		return errors.Join(err, file.Close())
	}
	current, err := os.ReadFile(filepath.Join(s.dir, "profile-library.json"))
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return err
	}
	if err == nil {
		if err := write(filepath.Join(dir, "profile-library.json"), current); err != nil {
			return err
		}
	}
	for _, file := range source.Files {
		if err := write(filepath.Join(dir, filepath.FromSlash(file.Path)), file.Raw); err != nil {
			return err
		}
	}
	return syncWorkspaceDirectory(dir)
}
