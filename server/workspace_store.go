package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"sync"
	"time"
)

type workspaceStore struct {
	mu   sync.Mutex
	root string
}
type workspaceProject struct {
	Record   json.RawMessage `json:"record"`
	Archived bool            `json:"archived,omitempty"`
	Revision uint64          `json:"revision"`
	Folder   string          `json:"folder,omitempty"`
}
type workspaceDraft struct {
	ID        string          `json:"id"`
	Sequence  uint64          `json:"sequence"`
	Document  json.RawMessage `json:"document"`
	Metadata  json.RawMessage `json:"metadata,omitempty"`
	UpdatedAt int64           `json:"updatedAt"`
}
type workspaceValue struct {
	Revision uint64          `json:"revision"`
	Document json.RawMessage `json:"document"`
}
type workspaceReceipt struct {
	Digest   string          `json:"digest"`
	Response json.RawMessage `json:"response"`
}
type workspaceData struct {
	Schema         int                         `json:"schema"`
	Projects       map[string]workspaceProject `json:"projects"`
	Drafts         map[string]workspaceDraft   `json:"drafts"`
	Values         map[string]workspaceValue   `json:"values"`
	Operations     map[string]workspaceReceipt `json:"operations"`
	Adoptions      map[string]json.RawMessage  `json:"adoptions"`
	Aliases        map[string]string           `json:"aliases"`
	Folders        map[string]workspaceFolder  `json:"folders,omitempty"`
	FolderRevision uint64                      `json:"folderRevision,omitempty"`
}

func newWorkspaceData() *workspaceData {
	return &workspaceData{Schema: 1, Projects: map[string]workspaceProject{}, Drafts: map[string]workspaceDraft{}, Values: map[string]workspaceValue{}, Operations: map[string]workspaceReceipt{}, Adoptions: map[string]json.RawMessage{}, Aliases: map[string]string{}, Folders: map[string]workspaceFolder{}}
}
func (st *workspaceStore) read() (*workspaceData, error) {
	raw, err := os.ReadFile(filepath.Join(st.root, "workspace.json"))
	if errors.Is(err, os.ErrNotExist) {
		return newWorkspaceData(), nil
	}
	if err != nil {
		return nil, err
	}
	var d workspaceData
	if err = json.Unmarshal(raw, &d); err != nil {
		return nil, err
	}
	if d.Schema != 1 || d.Projects == nil || d.Drafts == nil || d.Values == nil || d.Operations == nil || d.Adoptions == nil || d.Aliases == nil {
		return nil, errors.New("invalid workspace schema")
	}
	if d.Folders == nil {
		d.Folders = map[string]workspaceFolder{}
	}
	for id, project := range d.Projects {
		if bytes.Equal(bytes.TrimSpace(project.Record), []byte("null")) {
			project.Record = nil
			d.Projects[id] = project
		}
		if project.Record == nil {
			if !project.Archived {
				return nil, fmt.Errorf("active project %s has no retained record", id)
			}
			continue
		}
		if _, err := projectRecord(project.Record, id, project.Revision); err != nil {
			return nil, fmt.Errorf("invalid retained project %s: %w", id, err)
		}
	}
	return &d, nil
}
func durableJSON(path string, value any) error {
	raw, err := json.Marshal(value)
	if err != nil {
		return err
	}
	if err = os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return err
	}
	f, err := os.CreateTemp(filepath.Dir(path), ".workspace-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	_, err = f.Write(raw)
	if err == nil {
		err = f.Sync()
	}
	err = errors.Join(err, f.Close())
	if err != nil {
		return err
	}
	if err := os.Rename(f.Name(), path); err != nil {
		return err
	}
	return syncWorkspaceDirectory(filepath.Dir(path))
}
func syncWorkspaceDirectory(path string) error {
	if runtime.GOOS == "windows" {
		return nil
	}
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	return f.Sync()
}
func (st *workspaceStore) save(d *workspaceData) error {
	return durableJSON(filepath.Join(st.root, "workspace.json"), d)
}
func workspaceID(id string) bool {
	return id != "" && len(id) <= 200 && strings.Trim(id, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-") == "" && id != "." && id != ".."
}

func canonicalWorkspaceProject(d *workspaceData, id string) (string, error) {
	seen := map[string]bool{}
	for {
		next, alias := d.Aliases[id]
		if !alias {
			break
		}
		if seen[id] {
			return "", errors.New("The project alias chain contains a cycle. Keep the original inventory and repair its aliases.")
		}
		seen[id] = true
		if !workspaceID(next) {
			return "", errors.New("The project alias points to an invalid identity. Keep the original inventory and repair its aliases.")
		}
		id = next
	}
	if !workspaceID(id) {
		return "", errors.New("Invalid project identity.")
	}
	if len(seen) > 0 {
		if _, exists := d.Projects[id]; !exists {
			return "", errors.New("The project alias points to a missing project. Keep the original inventory and repair its aliases.")
		}
	}
	return id, nil
}
func workspaceFailure(w http.ResponseWriter, status int, code, message, recovery string, revision uint64) {
	name := "WorkspaceError"
	if code == "revision_conflict" {
		name = "ProjectConflictError"
	}
	if code == "project_archived" {
		name = "ProjectDeletedError"
	}
	writeJSON(w, status, map[string]any{"error": message, "name": name, "code": code, "recovery": recovery, "revision": revision})
}
func workspaceDecode(w http.ResponseWriter, r *http.Request, v any) bool {
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64<<20))
	if err := dec.Decode(v); err != nil {
		workspaceFailure(w, 400, "invalid_document", "The saved document is invalid or exceeds 64 MiB.", "export_and_repair", 0)
		return false
	}
	var extra any
	if dec.Decode(&extra) != io.EOF {
		workspaceFailure(w, 400, "invalid_document", "Expected one JSON document.", "export_and_repair", 0)
		return false
	}
	return true
}
func workspaceDigest(v any) string {
	raw, _ := json.Marshal(v)
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:])
}
func (st *workspaceStore) locked(w http.ResponseWriter, fn func(*workspaceData)) {
	w.Header().Set("Cache-Control", "no-store")
	st.mu.Lock()
	defer st.mu.Unlock()
	d, err := st.read()
	if err != nil {
		workspaceFailure(w, 503, "workspace_unavailable", "Saved work could not be read. Keep your current document and retry the service.", "retry_storage", 0)
		return
	}
	fn(d)
}

func (st *workspaceStore) view(w http.ResponseWriter, fn func(*workspaceData)) {
	st.locked(w, func(d *workspaceData) {
		w.Header().Set("Cache-Control", privateRevalidate)
		fn(d)
	})
}
func workspaceReplay(w http.ResponseWriter, d *workspaceData, op, digest string) bool {
	if op == "" || len(op) > 200 {
		workspaceFailure(w, 400, "operation_required", "A stable operationId is required for safe retry.", "retry_same_operation", 0)
		return true
	}
	if old, ok := d.Operations[op]; ok {
		if old.Digest != digest {
			workspaceFailure(w, 409, "operation_conflict", "This operationId was used for different data.", "reload_and_reapply", 0)
		} else {
			writeJSON(w, 200, json.RawMessage(old.Response))
		}
		return true
	}
	return false
}
func (st *workspaceStore) commit(w http.ResponseWriter, d *workspaceData, op, digest string, response any) bool {
	raw, _ := json.Marshal(response)
	d.Operations[op] = workspaceReceipt{digest, raw}
	if err := st.save(d); err != nil {
		workspaceFailure(w, 507, "workspace_write_failed", "Saved work could not be committed. Your draft must be kept; free storage and retry the same operation.", "free_storage_and_retry", 0)
		return false
	}
	writeJSON(w, 200, response)
	return true
}
func projectRecord(raw json.RawMessage, id string, revision uint64) (json.RawMessage, error) {
	var record map[string]json.RawMessage
	if json.Unmarshal(raw, &record) != nil || record == nil {
		return nil, errors.New("record is required")
	}
	var meta map[string]json.RawMessage
	var doc map[string]json.RawMessage
	if json.Unmarshal(record["meta"], &meta) != nil || meta == nil || json.Unmarshal(record["doc"], &doc) != nil || doc == nil {
		return nil, errors.New("meta and doc must be objects")
	}
	var name string
	var objects, savedAt *float64
	var thumbnail *string
	if json.Unmarshal(meta["name"], &name) != nil || strings.TrimSpace(name) == "" || len(name) > 500 || json.Unmarshal(meta["objects"], &objects) != nil || objects == nil || *objects < 0 || math.Trunc(*objects) != *objects || json.Unmarshal(meta["savedAt"], &savedAt) != nil || savedAt == nil || *savedAt < 0 {
		return nil, errors.New("project name, nonnegative savedAt and integer object count are required")
	}
	if raw := meta["thumbnail"]; len(raw) != 0 && json.Unmarshal(raw, &thumbnail) != nil || thumbnail != nil && strings.HasPrefix(*thumbnail, thumbnailRoute) {
		return nil, errors.New("project thumbnail must be an image data URL, another string or null")
	}
	meta["id"], _ = json.Marshal(id)
	meta["revision"], _ = json.Marshal(revision)
	delete(meta, "folderId")
	delete(meta, "change")
	record["meta"], _ = json.Marshal(meta)
	return json.Marshal(record)
}
func (st *workspaceStore) checkAssets(record json.RawMessage) error {
	var v struct {
		Doc struct {
			Files map[string]string `json:"files"`
		} `json:"doc"`
	}
	if err := json.Unmarshal(record, &v); err != nil {
		return err
	}
	for key := range v.Doc.Files {
		if _, err := os.Stat(st.assetPath(key)); err != nil {
			return fmt.Errorf("model asset %s is missing; upload it before saving", key)
		}
	}
	return nil
}
func projectMeta(p workspaceProject) json.RawMessage {
	var record map[string]json.RawMessage
	json.Unmarshal(p.Record, &record)
	var meta map[string]json.RawMessage
	if json.Unmarshal(record["meta"], &meta) != nil || meta == nil {
		return record["meta"]
	}
	var id, thumbnail string
	if json.Unmarshal(meta["id"], &id) == nil && json.Unmarshal(meta["thumbnail"], &thumbnail) == nil && strings.HasPrefix(thumbnail, "data:image/") {
		meta["thumbnail"], _ = json.Marshal(thumbnailURL(id, thumbnail))
	}
	meta["folderId"] = json.RawMessage("null")
	if p.Folder != "" {
		meta["folderId"], _ = json.Marshal(p.Folder)
	}
	raw, _ := json.Marshal(meta)
	return raw
}
func (st *workspaceStore) register(mux *http.ServeMux) {
	st.adoptionRoutes(mux)
	st.folderRoutes(mux)
	st.thumbnailRoutes(mux)
	mux.HandleFunc("GET /api/projects", func(w http.ResponseWriter, r *http.Request) {
		st.view(w, func(d *workspaceData) {
			out := []json.RawMessage{}
			archived := r.URL.Query().Get("archived") == "1"
			for _, p := range d.Projects {
				if p.Archived == archived && p.Record != nil {
					out = append(out, projectMeta(p))
				}
			}
			sort.Slice(out, func(i, j int) bool {
				var a, b struct {
					SavedAt float64 `json:"savedAt"`
				}
				json.Unmarshal(out[i], &a)
				json.Unmarshal(out[j], &b)
				return a.SavedAt > b.SavedAt
			})
			writeJSON(w, 200, out)
		})
	})
	mux.HandleFunc("GET /api/projects/references", func(w http.ResponseWriter, r *http.Request) {
		st.view(w, func(d *workspaceData) {
			type reference struct {
				ID      string `json:"id"`
				Name    string `json:"name"`
				Trashed bool   `json:"trashed"`
				Doc     struct {
					Printer       json.RawMessage `json:"printer,omitempty"`
					Process       json.RawMessage `json:"process,omitempty"`
					FilamentSlots json.RawMessage `json:"filamentSlots,omitempty"`
				} `json:"doc"`
			}
			documents := []reference{}
			for id, p := range d.Projects {
				if p.Record == nil {
					continue
				}
				var record struct {
					Meta struct {
						Name string `json:"name"`
					} `json:"meta"`
					Doc json.RawMessage `json:"doc"`
				}
				json.Unmarshal(p.Record, &record)
				ref := reference{ID: id, Name: record.Meta.Name, Trashed: p.Archived}
				json.Unmarshal(record.Doc, &ref.Doc)
				documents = append(documents, ref)
			}
			writeJSON(w, 200, map[string]any{"complete": true, "documents": documents})
		})
	})
	mux.HandleFunc("GET /api/projects/{id}", func(w http.ResponseWriter, r *http.Request) {
		st.view(w, func(d *workspaceData) {
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if err != nil {
				workspaceFailure(w, 409, "project_alias_invalid", err.Error(), "repair_aliases", 0)
				return
			}
			p, ok := d.Projects[id]
			if !ok {
				workspaceFailure(w, 404, "project_missing", "Project not found.", "open_projects", 0)
				return
			}
			if p.Record == nil {
				workspaceFailure(w, 410, "project_archived", "This project was deleted and has no retained document. Its deletion marker prevents stale writers from recreating it.", "open_projects", p.Revision)
				return
			}
			if p.Archived && r.URL.Query().Get("archived") != "1" {
				workspaceFailure(w, 410, "project_archived", "This project is in Trash. Restore it before editing.", "restore_project", p.Revision)
				return
			}
			writeJSON(w, 200, json.RawMessage(p.Record))
		})
	})
	mux.HandleFunc("PUT /api/projects/{id}", func(w http.ResponseWriter, r *http.Request) { st.projectWrite(w, r, "save") })
	for _, action := range []string{"rename", "duplicate", "archive", "restore"} {
		mux.HandleFunc("POST /api/projects/{id}/"+action, func(w http.ResponseWriter, r *http.Request) { st.projectWrite(w, r, action) })
	}
	mux.HandleFunc("GET /api/drafts", func(w http.ResponseWriter, r *http.Request) {
		st.view(w, func(d *workspaceData) {
			out := []workspaceDraft{}
			for _, v := range d.Drafts {
				v.Document = nil
				out = append(out, v)
			}
			sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
			writeJSON(w, 200, out)
		})
	})
	mux.HandleFunc("GET /api/drafts/{id}", func(w http.ResponseWriter, r *http.Request) {
		st.view(w, func(d *workspaceData) {
			v, ok := d.Drafts[r.PathValue("id")]
			if !ok {
				workspaceFailure(w, 404, "draft_missing", "Recovery draft not found.", "open_projects", 0)
				return
			}
			writeJSON(w, 200, v)
		})
	})
	mux.HandleFunc("PUT /api/drafts/{id}", st.putDraft)
	mux.HandleFunc("DELETE /api/drafts/{id}", st.deleteDraft)
	for _, kind := range []string{"session", "preferences"} {
		mux.HandleFunc("GET /api/"+kind, func(w http.ResponseWriter, r *http.Request) {
			st.view(w, func(d *workspaceData) {
				v := d.Values[kind]
				if v.Document == nil {
					v.Document = json.RawMessage(`{}`)
				}
				writeJSON(w, 200, v)
			})
		})
		method := "PUT"
		if kind == "session" {
			method = "PATCH"
		}
		mux.HandleFunc(method+" /api/"+kind, func(w http.ResponseWriter, r *http.Request) { st.putValue(w, r, kind) })
	}
	mux.HandleFunc("PUT /api/assets/{key}", st.putAsset)
	mux.HandleFunc("GET /api/assets/{key}", st.getAsset)
}
func (st *workspaceStore) projectWrite(w http.ResponseWriter, r *http.Request, action string) {
	var in struct {
		OperationID      string          `json:"operationId"`
		ExpectedRevision uint64          `json:"expectedRevision"`
		Record           json.RawMessage `json:"record"`
		Name             string          `json:"name"`
		NewID            string          `json:"newId"`
	}
	if !workspaceDecode(w, r, &in) {
		return
	}
	st.locked(w, func(d *workspaceData) {
		id, aliasErr := canonicalWorkspaceProject(d, r.PathValue("id"))
		if aliasErr != nil {
			workspaceFailure(w, 409, "project_alias_invalid", aliasErr.Error(), "repair_aliases", 0)
			return
		}
		digest := workspaceDigest([]any{action, id, in})
		if workspaceReplay(w, d, in.OperationID, digest) {
			return
		}
		p, exists := d.Projects[id]
		if p.Revision != in.ExpectedRevision {
			workspaceFailure(w, 409, "revision_conflict", "Another writer changed this project. Reload or keep a separate copy.", "reload_and_reapply", p.Revision)
			return
		}
		if !exists && action != "save" {
			workspaceFailure(w, 404, "project_missing", "Project not found.", "open_projects", 0)
			return
		}
		if p.Archived && p.Record == nil {
			workspaceFailure(w, 410, "project_archived", "This deleted project has no retained document to restore. Keep its deletion marker and open another project.", "open_projects", p.Revision)
			return
		}
		if p.Archived && action != "restore" {
			workspaceFailure(w, 410, "project_archived", "This project is in Trash. Restore it before saving.", "restore_project", p.Revision)
			return
		}
		next := p
		next.Revision++
		var err error
		switch action {
		case "save":
			next.Record, err = projectRecord(in.Record, id, next.Revision)
			if err == nil {
				err = st.checkAssets(next.Record)
			}
		case "rename", "duplicate":
			var record map[string]json.RawMessage
			json.Unmarshal(p.Record, &record)
			var meta map[string]json.RawMessage
			json.Unmarshal(record["meta"], &meta)
			meta["name"], _ = json.Marshal(in.Name)
			meta["savedAt"], _ = json.Marshal(time.Now().UnixMilli())
			record["meta"], _ = json.Marshal(meta)
			raw, _ := json.Marshal(record)
			if action == "duplicate" {
				if !workspaceID(in.NewID) {
					err = errors.New("a new stable project ID is required")
				} else if _, ok := d.Projects[in.NewID]; ok {
					err = errors.New("the duplicate ID already exists")
				} else if _, alias := d.Aliases[in.NewID]; alias {
					err = errors.New("the duplicate ID is reserved by a project alias")
				} else {
					id = in.NewID
					next.Revision = 1
				}
			}
			if err == nil {
				next.Record, err = projectRecord(raw, id, next.Revision)
			}
		case "archive":
			next.Archived = true
			next.Record, err = projectRecord(p.Record, id, next.Revision)
		case "restore":
			next.Archived = false
			next.Record, err = projectRecord(p.Record, id, next.Revision)
		}
		if err != nil {
			workspaceFailure(w, 400, "project_invalid", err.Error(), "export_and_repair", p.Revision)
			return
		}
		d.Projects[id] = next
		response := any(map[string]any{"revision": next.Revision, "operationId": in.OperationID})
		if action == "duplicate" {
			response = json.RawMessage(projectMeta(next))
		}
		if !st.commit(w, d, in.OperationID, digest, response) {
			return
		}
		switch action {
		case "save":
			st.recordVersion(id, next.Revision, next.Record, recordChange(in.Record), action)
		case "rename":
			st.recordVersion(id, next.Revision, next.Record, []versionChange{{Key: "name", Label: "Renamed to " + in.Name}}, action)
		case "duplicate":
			st.recordVersion(id, next.Revision, next.Record, []versionChange{{Label: "Copied"}}, action)
		}
	})
}
func (st *workspaceStore) putDraft(w http.ResponseWriter, r *http.Request) {
	var in struct {
		OperationID string          `json:"operationId"`
		Sequence    uint64          `json:"sequence"`
		Document    json.RawMessage `json:"document"`
		Metadata    json.RawMessage `json:"metadata"`
	}
	if !workspaceDecode(w, r, &in) {
		return
	}
	id := r.PathValue("id")
	if !workspaceID(id) || len(in.Document) == 0 {
		workspaceFailure(w, 400, "draft_invalid", "A stable draft ID and document are required.", "keep_draft", 0)
		return
	}
	if err := st.checkAssets(in.Document); err != nil {
		workspaceFailure(w, 400, "asset_missing", err.Error(), "reimport_asset", 0)
		return
	}
	digest := workspaceDigest([]any{id, in})
	st.locked(w, func(d *workspaceData) {
		if workspaceReplay(w, d, in.OperationID, digest) {
			return
		}
		if old, ok := d.Drafts[id]; ok && in.Sequence <= old.Sequence {
			workspaceFailure(w, 409, "draft_conflict", "A newer recovery draft exists. Keep both versions before resolving.", "recover_draft", old.Sequence)
			return
		}
		d.Drafts[id] = workspaceDraft{id, in.Sequence, in.Document, in.Metadata, time.Now().UnixMilli()}
		st.commit(w, d, in.OperationID, digest, map[string]any{"sequence": in.Sequence, "operationId": in.OperationID})
	})
}
func (st *workspaceStore) deleteDraft(w http.ResponseWriter, r *http.Request) {
	var through uint64
	if _, err := fmt.Sscan(r.URL.Query().Get("throughSequence"), &through); err != nil {
		workspaceFailure(w, 400, "sequence_required", "throughSequence is required to avoid deleting a newer draft.", "recover_draft", 0)
		return
	}
	st.locked(w, func(d *workspaceData) {
		id := r.PathValue("id")
		if d.Drafts[id].Sequence > through {
			workspaceFailure(w, 409, "draft_conflict", "A newer recovery draft was kept.", "recover_draft", d.Drafts[id].Sequence)
			return
		}
		delete(d.Drafts, id)
		if err := st.save(d); err != nil {
			workspaceFailure(w, 507, "workspace_write_failed", "Recovery draft could not be removed.", "retry_storage", 0)
			return
		}
		w.WriteHeader(204)
	})
}
func (st *workspaceStore) putValue(w http.ResponseWriter, r *http.Request, kind string) {
	var in struct {
		OperationID      string          `json:"operationId"`
		ExpectedRevision uint64          `json:"expectedRevision"`
		Document         json.RawMessage `json:"document"`
	}
	if !workspaceDecode(w, r, &in) {
		return
	}
	if len(in.Document) == 0 {
		workspaceFailure(w, 400, "invalid_document", "document is required.", "keep_draft", 0)
		return
	}
	digest := workspaceDigest([]any{kind, in})
	st.locked(w, func(d *workspaceData) {
		if workspaceReplay(w, d, in.OperationID, digest) {
			return
		}
		v := d.Values[kind]
		if v.Revision != in.ExpectedRevision {
			workspaceFailure(w, 409, "revision_conflict", "Another writer changed saved "+kind+". Reload before applying changes.", "reload_and_reapply", v.Revision)
			return
		}
		v = workspaceValue{v.Revision + 1, in.Document}
		d.Values[kind] = v
		st.commit(w, d, in.OperationID, digest, map[string]any{"revision": v.Revision, "operationId": in.OperationID})
	})
}
func (st *workspaceStore) assetPath(key string) string {
	return filepath.Join(st.root, "assets", workspaceDigest(key))
}
func (st *workspaceStore) putAsset(w http.ResponseWriter, r *http.Request) {
	key := r.PathValue("key")
	if key == "" || len(key) > 1024 {
		workspaceFailure(w, 400, "asset_invalid", "A valid asset key is required.", "reimport_asset", 0)
		return
	}
	dir := filepath.Join(st.root, "assets")
	if err := os.MkdirAll(dir, 0700); err != nil {
		workspaceFailure(w, 507, "asset_write_failed", "Asset storage is unavailable.", "free_storage_and_retry", 0)
		return
	}
	f, err := os.CreateTemp(dir, ".asset-*")
	if err != nil {
		workspaceFailure(w, 507, "asset_write_failed", err.Error(), "free_storage_and_retry", 0)
		return
	}
	defer os.Remove(f.Name())
	hash := sha256.New()
	_, err = io.Copy(io.MultiWriter(f, hash), http.MaxBytesReader(w, r.Body, 512<<20))
	if err == nil {
		err = f.Sync()
	}
	err = errors.Join(err, f.Close())
	var tooLarge *http.MaxBytesError
	var timeout net.Error
	switch {
	case errors.As(err, &tooLarge):
		workspaceFailure(w, 413, "asset_too_large", "The model exceeds 512 MiB.", "reimport_asset", 0)
		return
	case errors.As(err, &timeout) && timeout.Timeout():
		w.Header().Set("Connection", "close")
		workspaceFailure(w, 408, "asset_upload_timeout", "The model upload stalled. Retry.", "retry_same_asset", 0)
		return
	case err != nil:
		workspaceFailure(w, 400, "asset_upload_failed", "The model upload did not complete. Retry.", "retry_same_asset", 0)
		return
	}
	digest := hex.EncodeToString(hash.Sum(nil))
	st.mu.Lock()
	defer st.mu.Unlock()
	path := st.assetPath(key)
	if old, err := os.Open(path); err == nil {
		h := sha256.New()
		_, copyErr := io.Copy(h, old)
		old.Close()
		if copyErr != nil || hex.EncodeToString(h.Sum(nil)) != digest {
			workspaceFailure(w, 409, "asset_conflict", "This asset key already contains different bytes. Preserve the original key and use a new key for the edited asset.", "reimport_asset", 0)
			return
		}
	} else if !errors.Is(err, os.ErrNotExist) {
		workspaceFailure(w, 503, "asset_unavailable", "The existing asset could not be checked.", "retry_storage", 0)
		return
	} else if err = os.Rename(f.Name(), path); err != nil {
		workspaceFailure(w, 507, "asset_write_failed", "Asset could not be committed.", "free_storage_and_retry", 0)
		return
	}
	if err := syncWorkspaceDirectory(dir); err != nil {
		workspaceFailure(w, 507, "asset_write_failed", "Asset directory could not be synced. Retry the same bytes.", "retry_same_asset", 0)
		return
	}
	w.Header().Set("X-Content-SHA256", digest)
	writeJSON(w, 200, map[string]string{"key": key, "sha256": digest})
}
func (st *workspaceStore) getAsset(w http.ResponseWriter, r *http.Request) {
	path := st.assetPath(r.PathValue("key"))
	f, err := os.Open(path)
	if err != nil {
		workspaceFailure(w, 404, "asset_missing", "The original model asset is missing.", "reimport_asset", 0)
		return
	}
	defer f.Close()
	h := sha256.New()
	if _, err = io.Copy(h, f); err != nil {
		workspaceFailure(w, 503, "asset_unavailable", "The model asset could not be read.", "retry_storage", 0)
		return
	}
	sum := hex.EncodeToString(h.Sum(nil))
	w.Header().Set("X-Content-SHA256", sum)
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Cache-Control", privateRevalidate)
	w.Header().Set("ETag", `"`+sum+`"`)
	f.Seek(0, io.SeekStart)
	stat, err := f.Stat()
	if err != nil {
		workspaceFailure(w, 503, "asset_unavailable", "The model asset could not be read.", "retry_storage", 0)
		return
	}
	if serveAssetGzip(w, r, f, stat.Size(), sum) {
		return
	}
	http.ServeContent(w, r, "model", stat.ModTime(), f)
}

func workspaceHasReference(d *workspaceData, id string) bool {
	if d == nil {
		return false
	}
	var contains func(any) bool
	contains = func(v any) bool {
		switch x := v.(type) {
		case string:
			return x == id
		case []any:
			for _, item := range x {
				if contains(item) {
					return true
				}
			}
		case map[string]any:
			for key, item := range x {
				if key == id || contains(item) {
					return true
				}
			}
		}
		return false
	}
	check := func(raw json.RawMessage) bool { var v any; return json.Unmarshal(raw, &v) == nil && contains(v) }
	for _, p := range d.Projects {
		if check(p.Record) {
			return true
		}
	}
	for _, draft := range d.Drafts {
		if check(draft.Document) || check(draft.Metadata) {
			return true
		}
	}
	for _, v := range d.Values {
		if check(v.Document) {
			return true
		}
	}
	for _, raw := range d.Adoptions {
		if check(raw) {
			return true
		}
	}
	return false
}
