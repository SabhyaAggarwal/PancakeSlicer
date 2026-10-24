package main

import (
	"cmp"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math"
	"mime"
	"net"
	"net/http"
	"os"
	"path"
	"path/filepath"
	"regexp"
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
	sends   *SendManager
}

func NewServer(c Config, jobs *JobManager, presets *PresetIndex, users *UserPresetStore) http.Handler {
	s := &Server{c, jobs, presets, users, NewSendManager(c.Printers)}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /api/healthz", func(w http.ResponseWriter, r *http.Request) {
		q, n := jobs.Counts()
		writeJSON(w, 200, map[string]any{"status": "ok", "queued": q, "running": n, "tool_features": c.Features})
	})
	mux.HandleFunc("GET /api/source", s.source)
	mux.HandleFunc("GET /api/about", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, 200, buildAbout(c))
	})
	mux.HandleFunc("POST /api/jobs", s.createJob)
	mux.HandleFunc("POST /api/patch-preview", s.createJob)
	mux.HandleFunc("GET /api/jobs/{id}", s.getJob)
	mux.HandleFunc("DELETE /api/jobs/{id}", s.deleteJob)
	mux.HandleFunc("GET /api/jobs/{id}/events", s.events)
	mux.HandleFunc("GET /api/jobs/{id}/download", s.download)
	mux.HandleFunc("GET /api/jobs/{id}/moves", s.moves)
	mux.HandleFunc("POST /api/jobs/{id}/send", s.sendJob)
	mux.HandleFunc("GET /api/jobs/{id}/send", s.getSend)
	mux.HandleFunc("GET /api/printers", s.listPrinters)
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
	mux.HandleFunc("GET /api/user-heads", s.listUserHeads)
	mux.HandleFunc("POST /api/user-heads", s.saveUserHead)
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

func presetDisplayName(name string) string {
	return path.Base(strings.ReplaceAll(name, "\\", "/"))
}

const uploadIdleTimeout = 10 * time.Second
const uploadProgressBytes = 64 << 10

type uploadBody struct {
	io.ReadCloser
	controller   *http.ResponseController
	rate         int64
	pending      int
	rateDeadline time.Time
}

func (b *uploadBody) Read(p []byte) (int, error) {
	n, err := b.ReadCloser.Read(p)
	b.pending += n
	if b.pending >= uploadProgressBytes {
		b.rateDeadline = b.rateDeadline.Add(time.Duration(float64(b.pending) / float64(b.rate) * float64(time.Second)))
		b.pending = 0
		deadline := time.Now().Add(uploadIdleTimeout)
		if b.rateDeadline.Before(deadline) {
			deadline = b.rateDeadline
		}
		if deadlineErr := b.controller.SetReadDeadline(deadline); deadlineErr != nil && err == nil {
			err = deadlineErr
		}
	}
	return n, err
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
	controller := http.NewResponseController(w)
	deadline := time.Now().Add(uploadIdleTimeout)
	if err := controller.SetReadDeadline(deadline); err != nil {
		fail(w, 500, "upload read deadline unavailable")
		return
	}
	defer controller.SetReadDeadline(time.Time{})
	r.Body = &uploadBody{ReadCloser: http.MaxBytesReader(w, r.Body, s.cfg.MaxUploadSize),
		controller: controller, rate: s.cfg.UploadMinRate, rateDeadline: deadline}
	parseErr := r.ParseMultipartForm(8 << 20)
	if r.MultipartForm != nil {
		defer r.MultipartForm.RemoveAll()
	}
	if err := parseErr; err != nil {
		var tooLarge *http.MaxBytesError
		var timeout net.Error
		status := 400
		if errors.As(err, &tooLarge) {
			status = 413
		}
		if errors.As(err, &timeout) && timeout.Timeout() {
			status = http.StatusRequestTimeout
			w.Header().Set("Connection", "close")
			fail(w, status, "multipart upload read deadline exceeded")
			return
		}
		fail(w, status, "invalid multipart upload: "+err.Error())
		return
	}
	req := JobRequest{Orient: r.FormValue("operation") == "orient", Vendor: r.FormValue("vendor"), Printer: r.FormValue("printer"), Process: r.FormValue("process"), Filaments: r.MultipartForm.Value["filament[]"], Output: r.FormValue("output"), Filename: r.FormValue("filename")}
	switch op := r.FormValue("operation"); op {
	case "", "orient":
	case "arrange":
		if !s.cfg.Features.Arrange {
			fail(w, 400, "arrange requires a tool with --arrange-only")
			return
		}
		req.Arrange, req.ArrangeNoRotation = true, r.FormValue("arrange_rotation") == "0"
		if raw := r.FormValue("arrange_spacing"); raw != "" {
			v, err := strconv.ParseFloat(raw, 64)
			if err != nil || v < 0 || v > 100 || math.IsNaN(v) {
				fail(w, 400, "arrange_spacing must be 0 to 100 mm")
				return
			}
			req.ArrangeSpacing = v
		}
	case "cut":
		if !s.cfg.Features.Cut {
			fail(w, 400, "cutting requires a tool with --cut-only")
			return
		}
		req.Cut, req.CutKeep, req.CutPlaceLower = r.FormValue("cut"), r.FormValue("cut_keep"), r.FormValue("cut_place_lower") == "1"
		if req.CutKeep == "" {
			req.CutKeep = "both"
		}
		if !validCutPlane(req.Cut) || (req.CutKeep != "upper" && req.CutKeep != "lower" && req.CutKeep != "both") {
			fail(w, 400, "cut needs a plane Z or X,Y,Z,NX,NY,NZ with a nonzero normal and cut_keep upper, lower or both")
			return
		}
	default:
		fail(w, 400, "unknown operation")
		return
	}
	if strings.ContainsAny(req.Filename, "/\\\r\n") || len(req.Filename) > 240 || strings.IndexFunc(req.Filename, unicode.IsControl) >= 0 || req.Filename == "." || req.Filename == ".." {
		fail(w, 400, "filename must be a basename of at most 240 bytes")
		return
	}
	if req.Output == "" {
		req.Output = "gcode"
	}
	switch p := r.FormValue("priority"); p {
	case "", "normal":
	case "low":
		req.Priority = p
	default:
		fail(w, 400, "priority must be normal or low")
		return
	}
	if r.FormValue("force_planar") != "" {
		if !s.cfg.Features.ForcePlanar {
			fail(w, 400, "force_planar requires a tool with --force-planar")
			return
		}
		req.ForcePlanar = true
	}
	if r.URL.Path == "/api/patch-preview" {
		if !s.cfg.Features.PatchPreview {
			fail(w, 501, "this slicer has no patch preview")
			return
		}
		budget, err := strconv.ParseFloat(cmp.Or(r.FormValue("budget"), "2"), 64)
		if err != nil || !(budget >= 0.1 && budget <= 60) {
			fail(w, 400, "budget must be 0.1 to 60 seconds")
			return
		}
		req.Output, req.Priority, req.PreviewBudget = "patch-preview", "low", budget
	} else if req.Output != "gcode" && req.Output != "gcode.3mf" && req.Output != "3mf" {
		fail(w, 400, "output must be gcode, gcode.3mf or 3mf")
		return
	}
	if req.Vendor == "" {
		req.Vendor = s.presets.VendorOfPrinter(req.Printer)
	}
	if _, err := s.presets.names(); err != nil {
		fail(w, 500, "preset catalog unavailable")
		return
	}
	if s.presets.known[req.Vendor] == nil {
		fail(w, 400, "unknown vendor")
		return
	}
	for _, selection := range []struct{ kind, label, name string }{{"machine", "printer", req.Printer}, {"process", "process", req.Process}} {
		if !s.presets.knownPreset(req.Vendor, selection.kind, selection.name) {
			fail(w, 400, "unknown "+selection.label+" preset: "+presetDisplayName(selection.name))
			return
		}
	}
	for _, name := range req.Filaments {
		if !s.presets.knownPreset(req.Vendor, "filament", name) {
			fail(w, 400, "unknown filament preset: "+presetDisplayName(name))
			return
		}
	}
	vendor, ok := s.presets.Vendor(req.Vendor)
	if !ok {
		fail(w, 400, "unknown vendor")
		return
	}
	if vendor.machines[req.Printer] == nil {
		fail(w, 400, "unknown printer preset: "+presetDisplayName(req.Printer))
		return
	}
	if vendor.processes[req.Process] == nil {
		fail(w, 400, "unknown process preset: "+presetDisplayName(req.Process))
		return
	}
	if len(req.Filaments) == 0 {
		fail(w, 400, "at least one filament[] is required")
		return
	}
	for _, f := range req.Filaments {
		if vendor.filaments[f] == nil {
			fail(w, 400, "unknown filament preset: "+presetDisplayName(f))
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
	if len(req.Events) > 0 {
		rawHeight := vendor.machines[req.Printer].Config["printable_height"]
		heightText := stringValue(rawHeight)
		if heightText == "" {
			heightText = string(rawHeight)
		}
		maxHeight, _ := strconv.ParseFloat(heightText, 64)
		for _, event := range req.Events {
			text, _, hasType := strings.Cut(event, ":")
			height, err := strconv.ParseFloat(text, 64)
			if err != nil || !hasType || math.IsNaN(height) || math.IsInf(height, 0) || height <= 0 || !(height < maxHeight) {
				fail(w, 400, "event height must be finite, positive and below printable_height")
				return
			}
		}
	}
	files := r.MultipartForm.File["files[]"]
	if kind := r.FormValue("calib"); kind != "" {
		params := r.FormValue("calib_params")
		switch {
		case !s.cfg.Features.Calib:
			fail(w, 400, "calibrations require a tool with --calib")
		case !calibKinds[kind]:
			fail(w, 400, "unknown calibration")
		case !calibParams.MatchString(params):
			fail(w, 400, "calib_params must be key=value pairs separated by commas")
		case !calibHead.MatchString(r.FormValue("calib_head")):
			fail(w, 400, "calib_head must be a head library id")
		case len(files) > 0 || r.FormValue("plate") != "" || req.Output == "3mf" || req.Orient:
			fail(w, 400, "a calibration takes no files, plate or project output")
		default:
			req.Calib, req.CalibParams, req.CalibHead = kind, params, r.FormValue("calib_head")
			s.startJob(w, j, req, &started)
		}
		return
	}
	if len(files) == 0 {
		fail(w, 400, "files[] is required")
		return
	}
	names := map[string]bool{}
	for _, f := range files {
		_, parameters, headerErr := mime.ParseMediaType(f.Header.Get("Content-Disposition"))
		name := parameters["filename"]
		ext := strings.ToLower(filepath.Ext(name))
		step := s.cfg.Features.Step && (ext == ".step" || ext == ".stp")
		if headerErr != nil || name == "" || len(name) > 240 || strings.IndexFunc(name, unicode.IsControl) >= 0 || strings.ContainsAny(name, "/\\") || name != filepath.Base(name) || (ext != ".stl" && ext != ".obj" && ext != ".3mf" && !step) || names[name] {
			fail(w, 400, "uploads need unique STL, OBJ, STEP or 3MF basenames")
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
	rawPlate := r.FormValue("plate")
	if raw := rawPlate; raw != "" {
		var plate map[string]json.RawMessage
		var objects []map[string]json.RawMessage
		if json.Unmarshal([]byte(raw), &plate) != nil || json.Unmarshal(plate["objects"], &objects) != nil || len(objects) == 0 {
			fail(w, 400, "plate needs a non-empty objects array")
			return
		}
		for _, obj := range objects {
			var file string
			var volumes []struct {
				File string `json:"file"`
			}
			if json.Unmarshal(obj["file"], &file) != nil || !names[file] || (obj["volumes"] != nil && json.Unmarshal(obj["volumes"], &volumes) != nil) {
				fail(w, 400, "plate objects must reference uploaded basenames")
				return
			}
			if len(volumes) > 0 && !s.cfg.Features.Volumes {
				fail(w, 400, "parts and modifiers require a tool with plate-JSON volumes")
				return
			}
			if obj["paint"] != nil && !s.cfg.Features.Paint {
				fail(w, 400, "painting requires a tool with plate-JSON paint")
				return
			}
			if obj["layer_height_profile"] != nil && !s.cfg.Features.LayerProfile {
				fail(w, 400, "variable layer height requires a tool with plate-JSON layer height profiles")
				return
			}
			for _, v := range volumes {
				if !names[v.File] {
					fail(w, 400, "plate volumes must reference uploaded basenames")
					return
				}
			}
		}
		if err := validatePlatePlacement([]byte(raw), req.Output == "3mf", s.cfg.Features.PlatePlacement, s.cfg.Features.MeshPlates); err != nil {
			fail(w, 400, err.Error())
			return
		}
		if err := os.WriteFile(filepath.Join(j.Dir, "input", "plate.json"), []byte(raw), 0600); err != nil {
			fail(w, 500, err.Error())
			return
		}
		req.Plate = true
	} else if raw := r.FormValue("project_edits"); raw != "" {
		if !s.cfg.Features.Edit {
			fail(w, 400, "editing an opened project requires a tool with --edit")
			return
		}
		if err := writeProjectEdits(filepath.Join(j.Dir, "input"), req.Files, names, raw, s.cfg.Features.ProjectVolumes, s.cfg.Features.ProjectPaint); err != nil {
			fail(w, 400, err.Error())
			return
		}
		req.Edits = true
	} else if len(files) != 1 {
		fail(w, 400, "multiple uploads require plate JSON")
		return
	}
	if raw := r.FormValue("plate_index"); raw != "" {
		req.PlateIndex, err = strconv.Atoi(raw)
		if err != nil || req.PlateIndex < 1 || req.Plate || req.Orient || len(req.Files) != 1 && !req.Edits || !strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
			fail(w, 400, "plate_index requires a positive 1-based index and one 3MF project")
			return
		}
		if req.Output != "3mf" && !s.cfg.Features.PlateIndex {
			fail(w, 400, "slicing a selected project plate requires an upgraded ToolRecord tool with --plate-index support")
			return
		}
	}
	if !req.Plate && strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
		layout, layoutErr := readProjectLayout(filepath.Join(j.Dir, "input", req.Files[0]))
		if layoutErr != nil {
			fail(w, 400, layoutErr.Error())
			return
		}
		if req.PlateIndex > 0 && req.Output != "3mf" {
			if layout.Width <= 0 || layout.Depth <= 0 {
				fail(w, 400, "selected project has no printable area")
				return
			}
			for _, plate := range layout.Membership {
				if plate == 0 {
					fail(w, 400, "cannot resolve saved plate membership for every build item")
					return
				}
			}
		}
		req.SourcePlates = layout.Plates
		if req.PlateIndex > 0 && !layout.hasPlate(req.PlateIndex) {
			fail(w, 400, "plate_index does not name a saved project plate")
			return
		}
		if req.Output != "3mf" && len(layout.Plates) > 1 && req.PlateIndex == 0 {
			fail(w, 400, "multi-plate project: select a plate using plate_index; slicing requires a ToolRecord tool with --plate-index support")
			return
		}
	}
	if req.Output == "3mf" && !req.Plate && strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") && !s.cfg.Features.PlatePlacement {
		fail(w, 400, "saving loaded projects requires the R238 tool (native placement and identify IDs)")
		return
	}
	if raw := r.FormValue("project_instances"); raw != "" {
		if req.Plate || len(req.Files) != 1 && !req.Edits || !strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
			fail(w, 400, "project_instances requires one 3MF project")
			return
		}
		var instances []projectInstance
		if json.Unmarshal([]byte(raw), &instances) != nil || len(instances) == 0 {
			fail(w, 400, "project_instances needs a non-empty array")
			return
		}
		if err := projectTransformsWorkaround(filepath.Join(j.Dir, "input", req.Files[0]), instances); err != nil {
			fail(w, 400, err.Error())
			return
		}
	}
	if raw := r.FormValue("project_settings"); raw != "" {
		if req.Plate || len(req.Files) != 1 && !req.Edits || !strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
			fail(w, 400, "project_settings requires one 3MF project")
			return
		}
		settings, err := overrides(raw)
		if err != nil {
			fail(w, 400, "project_settings: "+err.Error())
			return
		}
		if err := patchProjectSettings(filepath.Join(j.Dir, "input", req.Files[0]), settings); err != nil {
			fail(w, 400, err.Error())
			return
		}
		req.ProjectSettings = settings
	}
	if !req.Plate && strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
		machine := vendor.machines[req.Printer]
		selected := make([][2]string, 0, len(machine.Config)+len(req.Overrides))
		for key, raw := range machine.Config {
			if configKinds[key] == "" || hiddenSetting.MatchString(key) {
				continue
			}
			value, err := configValue(key, raw)
			if err != nil {
				fail(w, 500, "invalid system printer setting: "+key)
				return
			}
			selected = append(selected, [2]string{key, value})
		}
		req.Overrides = append(selected, req.Overrides...)
	}
	if req.Cut != "" && (req.Output != "gcode" || req.Plate || len(req.Files) != 1 || !strings.EqualFold(filepath.Ext(req.Files[0]), ".stl")) {
		fail(w, 400, "cut requires one STL without plate JSON or project output")
		return
	}
	if req.Arrange && (!req.Plate || req.Output != "gcode") {
		fail(w, 400, "arrange requires plate JSON")
		return
	}
	if req.Orient && (req.Output == "3mf" || req.Plate || len(req.Files) != 1 || !strings.EqualFold(filepath.Ext(req.Files[0]), ".stl")) {
		fail(w, 400, "orient requires one STL without plate JSON or project output")
		return
	}
	if !req.Orient && !req.Arrange && req.Cut == "" && req.Output != "3mf" && req.Output != "patch-preview" {
		if err := validatePlateBounds(filepath.Join(j.Dir, "input"), req, vendor.machines[req.Printer], rawPlate); err != nil {
			fail(w, 400, err.Error())
			return
		}
	}
	s.startJob(w, j, req, &started)
}

var calibKinds = map[string]bool{"cornering": true, "flow-rate": true, "input-shaping-freq": true, "input-shaping-damp": true,
	"max-volumetric-speed": true, "pa-line": true, "pa-pattern": true, "pa-tower": true, "retraction": true, "temp-tower": true, "vfa": true,
	"nonplanar-clearance": true, "nonplanar-tilt-flow": true, "nonplanar-curved-top": true, "nonplanar-verifier": true}
var calibHead = regexp.MustCompile(`^[a-z0-9_-]{0,64}$`)
var calibParams = regexp.MustCompile(`^([a-z_]+=[A-Za-z0-9_.;-]*(,[a-z_]+=[A-Za-z0-9_.;-]*)*)?$`)

func (s *Server) startJob(w http.ResponseWriter, j *Job, req JobRequest, started *bool) {
	j.Request = req
	if err := s.jobs.Start(j); err != nil {
		admissionFailure(w, err)
		return
	}
	*started = true
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
	path, name := j.OutputPath(j.Request.Output), j.DownloadName()
	if j.Request.Cut != "" {
		side := r.URL.Query().Get("piece")
		if side != "upper" && side != "lower" {
			fail(w, 400, "a cut job's download needs piece=upper or piece=lower")
			return
		}
		path, name = j.PiecePath(side), outputBase(j.Request)+"."+side+".stl"
	}
	f, err := os.Open(path)
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
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{"filename": name}))
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

func validCutPlane(plane string) bool {
	fields := strings.Split(plane, ",")
	if len(fields) != 1 && len(fields) != 6 {
		return false
	}
	v := make([]float64, len(fields))
	for i, f := range fields {
		x, err := strconv.ParseFloat(strings.TrimSpace(f), 64)
		if err != nil || math.IsNaN(x) || math.IsInf(x, 0) {
			return false
		}
		v[i] = x
	}
	return len(v) == 1 || math.Hypot(math.Hypot(v[3], v[4]), v[5]) > 1e-9
}
