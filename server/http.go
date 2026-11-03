package main

import (
	"cmp"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"math"
	"mime"
	"net"
	"net/http"
	"os"
	"os/exec"
	"path"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
	"unicode"
)

type Server struct {
	cfg       Config
	jobs      *JobManager
	presets   *PresetIndex
	library   *ProfileLibraryStore
	workspace *workspaceStore
	sends     *SendManager
	blobs     *blobStore
	fontsOnce sync.Once
	fonts     []byte
	fontsErr  error
}

func NewServer(c Config, jobs *JobManager, presets *PresetIndex, library *ProfileLibraryStore) http.Handler {
	s := &Server{cfg: c, jobs: jobs, presets: presets, library: library, sends: NewSendManager(c.Printers, c.PrinterStore)}
	desktopReadyNonce := os.Getenv("ORCA_DESKTOP_READY_NONCE")
	healthProbe := newEngineHealth(c)
	jobs.cfg.NativeHealth = healthProbe
	mux := http.NewServeMux()
	workspace := &workspaceStore{root: filepath.Join(c.DataDir, "workspace")}
	workspace.register(mux)
	s.workspace = workspace
	s.blobs = &blobStore{root: filepath.Join(c.DataDir, "blobs"), limit: blobCacheBytes}
	s.blobs.register(mux, c.MaxUploadSize)
	mux.HandleFunc("GET /api/healthz", func(w http.ResponseWriter, r *http.Request) {
		q, n := jobs.Counts()
		health := healthProbe.snapshot(c.Tool)
		if desktopReadyNonce != "" {
			health["desktop_ready_nonce"] = desktopReadyNonce
			w.Header().Set("Cache-Control", "no-store")
		}
		health["status"], health["queued"], health["running"] = "ok", q, n
		health["profile_library_schema"] = 1
		health["profile_recovery_schema"] = 1
		health["workspace_schema"] = 1
		health["server_revision"] = buildAbout(c).Revision
		health["max_concurrency"], health["max_queue"] = jobs.cfg.MaxJobs, jobs.cfg.MaxQueue
		if jobResultStats != nil {
			health["result_cache"] = jobResultStats(jobs)
		}
		writeJSON(w, 200, health)
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
	mux.HandleFunc("GET /api/jobs/{id}/transitions", s.jobTransitions)
	mux.HandleFunc("POST /api/jobs/{id}/cancel", s.cancelJob)
	mux.HandleFunc("POST /api/jobs/{id}/retry", s.retryJob)
	mux.HandleFunc("GET /api/jobs/{id}/download", s.download)
	mux.HandleFunc("GET /api/jobs/{id}/moves", s.moves)
	mux.HandleFunc("GET /api/jobs/{id}/exploded/{name}", s.exploded)
	mux.HandleFunc("POST /api/jobs/{id}/send", s.sendJob)
	mux.HandleFunc("GET /api/jobs/{id}/send", s.getSend)
	mux.HandleFunc("GET /api/printers", s.listPrinters)
	mux.HandleFunc("POST /api/printers", s.addPrinter)
	mux.HandleFunc("POST /api/printers/test", s.testPrinter)
	mux.HandleFunc("POST /api/printers/{name}/ams", s.amsPrinter)
	mux.HandleFunc("PUT /api/printers/{name}", s.updatePrinter)
	mux.HandleFunc("DELETE /api/printers/{name}", s.removePrinter)
	mux.HandleFunc("GET /api/presets", s.listPresets)
	options := c.Options
	if options == nil {
		options = cacheResponse(optionData)
	}
	mux.HandleFunc("GET /api/options", func(w http.ResponseWriter, r *http.Request) {
		options.serve(w, r, "options.json", "application/json", catalogCache, time.Time{})
	})
	mux.HandleFunc("GET /api/profile-library", s.getProfileLibrary)
	mux.HandleFunc("PUT /api/profile-library", s.putProfileLibrary)
	mux.HandleFunc("GET /api/profile-library/recovery", s.getProfileRecovery)
	mux.HandleFunc("POST /api/profile-library/recovery", s.postProfileRecovery)
	mux.HandleFunc("GET /api/handy-models/{name}", s.handyModel)
	mux.HandleFunc("GET /api/emboss-fonts", s.embossFonts)
	mux.HandleFunc("GET /api/user-heads", s.listUserHeads)
	mux.HandleFunc("POST /api/user-heads", s.saveUserHead)
	mux.HandleFunc("/api/", func(w http.ResponseWriter, r *http.Request) { fail(w, 404, "unknown API endpoint") })
	mux.Handle("/", spaFiles(c.StaticDir))
	if c.Metrics {
		mux.HandleFunc("GET /metrics", s.metrics)
	}
	return gzipJSON(s.middleware(revalidate(s.fallbackJobReads(mux))))
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
		if models := r.URL.Query()["model"]; len(models) > 0 {
			v, err = s.presets.modelsResponse(name, models)
		}
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
	key, err := submissionKey(r.URL.Path, r.Header.Get("Idempotency-Key"))
	if err != nil {
		fail(w, 400, err.Error())
		return
	}
	existing, finish, err := s.jobs.reserve(r.Context(), key)
	if err != nil {
		fail(w, 408, err.Error())
		return
	}
	if existing != nil {
		w.Header().Set("Location", "/api/jobs/"+existing.ID)
		writeJSON(w, 202, existing.View())
		return
	}
	var accepted *Job
	defer func() { finish(accepted) }()
	j, err := s.jobs.NewJob(s.clientIP(r))
	if err != nil {
		admissionFailure(w, err)
		return
	}
	j.IdempotencyKey = key
	started := false
	defer func() {
		if started {
			accepted = j
		}
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
	req := JobRequest{Orient: r.FormValue("operation") == "orient" || r.FormValue("operation") == "orient-scores", Vendor: r.FormValue("vendor"), Printer: r.FormValue("printer"), Process: r.FormValue("process"), Filaments: r.MultipartForm.Value["filament[]"], Output: r.FormValue("output"), Filename: r.FormValue("filename")}
	if raw := r.FormValue("profile_ids"); raw != "" {
		req.ProfileIDs = &jobProfileIDs{}
		if json.Unmarshal([]byte(raw), req.ProfileIDs) != nil {
			fail(w, 400, "profile_ids must contain the saved profile IDs and revisions")
			return
		}
	}
	switch op := r.FormValue("operation"); op {
	case "", "orient":
	case "orient-scores":
		if !s.cfg.Features.OrientScores {
			fail(w, 400, "orientation scores require a tool with --orient-scores")
			return
		}
		req.OrientScores = true
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
		if raw := r.FormValue("arrange_multi_materials"); raw != "" {
			if !s.cfg.Features.ArrangeMaterials || raw != "0" && raw != "1" {
				fail(w, 400, "arrange_multi_materials must be 0 or 1 and requires a tool with --arrange-multi-materials")
				return
			}
			req.ArrangeMaterials = raw
		}
	case "repair", "simplify":
		if op := r.FormValue("operation"); op == "repair" && !s.cfg.Features.MeshRepair || op == "simplify" && !s.cfg.Features.MeshSimplify {
			fail(w, 400, "Fix Model and Simplify Model require a tool with --repair-mesh and --simplify-mesh")
			return
		}
		req.MeshOp = r.FormValue("operation")
		if req.MeshOp == "simplify" {
			ratio, triangles, maxErr := r.FormValue("simplify_ratio"), r.FormValue("simplify_triangles"), r.FormValue("simplify_max_error")
			v, errR := strconv.ParseFloat(ratio, 64)
			n, errT := strconv.Atoi(triangles)
			e, errE := strconv.ParseFloat(maxErr, 64)
			switch {
			case maxErr != "" && ratio == "" && triangles == "" && s.cfg.Features.MeshSimplifyError && errE == nil && e > 0 && e <= 10:
				req.SimplifyMaxError = e
			case ratio != "" && triangles == "" && errR == nil && v > 0 && v <= 1:
				req.SimplifyRatio = v
			case triangles != "" && ratio == "" && errT == nil && n > 0:
				req.SimplifyTriangles = n
			default:
				fail(w, 400, "simplify needs simplify_ratio in (0, 1], a positive simplify_triangles, or simplify_max_error in (0, 10] with a tool that takes --max-error")
				return
			}
		}
	case "export":
		format := r.FormValue("export_format")
		if format == "drc" && !s.cfg.Features.ExportDRC || format == "generic3mf" && !s.cfg.Features.ExportGeneric3MF {
			fail(w, 400, "this export requires a tool with --export-drc / --export-3mf-generic")
			return
		}
		if format != "drc" && format != "generic3mf" {
			fail(w, 400, "export_format must be generic3mf or drc")
			return
		}
		req.MeshOp, req.ExportFormat = "export", format
		if raw := r.FormValue("draco_bits"); raw != "" {
			n, err := strconv.Atoi(raw)
			if err != nil || n < 0 || n > 30 || format != "drc" {
				fail(w, 400, "draco_bits must be 0 to 30, for drc only")
				return
			}
			req.DracoBits = n
		}
	case "boolean":
		op := r.FormValue("boolean_op")
		if !s.cfg.Features.MeshBoolean {
			fail(w, 400, "Mesh Boolean requires a tool with --mesh-boolean")
			return
		}
		if op != "union" && op != "difference" && op != "intersection" {
			fail(w, 400, "boolean_op must be union, difference or intersection")
			return
		}
		req.MeshOp, req.BooleanOp = "boolean", op
	case "svg":
		if !s.cfg.Features.EmbossSVG {
			fail(w, 400, "SVG requires a tool with --emboss-svg")
			return
		}
		args := []string{}
		for _, f := range []struct {
			key, flag string
			lo, hi    float64
		}{{"depth", "--depth", 0.01, 1000}, {"size", "--size", 0.1, 1000}} {
			raw := r.FormValue(f.key)
			if raw == "" && f.key == "size" {
				continue
			}
			v, err := strconv.ParseFloat(raw, 64)
			if err != nil || math.IsNaN(v) || v < f.lo || v > f.hi {
				fail(w, 400, fmt.Sprintf("%s must be %g to %g", f.key, f.lo, f.hi))
				return
			}
			args = append(args, f.flag, strconv.FormatFloat(v, 'f', -1, 64))
		}
		req.MeshOp, req.Text = "svg", args
		if r.FormValue("use_surface") == "1" {
			features := s.cfg.Features
			features.EmbossSurface = features.EmbossSVGSurface
			surface, err := surfaceArgs(r, features)
			if err != nil {
				fail(w, 400, err.Error())
				return
			}
			req.TextSurface = surface
		}
	case "text":
		if !s.cfg.Features.EmbossText {
			fail(w, 400, "Text requires a tool with --emboss-text")
			return
		}
		args, err := textArgs(r)
		if err != nil {
			fail(w, 400, err.Error())
			return
		}
		req.MeshOp, req.Text = "text", args
		if r.FormValue("use_surface") == "1" {
			if req.TextSurface, err = surfaceArgs(r, s.cfg.Features); err != nil {
				fail(w, 400, err.Error())
				return
			}
		}
	case "explode":
		if !s.cfg.Features.Explode {
			fail(w, 400, "opening a project as an editable plate requires a tool with --explode-3mf")
			return
		}
		req.Explode = true
	case "wipe-tower":
		if !s.cfg.Features.WipeTower {
			fail(w, 400, "the prime tower estimate requires a tool with --wipe-tower-only")
			return
		}
		req.WipeTower = true
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
		req.CutConnectors, req.CutDovetail = r.FormValue("cut_connectors"), r.FormValue("cut_dovetail")
		if msg := cutJoinError(req, s.cfg.Features.CutConnectors); msg != "" {
			fail(w, 400, msg)
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
	case "", "normal", "interactive":
	case "low", "batch":
		req.Priority = p
	default:
		fail(w, 400, "priority must be interactive/normal or batch/low")
		return
	}
	if r.FormValue("force_planar") != "" {
		if !s.cfg.Features.ForcePlanar {
			fail(w, 400, "force_planar requires a tool with --force-planar")
			return
		}
		req.ForcePlanar = true
	}
	if r.FormValue("floating") != "" {
		if !s.cfg.Features.Floating {
			fail(w, 400, "floating requires a tool with --floating")
			return
		}
		req.Floating = true
	}
	if r.FormValue("allow_mixed_temperatures") != "" {
		if !s.cfg.Features.MixedTemperatures {
			fail(w, 400, "allow_mixed_temperatures requires a tool with --allow-mixed-temperatures")
			return
		}
		req.AllowMixedTemperatures = true
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
	vendor, problem := s.resolveJobProfiles(&req)
	if problem != nil {
		problem.write(w)
		return
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
	var files []struct {
		Name   string `json:"name"`
		SHA256 string `json:"sha256"`
	}
	if raw := r.FormValue("files"); raw != "" && json.Unmarshal([]byte(raw), &files) != nil {
		fail(w, 400, "files must be an array of {name, sha256}")
		return
	}
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
	if req.MeshOp == "text" && len(req.TextSurface) == 0 {
		if len(files) > 0 || r.FormValue("plate") != "" || req.Output == "3mf" {
			fail(w, 400, "text takes no files, plate or project output")
			return
		}
		s.startJob(w, j, req, &started)
		return
	}
	if len(files) == 0 {
		fail(w, 400, "files is required")
		return
	}
	names := map[string]bool{}
	var missing []string
	for _, f := range files {
		name := f.Name
		ext := strings.ToLower(filepath.Ext(name))
		other := s.cfg.Features.Step && (ext == ".step" || ext == ".stp") || ext == ".drc" || req.MeshOp == "svg" && ext == ".svg"
		if name == "" || len(name) > 240 || strings.IndexFunc(name, unicode.IsControl) >= 0 || strings.ContainsAny(name, "/\\") || name != filepath.Base(name) || name == "." || name == ".." || (ext != ".stl" && ext != ".obj" && ext != ".3mf" && !other) || names[name] {
			fail(w, 400, "uploads need unique STL, OBJ, STEP, DRC, SVG or 3MF basenames")
			return
		}
		if !blobDigest.MatchString(f.SHA256) {
			fail(w, 400, "each file needs its lowercase hex sha256")
			return
		}
		names[name] = true
		req.Files = append(req.Files, name)
		if err := s.blobs.link(f.SHA256, filepath.Join(j.Dir, "input", name)); errors.Is(err, fs.ErrNotExist) {
			missing = append(missing, f.SHA256)
		} else if err != nil {
			fail(w, 500, err.Error())
			return
		}
	}
	if len(missing) > 0 {
		writeJSON(w, 409, map[string]any{"error": "Uploaded models expired on the service. Sending them again.", "code": "blob_missing", "missing": missing})
		return
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
				Type string `json:"type"`
			}
			if obj["volumes"] != nil && json.Unmarshal(obj["volumes"], &volumes) != nil ||
				(obj["file"] != nil || len(volumes) == 0) && (json.Unmarshal(obj["file"], &file) != nil || !names[file]) {
				fail(w, 400, "plate objects must reference uploaded basenames")
				return
			}
			if len(volumes) > 0 && !s.cfg.Features.Volumes {
				fail(w, 400, "parts and modifiers require a tool with plate-JSON volumes")
				return
			}
			for _, v := range volumes {
				if (v.Type == "seam" || strings.HasPrefix(v.Type, "precise_seam_")) && !s.cfg.Features.SeamParts {
					fail(w, 400, "precise seam parts require a tool with plate-JSON seam volumes")
					return
				}
			}
			if obj["paint"] != nil && !s.cfg.Features.Paint {
				fail(w, 400, "painting requires a tool with plate-JSON paint")
				return
			}
			if obj["layer_height_profile"] != nil && !s.cfg.Features.LayerProfile {
				fail(w, 400, "variable layer height requires a tool with plate-JSON layer height profiles")
				return
			}
			if obj["ranges"] != nil && !s.cfg.Features.Ranges {
				fail(w, 400, "height range modifiers require a tool with plate-JSON ranges")
				return
			}
			if obj["brim_ears"] != nil && !s.cfg.Features.BrimEars {
				fail(w, 400, "brim ears require a tool with plate-JSON brim_ears")
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
	} else if len(files) != 1 && req.MeshOp != "boolean" && req.MeshOp != "svg" {
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
	if req.Explode && (req.Plate || len(req.Files) != 1 || !strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") || req.PlateIndex > 0) {
		fail(w, 400, "explode requires one 3MF project")
		return
	}
	if !req.Plate && !req.Explode && strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf") {
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
	if req.MeshOp == "svg" {
		want := map[bool]int{true: 2, false: 1}[len(req.TextSurface) > 0]
		if req.Output != "gcode" || req.Plate || len(req.Files) != want || !strings.EqualFold(filepath.Ext(req.Files[0]), ".svg") || want == 2 && !allSTL(req.Files[1:]) {
			fail(w, 400, "svg takes the SVG, and with use_surface one STL target, without plate JSON or project output")
			return
		}
	} else if want := map[bool]int{true: 2, false: 1}[req.MeshOp == "boolean"]; req.MeshOp != "" && (req.Output != "gcode" || req.Plate || len(req.Files) != want || !allSTL(req.Files) && !(req.MeshOp == "export" && strings.EqualFold(filepath.Ext(req.Files[0]), ".3mf"))) {
		fail(w, 400, fmt.Sprintf("this mesh operation takes %d STL without plate JSON or project output", want))
		return
	}
	if req.Cut != "" && (req.Output != "gcode" || req.Plate || len(req.Files) != 1 || !strings.EqualFold(filepath.Ext(req.Files[0]), ".stl")) {
		fail(w, 400, "cut requires one STL without plate JSON or project output")
		return
	}
	if (req.Arrange || req.WipeTower) && (!req.Plate || req.Output != "gcode") {
		fail(w, 400, "arrange and the prime tower estimate require plate JSON")
		return
	}
	if req.Orient && (req.Output == "3mf" || req.Plate || len(req.Files) != 1 || !strings.EqualFold(filepath.Ext(req.Files[0]), ".stl")) {
		fail(w, 400, "orient requires one STL without plate JSON or project output")
		return
	}
	if !req.Orient && !req.Arrange && !req.WipeTower && !req.Explode && req.Cut == "" && req.MeshOp == "" && req.Output != "3mf" && req.Output != "patch-preview" {
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
	vendor, problem := s.resolveJobProfiles(&req)
	if problem != nil {
		problem.write(w)
		return
	}
	if problem := s.validateProfileBindings(&req); problem != nil {
		problem.write(w)
		return
	}
	if problem := s.validateJobMethods(j, req, vendor); problem != nil {
		writeJSON(w, 409, problem)
		return
	}
	if err := s.captureSubmission(j, &req, vendor); err != nil {
		fail(w, 500, "The submitted configuration could not be saved. Try again before slicing.")
		return
	}
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
	if s.jobs.Cancel(j) || !j.State().Finished() {
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
	if j.Request.MeshOp != "" {
		path, name = j.OutputPath(meshExt(j.Request)), outputBase(j.Request)+"."+meshExt(j.Request)
	}
	if j.Request.Cut != "" {
		side := r.URL.Query().Get("piece")
		if !validPiece(side) {
			fail(w, 400, "a cut job's download needs piece=upper, lower or dowel-N")
			return
		}
		path, name = j.PiecePath(side), outputBase(j.Request)+"."+side+".stl"
	}
	f, err := os.Open(path)
	if err != nil {
		if os.IsNotExist(err) {
			j.setState(StateStale, "completed output is unavailable; slice again")
			writeJSON(w, 410, map[string]string{"error": "This output is no longer available. Slice the project again.", "code": "job_output_expired", "recovery": "slice_again"})
		} else {
			fail(w, 503, "The output could not be opened. Try downloading again.")
		}
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
	if strings.HasSuffix(path, ".3mf") || r.Header.Get("Range") != "" {
		http.ServeContent(w, r, st.Name(), st.ModTime(), f)
		return
	}
	serveGzipped(w, r, path, st, ".gz", `W/"%x-%x"`, func(src io.Reader, dst io.Writer, _ func() error) error {
		_, err := io.Copy(dst, src)
		return err
	})
}

var explodedName = regexp.MustCompile(`^object-[0-9]+-volume-[0-9]+\.3mf$`)

func (s *Server) exploded(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	name := r.PathValue("name")
	if !j.Request.Explode || j.State() != StateDone || !explodedName.MatchString(name) {
		fail(w, 404, "no such exploded mesh")
		return
	}
	f, err := os.Open(filepath.Join(j.ExplodedDir(), name))
	if err != nil {
		fail(w, 404, "no such exploded mesh")
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Type", "model/3mf")
	serveStream(w, r, f, st, "private, max-age=300, must-revalidate")
}

func (s *Server) moves(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	path := j.MovesPath()
	if r.URL.Query().Get("extended") == "1" {
		if _, err := os.Stat(j.ExtendedMovesPath()); err == nil {
			path = j.ExtendedMovesPath()
		}
	}
	f, err := os.Open(path)
	if err != nil || j.State() != StateDone {
		if f != nil {
			f.Close()
		}
		fail(w, 404, "moves are not available")
		return
	}
	st, err := f.Stat()
	f.Close()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	serveMoves(w, r, path, st)
}

func (s *Server) events(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	from := 0
	if last := r.Header.Get("Last-Event-ID"); last != "" {
		n, err := strconv.Atoi(last)
		if err != nil || n < 0 {
			fail(w, 400, "invalid Last-Event-ID")
			return
		}
		j.mu.Lock()
		latest := j.nextEvent
		j.mu.Unlock()
		if n > latest {
			fail(w, 400, "Last-Event-ID is ahead of this job")
			return
		}
		from = n
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
			from = e.ID
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

func cutJoinError(req JobRequest, supported bool) string {
	if req.CutConnectors == "" && req.CutDovetail == "" {
		return ""
	}
	var connectors []map[string]any
	var dovetail map[string]any
	switch {
	case !supported:
		return "connectors and dovetails require a tool with --cut-connectors"
	case req.CutConnectors != "" && req.CutDovetail != "":
		return "a dovetail cut takes no separate connectors"
	case req.CutKeep != "both":
		return "connectors and dovetails need cut_keep both"
	case req.CutConnectors != "" && (json.Unmarshal([]byte(req.CutConnectors), &connectors) != nil || len(connectors) == 0 || len(connectors) > 256):
		return "cut_connectors must be a JSON array of 1 to 256 connectors"
	case req.CutDovetail != "" && json.Unmarshal([]byte(req.CutDovetail), &dovetail) != nil:
		return "cut_dovetail must be a JSON object"
	}
	return ""
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

var handyModels = map[string]bool{"OrcaCube_v2.drc": true, "OrcaPlug_v2.drc": true, "OrcaSliced.3mf": true, "OrcaBadge.3mf": true,
	"OrcaToleranceTest.drc": true, "3DBenchy.drc": true, "calicat.drc": true, "ksr_fdmtest_v4.drc": true, "Voron_Design_Cube_v7.drc": true,
	"Stanford_Bunny.drc": true, "Orca_stringhell.drc": true}

func allSTL(files []string) bool {
	for _, f := range files {
		if !strings.EqualFold(filepath.Ext(f), ".stl") {
			return false
		}
	}
	return true
}

var fontID = regexp.MustCompile(`^[A-Za-z0-9_ .-]{1,80}$`)

func textArgs(r *http.Request) ([]string, error) {
	text := r.FormValue("text")
	if strings.TrimSpace(text) == "" || len(text) > 4000 {
		return nil, errors.New("text must be 1 to 4000 bytes")
	}
	args := []string{"--text", text}
	if font := r.FormValue("font"); font != "" {
		if !fontID.MatchString(font) {
			return nil, errors.New("font must be a bundled font id")
		}
		args = append(args, "--font-name", font)
	}
	number := func(key string, lo, hi, def float64) (string, error) {
		raw := r.FormValue(key)
		if raw == "" {
			return strconv.FormatFloat(def, 'f', -1, 64), nil
		}
		v, err := strconv.ParseFloat(raw, 64)
		if err != nil || math.IsNaN(v) || v < lo || v > hi {
			return "", fmt.Errorf("%s must be %g to %g", key, lo, hi)
		}
		return strconv.FormatFloat(v, 'f', -1, 64), nil
	}
	for _, f := range []struct {
		key, flag   string
		lo, hi, def float64
	}{{"size", "--size", 0.1, 1000, 10}, {"depth", "--depth", 0.01, 1000, 1}, {"line_spacing", "--line-spacing", 0.1, 10, 1}, {"char_gap", "--char-gap", -100, 100, 0}} {
		v, err := number(f.key, f.lo, f.hi, f.def)
		if err != nil {
			return nil, err
		}
		args = append(args, f.flag, v)
	}
	for _, flag := range []string{"bold", "italic"} {
		if r.FormValue(flag) == "1" {
			args = append(args, "--"+flag)
		}
	}
	switch align := r.FormValue("align"); align {
	case "", "center", "left", "right":
		if align != "" {
			args = append(args, "--align", align)
		}
	default:
		return nil, errors.New("align must be left, center or right")
	}
	return args, nil
}

var vector3 = regexp.MustCompile(`^-?[0-9.]+(e-?[0-9]+)?,-?[0-9.]+(e-?[0-9]+)?,-?[0-9.]+(e-?[0-9]+)?$`)

func surfaceArgs(r *http.Request, f ToolFeatures) ([]string, error) {
	if !f.EmbossSurface {
		return nil, errors.New("Use surface requires a tool with --use-surface")
	}
	args := []string{}
	for _, k := range []string{"at", "normal", "up"} {
		v := r.FormValue(k)
		if v == "" && k == "up" {
			continue
		}
		if !vector3.MatchString(v) {
			return nil, fmt.Errorf("%s must be x,y,z", k)
		}
		args = append(args, "--"+k, v)
	}
	if v := r.FormValue("angle"); v != "" {
		a, err := strconv.ParseFloat(v, 64)
		if err != nil || math.IsNaN(a) || a < -360 || a > 360 {
			return nil, errors.New("angle must be -360 to 360")
		}
		args = append(args, "--angle", strconv.FormatFloat(a, 'f', -1, 64))
	}
	if r.FormValue("outside") == "1" {
		if !f.EmbossSurfaceOutside {
			return nil, errors.New("raised surface text requires a tool with --surface-outside")
		}
		args = append(args, "--surface-outside")
	}
	return args, nil
}

func (s *Server) embossFonts(w http.ResponseWriter, r *http.Request) {
	if !s.cfg.Features.EmbossText {
		fail(w, 404, "the tool has no text")
		return
	}
	s.fontsOnce.Do(func() {
		ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
		defer cancel()
		cmd := exec.CommandContext(ctx, s.cfg.Tool, "--emboss-fonts", "--profiles", filepath.Join(s.cfg.Resources, "profiles"))
		configureKill(cmd)
		cmd.WaitDelay = 5 * time.Second
		out, err := cmd.Output()
		var catalog struct {
			Fonts []struct {
				ID   string `json:"id"`
				Name string `json:"name"`
			} `json:"fonts"`
		}
		if err == nil {
			err = json.Unmarshal(out, &catalog)
		}
		if err != nil {
			s.fontsErr = fmt.Errorf("font catalog: %w", err)
			return
		}
		s.fonts, _ = json.Marshal(map[string]any{"fonts": catalog.Fonts})
	})
	if s.fonts == nil {
		fail(w, 500, fmt.Sprint(s.fontsErr))
		return
	}
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "public, max-age=3600")
	w.Write(s.fonts)
}

func (s *Server) handyModel(w http.ResponseWriter, r *http.Request) {
	name := r.PathValue("name")
	if !handyModels[name] {
		fail(w, 404, "unknown handy model")
		return
	}
	f, err := os.Open(filepath.Join(s.cfg.Resources, "handy_models", name))
	if err != nil {
		fail(w, 404, "handy model unavailable")
		return
	}
	defer f.Close()
	st, err := f.Stat()
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	w.Header().Set("Content-Type", "application/octet-stream")
	w.Header().Set("Cache-Control", "public, max-age=86400")
	http.ServeContent(w, r, name, st.ModTime(), f)
}

func (s *Server) jobTransitions(w http.ResponseWriter, r *http.Request) {
	if j := s.job(w, r); j != nil {
		writeJSON(w, 200, j.Transitions())
	}
}

func (s *Server) cancelJob(w http.ResponseWriter, r *http.Request) {
	if j := s.job(w, r); j != nil {
		s.jobs.Cancel(j)
		writeJSON(w, 200, j.View())
	}
}

func (s *Server) retryJob(w http.ResponseWriter, r *http.Request) {
	key, err := submissionKey(r.URL.Path, r.Header.Get("Idempotency-Key"))
	if err != nil {
		fail(w, 400, err.Error())
		return
	}
	existing, finish, err := s.jobs.reserve(r.Context(), key)
	if err != nil {
		fail(w, 408, err.Error())
		return
	}
	if existing != nil {
		w.Header().Set("Location", "/api/jobs/"+existing.ID)
		writeJSON(w, 202, existing.View())
		return
	}
	var accepted *Job
	defer func() { finish(accepted) }()
	parent := s.job(w, r)
	if parent == nil {
		return
	}
	j, err := s.jobs.Retry(parent, s.clientIP(r))
	if err != nil {
		admissionFailure(w, err)
		return
	}
	j.IdempotencyKey = key
	req := j.Request
	vendor, problem := s.resolveJobProfiles(&req)
	if problem != nil {
		s.jobs.releaseUpload(j)
		os.RemoveAll(j.Dir)
		problem.write(w)
		return
	}
	if problem := s.validateProfileBindings(&req); problem != nil {
		s.jobs.releaseUpload(j)
		os.RemoveAll(j.Dir)
		problem.write(w)
		return
	}
	if problem := s.validateJobMethods(j, req, vendor); problem != nil {
		s.jobs.releaseUpload(j)
		os.RemoveAll(j.Dir)
		writeJSON(w, 409, problem)
		return
	}
	if err := s.captureSubmission(j, &req, vendor); err != nil {
		s.jobs.releaseUpload(j)
		os.RemoveAll(j.Dir)
		fail(w, 500, "The retry configuration could not be saved. Submit the project again.")
		return
	}
	j.Request = req
	if err := s.jobs.Start(j); err != nil {
		s.jobs.releaseUpload(j)
		os.RemoveAll(j.Dir)
		admissionFailure(w, err)
		return
	}
	accepted = j
	w.Header().Set("Location", "/api/jobs/"+j.ID)
	writeJSON(w, 202, j.View())
}

type HealthState string

const (
	HealthUnknown     HealthState = "unknown"
	HealthHealthy     HealthState = "healthy"
	HealthDegraded    HealthState = "degraded"
	HealthQualified   HealthState = "qualified"
	HealthUnqualified HealthState = "unqualified"
)

var qualifiedNativeSources = map[string]bool{
	"7ab64368bbd9483b9d2fe46aafa27e8585426a0d": true,
	"1e6bad5db420789671ec1ed7728e59f872919ece": true,
}

var healthTransitions = map[HealthState]map[HealthState]bool{
	HealthUnknown:     {HealthHealthy: true, HealthDegraded: true, HealthQualified: true, HealthUnqualified: true},
	HealthHealthy:     {HealthDegraded: true, HealthQualified: true, HealthUnqualified: true},
	HealthDegraded:    {HealthHealthy: true, HealthQualified: true, HealthUnqualified: true},
	HealthQualified:   {HealthHealthy: true, HealthDegraded: true, HealthUnqualified: true},
	HealthUnqualified: {HealthHealthy: true, HealthDegraded: true, HealthQualified: true},
}

type engineHealth struct {
	mu            sync.Mutex
	state         HealthState
	artifactState HealthState
	reason        string
	manifest      map[string]any
	tool          os.FileInfo
	features      map[string]any
	outputs       map[string]bool
}

func (h *engineHealth) setState(next HealthState) {
	if h.state == next {
		return
	}
	if !healthTransitions[h.state][next] {
		log.Printf("illegal engine health transition %s -> %s", h.state, next)
		return
	}
	log.Printf("engine health %s -> %s", h.state, next)
	h.state = next
}

func bundleFileHash(name string) (string, error) {
	f, err := os.Open(name)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err = io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func bundleResourceHash(root string) (string, error) {
	h := sha256.New()
	err := filepath.WalkDir(root, func(name string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		if !d.Type().IsRegular() {
			return errors.New("nonregular resource")
		}
		digest, err := bundleFileHash(name)
		if err != nil {
			return err
		}
		relative, err := filepath.Rel(root, name)
		if err != nil {
			return err
		}
		fmt.Fprintf(h, "%s\x00%s\n", filepath.ToSlash(relative), digest)
		return nil
	})
	if err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func newEngineHealth(c Config) *engineHealth {
	h := &engineHealth{state: HealthUnknown, artifactState: HealthUnknown, features: map[string]any{}}
	b, _ := json.Marshal(c.Features)
	json.Unmarshal(b, &h.features)
	if c.Features.DumpOptions && c.Options != nil {
		var options map[string]struct {
			Values []string `json:"enum_values"`
		}
		if json.Unmarshal(c.Options.plain, &options) == nil {
			for _, v := range options["nonplanar_mode"].Values {
				if v == "curved_tops" {
					h.features["curved_tops"] = true
				}
			}
			for _, v := range options["nonplanar_wave"].Values {
				if v == "whole" {
					h.features["nonplanar_wave"] = true
				}
			}
		}
	}
	st, err := os.Stat(c.Tool)
	if err != nil || !st.Mode().IsRegular() {
		h.reason = "engine executable unavailable"
		h.setState(HealthDegraded)
		return h
	}
	h.tool = st
	h.setState(HealthHealthy)
	manifestPath := c.SurfaceManifest
	if manifestPath == "" {
		manifestPath = os.Getenv("ORCA_WEB_SURFACE_MANIFEST")
	}
	if manifestPath == "" {
		manifestPath = os.Getenv("ORCA_WEB_ARTIFACT_MANIFEST")
	}
	if manifestPath == "" {
		return h
	}
	h.artifactState = HealthUnqualified
	raw, err := os.ReadFile(manifestPath)
	var manifest struct {
		Backend        string          `json:"backend"`
		Source         string          `json:"source_sha"`
		Available      bool            `json:"available"`
		Preview        bool            `json:"preview_ready"`
		PrintQualified bool            `json:"print_qualified"`
		Binary         string          `json:"binary_sha256"`
		Resources      string          `json:"resources_sha256"`
		Curved         bool            `json:"validated_curved_benchy_clearance"`
		Wave           bool            `json:"validated_wave_export"`
		Features       map[string]bool `json:"features"`
	}
	if err != nil || json.Unmarshal(raw, &manifest) != nil {
		h.reason = "artifact manifest unreadable"
		h.setState(HealthDegraded)
		return h
	}
	binary, binaryErr := bundleFileHash(c.Tool)
	resources, resourceErr := bundleResourceHash(c.Resources)
	if binaryErr != nil || resourceErr != nil || manifest.Binary != binary || manifest.Resources != resources || !manifest.Available {
		h.reason = "artifact identity mismatch"
		h.setState(HealthDegraded)
		return h
	}
	var proof struct {
		Ready     bool   `json:"ready"`
		Source    string `json:"source_sha"`
		Binary    string `json:"binary_sha256"`
		Resources string `json:"resources_sha256"`
		Manifest  string `json:"manifest_sha256"`
	}
	proofPath := c.ArtifactQualification
	if proofPath == "" {
		proofPath = os.Getenv("ORCA_WEB_ARTIFACT_QUALIFICATION")
	}
	proofRaw, proofErr := os.ReadFile(proofPath)
	manifestHash := sha256.Sum256(raw)
	qualified := proofErr == nil && json.Unmarshal(proofRaw, &proof) == nil && proof.Ready &&
		qualifiedNativeSources[manifest.Source] && proof.Source == manifest.Source &&
		proof.Binary == binary && proof.Resources == resources && proof.Manifest == hex.EncodeToString(manifestHash[:]) && manifest.PrintQualified
	if qualified {
		h.outputs = nativeOutputHashes(raw)
		h.artifactState = HealthQualified
		h.setState(HealthQualified)
	} else {
		h.setState(HealthUnqualified)
	}
	wave := qualified && manifest.Preview && manifest.Wave && manifest.Features["nonplanar_wave"]
	modes := []string{}
	if wave {
		modes = append(modes, "whole")
	}
	h.manifest = map[string]any{"backend": manifest.Backend, "source_sha": manifest.Source,
		"validated": manifest.Preview, "preview_ready": manifest.Preview, "print_qualified": qualified,
		"binary_sha256": binary, "resources_sha256": resources, "wave_modes": modes,
		"features":            map[string]bool{"curved_tops": manifest.Preview && manifest.Curved, "nonplanar_wave": wave},
		"qualification_scope": "exact audited reference outputs"}
	return h
}

func (h *engineHealth) snapshot(tool string) map[string]any {
	h.mu.Lock()
	defer h.mu.Unlock()
	st, err := os.Stat(tool)
	if err != nil || h.tool == nil || !os.SameFile(st, h.tool) || st.Size() != h.tool.Size() || !st.ModTime().Equal(h.tool.ModTime()) {
		h.reason = "engine executable changed; restart to reprobe"
		h.artifactState = HealthUnqualified
		h.manifest = nil
		h.setState(HealthDegraded)
	}
	return map[string]any{"engine_state": h.state, "artifact_state": h.artifactState, "reason": h.reason,
		"artifact_manifest": h.manifest, "tool_features": h.features}
}
