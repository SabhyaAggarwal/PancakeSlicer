package main

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"io/fs"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

const (
	versionIdle      = 2 * time.Minute
	versionSpan      = 15 * time.Minute
	versionKeepFresh = 40
)

type versionChange struct {
	Key   string `json:"key,omitempty"`
	Label string `json:"label"`
	From  string `json:"from,omitempty"`
	To    string `json:"to,omitempty"`
}
type versionSlice struct {
	Key    string   `json:"key"`
	Job    string   `json:"job"`
	At     int64    `json:"at"`
	TimeS  *float64 `json:"timeS,omitempty"`
	Grams  *float64 `json:"grams,omitempty"`
	Layers *float64 `json:"layers,omitempty"`
}
type projectVersion struct {
	ID       uint64          `json:"id"`
	Revision uint64          `json:"revision"`
	At       int64           `json:"at"`
	Until    int64           `json:"until"`
	Record   string          `json:"record"`
	Name     string          `json:"name"`
	Objects  int             `json:"objects"`
	Change   []versionChange `json:"change"`
	Sealed   bool            `json:"sealed,omitempty"`
	Slice    *versionSlice   `json:"slice,omitempty"`
}
type versionIndex struct {
	Next     uint64           `json:"next"`
	Versions []projectVersion `json:"versions"`
}

func (st *workspaceStore) versionDir(id string) string { return filepath.Join(st.root, "versions", id) }
func (st *workspaceStore) readVersions(id string) (*versionIndex, error) {
	raw, err := os.ReadFile(filepath.Join(st.versionDir(id), "index.json"))
	if errors.Is(err, os.ErrNotExist) {
		return &versionIndex{Next: 1}, nil
	}
	if err != nil {
		return nil, err
	}
	var x versionIndex
	if err = json.Unmarshal(raw, &x); err != nil {
		return nil, err
	}
	return &x, nil
}

func recordChange(raw json.RawMessage) []versionChange {
	var v struct {
		Meta struct {
			Change []versionChange `json:"change"`
		} `json:"meta"`
	}
	json.Unmarshal(raw, &v)
	return v.Meta.Change
}

func mergeChanges(into, more []versionChange) []versionChange {
	out := append([]versionChange(nil), into...)
next:
	for _, c := range more {
		if c.Label == "" {
			continue
		}
		for i := range out {
			if c.Key != "" && out[i].Key == c.Key || c.Key == "" && out[i].Key == "" && out[i].Label == c.Label {
				out[i].Label, out[i].To = c.Label, c.To
				if out[i].From != "" && out[i].From == out[i].To {
					out = append(out[:i], out[i+1:]...)
				}
				continue next
			}
		}
		out = append(out, c)
	}
	if len(out) > 8 {
		out = out[len(out)-8:]
	}
	return out
}

func (st *workspaceStore) recordVersion(id string, revision uint64, record json.RawMessage, change []versionChange, kind string) {
	if err := st.writeVersion(id, revision, record, change, kind); err != nil {
		log.Printf("project %s version history: %v", id, err)
	}
}
func (st *workspaceStore) writeVersion(id string, revision uint64, record json.RawMessage, change []versionChange, kind string) error {
	dir := st.versionDir(id)
	if err := os.MkdirAll(filepath.Join(dir, "records"), 0700); err != nil {
		return err
	}
	sum := sha256.Sum256(record)
	sha := hex.EncodeToString(sum[:])
	path := filepath.Join(dir, "records", sha+".json")
	if _, err := os.Stat(path); errors.Is(err, os.ErrNotExist) {
		if err := atomicJobFile(path, record); err != nil {
			return err
		}
	}
	x, err := st.readVersions(id)
	if err != nil {
		return err
	}
	var meta struct {
		Meta struct {
			Name    string `json:"name"`
			Objects int    `json:"objects"`
		} `json:"meta"`
	}
	json.Unmarshal(record, &meta)
	now := time.Now().UnixMilli()
	n := len(x.Versions)
	if n > 0 && x.Versions[n-1].Record == sha && x.Versions[n-1].Name == meta.Meta.Name {
		x.Versions[n-1].Revision, x.Versions[n-1].Until = revision, now
	} else if last := lastVersion(x); last != nil && kind != "duplicate" && !last.Sealed && last.Slice == nil &&
		now-last.Until < versionIdle.Milliseconds() && now-last.At < versionSpan.Milliseconds() {
		last.Revision, last.Until, last.Record, last.Name, last.Objects = revision, now, sha, meta.Meta.Name, meta.Meta.Objects
		last.Change = mergeChanges(last.Change, change)
	} else {
		x.Versions = append(x.Versions, projectVersion{ID: x.Next, Revision: revision, At: now, Until: now, Record: sha,
			Name: meta.Meta.Name, Objects: meta.Meta.Objects, Change: mergeChanges(nil, change)})
		x.Next++
	}
	return st.saveVersions(id, x)
}
func lastVersion(x *versionIndex) *projectVersion {
	if len(x.Versions) == 0 {
		return nil
	}
	return &x.Versions[len(x.Versions)-1]
}

func (st *workspaceStore) saveVersions(id string, x *versionIndex) error {
	kept, fresh, days := x.Versions[:0:0], 0, map[string]bool{}
	for i := len(x.Versions) - 1; i >= 0; i-- {
		v := x.Versions[i]
		keep := i == len(x.Versions)-1 || v.Slice != nil
		if !keep && fresh < versionKeepFresh {
			keep, fresh = true, fresh+1
		} else if !keep {
			day := time.UnixMilli(v.Until).UTC().Format("2006-01-02")
			keep, days[day] = !days[day], true
		}
		if keep {
			kept = append(kept, v)
		}
	}
	sort.Slice(kept, func(i, j int) bool { return kept[i].ID < kept[j].ID })
	x.Versions = kept
	dir := st.versionDir(id)
	if err := durableJSON(filepath.Join(dir, "index.json"), x); err != nil {
		return err
	}
	records, slices := map[string]bool{}, map[string]bool{}
	for _, v := range x.Versions {
		records[v.Record+".json"] = true
		if v.Slice != nil {
			slices[v.Slice.Key] = true
		}
	}
	for sub, used := range map[string]map[string]bool{"records": records, "slices": slices} {
		entries, _ := os.ReadDir(filepath.Join(dir, sub))
		for _, e := range entries {
			if !used[e.Name()] && e.Name()[0] != '.' {
				os.RemoveAll(filepath.Join(dir, sub, e.Name()))
			}
		}
	}
	return nil
}

func sliceNumbers(raw json.RawMessage, s *versionSlice) {
	var r struct {
		Totals     map[string]any `json:"totals"`
		Text       map[string]any `json:"text_summary"`
		Statistics map[string]any `json:"statistics"`
	}
	json.Unmarshal(raw, &r)
	num := func(maps ...map[string]any) func(string) *float64 {
		return func(key string) *float64 {
			for _, m := range maps {
				switch v := m[key].(type) {
				case float64:
					return &v
				case string:
					if f, err := strconv.ParseFloat(v, 64); err == nil {
						return &f
					}
				}
			}
			return nil
		}
	}
	s.TimeS = num(r.Totals, r.Text)("estimated_time_s")
	s.Grams = num(r.Statistics)("total_weight_g")
	s.Layers = num(r.Totals, r.Statistics)("layer_count")
}

func sliceJob(j *Job) bool {
	r := j.Request
	return j.State() == StateDone && !r.Orient && !r.Explode && r.Cut == "" && r.MeshOp == "" && !r.Arrange && !r.WipeTower && r.Output != "patch-preview"
}

func (s *Server) versionRoutes(mux *http.ServeMux) {
	st := s.workspace
	project := func(w http.ResponseWriter, r *http.Request, fn func(d *workspaceData, id string)) {
		st.locked(w, func(d *workspaceData) {
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if err != nil {
				workspaceFailure(w, 409, "project_alias_invalid", err.Error(), "repair_aliases", 0)
				return
			}
			if p, ok := d.Projects[id]; !ok || p.Record == nil {
				workspaceFailure(w, 404, "project_missing", "Project not found.", "open_projects", 0)
				return
			}
			fn(d, id)
		})
	}
	version := func(w http.ResponseWriter, r *http.Request, fn func(d *workspaceData, id string, x *versionIndex, v *projectVersion)) {
		project(w, r, func(d *workspaceData, id string) {
			x, err := st.readVersions(id)
			if err != nil {
				workspaceFailure(w, 503, "versions_unavailable", "Version history could not be read.", "retry_storage", 0)
				return
			}
			want, _ := strconv.ParseUint(r.PathValue("v"), 10, 64)
			for i := range x.Versions {
				if x.Versions[i].ID == want {
					fn(d, id, x, &x.Versions[i])
					return
				}
			}
			workspaceFailure(w, 404, "version_missing", "This version is no longer kept.", "open_versions", 0)
		})
	}
	mux.HandleFunc("GET /api/versions/{id}", func(w http.ResponseWriter, r *http.Request) {
		project(w, r, func(d *workspaceData, id string) {
			x, err := st.readVersions(id)
			if err != nil {
				workspaceFailure(w, 503, "versions_unavailable", "Version history could not be read.", "retry_storage", 0)
				return
			}
			out := make([]projectVersion, 0, len(x.Versions))
			for i := len(x.Versions) - 1; i >= 0; i-- {
				out = append(out, x.Versions[i])
			}
			writeJSON(w, 200, map[string]any{"revision": d.Projects[id].Revision, "versions": out})
		})
	})
	mux.HandleFunc("GET /api/versions/{id}/{v}", func(w http.ResponseWriter, r *http.Request) {
		version(w, r, func(_ *workspaceData, id string, _ *versionIndex, v *projectVersion) {
			raw, err := os.ReadFile(filepath.Join(st.versionDir(id), "records", v.Record+".json"))
			if err != nil {
				workspaceFailure(w, 503, "version_unavailable", "This version could not be read.", "retry_storage", 0)
				return
			}
			w.Header().Set("Cache-Control", "private, max-age=31536000, immutable")
			writeJSON(w, 200, json.RawMessage(raw))
		})
	})
	mux.HandleFunc("POST /api/versions/{id}/seal", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Revision uint64 `json:"revision"`
		}
		if !workspaceDecode(w, r, &in) {
			return
		}
		project(w, r, func(_ *workspaceData, id string) {
			x, err := st.readVersions(id)
			last := lastVersion(x)
			if err != nil || last == nil || last.Revision != in.Revision {
				workspaceFailure(w, 409, "version_moved", "The project changed after this revision.", "reload", 0)
				return
			}
			if !last.Sealed {
				last.Sealed = true
				if err = st.saveVersions(id, x); err != nil {
					workspaceFailure(w, 507, "versions_write_failed", "Version history could not be saved.", "retry_storage", 0)
					return
				}
			}
			writeJSON(w, 200, last)
		})
	})
	mux.HandleFunc("POST /api/versions/{id}/{v}/slice", func(w http.ResponseWriter, r *http.Request) {
		var in struct {
			Revision uint64 `json:"revision"`
			Job      string `json:"jobId"`
		}
		if !workspaceDecode(w, r, &in) {
			return
		}
		j := s.jobs.Get(in.Job)
		if j == nil || !sliceJob(j) {
			workspaceFailure(w, 409, "slice_unavailable", "The slice is not finished or no longer exists.", "slice_again", 0)
			return
		}
		key := j.ID
		if marker, err := os.ReadFile(filepath.Join(j.Dir, ".result-cache-key")); err == nil {
			sum := sha256.Sum256(marker)
			key = hex.EncodeToString(sum[:])
		}
		view := j.View()
		root := filepath.Join(st.root, "versions")
		if err := os.MkdirAll(root, 0700); err != nil {
			workspaceFailure(w, 507, "slice_copy_failed", "The slice could not be kept with this version.", "retry_storage", 0)
			return
		}
		var pending string
		var err error
		if _, kept := os.Stat(filepath.Join(st.versionDir(r.PathValue("id")), "slices", key)); kept != nil {
			pending, err = os.MkdirTemp(root, ".pending-")
		}
		if pending != "" {
			defer os.RemoveAll(pending)
			request, _ := json.Marshal(view.Request)
			err = errors.Join(copyKeptSlice(j.Dir, pending), atomicJobFile(filepath.Join(pending, "record.json"), view.Slice), atomicJobFile(filepath.Join(pending, "request.json"), request))
		}
		if err != nil {
			workspaceFailure(w, 507, "slice_copy_failed", "The slice could not be kept with this version.", "retry_storage", 0)
			return
		}
		version(w, r, func(_ *workspaceData, id string, x *versionIndex, v *projectVersion) {
			if v.Revision != in.Revision {
				workspaceFailure(w, 409, "version_moved", "This version changed after the slice started.", "slice_again", 0)
				return
			}
			target := filepath.Join(st.versionDir(id), "slices", key)
			if _, err := os.Stat(target); errors.Is(err, os.ErrNotExist) {
				if pending == "" {
					err = errors.New("no staged copy")
				} else {
					err = errors.Join(os.MkdirAll(filepath.Dir(target), 0700), os.Rename(pending, target))
				}
				if err != nil {
					workspaceFailure(w, 507, "slice_copy_failed", "The slice could not be kept with this version.", "retry_storage", 0)
					return
				}
			}
			v.Sealed = true
			v.Slice = &versionSlice{Key: key, Job: j.ID, At: time.Now().UnixMilli()}
			sliceNumbers(view.Slice, v.Slice)
			if err := st.saveVersions(id, x); err != nil {
				workspaceFailure(w, 507, "versions_write_failed", "Version history could not be saved.", "retry_storage", 0)
				return
			}
			writeJSON(w, 200, v)
		})
	})
	mux.HandleFunc("POST /api/versions/{id}/{v}/open-slice", func(w http.ResponseWriter, r *http.Request) {
		version(w, r, func(_ *workspaceData, id string, x *versionIndex, v *projectVersion) {
			if v.Slice == nil {
				workspaceFailure(w, 404, "slice_missing", "This version was not sliced.", "slice_again", 0)
				return
			}
			if j := s.jobs.Get(v.Slice.Job); j != nil && sliceJob(j) {
				writeJSON(w, 200, j.View())
				return
			}
			src := filepath.Join(st.versionDir(id), "slices", v.Slice.Key)
			record, err := os.ReadFile(filepath.Join(src, "record.json"))
			var request JobRequest
			if err == nil {
				var raw []byte
				if raw, err = os.ReadFile(filepath.Join(src, "request.json")); err == nil {
					err = json.Unmarshal(raw, &request)
				}
			}
			var j *Job
			if err == nil {
				j, err = s.jobs.Rehydrate(s.clientIP(r), request, src, record)
			}
			if err != nil {
				log.Printf("project %s version %d slice: %v", id, v.ID, err)
				workspaceFailure(w, 503, "slice_unavailable", "The kept slice could not be opened.", "slice_again", 0)
				return
			}
			v.Slice.Job = j.ID
			if err := st.saveVersions(id, x); err != nil {
				log.Printf("project %s version %d slice job: %v", id, v.ID, err)
			}
			writeJSON(w, 200, j.View())
		})
	})
}

func copyKeptSlice(src, dst string) error {
	for _, name := range resultPaths {
		base := filepath.Join(src, name)
		err := filepath.WalkDir(base, func(path string, d fs.DirEntry, err error) error {
			if errors.Is(err, os.ErrNotExist) && path == base {
				return nil
			}
			if err != nil {
				return err
			}
			rel, err := filepath.Rel(src, path)
			if err != nil {
				return err
			}
			target := filepath.Join(dst, rel)
			if d.IsDir() {
				return os.MkdirAll(target, 0700)
			}
			if n := d.Name(); strings.HasSuffix(n, ".tmp") || strings.HasSuffix(n, ".gz") {
				return nil
			}
			info, err := d.Info()
			if err != nil {
				return err
			}
			if !info.Mode().IsRegular() {
				return errors.New("nonregular result file")
			}
			in, err := os.Open(path)
			if err != nil {
				return err
			}
			out, err := os.OpenFile(target, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0600)
			if err != nil {
				in.Close()
				return err
			}
			_, copyErr := io.Copy(out, in)
			if err = errors.Join(copyErr, out.Sync(), out.Close(), in.Close()); err != nil {
				return err
			}
			return os.Chtimes(target, info.ModTime(), info.ModTime())
		})
		if err != nil {
			return err
		}
	}
	return nil
}

func (m *JobManager) Rehydrate(client string, request JobRequest, src string, record json.RawMessage) (*Job, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return nil, err
	}
	id := hex.EncodeToString(b[:])
	dir := filepath.Join(m.cfg.JobRoot, id)
	for _, sub := range []string{"input", "out", "datadir", "tmp"} {
		if err := os.MkdirAll(filepath.Join(dir, sub), 0o755); err != nil {
			return nil, errors.Join(err, os.RemoveAll(dir))
		}
	}
	if err := copyKeptSlice(src, dir); err != nil {
		return nil, errors.Join(err, os.RemoveAll(dir))
	}
	code := 0
	j := &Job{ID: id, Client: client, Dir: dir, Request: request, Created: time.Now(), state: StateIdle, changed: make(chan struct{}),
		stageHistory: m.stages, stageDurations: map[string]float64{}, slice: record, exitCode: &code}
	for _, s := range []JobState{StateQueued, StatePreparing, StateDone} {
		if !j.setState(s, "") {
			return nil, errors.Join(errors.New("job state rejected"), os.RemoveAll(dir))
		}
	}
	m.mu.Lock()
	m.jobs[id] = j
	m.mu.Unlock()
	return j, nil
}
