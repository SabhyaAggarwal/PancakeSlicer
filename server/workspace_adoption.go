package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"sort"
)

type adoptionAsset struct {
	Key    string `json:"key"`
	Part   string `json:"part"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
}
type adoptionKV struct {
	Key   string          `json:"key"`
	Value json.RawMessage `json:"value"`
}
type workspaceAdoption struct {
	OperationID string            `json:"operationId"`
	SourceID    string            `json:"sourceId"`
	Digest      string            `json:"digest"`
	Projects    []json.RawMessage `json:"projects"`
	Trash       []json.RawMessage `json:"trash"`
	Tombstones  []adoptionKV      `json:"tombstones"`
	Aliases     []adoptionKV      `json:"aliases"`
	Drafts      []workspaceDraft  `json:"drafts"`
	Assets      []adoptionAsset   `json:"assets"`
	Legacy      json.RawMessage   `json:"legacy"`
	LegacyText  string            `json:"legacyText"`
	Preferences json.RawMessage   `json:"preferences,omitempty"`
	Session     json.RawMessage   `json:"session,omitempty"`
}
type adoptionPart struct {
	Path   string
	Size   int64
	SHA256 string
}

func canonicalEqual(a, b any) bool {
	rawA, _ := json.Marshal(a)
	rawB, _ := json.Marshal(b)
	var x, y any
	da := json.NewDecoder(bytes.NewReader(rawA))
	da.UseNumber()
	db := json.NewDecoder(bytes.NewReader(rawB))
	db.UseNumber()
	if da.Decode(&x) != nil || db.Decode(&y) != nil {
		return false
	}
	return workspaceDigest(x) == workspaceDigest(y)
}
func hashFile(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	_, err = io.Copy(h, f)
	return hex.EncodeToString(h.Sum(nil)), err
}
func (st *workspaceStore) adoptionRoutes(mux *http.ServeMux) {
	mux.HandleFunc("POST /api/projects/adopt", st.adopt)
	mux.HandleFunc("GET /api/projects/adoptions/{operation}", func(w http.ResponseWriter, r *http.Request) {
		st.locked(w, func(d *workspaceData) {
			raw, ok := d.Adoptions[r.PathValue("operation")]
			if !ok {
				workspaceFailure(w, 404, "adoption_missing", "No durable adoption acknowledgement exists for this operation.", "retry_same_operation", 0)
				return
			}
			var saved struct {
				Receipt json.RawMessage `json:"receipt"`
			}
			if json.Unmarshal(raw, &saved) != nil || saved.Receipt == nil {
				workspaceFailure(w, 503, "adoption_unavailable", "The adoption receipt could not be read. Keep all original source data.", "retry_storage", 0)
				return
			}
			writeJSON(w, 200, json.RawMessage(saved.Receipt))
		})
	})
}
func (st *workspaceStore) adopt(w http.ResponseWriter, r *http.Request) {
	r.Body = http.MaxBytesReader(w, r.Body, 2<<30)
	reader, err := r.MultipartReader()
	if err != nil {
		workspaceFailure(w, 400, "adoption_invalid", "Expected multipart manifest and model assets.", "keep_legacy_inventory", 0)
		return
	}
	if err = os.MkdirAll(st.root, 0700); err != nil {
		workspaceFailure(w, 507, "workspace_write_failed", "Migration staging is unavailable. Keep all original data.", "free_storage_and_retry", 0)
		return
	}
	temp, err := os.MkdirTemp(st.root, ".adoption-*")
	if err != nil {
		workspaceFailure(w, 507, "workspace_write_failed", err.Error(), "free_storage_and_retry", 0)
		return
	}
	defer os.RemoveAll(temp)
	parts := map[string]adoptionPart{}
	var rawManifest []byte
	for n := 0; ; n++ {
		part, e := reader.NextPart()
		if e == io.EOF {
			break
		}
		if e != nil || n >= 4096 {
			workspaceFailure(w, 400, "adoption_invalid", "Migration upload is incomplete or has too many parts.", "retry_same_operation", 0)
			return
		}
		name := part.FormName()
		if name == "manifest" {
			if rawManifest != nil {
				workspaceFailure(w, 400, "adoption_invalid", "Only one manifest is accepted.", "keep_legacy_inventory", 0)
				return
			}
			rawManifest, e = io.ReadAll(io.LimitReader(part, (64<<20)+1))
			part.Close()
			if e != nil || len(rawManifest) > 64<<20 {
				workspaceFailure(w, 400, "adoption_invalid", "Migration manifest exceeds 64 MiB.", "export_and_repair", 0)
				return
			}
			continue
		}
		if name == "" {
			workspaceFailure(w, 400, "adoption_invalid", "Every asset part needs a manifest name.", "keep_legacy_inventory", 0)
			return
		}
		if _, exists := parts[name]; exists {
			workspaceFailure(w, 400, "adoption_invalid", "Duplicate asset part names are ambiguous.", "keep_legacy_inventory", 0)
			return
		}
		f, e := os.CreateTemp(temp, "asset-*")
		if e != nil {
			workspaceFailure(w, 507, "asset_write_failed", e.Error(), "free_storage_and_retry", 0)
			return
		}
		h := sha256.New()
		size, e := io.Copy(io.MultiWriter(f, h), io.LimitReader(part, (512<<20)+1))
		part.Close()
		if e == nil {
			e = f.Sync()
		}
		e = errors.Join(e, f.Close())
		if e != nil || size > 512<<20 {
			workspaceFailure(w, 400, "asset_upload_failed", "A complete migration asset could not be staged (512 MiB per asset). Keep source bytes.", "retry_same_operation", 0)
			return
		}
		parts[name] = adoptionPart{f.Name(), size, hex.EncodeToString(h.Sum(nil))}
	}
	var in workspaceAdoption
	if json.Unmarshal(rawManifest, &in) != nil || in.OperationID == "" || in.SourceID == "" || len(in.Digest) != 64 || len(in.Legacy) == 0 || in.LegacyText == "" {
		workspaceFailure(w, 400, "adoption_invalid", "Manifest requires operationId, sourceId, digest, exact legacyText and legacy inventory.", "keep_legacy_inventory", 0)
		return
	}
	sum := sha256.Sum256([]byte(in.LegacyText))
	var text struct {
		Version   int             `json:"version"`
		Inventory json.RawMessage `json:"inventory"`
	}
	if hex.EncodeToString(sum[:]) != in.Digest || json.Unmarshal([]byte(in.LegacyText), &text) != nil || text.Version != 1 || !canonicalEqual(text.Inventory, in.Legacy) {
		workspaceFailure(w, 400, "adoption_digest_mismatch", "The exact legacy inventory does not match its source digest. Keep source data and rebuild the capture.", "recapture_inventory", 0)
		return
	}
	assetByKey := map[string]adoptionPart{}
	used := map[string]bool{}
	for _, a := range in.Assets {
		p, ok := parts[a.Part]
		if !ok || a.Key == "" || len(a.Key) > 1024 || a.Size != p.Size || a.SHA256 != p.SHA256 {
			workspaceFailure(w, 400, "asset_digest_mismatch", "A migration asset does not match its declared key, size and SHA-256.", "retry_same_operation", 0)
			return
		}
		if old, ok := assetByKey[a.Key]; ok && old.SHA256 != p.SHA256 {
			workspaceFailure(w, 409, "asset_conflict", "Two legacy assets use one key for different bytes. Neither source was removed.", "resolve_inventory_conflict", 0)
			return
		}
		assetByKey[a.Key] = p
		used[a.Part] = true
	}
	if len(used) != len(parts) {
		workspaceFailure(w, 400, "adoption_invalid", "An uploaded asset is absent from the manifest.", "keep_legacy_inventory", 0)
		return
	}
	digest := workspaceDigest(in)
	st.locked(w, func(d *workspaceData) {
		if workspaceReplay(w, d, in.OperationID, digest) {
			return
		}
		conflict := func(message string) {
			workspaceFailure(w, 409, "adoption_conflict", message+" No project inventory was adopted; keep all original sources.", "resolve_inventory_conflict", 0)
		}
		for key, p := range assetByKey {
			old, e := hashFile(st.assetPath(key))
			if e == nil && old != p.SHA256 {
				conflict("A saved model asset has different bytes.")
				return
			}
			if e != nil && !errors.Is(e, os.ErrNotExist) {
				workspaceFailure(w, 503, "asset_unavailable", "A saved asset could not be checked.", "retry_storage", 0)
				return
			}
		}
		ids := []string{}
		seen := map[string]bool{}
		for index, records := range [][]json.RawMessage{in.Projects, in.Trash} {
			for _, record := range records {
				var v struct {
					Meta struct {
						ID       string `json:"id"`
						Revision uint64 `json:"revision"`
					} `json:"meta"`
					Doc struct {
						Files map[string]string `json:"files"`
					} `json:"doc"`
				}
				if json.Unmarshal(record, &v) != nil || !workspaceID(v.Meta.ID) || seen[v.Meta.ID] {
					conflict("A project ID is invalid or duplicated.")
					return
				}
				seen[v.Meta.ID] = true
				normalized, e := projectRecord(record, v.Meta.ID, v.Meta.Revision)
				if e != nil {
					conflict(e.Error())
					return
				}
				next := workspaceProject{Record: normalized, Archived: index == 1, Revision: v.Meta.Revision}
				if old, ok := d.Projects[v.Meta.ID]; ok {
					next.Folder = old.Folder
				}
				if old, ok := d.Projects[v.Meta.ID]; ok && !canonicalEqual(old, next) {
					conflict("An existing project revision or document differs.")
					return
				}
				for key := range v.Doc.Files {
					if _, ok := assetByKey[key]; !ok {
						if _, e := os.Stat(st.assetPath(key)); e != nil {
							conflict("An original project model asset is missing.")
							return
						}
					}
				}
				d.Projects[v.Meta.ID] = next
				ids = append(ids, v.Meta.ID)
			}
		}
		for _, t := range in.Tombstones {
			if !workspaceID(t.Key) {
				conflict("A tombstone ID is invalid.")
				return
			}
			var v struct {
				Revision uint64 `json:"revision"`
			}
			if json.Unmarshal(t.Value, &v) != nil {
				conflict("A tombstone revision is invalid.")
				return
			}
			p, ok := d.Projects[t.Key]
			if ok && !p.Archived {
				conflict("A deleted legacy project collides with an active project.")
				return
			}
			if !ok {
				p = workspaceProject{Archived: true, Revision: v.Revision}
			} else if v.Revision > p.Revision {
				p.Revision = v.Revision
				if p.Record != nil {
					p.Record, _ = projectRecord(p.Record, t.Key, p.Revision)
				}
			}
			d.Projects[t.Key] = p
		}
		for _, a := range in.Aliases {
			var target string
			if a.Key == "" || json.Unmarshal(a.Value, &target) != nil || !workspaceID(target) {
				conflict("A legacy alias target is invalid.")
				return
			}
			if old, ok := d.Aliases[a.Key]; ok && old != target {
				conflict("An existing project alias differs.")
				return
			}
			d.Aliases[a.Key] = target
		}

		for alias := range d.Aliases {
			canonical, err := canonicalWorkspaceProject(d, alias)
			if err != nil {
				conflict(err.Error())
				return
			}
			if _, ok := d.Projects[canonical]; !ok {
				conflict("A legacy alias points to a missing project.")
				return
			}
		}

		for _, draft := range in.Drafts {
			if !workspaceID(draft.ID) || len(draft.Document) == 0 {
				conflict("A recovery draft is invalid.")
				return
			}
			if old, ok := d.Drafts[draft.ID]; ok && !canonicalEqual(old, draft) {
				conflict("An existing recovery draft differs.")
				return
			}
			var refs struct {
				Doc struct {
					Files map[string]string `json:"files"`
				} `json:"doc"`
			}
			if json.Unmarshal(draft.Document, &refs) != nil {
				conflict("A recovery document is invalid.")
				return
			}
			for key := range refs.Doc.Files {
				if _, ok := assetByKey[key]; !ok {
					if _, e := os.Stat(st.assetPath(key)); e != nil {
						conflict("A recovery model asset is missing.")
						return
					}
				}
			}
			d.Drafts[draft.ID] = draft
		}
		for kind, raw := range map[string]json.RawMessage{"preferences": in.Preferences, "session": in.Session} {
			if len(raw) == 0 {
				continue
			}
			if kind == "preferences" {
				old := d.Values[kind]
				incoming := map[string]json.RawMessage{}
				saved := map[string]json.RawMessage{}
				if json.Unmarshal(raw, &incoming) != nil || incoming == nil || len(old.Document) > 0 && (json.Unmarshal(old.Document, &saved) != nil || saved == nil) {
					conflict("Saved preferences must be complete JSON objects.")
					return
				}
				for key, value := range saved {
					next, ok := incoming[key]
					if !ok || !canonicalEqual(value, next) {
						conflict("An existing saved preference is missing or changed in the legacy union.")
						return
					}
				}
				if len(incoming) > len(saved) {
					d.Values[kind] = workspaceValue{old.Revision + 1, raw}
				}
				continue
			}
			if old, ok := d.Values[kind]; ok && !canonicalEqual(old.Document, raw) {
				conflict("Saved " + kind + " differs from the legacy capture.")
				return
			}
			if _, ok := d.Values[kind]; !ok {
				d.Values[kind] = workspaceValue{1, raw}
			}
		}
		if err := os.MkdirAll(filepath.Join(st.root, "assets"), 0700); err != nil {
			workspaceFailure(w, 507, "asset_write_failed", "Asset storage unavailable.", "free_storage_and_retry", 0)
			return
		}
		keys := []string{}
		for key, p := range assetByKey {
			if _, e := os.Stat(st.assetPath(key)); errors.Is(e, os.ErrNotExist) {
				if e = os.Link(p.Path, st.assetPath(key)); e != nil {
					workspaceFailure(w, 507, "asset_write_failed", "Migration asset could not be committed.", "free_storage_and_retry", 0)
					return
				}
			}
			keys = append(keys, key)
		}
		if err := syncWorkspaceDirectory(filepath.Join(st.root, "assets")); err != nil {
			workspaceFailure(w, 507, "asset_write_failed", "Migration asset directory could not be synced. Keep all sources and retry.", "retry_same_operation", 0)
			return
		}
		sort.Strings(ids)
		sort.Strings(keys)
		receipt := map[string]any{"durable": true, "digest": in.Digest, "operationId": in.OperationID, "sourceId": in.SourceID, "projectIds": ids, "assetKeys": keys}
		saved, _ := json.Marshal(map[string]any{"manifest": json.RawMessage(rawManifest), "receipt": receipt})
		d.Adoptions[in.OperationID] = saved
		st.commit(w, d, in.OperationID, digest, receipt)
	})
}
