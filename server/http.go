package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"
	"time"
	"unicode"
)

type Server struct {
	cfg     Config
	jobs    *JobManager
	presets *PresetIndex
	users   *UserPresetStore
}

func NewServer(c Config, jobs *JobManager, presets *PresetIndex, users *UserPresetStore) http.Handler {
	s := &Server{c, jobs, presets, users}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/healthz", func(w http.ResponseWriter, r *http.Request) {
		q, n := jobs.Counts()
		writeJSON(w, 200, map[string]any{"status": "ok", "queued": q, "running": n, "tool_features": c.Features})
	})
	mux.HandleFunc("POST /api/jobs", s.createJob)
	mux.HandleFunc("GET /api/jobs/{id}", s.getJob)
	mux.HandleFunc("DELETE /api/jobs/{id}", s.deleteJob)
	mux.HandleFunc("GET /api/jobs/{id}/events", s.events)
	mux.HandleFunc("GET /api/jobs/{id}/download", s.download)
	mux.HandleFunc("GET /api/jobs/{id}/moves", s.moves)
	mux.HandleFunc("GET /api/presets", s.listPresets)
	options := c.Options
	if options == nil {
		options = cacheResponse(optionData)
	}
	mux.HandleFunc("GET /api/options", func(w http.ResponseWriter, r *http.Request) {
		options.serve(w, r, "options.json", "application/json", catalogCache, time.Time{})
	})
	mux.HandleFunc("GET /api/user-presets", s.listUserPresets)
	mux.HandleFunc("POST /api/user-presets", s.saveUserPreset)
	mux.HandleFunc("DELETE /api/user-presets/{type}/{name}", s.deleteTypedUserPreset)
	mux.HandleFunc("/api/user-presets/{name}", s.userPreset)
	mux.HandleFunc("/api/", func(w http.ResponseWriter, r *http.Request) { fail(w, 404, "unknown API endpoint") })
	mux.Handle("/", spaFiles(c.StaticDir))
	if c.Metrics {
		mux.HandleFunc("GET /metrics", s.metrics)
	}
	return s.middleware(mux)
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(value)
}
func fail(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]string{"error": message})
}

func (s *Server) listPresets(w http.ResponseWriter, r *http.Request) {
	if name := r.URL.Query().Get("vendor"); name != "" {
		v, err := s.presets.response(name)
		if errors.Is(err, os.ErrNotExist) {
			fail(w, 404, "unknown vendor")
			return
		}
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
		v.serve(w, r, "presets.json", "application/json", catalogCache, time.Time{})
		return
	}
	vs, err := s.presets.allResponseJSON()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	vs.serve(w, r, "presets.json", "application/json", catalogCache, time.Time{})
}

func (s *Server) createJob(w http.ResponseWriter, r *http.Request) {
	j, err := s.jobs.NewJob(s.clientIP(r))
	if err != nil {
		admissionFailure(w, err)
		return
	}
	started := false
	defer func() {
		s.jobs.releaseUpload(j)
		if !started {
			os.RemoveAll(j.Dir)
		}
	}()

	if r.ContentLength > s.cfg.MaxUploadSize {
		fail(w, http.StatusRequestEntityTooLarge, "multipart upload exceeds max-upload")
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, s.cfg.MaxUploadSize)
	parseErr := r.ParseMultipartForm(8 << 20)
	if r.MultipartForm != nil {
		defer r.MultipartForm.RemoveAll()
	}
	if err := parseErr; err != nil {
		var tooLarge *http.MaxBytesError
		status := 400
		if errors.As(err, &tooLarge) {
			status = 413
		}
		fail(w, status, "invalid multipart upload: "+err.Error())
		return
	}
	req := JobRequest{Vendor: r.FormValue("vendor"), Printer: r.FormValue("printer"), Process: r.FormValue("process"), Filaments: r.MultipartForm.Value["filament[]"], Output: r.FormValue("output"), Filename: r.FormValue("filename")}
	if strings.ContainsAny(req.Filename, "/\\\r\n") || len(req.Filename) > 240 || strings.IndexFunc(req.Filename, unicode.IsControl) >= 0 || req.Filename == "." || req.Filename == ".." {
		fail(w, 400, "filename must be a basename of at most 240 bytes")
		return
	}
	if req.Output == "" {
		req.Output = "gcode"
	}
	if req.Output != "gcode" && req.Output != "gcode.3mf" {
		fail(w, 400, "output must be gcode or gcode.3mf")
		return
	}
	if req.Vendor == "" {
		req.Vendor = s.presets.VendorOfPrinter(req.Printer)
	}
	vendor, ok := s.presets.Vendor(req.Vendor)
	if !ok {
		fail(w, 400, "unknown vendor")
		return
	}
	if findPreset(vendor.Machines, req.Printer) == nil {
		fail(w, 400, "unknown printer preset: "+req.Printer)
		return
	}
	if findPreset(vendor.Processes, req.Process) == nil {
		fail(w, 400, "unknown process preset: "+req.Process)
		return
	}
	if len(req.Filaments) == 0 {
		fail(w, 400, "at least one filament[] is required")
		return
	}
	for _, f := range req.Filaments {
		if findPreset(vendor.Filaments, f) == nil {
			fail(w, 400, "unknown filament preset: "+f)
			return
		}
	}
	if raw := r.FormValue("overrides"); raw != "" {
		req.Overrides, err = overrides(raw)
		if err != nil {
			fail(w, 400, err.Error())
			return
		}
	}
	if raw := r.FormValue("events"); raw != "" {
		if json.Unmarshal([]byte(raw), &req.Events) != nil {
			fail(w, 400, "events must be an array of strings")
			return
		}
	}
	files := r.MultipartForm.File["files[]"]
	if len(files) == 0 {
		fail(w, 400, "files[] is required")
		return
	}
	names := map[string]bool{}
	for _, f := range files {
		_, parameters, headerErr := mime.ParseMediaType(f.Header.Get("Content-Disposition"))
		name := parameters["filename"]
		ext := strings.ToLower(filepath.Ext(name))
		if headerErr != nil || name == "" || len(name) > 240 || strings.IndexFunc(name, unicode.IsControl) >= 0 || strings.ContainsAny(name, "/\\") || name != filepath.Base(name) || (ext != ".stl" && ext != ".obj" && ext != ".3mf") || names[name] {
			fail(w, 400, "uploads need unique STL, OBJ or 3MF basenames")
			return
		}
		names[name] = true
		req.Files = append(req.Files, name)
		in, err := f.Open()
		if err != nil {
			fail(w, 400, err.Error())
			return
		}
		out, err := os.OpenFile(filepath.Join(j.Dir, "input", name), os.O_CREATE|os.O_EXCL|os.O_WRONLY, 0600)
		if err != nil {
			in.Close()
			fail(w, 500, err.Error())
			return
		}
		_, copyErr := io.Copy(out, in)
		err = errors.Join(copyErr, in.Close(), out.Close())
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
	}
	if raw := r.FormValue("plate"); raw != "" {
		var plate map[string]json.RawMessage
		var objects []map[string]json.RawMessage
		if json.Unmarshal([]byte(raw), &plate) != nil || json.Unmarshal(plate["objects"], &objects) != nil || len(objects) == 0 {
			fail(w, 400, "plate needs a non-empty objects array")
			return
		}
		for _, obj := range objects {
			var file string
			if json.Unmarshal(obj["file"], &file) != nil || !names[file] {
				fail(w, 400, "plate objects must reference uploaded basenames")
				return
			}
		}
		if err := os.WriteFile(filepath.Join(j.Dir, "input", "plate.json"), []byte(raw), 0600); err != nil {
			fail(w, 500, err.Error())
			return
		}
		req.Plate = true
	} else if len(files) != 1 {
		fail(w, 400, "multiple uploads require plate JSON")
		return
	}
	j.Request = req
	if err := s.jobs.Start(j); err != nil {
		admissionFailure(w, err)
		return
	}
	started = true
	w.Header().Set("Location", "/api/jobs/"+j.ID)
	writeJSON(w, 202, map[string]string{"id": j.ID})
}

func (s *Server) job(w http.ResponseWriter, r *http.Request) *Job {
	j := s.jobs.Get(r.PathValue("id"))
	if j == nil {
		fail(w, 404, "job not found")
	}
	return j
}
func (s *Server) getJob(w http.ResponseWriter, r *http.Request) {
	if j := s.job(w, r); j != nil {
		writeJSON(w, 200, j.View())
	}
}
func (s *Server) deleteJob(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	if s.jobs.Cancel(j) {
		writeJSON(w, 202, map[string]string{"id": j.ID, "state": "cancelling"})
		return
	}
	if err := s.jobs.Remove(j); err != nil {
		fail(w, 500, "cannot remove job: "+err.Error())
		return
	}
	w.WriteHeader(204)
}
func (s *Server) download(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	if j.State() != StateDone {
		fail(w, 409, "job has no completed download")
		return
	}
	f, err := os.Open(j.OutputPath(j.Request.Output))
	if err != nil {
		fail(w, 404, "download expired")
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": j.DownloadName()}))
	w.Header().Set("Content-Type", "application/octet-stream")
	http.ServeContent(w, r, st.Name(), st.ModTime(), f)
}

func (s *Server) moves(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	f, err := os.Open(j.MovesPath())
	if err != nil || j.State() != StateDone {
		if f != nil {
			f.Close()
		}
		fail(w, 404, "moves are not available")
		return
	}
	defer f.Close()
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": filepath.Base(j.MovesPath())}))
	st, err := f.Stat()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	serveStream(w, r, f, st, "private, max-age=300, must-revalidate")
}

func (s *Server) events(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	from := 0
	if last := r.Header.Get("Last-Event-ID"); last != "" {
		n, err := strconv.Atoi(last)
		if err != nil || n < 0 || n >= maxEvents+10 {
			fail(w, 400, "invalid Last-Event-ID")
			return
		}
		from = n + 1
	}
	controller := http.NewResponseController(w)
	if _, ok := w.(http.Flusher); !ok {
		fail(w, 500, "streaming unavailable")
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("X-Accel-Buffering", "no")
	defer controller.SetWriteDeadline(time.Time{})
	heartbeat := time.NewTicker(15 * time.Second)
	defer heartbeat.Stop()
	for {
		if err := controller.SetWriteDeadline(time.Now().Add(15 * time.Second)); err != nil {
			return
		}
		events, changed, done := j.EventsSince(from)
		for _, e := range events {
			b, _ := json.Marshal(e)
			if _, err := fmt.Fprintf(w, "id: %d\nevent: %s\ndata: %s\n\n", e.ID, e.Type, b); err != nil {
				return
			}
			from = e.ID + 1
		}
		if err := controller.Flush(); err != nil {
			return
		}
		if done {
			return
		}
		select {
		case <-r.Context().Done():
			return
		case <-changed:
		case <-heartbeat.C:
			if err := controller.SetWriteDeadline(time.Now().Add(15 * time.Second)); err != nil {
				return
			}
			if _, err := io.WriteString(w, ": keepalive\n\n"); err != nil {
				return
			}
			if err := controller.Flush(); err != nil {
				return
			}
		}
	}
}
func (s *Server) userPreset(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	if err := validPresetName(name); err != nil {
		fail(w, 400, err.Error())
		return
	}
	switch r.Method {
	case "PUT":
		b, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 1<<20))
		if err != nil {
			fail(w, 413, err.Error())
			return
		}
		if _, err = overrides(string(b)); err != nil {
			fail(w, 400, err.Error())
			return
		}
		err = s.users.Put(&UserPreset{Name: name, Overrides: b})
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
		writeJSON(w, 200, json.RawMessage(b))
	case "GET":
		p, err := s.users.Get(name)
		if errors.Is(err, errPresetNotFound) {
			fail(w, 404, err.Error())
			return
		}
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
		writeJSON(w, 200, p.Overrides)
	case "DELETE":
		err := s.users.Delete(name)
		if errors.Is(err, errPresetNotFound) {
			fail(w, 404, err.Error())
			return
		}
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
		w.WriteHeader(204)
	default:
		w.Header().Set("Allow", "GET, PUT, DELETE")
		fail(w, 405, "method not allowed")
	}
}

func spaFiles(dir string) http.Handler {
	cache := &staticCache{files: map[string]staticEntry{}}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			w.WriteHeader(405)
			return
		}
		clean := path.Clean("/" + r.URL.Path)
		if clean == "/metrics" || clean == "/api" || strings.HasPrefix(clean, "/api/") {
			http.NotFound(w, r)
			return
		}
		relative := filepath.FromSlash(strings.TrimPrefix(clean, "/"))
		if strings.Contains(r.URL.Path, "\\") || (relative != "" && !filepath.IsLocal(relative)) {
			http.NotFound(w, r)
			return
		}
		file := filepath.Join(dir, relative)
		st, err := os.Stat(file)
		if err == nil && st.IsDir() {
			file = filepath.Join(file, "index.html")
			st, err = os.Stat(file)
		}
		if err != nil {
			if strings.HasPrefix(clean, "/images/") || strings.HasPrefix(clean, "/profiles/") || filepath.Ext(clean) != "" {
				http.NotFound(w, r)
				return
			}
			file = filepath.Join(dir, "index.html")
			st, err = os.Stat(file)
		}
		if err != nil || !st.Mode().IsRegular() {
			http.NotFound(w, r)
			return
		}
		cache.serve(w, r, file, st)
	})
}

func validPresetType(t string) bool { return t == "machine" || t == "process" || t == "filament" }
func (s *Server) listUserPresets(w http.ResponseWriter, r *http.Request) {
	ps, err := s.users.List()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, ps)
}
func (s *Server) saveUserPreset(w http.ResponseWriter, r *http.Request) {
	b, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 1<<20))
	if err != nil {
		fail(w, 413, err.Error())
		return
	}
	var p UserPreset
	if json.Unmarshal(b, &p) != nil {
		fail(w, 400, "expected {name,type,inherits,overrides}")
		return
	}
	if err := validPresetName(p.Name); err != nil {
		fail(w, 400, err.Error())
		return
	}
	if !validPresetType(p.Type) || strings.TrimSpace(p.Inherits) == "" {
		fail(w, 400, "type must be machine, process or filament; inherits is required")
		return
	}
	if _, err := overrides(string(p.Overrides)); err != nil {
		fail(w, 400, err.Error())
		return
	}
	if err := s.users.Put(&p); err != nil {
		fail(w, 500, err.Error())
		return
	}
	writeJSON(w, 200, p)
}
func (s *Server) deleteTypedUserPreset(w http.ResponseWriter, r *http.Request) {
	t, name := r.PathValue("type"), r.PathValue("name")
	if !validPresetType(t) {
		fail(w, 400, "unknown preset type")
		return
	}
	if err := validPresetName(name); err != nil {
		fail(w, 400, err.Error())
		return
	}
	err := s.users.DeleteTyped(t, name)
	if errors.Is(err, errPresetNotFound) {
		fail(w, 404, err.Error())
		return
	}
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	w.WriteHeader(204)
}
