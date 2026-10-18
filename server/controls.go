package main

import (
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"math"
	"net"
	"net/http"
	"net/netip"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
)

type admissionError struct {
	status  int
	message string
	retry   int
}

func (e *admissionError) Error() string { return e.message }
func admissionFailure(w http.ResponseWriter, err error) {
	var e *admissionError
	if errors.As(err, &e) {
		if e.retry > 0 {
			w.Header().Set("Retry-After", strconv.Itoa(e.retry))
		}
		fail(w, e.status, e.message)
	} else {
		fail(w, 500, err.Error())
	}
}

type tokenBucket struct {
	tokens float64
	at     time.Time
}

func (m *JobManager) admitLocked(client string) error {
	if m.closing {
		return &admissionError{503, "service is shutting down", 1}
	}
	if m.cfg.SubmitRate > 0 {
		now := time.Now()
		b, ok := m.buckets[client]
		if !ok {
			b = tokenBucket{float64(m.cfg.SubmitBurst), now}
		}
		b.tokens = math.Min(float64(m.cfg.SubmitBurst), b.tokens+now.Sub(b.at).Seconds()*m.cfg.SubmitRate)
		b.at = now
		if b.tokens < 1 {
			m.buckets[client] = b
			return &admissionError{429, "submission rate exceeded", int(math.Min(86400, math.Max(1, math.Ceil((1-b.tokens)/m.cfg.SubmitRate))))}
		}
		b.tokens--
		m.buckets[client] = b
	}
	if m.cfg.MaxClientJobs > 0 || m.cfg.MaxQueue >= 0 {
		active, own := len(m.uploads), 0
		for _, j := range m.uploads {
			if j.Client == client {
				own++
			}
		}
		for _, j := range m.jobs {
			if !j.State().Finished() {
				active++
				if j.Client == client {
					own++
				}
			}
		}
		if m.cfg.MaxClientJobs > 0 && own >= m.cfg.MaxClientJobs {
			return &admissionError{429, "client active-job limit reached", 1}
		}
		if m.cfg.MaxQueue >= 0 && active >= m.cfg.MaxJobs && active-m.cfg.MaxJobs >= m.cfg.MaxQueue {
			return &admissionError{503, "job queue is full", 1}
		}
	}
	return m.quotaLocked()
}

func (m *JobManager) releaseUpload(j *Job) {
	m.mu.Lock()
	delete(m.uploads, j.ID)
	m.mu.Unlock()
}

func directoryBytes(dir string) (int64, error) {
	var size int64
	err := filepath.WalkDir(dir, func(_ string, d fs.DirEntry, err error) error {
		if os.IsNotExist(err) {
			return nil
		}
		if err != nil {
			return err
		}
		if d.Type().IsRegular() {
			st, err := d.Info()
			if os.IsNotExist(err) {
				return nil
			}
			if err != nil {
				return err
			}
			size += st.Size()
		}
		return nil
	})
	return size, err
}

func (m *JobManager) quotaLocked() error {
	if m.cfg.DiskQuota == 0 {
		return nil
	}
	entries, err := os.ReadDir(m.cfg.JobRoot)
	if err != nil {
		return err
	}
	var total int64
	sizes := map[string]int64{}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		size, err := directoryBytes(filepath.Join(m.cfg.JobRoot, entry.Name()))
		if err != nil {
			return err
		}
		sizes[entry.Name()] = size
		total += size
	}
	var finished []*Job
	for _, j := range m.jobs {
		if j.State().Finished() {
			finished = append(finished, j)
		}
	}
	sort.Slice(finished, func(i, k int) bool {
		finished[i].mu.Lock()
		a := finished[i].finished
		finished[i].mu.Unlock()
		finished[k].mu.Lock()
		b := finished[k].finished
		finished[k].mu.Unlock()
		return a.Before(b)
	})
	for _, j := range finished {
		if total <= m.cfg.DiskQuota {
			break
		}
		if err := m.removeLocked(j); err != nil {
			return err
		}
		total -= sizes[j.ID]
	}
	if total > m.cfg.DiskQuota {
		return &admissionError{507, "job disk quota exceeded", 0}
	}
	return nil
}

var durationBounds = [...]float64{1, 5, 15, 30, 60, 300, 900, 3600}

func (m *JobManager) completed(j *Job) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.cfg.Metrics {
		j.mu.Lock()
		if !j.started.IsZero() {
			seconds := j.finished.Sub(j.started).Seconds()
			m.durationCount++
			m.durationSum += seconds
			for i, bound := range durationBounds {
				if seconds <= bound {
					m.durations[i]++
				}
			}
		}
		j.mu.Unlock()
	}
	if err := m.quotaLocked(); err != nil {
		log.Printf("job quota: %v", err)
	}
}

func (s *Server) metrics(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "text/plain; version=0.0.4; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	m := s.jobs
	m.mu.Lock()
	defer m.mu.Unlock()
	counts := map[JobState]int{}
	for _, j := range m.jobs {
		counts[j.State()]++
	}
	fmt.Fprintln(w, "# HELP orca_jobs Retained jobs by state.\n# TYPE orca_jobs gauge")
	for _, state := range []JobState{StateQueued, StateRunning, StateDone, StateFailed, StateCancelled} {
		fmt.Fprintf(w, "orca_jobs{state=%q} %d\n", state, counts[state])
	}
	fmt.Fprintln(w, "# HELP orca_slice_duration_seconds Running job duration, including failed and canceled slices.\n# TYPE orca_slice_duration_seconds histogram")
	for i, bound := range durationBounds {
		fmt.Fprintf(w, "orca_slice_duration_seconds_bucket{le=%q} %d\n", strconv.FormatFloat(bound, 'f', -1, 64), m.durations[i])
	}
	fmt.Fprintf(w, "orca_slice_duration_seconds_bucket{le=\"+Inf\"} %d\norca_slice_duration_seconds_sum %g\norca_slice_duration_seconds_count %d\n", m.durationCount, m.durationSum, m.durationCount)
	fmt.Fprintln(w, "# HELP orca_tool_feature Enabled native tool hooks.\n# TYPE orca_tool_feature gauge")
	b, _ := json.Marshal(s.cfg.Features)
	var features map[string]bool
	json.Unmarshal(b, &features)
	for _, name := range []string{"dump_options", "dump_presets", "progress", "moves_out", "object_settings"} {
		value := 0
		if features[name] {
			value = 1
		}
		fmt.Fprintf(w, "orca_tool_feature{feature=%q} %d\n", name, value)
	}
}

func (s *Server) trusted(ip netip.Addr) bool {
	for _, prefix := range s.cfg.TrustedProxies {
		if prefix.Contains(ip) {
			return true
		}
	}
	return false
}
func (s *Server) clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		host = r.RemoteAddr
	}
	peer, err := netip.ParseAddr(host)
	if err != nil {
		return host
	}
	peer = peer.Unmap()
	socketPeer := peer
	if !s.trusted(peer) {
		return peer.String()
	}
	chain := strings.Split(strings.Join(r.Header.Values("X-Forwarded-For"), ","), ",")
	for i := len(chain) - 1; i >= 0; i-- {
		ip, err := netip.ParseAddr(strings.TrimSpace(chain[i]))
		if err != nil {
			return socketPeer.String()
		}
		peer = ip.Unmap()
		if !s.trusted(peer) {
			break
		}
	}
	return peer.String()
}

type requestWriter struct {
	http.ResponseWriter
	status int
}

func (w *requestWriter) Unwrap() http.ResponseWriter { return w.ResponseWriter }
func (w *requestWriter) WriteHeader(code int) {
	if code >= 100 && code < 200 {
		w.ResponseWriter.WriteHeader(code)
		return
	}
	if w.status == 0 {
		w.status = code
		w.ResponseWriter.WriteHeader(code)
	}
}
func (w *requestWriter) Write(b []byte) (int, error) {
	if w.status == 0 {
		w.WriteHeader(200)
	}
	return w.ResponseWriter.Write(b)
}
func (w *requestWriter) Flush() { _ = w.FlushError() }
func (w *requestWriter) FlushError() error {
	if w.status == 0 {
		w.WriteHeader(200)
	}
	return http.NewResponseController(w.ResponseWriter).Flush()
}

func (s *Server) middleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if s.cfg.RequestLog {
			started := time.Now()
			recorder := &requestWriter{ResponseWriter: w}
			w = recorder
			defer func() {
				status := recorder.status
				if status == 0 {
					status = 200
				}
				id := r.PathValue("id")
				if id == "" && strings.HasPrefix(r.URL.Path, "/api/jobs/") {
					id, _, _ = strings.Cut(strings.TrimPrefix(r.URL.Path, "/api/jobs/"), "/")
				}
				if id == "" {
					id = strings.TrimPrefix(w.Header().Get("Location"), "/api/jobs/")
				}
				b, _ := json.Marshal(map[string]any{"method": r.Method, "path": r.URL.Path, "status": status, "duration_ms": float64(time.Since(started).Microseconds()) / 1000, "client": s.clientIP(r), "job_id": id})
				log.Writer().Write(append(b, '\n'))
			}()
		}
		protected := r.Method == "DELETE" || (r.Method == "POST" && r.URL.Path == "/api/jobs") || ((r.Method == "POST" || r.Method == "PUT" || r.Method == "PATCH") && (r.URL.Path == "/api/user-presets" || strings.HasPrefix(r.URL.Path, "/api/user-presets/")))
		if s.cfg.BearerToken != "" && protected {
			token := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
			if !strings.HasPrefix(r.Header.Get("Authorization"), "Bearer ") || subtle.ConstantTimeCompare([]byte(token), []byte(s.cfg.BearerToken)) != 1 {
				w.Header().Set("WWW-Authenticate", "Bearer")
				fail(w, 401, "bearer token required")
				return
			}
		}
		next.ServeHTTP(w, r)
	})
}
