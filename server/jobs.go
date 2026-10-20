package main

import (
	"bufio"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

type JobState string

const (
	StateQueued    JobState = "queued"
	StateRunning   JobState = "running"
	StateDone      JobState = "done"
	StateFailed    JobState = "failed"
	StateCancelled JobState = "canceled"
)

func (s JobState) Finished() bool { return s == StateDone || s == StateFailed || s == StateCancelled }

type JobRequest struct {
	SourcePlates    []projectPlate `json:"source_plates,omitempty"`
	PlateIndex      int            `json:"plate_index,omitempty"`
	Orient          bool           `json:"orient,omitempty"`
	Cut             string         `json:"cut,omitempty"`
	CutKeep         string         `json:"cut_keep,omitempty"`
	CutPlaceLower   bool           `json:"cut_place_lower,omitempty"`
	Vendor          string         `json:"vendor"`
	Printer         string         `json:"printer"`
	Process         string         `json:"process"`
	Filaments       []string       `json:"filaments"`
	Overrides       [][2]string    `json:"overrides"`
	Events          []string       `json:"events"`
	Files           []string       `json:"files"`
	Plate           bool           `json:"plate"`
	Output          string         `json:"output"`
	Filename        string         `json:"filename,omitempty"`
	ProjectSettings [][2]string    `json:"project_settings,omitempty"`
	ForcePlanar     bool           `json:"force_planar,omitempty"`
	Calib           string         `json:"calib,omitempty"`
	CalibParams     string         `json:"calib_params,omitempty"`
	Edits           bool           `json:"edits,omitempty"`
	Priority        string         `json:"priority,omitempty"`
	PreviewBudget   float64        `json:"preview_budget,omitempty"`
}

type Event struct {
	ID     int      `json:"id"`
	Type   string   `json:"type"`
	Stream string   `json:"stream,omitempty"`
	Line   string   `json:"line,omitempty"`
	State  JobState `json:"state,omitempty"`
	Error  string   `json:"error,omitempty"`
	Stage  string   `json:"stage,omitempty"`
	Pct    *float64 `json:"pct,omitempty"`
}

const maxEvents = 20000
const maxEventBytes = 8 << 20

type Job struct {
	ID      string
	Client  string
	Dir     string
	Request JobRequest
	Created time.Time

	mu         sync.Mutex
	state      JobState
	started    time.Time
	finished   time.Time
	errMsg     string
	exitCode   *int
	slice      json.RawMessage
	events     []Event
	dropped    int
	eventBytes int
	changed    chan struct{}
	cancel     context.CancelFunc
	stopReason string
	stage      string
	progress   *float64
	send       *Transfer
}

type JobView struct {
	ID          string          `json:"id"`
	Stage       string          `json:"stage,omitempty"`
	Progress    *float64        `json:"progress,omitempty"`
	State       JobState        `json:"state"`
	Created     time.Time       `json:"created"`
	Started     *time.Time      `json:"started,omitempty"`
	Finished    *time.Time      `json:"finished,omitempty"`
	Error       string          `json:"error,omitempty"`
	ExitCode    *int            `json:"exit_code,omitempty"`
	Request     JobRequest      `json:"request"`
	DownloadURL string          `json:"download_url,omitempty"`
	EventsURL   string          `json:"events_url"`
	Slice       json.RawMessage `json:"record,omitempty"`
	Filename    string          `json:"filename,omitempty"`
}

func (j *Job) View() JobView {
	j.mu.Lock()
	defer j.mu.Unlock()
	v := JobView{ID: j.ID, State: j.state, Created: j.Created, Error: j.errMsg, ExitCode: j.exitCode,
		Stage: j.stage, Progress: j.progress, Request: j.Request, EventsURL: "/api/jobs/" + j.ID + "/events", Slice: j.slice}
	if !j.started.IsZero() {
		t := j.started
		v.Started = &t
	}
	if !j.finished.IsZero() {
		t := j.finished
		v.Finished = &t
	}
	if j.state == StateDone && !j.Request.Orient && j.Request.Output != "patch-preview" {
		v.DownloadURL = "/api/jobs/" + j.ID + "/download"
		v.Filename = j.DownloadName()
	}
	return v
}

func (j *Job) State() JobState {
	j.mu.Lock()
	defer j.mu.Unlock()
	return j.state
}

func (j *Job) notifyLocked() {
	close(j.changed)
	j.changed = make(chan struct{})
}

func (j *Job) appendLocked(e Event) {
	if (e.Type == "log" || e.Type == "progress") && (len(j.events) >= maxEvents || len(e.Line) > maxEventBytes-j.eventBytes) {
		j.dropped++
		return
	}
	j.eventBytes += len(e.Line)
	e.ID = len(j.events)
	j.events = append(j.events, e)
	j.notifyLocked()
}

func (j *Job) log(stream, line string) {
	j.mu.Lock()
	defer j.mu.Unlock()
	e := Event{Type: "log", Stream: stream, Line: line}
	fields := strings.Fields(line)
	if (stream == "stdout" || stream == "server") && len(fields) == 3 && fields[0] == "progress" {
		if pct, err := strconv.ParseFloat(fields[2], 64); err == nil && pct >= 0 && pct <= 100 {
			e.Type = "progress"
			e.Stage = fields[1]
			e.Pct = &pct
			j.stage, j.progress = e.Stage, e.Pct
		}
	}
	j.appendLocked(e)
}

func (j *Job) setState(s JobState, errMsg string) {
	j.mu.Lock()
	defer j.mu.Unlock()
	if s.Finished() && j.stopReason != "" {
		s = StateCancelled
		errMsg = j.stopReason
	}
	j.state = s
	j.errMsg = errMsg
	switch {
	case s == StateRunning:
		j.started = time.Now()
	case s.Finished():
		j.finished = time.Now()
		if j.dropped > 0 {
			j.appendLocked(Event{Type: "log", Stream: "server", Line: fmt.Sprintf("%d further output lines not kept", j.dropped)})
		}
	}
	eventType := "state"
	if s.Finished() {
		eventType = "done"
	}
	j.appendLocked(Event{Type: eventType, State: s, Error: errMsg})
}

func (j *Job) EventsSince(from int) ([]Event, <-chan struct{}, bool) {
	j.mu.Lock()
	defer j.mu.Unlock()
	var out []Event
	if from < len(j.events) {
		out = append(out, j.events[from:]...)
	}
	return out, j.changed, j.state.Finished()
}

func (j *Job) OutputPath(kind string) string {
	return filepath.Join(j.Dir, "out", outputBase(j.Request)+"."+kind)
}

func (j *Job) PiecePath(side string) string {
	return filepath.Join(j.Dir, "out", outputBase(j.Request)+"."+side+".stl")
}

func (j *Job) MovesPath() string { return filepath.Join(j.Dir, "moves.bin") }

func (j *Job) DownloadName() string {
	name := j.Request.Filename
	if name == "" && len(j.Request.Files) > 0 {
		name = j.Request.Files[0]
	}
	for _, ext := range []string{".gcode.3mf", ".gcode", ".3mf", ".stl", ".obj"} {
		if strings.HasSuffix(strings.ToLower(name), ext) {
			name = name[:len(name)-len(ext)]
			break
		}
	}
	if name == "" {
		name = "plate_1"
	}
	return name + "." + j.Request.Output
}

func outputBase(r JobRequest) string {
	if r.Plate || len(r.Files) == 0 {
		return "plate_1"
	}
	name := r.Files[0]
	for _, ext := range []string{".3mf", ".stl", ".obj", ".step", ".stp", ".amf"} {
		if strings.HasSuffix(strings.ToLower(name), ext) {
			return name[:len(name)-len(ext)]
		}
	}
	return name
}

type JobManager struct {
	cfg           Config
	sem           chan struct{}
	wg            sync.WaitGroup
	mu            sync.Mutex
	jobs          map[string]*Job
	stop          chan struct{}
	reaped        chan struct{}
	closing       bool
	uploads       map[string]*Job
	buckets       map[string]tokenBucket
	durations     [8]uint64
	durationCount uint64
	durationSum   float64
	submitted     uint64
	completedJobs [3]uint64
	rejects       [200]uint64
	responseBytes uint64
	downloadBytes uint64
}

var jobIDPattern = regexp.MustCompile(`^[0-9a-f]{32}$`)

func NewJobManager(cfg Config) (*JobManager, error) {
	if err := os.MkdirAll(cfg.JobRoot, 0o755); err != nil {
		return nil, err
	}
	entries, err := os.ReadDir(cfg.JobRoot)
	if err != nil {
		return nil, err
	}
	for _, e := range entries {
		if e.IsDir() && jobIDPattern.MatchString(e.Name()) {
			if err := os.RemoveAll(filepath.Join(cfg.JobRoot, e.Name())); err != nil {
				return nil, fmt.Errorf("remove orphan job: %w", err)
			}
		}
	}
	m := &JobManager{cfg: cfg, sem: make(chan struct{}, cfg.MaxJobs), jobs: map[string]*Job{}, stop: make(chan struct{}), reaped: make(chan struct{}), uploads: map[string]*Job{}, buckets: map[string]tokenBucket{}}
	go m.reap()
	return m, nil
}

func (m *JobManager) NewJob(client string) (*Job, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if err := m.admitLocked(client); err != nil {
		return nil, err
	}

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
	j := &Job{ID: id, Client: client, Dir: dir, Created: time.Now(), state: StateQueued, changed: make(chan struct{})}
	m.uploads[id] = j
	return j, nil
}

func (m *JobManager) Start(j *Job) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.closing {
		return &admissionError{503, "service is shutting down", 1}
	}
	if err := m.quotaLocked(); err != nil {
		return err
	}
	delete(m.uploads, j.ID)
	ctx, cancel := context.WithCancel(context.Background())
	j.mu.Lock()
	j.cancel = cancel
	j.appendLocked(Event{Type: "state", State: StateQueued})
	j.mu.Unlock()
	m.jobs[j.ID] = j
	if m.cfg.Metrics {
		m.submitted++
	}
	m.wg.Add(1)
	go func() {
		defer m.wg.Done()
		defer m.completed(j)
		defer cancel()
		if !m.acquire(ctx, j.Request.Priority == "low") {
			j.setState(StateCancelled, "cancelled before it started")
			return
		}
		defer func() { <-m.sem }()
		m.run(ctx, j)
	}()
	return nil
}

func (m *JobManager) acquire(ctx context.Context, low bool) bool {
	if !low {
		select {
		case m.sem <- struct{}{}:
			return true
		case <-ctx.Done():
			return false
		}
	}
	t := time.NewTicker(250 * time.Millisecond)
	defer t.Stop()
	for {
		select {
		case m.sem <- struct{}{}:
			return true
		default:
		}
		select {
		case <-t.C:
		case <-ctx.Done():
			return false
		}
	}
}

func (m *JobManager) Get(id string) *Job {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.jobs[id]
}

func (m *JobManager) Cancel(j *Job) bool {
	j.mu.Lock()
	defer j.mu.Unlock()
	if j.state.Finished() {
		return false
	}
	j.stopReason = "cancelled"
	j.cancel()
	return true
}

func (m *JobManager) Remove(j *Job) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.removeLocked(j)
}

func (m *JobManager) removeLocked(j *Job) error {
	if err := os.RemoveAll(j.Dir); err != nil {
		return err
	}
	delete(m.jobs, j.ID)
	return nil
}

func (m *JobManager) Counts() (queued, running int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, j := range m.jobs {
		switch j.State() {
		case StateQueued:
			queued++
		case StateRunning:
			running++
		}
	}
	return
}

func (m *JobManager) Shutdown() {
	m.mu.Lock()
	m.closing = true
	close(m.stop)
	m.mu.Unlock()
	<-m.reaped
	m.mu.Lock()
	for _, j := range m.jobs {
		j.mu.Lock()
		if !j.state.Finished() && j.cancel != nil {
			if j.stopReason == "" {
				j.stopReason = "server shutting down"
			}
			j.cancel()
		}
		j.mu.Unlock()
	}
	m.mu.Unlock()
	m.wg.Wait()
}

func (m *JobManager) Cleanup() {
	m.mu.Lock()
	remaining := make([]*Job, 0, len(m.jobs))
	for _, j := range m.jobs {
		remaining = append(remaining, j)
	}
	m.mu.Unlock()
	for _, j := range remaining {
		if err := m.Remove(j); err != nil {
			log.Printf("remove job %s: %v", j.ID, err)
		}
	}
}

func (m *JobManager) reap() {
	defer close(m.reaped)
	interval := time.Minute
	if m.cfg.DiskQuota > 0 {
		interval = 5 * time.Second
	}
	t := time.NewTicker(interval)
	defer t.Stop()
	for {
		select {
		case <-m.stop:
			return
		case now := <-t.C:
			m.mu.Lock()
			if err := m.quotaLocked(); err != nil {
				log.Printf("job quota: %v", err)
			}
			for client, bucket := range m.buckets {
				if now.Sub(bucket.at).Seconds()*m.cfg.SubmitRate >= float64(m.cfg.SubmitBurst) {
					delete(m.buckets, client)
				}
			}
			m.mu.Unlock()
			var old []*Job
			m.mu.Lock()
			for _, j := range m.jobs {
				j.mu.Lock()
				if j.state.Finished() && now.Sub(j.finished) > m.cfg.JobTTL {
					old = append(old, j)
				}
				j.mu.Unlock()
			}
			m.mu.Unlock()
			for _, j := range old {
				if err := m.Remove(j); err != nil {
					log.Printf("expire job %s: %v", j.ID, err)
				}
			}
		}
	}
}

func (m *JobManager) toolArgs(j *Job) []string {
	r := j.Request
	in := filepath.Join(j.Dir, "input")
	args := []string{
		"--profiles", filepath.Join(m.cfg.Resources, "profiles"),
		"--datadir", filepath.Join(j.Dir, "datadir"),
		"--vendor", r.Vendor,
		"--printer", r.Printer,
		"--process", r.Process,
	}
	for _, f := range r.Filaments {
		args = append(args, "--filament", f)
	}
	for _, kv := range r.Overrides {
		args = append(args, "--set", kv[0]+"="+kv[1])
	}
	for _, e := range r.Events {
		args = append(args, "--event", e)
	}
	if r.Orient {
		return append(args, "--orient-only", filepath.Join(in, r.Files[0]))
	}
	if r.Cut != "" {
		args = append(args, "--cut-only", r.Cut, "--cut-keep", r.CutKeep, "-o", j.OutputPath("stl"))
		if r.CutPlaceLower {
			args = append(args, "--cut-place-lower")
		}
		return append(args, filepath.Join(in, r.Files[0]))
	}
	if r.Output == "patch-preview" {
		args = append(args, "--patch-preview", j.OutputPath("json"), "--patch-preview-budget", strconv.FormatFloat(r.PreviewBudget, 'f', -1, 64))
		if r.Plate {
			return append(args, "--plate", filepath.Join(in, "plate.json"))
		}
		return append(args, filepath.Join(in, r.Files[0]))
	}
	if m.cfg.Features.MovesOut {
		args = append(args, "--moves-out", j.MovesPath())
	}
	if m.cfg.Features.Progress {
		args = append(args, "--progress")
	}
	if r.Output == "3mf" {
		args = append(args, "--save-3mf", j.OutputPath("3mf"))
	} else {
		args = append(args, "--json", j.OutputPath("json"), "-o", j.OutputPath("gcode"))
		if m.cfg.Features.SurfaceGrid {
			args = append(args, "--surface-grid")
		}
	}
	if r.Output == "gcode.3mf" {
		args = append(args, "--gcode-3mf", j.OutputPath("gcode.3mf"))
	}
	if r.PlateIndex > 0 && r.Output != "3mf" {
		args = append(args, "--plate-index", fmt.Sprint(r.PlateIndex))
	}
	if r.ForcePlanar {
		args = append(args, "--force-planar")
	}
	if r.Edits {
		args = append(args, "--edit", filepath.Join(in, "edits.json"))
	}
	if r.Calib != "" {
		args = append(args, "--calib", r.Calib)
		if r.CalibParams != "" {
			args = append(args, "--calib-params", r.CalibParams)
		}
	} else if r.Plate {
		args = append(args, "--plate", filepath.Join(in, "plate.json"))
	} else {
		args = append(args, filepath.Join(in, r.Files[0]))
	}
	return args
}

func (m *JobManager) run(ctx context.Context, j *Job) {
	ctx, cancel := context.WithTimeout(ctx, m.cfg.JobTimeout)
	defer cancel()
	j.setState(StateRunning, "")
	args := m.toolArgs(j)
	if !m.cfg.Features.Progress {
		j.log("server", "progress slicing 0")
	}
	j.log("server", "nonplanar_slice_tool "+quoteArgs(args))
	if j.Request.Output == "patch-preview" && m.previewCached(j, args) {
		return
	}

	cmd, commandErr := lineBufferedCommand(ctx, m.cfg.Tool, args)
	if commandErr != nil {
		j.setState(StateFailed, sanitizeToolError(commandErr))
		return
	}
	cmd.Dir = j.Dir
	cmd.Env = append(os.Environ(), "TMPDIR="+filepath.Join(j.Dir, "tmp"))
	configureKill(cmd)
	cmd.WaitDelay = 5 * time.Second
	stdout, stdoutWriter := io.Pipe()
	stderr, stderrWriter := io.Pipe()
	defer stdout.Close()
	defer stderr.Close()
	defer stdoutWriter.Close()
	defer stderrWriter.Close()
	cmd.Stdout = stdoutWriter
	cmd.Stderr = stderrWriter
	if err := cmd.Start(); err != nil {
		switch {
		case errors.Is(ctx.Err(), context.Canceled):
			j.setState(StateCancelled, "cancelled before the slicer started")
		case errors.Is(ctx.Err(), context.DeadlineExceeded):
			j.setState(StateFailed, "slicing exceeded the time limit")
		default:
			j.setState(StateFailed, "cannot start the slicer: "+sanitizeToolError(err))
		}
		return
	}
	var rotation []float64
	var pieces []map[string]any
	var lastErr lastLines
	var readers sync.WaitGroup
	readers.Add(2)
	go func() {
		defer readers.Done()
		scanLines(stdout, func(l string) {
			j.log("stdout", l)
			if j.Request.Orient && strings.HasPrefix(l, "auto_orient_rotation ") {
				fields := strings.Fields(l)[1:]
				if len(fields) == 9 {
					values := make([]float64, 9)
					for i, field := range fields {
						value, err := strconv.ParseFloat(field, 64)
						if err != nil || math.IsNaN(value) || math.IsInf(value, 0) {
							return
						}
						values[i] = value
					}
					rotation = values
				}
			}
			if f := strings.Fields(l); j.Request.Cut != "" && len(f) == 8 && f[0] == "cut_piece" && f[2] == "triangles" && f[4] == "open_edges" && f[6] == "volume_mm3" {
				triangles, e1 := strconv.Atoi(f[3])
				open, e2 := strconv.Atoi(f[5])
				volume, e3 := strconv.ParseFloat(f[7], 64)
				if e1 == nil && e2 == nil && e3 == nil && (f[1] == "upper" || f[1] == "lower") && !math.IsNaN(volume) && !math.IsInf(volume, 0) {
					pieces = append(pieces, map[string]any{"side": f[1], "triangles": triangles, "open_edges": open, "volume_mm3": volume})
				}
			}
		})
	}()
	go func() {
		defer readers.Done()
		scanLines(stderr, func(l string) { lastErr.add(l); j.log("stderr", l) })
	}()
	waitErr := cmd.Wait()
	if cmd.Cancel != nil {
		cmd.Cancel()
	}
	stdoutWriter.Close()
	stderrWriter.Close()
	readers.Wait()

	code := cmd.ProcessState.ExitCode()
	j.mu.Lock()
	j.exitCode = &code
	j.mu.Unlock()
	raw, recordErr := os.ReadFile(j.OutputPath("json"))
	if j.Request.Orient {
		if len(rotation) == 9 {
			raw, recordErr = json.Marshal(map[string]any{"rotation": rotation})
		} else {
			recordErr = errors.New("missing native orientation rotation")
		}
	}
	if j.Request.Cut != "" {
		raw, recordErr = json.Marshal(map[string]any{"pieces": pieces})
		for _, p := range pieces {
			if _, err := os.Stat(j.PiecePath(p["side"].(string))); err != nil {
				recordErr = errors.New("missing cut piece " + p["side"].(string))
			}
		}
		if len(pieces) == 0 {
			recordErr = errors.New("missing cut pieces")
		}
	}
	if !j.Request.Orient && j.Request.Cut == "" && j.Request.Output == "3mf" {
		recordErr = nil
		raw = nil
	}
	if recordErr == nil && j.Request.Output != "3mf" && !json.Valid(raw) {
		recordErr = errors.New("invalid slice JSON")
	}
	if recordErr == nil {
		j.mu.Lock()
		j.slice = raw
		j.mu.Unlock()
	}
	switch {
	case errors.Is(ctx.Err(), context.Canceled):
		j.setState(StateCancelled, "cancelled")
	case errors.Is(ctx.Err(), context.DeadlineExceeded):
		j.setState(StateFailed, fmt.Sprintf("slicing exceeded the %s time limit", m.cfg.JobTimeout))
	case waitErr != nil:
		j.setState(StateFailed, lastErr.message(waitErr))
	case recordErr != nil:
		j.setState(StateFailed, "the slicer output could not be read: "+sanitizeToolError(recordErr))
	default:
		kind := j.Request.Output
		if kind == "patch-preview" {
			kind = "json"
			m.keepPreview(j, args, raw)
		}
		if _, err := os.Stat(j.OutputPath(kind)); !j.Request.Orient && j.Request.Cut == "" && err != nil {
			j.setState(StateFailed, "the slicer finished without writing "+filepath.Base(j.OutputPath(kind)))
			return
		}
		if !m.cfg.Features.Progress {
			j.log("server", "progress export 100")
		}
		j.setState(StateDone, "")
	}
	log.Printf("job %s %s (exit %d)", j.ID, j.State(), code)
}

func scanLines(r io.Reader, fn func(string)) {
	s := bufio.NewScanner(r)
	s.Buffer(make([]byte, 64<<10), 4<<20)
	for s.Scan() {
		fn(s.Text())
	}
	io.Copy(io.Discard, r)
}

type lastLines struct{ lines []string }

func (l *lastLines) add(s string) {
	if strings.TrimSpace(s) == "" {
		return
	}
	l.lines = append(l.lines, s)
	if len(l.lines) > 20 {
		l.lines = l.lines[1:]
	}
}

func (l *lastLines) message(waitErr error) string {
	for i := len(l.lines) - 1; i >= 0; i-- {
		if strings.HasPrefix(strings.ToLower(strings.TrimSpace(l.lines[i])), "error") {
			return sanitizeToolError(errors.New(strings.TrimSpace(l.lines[i])))
		}
	}
	if len(l.lines) > 0 {
		return sanitizeToolError(errors.New(strings.TrimSpace(l.lines[len(l.lines)-1])))
	}
	return "slicer " + waitErr.Error()
}

func quoteArgs(args []string) string {
	q := make([]string, len(args))
	for i, a := range args {
		if a == "" || strings.ContainsAny(a, " \t\"'\\$`") {
			q[i] = "'" + strings.ReplaceAll(a, "'", `'\''`) + "'"
		} else {
			q[i] = a
		}
	}
	return strings.Join(q, " ")
}

func sanitizeToolError(err error) string {
	for {
		next := errors.Unwrap(err)
		if next == nil {
			break
		}
		err = next
	}
	return toolErrorPath.ReplaceAllStringFunc(err.Error(), func(token string) string {
		parts := toolErrorPath.FindStringSubmatch(token)
		return parts[1] + filepath.Base(strings.ReplaceAll(parts[2], "\\", "/"))
	})
}

var toolErrorPath = regexp.MustCompile(`(^|[\s"'‘(])((?:[A-Za-z]:[\\/]|/)[^\s"'’<>:]+)`)
