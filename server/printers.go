package main

import (
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"math"
	"net"
	"net/http"
	"net/url"
	"os"
	"regexp"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unicode"
)

type PrinterConfig struct {
	Name           string   `json:"name"`
	Kind           string   `json:"kind"`
	URL            string   `json:"url,omitempty"`
	Host           string   `json:"host,omitempty"`
	Serial         string   `json:"serial,omitempty"`
	Auth           string   `json:"auth,omitempty"`
	Username       string   `json:"username,omitempty"`
	Storage        string   `json:"storage,omitempty"`
	CredentialFile string   `json:"credential_file,omitempty"`
	TLS            string   `json:"tls,omitempty"`
	Fingerprint    string   `json:"fingerprint,omitempty"`
	Presets        []string `json:"presets,omitempty"`

	fingerprint []byte
	bambuRoots  *x509.CertPool
	editable    bool
	hosts       *hostPolicy
	model       string
	trusted     func(mode string, fp []byte)
}

type PrinterSummary struct {
	Name     string `json:"name"`
	Kind     string `json:"kind"`
	Editable bool   `json:"editable,omitempty"`
	Host     string `json:"host,omitempty"`
	Serial   string `json:"serial,omitempty"`
	Model    string `json:"model,omitempty"`
	URL      string `json:"url,omitempty"`
	Auth     string `json:"auth,omitempty"`
	Username string `json:"username,omitempty"`
}

func (p *PrinterConfig) summary() PrinterSummary {
	if !p.editable {
		return PrinterSummary{Name: p.Name, Kind: p.Kind}
	}
	if p.Kind != "bambu-lan" {
		return PrinterSummary{Name: p.Name, Kind: p.Kind, Editable: true, URL: p.URL, Auth: p.Auth, Username: p.Username}
	}
	return PrinterSummary{Name: p.Name, Kind: p.Kind, Editable: true, Host: p.Host, Serial: p.Serial, Model: p.model}
}

var bambuSerial = regexp.MustCompile(`^[A-Za-z0-9]{6,32}$`)

func loadPrinters(path, resources string) ([]*PrinterConfig, error) {
	if path == "" {
		return nil, nil
	}
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var file struct {
		Printers []*PrinterConfig `json:"printers"`
	}
	decoder := json.NewDecoder(strings.NewReader(string(b)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&file); err != nil {
		return nil, fmt.Errorf("printers file: %w", err)
	}
	seen := map[string]bool{}
	for i, p := range file.Printers {
		if p == nil {
			return nil, fmt.Errorf("printer %d: empty entry", i+1)
		}
		if err := p.validate(resources); err != nil {
			return nil, fmt.Errorf("printer %q: %w", p.Name, err)
		}
		if seen[p.Name] {
			return nil, fmt.Errorf("printer %q: duplicate name", p.Name)
		}
		seen[p.Name] = true
		if p.CredentialFile != "" {
			if _, err := readCredential(p.CredentialFile); err != nil {
				log.Printf("warning: printer %q credential file %s: %v", p.Name, p.CredentialFile, err)
			}
		}
	}
	return file.Printers, nil
}

func (p *PrinterConfig) validate(resources string) error {
	if p.Name == "" || len(p.Name) > 64 || strings.IndexFunc(p.Name, unicode.IsControl) >= 0 || strings.TrimSpace(p.Name) != p.Name {
		return errors.New("name must be 1-64 bytes without control characters or edge spaces")
	}
	switch p.Kind {
	case "prusalink", "octoprint", "moonraker":
		u, err := url.Parse(p.URL)
		if err != nil || (u.Scheme != "http" && u.Scheme != "https") || u.Host == "" || u.User != nil || u.RawQuery != "" || u.Fragment != "" {
			return errors.New("url must be an http(s) base URL without credentials, query or fragment")
		}
		if p.TLS == "" && u.Scheme == "https" {
			p.TLS = "system"
		}
		if u.Scheme == "http" && p.TLS != "" {
			return errors.New("tls applies only to https URLs")
		}
		if p.TLS == "bambu-ca" {
			return errors.New("tls bambu-ca applies only to bambu-lan printers")
		}
		if p.Host != "" || p.Serial != "" {
			return errors.New("host and serial apply only to bambu-lan printers")
		}
	case "bambu-lan":
		if p.Host == "" || strings.ContainsAny(p.Host, "/:@ ") && net.ParseIP(p.Host) == nil {
			return errors.New("host must be the printer's IP address or host name")
		}
		if !bambuSerial.MatchString(p.Serial) {
			return errors.New("serial must be the printer's serial number")
		}
		if p.URL != "" {
			return errors.New("url does not apply to bambu-lan printers; use host")
		}
		if p.CredentialFile == "" {
			return errors.New("credential_file must hold the printer's LAN access code")
		}
		if p.TLS == "" {
			p.TLS = "bambu-ca"
		}
		if p.TLS == "system" {
			return errors.New("bambu-lan printers present Bambu-signed certificates: use tls bambu-ca or pin")
		}
		if p.TLS == "tofu" && !p.editable {
			return errors.New("tls must be system, pin or bambu-ca")
		}
		if p.TLS == "bambu-ca" || p.TLS == "tofu" {
			pem, err := os.ReadFile(resources + "/cert/printer.cer")
			if err != nil {
				return fmt.Errorf("tls bambu-ca needs resources/cert/printer.cer: %w", err)
			}
			p.bambuRoots = x509.NewCertPool()
			if !p.bambuRoots.AppendCertsFromPEM(pem) {
				return errors.New("resources/cert/printer.cer holds no certificates")
			}
		}
	default:
		return errors.New("kind must be prusalink, octoprint, moonraker or bambu-lan")
	}
	switch p.TLS {
	case "", "system", "bambu-ca", "tofu":
		if p.Fingerprint != "" {
			return errors.New("fingerprint needs tls pin")
		}
	case "pin":
		fp, err := hex.DecodeString(strings.NewReplacer(":", "", " ", "").Replace(strings.TrimPrefix(strings.ToLower(p.Fingerprint), "sha256:")))
		if err != nil || len(fp) != sha256.Size {
			return errors.New("tls pin needs fingerprint: the certificate's SHA-256 (see printer-fingerprint)")
		}
		p.fingerprint = fp
	default:
		return errors.New("tls must be system, pin or bambu-ca")
	}
	if p.Kind == "prusalink" {
		if p.Auth == "" {
			p.Auth = "apikey"
		}
		if p.Auth != "apikey" && p.Auth != "digest" {
			return errors.New("auth must be apikey or digest")
		}
		if p.Auth == "digest" && (p.Username == "" || p.CredentialFile == "") {
			return errors.New("digest auth needs username and credential_file")
		}
		if p.Storage == "" {
			p.Storage = "usb"
		}
		if strings.ContainsAny(p.Storage, "/?#%") || strings.IndexFunc(p.Storage, unicode.IsControl) >= 0 {
			return errors.New("storage must be a single path segment")
		}
	} else if p.Auth != "" || p.Username != "" || p.Storage != "" {
		return errors.New("auth, username and storage apply only to prusalink printers")
	}
	if p.Kind == "octoprint" && p.CredentialFile == "" {
		return errors.New("octoprint needs credential_file holding an API key")
	}
	return nil
}

func readCredential(path string) (string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return "", errors.New("unreadable")
	}
	value := strings.TrimRight(string(b), "\r\n")
	if value == "" || strings.IndexFunc(value, unicode.IsControl) >= 0 {
		return "", errors.New("must hold one nonempty line")
	}
	return value, nil
}

func (p *PrinterConfig) tlsConfig(serverName string) *tls.Config {
	c := &tls.Config{ServerName: serverName, MinVersion: tls.VersionTLS12}
	switch p.TLS {
	case "pin":
		c.InsecureSkipVerify = true
		c.VerifyConnection = func(cs tls.ConnectionState) error {
			if len(cs.PeerCertificates) == 0 {
				return errors.New("printer presented no certificate")
			}
			sum := sha256.Sum256(cs.PeerCertificates[0].Raw)
			if string(sum[:]) != string(p.fingerprint) {
				return errPinMismatch
			}
			return nil
		}
	case "bambu-ca":
		c.InsecureSkipVerify = true
		c.VerifyConnection = func(cs tls.ConnectionState) error {
			leaf, err := p.bambuLeaf(cs)
			if err != nil {
				return errors.New("printer certificate is not signed by the Bambu printer CA")
			}
			if !strings.EqualFold(leaf.Subject.CommonName, p.Serial) {
				return &serialMismatch{leaf.Subject.CommonName}
			}
			return nil
		}
	case "tofu":
		c.InsecureSkipVerify = true
		c.VerifyConnection = func(cs tls.ConnectionState) error {
			if len(cs.PeerCertificates) == 0 {
				return errors.New("printer presented no certificate")
			}
			if leaf, err := p.bambuLeaf(cs); err == nil {
				if !strings.EqualFold(leaf.Subject.CommonName, p.Serial) {
					return &serialMismatch{leaf.Subject.CommonName}
				}
				p.trust("bambu-ca", nil)
				return nil
			}
			sum := sha256.Sum256(cs.PeerCertificates[0].Raw)
			p.trust("pin", sum[:])
			return nil
		}
	}
	return c
}

var errPinMismatch = errors.New("printer certificate does not match the pinned fingerprint")

type serialMismatch struct{ actual string }

func (e *serialMismatch) Error() string { return "the printer at this address has serial " + e.actual }

func (p *PrinterConfig) bambuLeaf(cs tls.ConnectionState) (*x509.Certificate, error) {
	if len(cs.PeerCertificates) == 0 {
		return nil, errors.New("printer presented no certificate")
	}
	intermediates := x509.NewCertPool()
	for _, cert := range cs.PeerCertificates[1:] {
		intermediates.AddCert(cert)
	}
	leaf := cs.PeerCertificates[0]
	_, err := leaf.Verify(x509.VerifyOptions{Roots: p.bambuRoots, Intermediates: intermediates, KeyUsages: []x509.ExtKeyUsage{x509.ExtKeyUsageAny}})
	return leaf, err
}

func (p *PrinterConfig) trust(mode string, fp []byte) {
	if p.trusted != nil {
		p.trusted(mode, fp)
	}
}

type Transfer struct {
	Printer  string     `json:"printer"`
	Kind     string     `json:"kind"`
	Start    bool       `json:"start"`
	Filename string     `json:"filename"`
	State    string     `json:"state"`
	Sent     int64      `json:"sent"`
	Total    int64      `json:"total"`
	Message  string     `json:"message,omitempty"`
	Error    string     `json:"error,omitempty"`
	Started  time.Time  `json:"started"`
	Finished *time.Time `json:"finished,omitempty"`

	sent *atomic.Int64
}

type SendManager struct {
	admin   []*PrinterConfig
	saved   []*PrinterConfig
	store   *printerStore
	mu      sync.Mutex
	busy    map[string]bool
	cards   map[string]string
	buckets map[string]tokenBucket
}

const sendBurst = 3
const sendRate = 0.1

func NewSendManager(printers []*PrinterConfig, store *printerStore) *SendManager {
	m := &SendManager{admin: printers, store: store, busy: map[string]bool{}, buckets: map[string]tokenBucket{}}
	if store != nil {
		m.saved = store.printers
		for _, p := range m.saved {
			m.watchTrust(p)
		}
	}
	return m
}

func (m *SendManager) printer(name string) *PrinterConfig {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, list := range [][]*PrinterConfig{m.admin, m.saved} {
		for _, p := range list {
			if p.Name == name {
				return p
			}
		}
	}
	return nil
}

func (m *SendManager) allow(client string) (bool, int) { return m.allowBurst(client, sendBurst) }

func (m *SendManager) allowBurst(client string, burst float64) (bool, int) {
	m.mu.Lock()
	defer m.mu.Unlock()
	now := time.Now()
	b, ok := m.buckets[client]
	if !ok {
		b = tokenBucket{burst, now}
	}
	b.tokens = math.Min(burst, b.tokens+now.Sub(b.at).Seconds()*sendRate)
	b.at = now
	if b.tokens < 1 {
		m.buckets[client] = b
		return false, int(math.Ceil((1 - b.tokens) / sendRate))
	}
	b.tokens--
	m.buckets[client] = b
	if len(m.buckets) > 4096 {
		for key, old := range m.buckets {
			if now.Sub(old.at) > time.Minute {
				delete(m.buckets, key)
			}
		}
	}
	return true, 0
}

func (s *Server) listPrinters(w http.ResponseWriter, r *http.Request) {
	m := s.sends
	m.mu.Lock()
	list := []PrinterSummary{}
	for _, p := range append(append([]*PrinterConfig{}, m.admin...), m.saved...) {
		list = append(list, p.summary())
	}
	m.mu.Unlock()
	if m.store != nil && m.store.bambu {
		w.Header().Set("X-Printer-Edit", "true")
	}
	if m.store != nil && m.store.hosts != nil {
		w.Header().Set("X-Printer-Hosts", "prusalink,octoprint")
	}
	writeJSON(w, 200, list)
}

func (j *Job) transferView() *Transfer {
	j.mu.Lock()
	defer j.mu.Unlock()
	if j.send == nil {
		return nil
	}
	view := *j.send
	view.Sent = j.send.sent.Load()
	return &view
}

func (s *Server) getSend(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	if t := j.transferView(); t != nil {
		writeJSON(w, 200, t)
		return
	}
	fail(w, 404, "this job has not been sent to a printer")
}

func (s *Server) sendJob(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	var body struct {
		Printer     string `json:"printer"`
		Start       *bool  `json:"start"`
		Filename    string `json:"filename"`
		BedLeveling *bool  `json:"bed_leveling"`
		Timelapse   bool   `json:"timelapse"`
		UseAMS      bool   `json:"use_ams"`
		AMSMapping  []int  `json:"ams_mapping"`
	}
	raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 4<<10))
	if err != nil {
		fail(w, 413, "send request exceeds 4 KiB")
		return
	}
	if json.Unmarshal(raw, &body) != nil || body.Printer == "" {
		fail(w, 400, `expected {"printer": name, "start": boolean}`)
		return
	}
	p := s.sends.printer(body.Printer)
	if p == nil {
		fail(w, 400, "printer is not configured on this server")
		return
	}
	start := body.Start == nil || *body.Start
	filename := j.DownloadName()
	if body.Filename != "" {
		if strings.ContainsAny(body.Filename, "/\\") || len(body.Filename) > 240 || strings.IndexFunc(body.Filename, unicode.IsControl) >= 0 || !strings.HasSuffix(strings.ToLower(body.Filename), "."+j.Request.Output) || strings.HasPrefix(body.Filename, ".") {
			fail(w, 400, "filename must be a basename ending in ."+j.Request.Output)
			return
		}
		filename = body.Filename
	}
	if j.AwaitFinished(r.Context()) != StateDone || j.Request.Orient || j.Request.Output == "3mf" {
		fail(w, 409, "only a finished sliced job can be sent")
		return
	}
	if want := map[bool]string{true: "gcode.3mf", false: "gcode"}[p.Kind == "bambu-lan"]; j.Request.Output != want {
		fail(w, 400, fmt.Sprintf("%s needs a .%s export; this job produced .%s", p.Name, want, j.Request.Output))
		return
	}
	if body.UseAMS && (p.Kind != "bambu-lan" || len(j.Request.Filaments) != 1) {
		fail(w, 400, "AMS slot 1 applies only to a single-filament job sent to a Bambu printer")
		return
	}
	if body.AMSMapping != nil {
		valid := p.Kind == "bambu-lan" && len(body.AMSMapping) == len(j.Request.Filaments)
		for _, t := range body.AMSMapping {
			valid = valid && (t >= 0 && t < 16 || t == externalTray)
		}
		if !valid {
			fail(w, 400, fmt.Sprintf("ams_mapping needs one tray (0 to 15, or %d for the external spool) per filament of the job, for a Bambu printer", externalTray))
			return
		}
	}
	if len(p.Presets) > 0 && !containsString(p.Presets, j.Request.Printer) {
		fail(w, 400, fmt.Sprintf("%s accepts G-code sliced for %s, not %s", p.Name, strings.Join(p.Presets, " or "), j.Request.Printer))
		return
	}
	client := s.clientIP(r)
	if ok, retry := s.sends.allow(client); !ok {
		w.Header().Set("Retry-After", fmt.Sprint(min(retry, 86400)))
		fail(w, 429, "too many sends; wait before sending again")
		return
	}
	f, err := os.Open(j.OutputPath(j.Request.Output))
	if err != nil {
		fail(w, 404, "download expired")
		return
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		fail(w, 500, "cannot read the export")
		return
	}
	s.sends.mu.Lock()
	if s.sends.busy[p.Name] {
		s.sends.mu.Unlock()
		f.Close()
		fail(w, 409, p.Name+" is receiving another upload; try again when it finishes")
		return
	}
	j.mu.Lock()
	if j.send != nil && (j.send.State == "uploading" || j.send.State == "starting") {
		j.mu.Unlock()
		s.sends.mu.Unlock()
		f.Close()
		fail(w, 409, "this job is already being sent")
		return
	}
	card := p.Name + "\x00" + filename
	resend := s.sends.cards[card] == j.ID
	delete(s.sends.cards, card)
	t := &Transfer{Printer: p.Name, Kind: p.Kind, Start: start, Filename: filename, State: "uploading", Total: st.Size(), Started: time.Now().UTC(), sent: &atomic.Int64{}}
	j.send = t
	view := *t
	j.mu.Unlock()
	s.sends.busy[p.Name] = true
	s.sends.mu.Unlock()
	audit(map[string]any{"event": "send_accepted", "job_id": j.ID, "printer": p.Name, "kind": p.Kind, "client": client, "start_print": start, "bytes": st.Size()})
	options := bambuOptions{bedLeveling: body.BedLeveling == nil || *body.BedLeveling, timelapse: body.Timelapse, useAMS: body.UseAMS, amsMapping: body.AMSMapping}
	if body.AMSMapping != nil {
		options.useAMS = false
		for _, t := range body.AMSMapping {
			options.useAMS = options.useAMS || t != externalTray
		}
		if !options.useAMS {
			options.amsMapping = nil
		}
	}
	go s.runSend(j, t, p, f, client, options, resend)
	w.Header().Set("Location", "/api/jobs/"+j.ID+"/send")
	writeJSON(w, 202, &view)
}

func containsString(values []string, value string) bool {
	for _, v := range values {
		if v == value {
			return true
		}
	}
	return false
}

func audit(fields map[string]any) {
	fields["audit"] = "printer_send"
	b, _ := json.Marshal(fields)
	log.Writer().Write(append(b, '\n'))
}

const sendTimeout = 30 * time.Minute
const sendStall = 30 * time.Second

type upload struct {
	printer        *PrinterConfig
	secret         string
	filename       string
	file           *os.File
	size           int64
	start          bool
	progress       *atomic.Int64
	starting       func()
	stored         func()
	options        bambuOptions
	resend, onCard bool
}

func (u *upload) placed() string {
	if u.onCard {
		return u.filename + " was already on " + u.printer.Name + "'s SD card"
	}
	return "Uploaded " + u.filename + " to " + u.printer.Name
}

type bambuOptions struct {
	bedLeveling, timelapse, useAMS bool
	amsMapping                     []int
}

func (s *Server) runSend(j *Job, t *Transfer, p *PrinterConfig, f *os.File, client string, options bambuOptions, resend bool) {
	defer f.Close()
	began := time.Now()
	base, cancelTimeout := context.WithTimeoutCause(context.Background(), sendTimeout, errors.New("the upload took longer than 30 minutes"))
	defer cancelTimeout()
	ctx, cancel := context.WithCancelCause(base)
	defer cancel(nil)
	stalled := watchProgress(ctx, cancel, t.sent, t.Total)
	u := &upload{printer: p, filename: t.Filename, file: f, size: t.Total, start: t.Start, progress: t.sent,
		starting: func() { j.mu.Lock(); t.State = "starting"; j.mu.Unlock() },
		stored: func() {
			s.sends.mu.Lock()
			if s.sends.cards == nil {
				s.sends.cards = map[string]string{}
			}
			s.sends.cards[p.Name+"\x00"+t.Filename] = j.ID
			s.sends.mu.Unlock()
		}, options: options, resend: resend}
	var message string
	var err error
	if p.CredentialFile != "" {
		if u.secret, err = readCredential(p.CredentialFile); err != nil {
			err = errors.New("the server cannot read this printer's credential file; ask the administrator")
		}
	}
	if err == nil {
		switch p.Kind {
		case "bambu-lan":
			message, err = sendBambu(ctx, u)
		default:
			message, err = sendHTTP(ctx, u)
		}
	}
	if err != nil {
		if cause := context.Cause(ctx); cause != nil && !errors.Is(cause, context.Canceled) {
			err = cause
		}
		if stalled.Load() {
			err = errors.New("the printer stopped accepting data for 30 seconds")
		}
	}
	finished := time.Now().UTC()
	j.mu.Lock()
	t.Finished = &finished
	if err != nil {
		t.State, t.Error = "failed", redact(err.Error(), u.secret)
	} else {
		t.State, t.Message = "done", redact(message, u.secret)
	}
	result, errText := t.State, t.Error
	j.mu.Unlock()
	s.sends.mu.Lock()
	delete(s.sends.busy, p.Name)
	s.sends.mu.Unlock()
	audit(map[string]any{"event": "send_" + result, "job_id": j.ID, "printer": p.Name, "kind": p.Kind, "client": client, "start_print": t.Start, "bytes": t.sent.Load(), "on_card": u.onCard, "duration_ms": time.Since(began).Milliseconds(), "error": errText})
}

func watchProgress(ctx context.Context, cancel context.CancelCauseFunc, sent *atomic.Int64, total int64) *atomic.Bool {
	stalled := &atomic.Bool{}
	go func() {
		last, at := sent.Load(), time.Now()
		ticker := time.NewTicker(time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case now := <-ticker.C:
				if n := sent.Load(); n != last {
					last, at = n, now
				} else if now.Sub(at) > sendStall && last < total {
					stalled.Store(true)
					cancel(errors.New("stalled"))
					return
				}
			}
		}
	}()
	return stalled
}

func redact(text, secret string) string {
	if secret == "" {
		return text
	}
	return strings.ReplaceAll(text, secret, "[redacted]")
}

type progressReader struct {
	r io.Reader
	n *atomic.Int64
}

func (p progressReader) Read(b []byte) (int, error) {
	n, err := p.r.Read(b)
	p.n.Add(int64(n))
	return n, err
}

func netError(err error, what string) error {
	var dns *net.DNSError
	var opErr *net.OpError
	var verify *tls.CertificateVerificationError
	switch {
	case errors.Is(err, context.DeadlineExceeded) || os.IsTimeout(err):
		return fmt.Errorf("%s did not answer in time; check that it is on and reachable from the server", what)
	case errors.As(err, &dns):
		return fmt.Errorf("cannot resolve %s's address", what)
	case errors.As(err, &verify) || strings.Contains(err.Error(), "certificate"):
		return fmt.Errorf("%s's TLS certificate was rejected: %v", what, lastClause(err))
	case errors.As(err, &opErr) && opErr.Op == "dial":
		return fmt.Errorf("cannot connect to %s; check that it is on and reachable from the server", what)
	}
	return fmt.Errorf("connection to %s failed: %v", what, lastClause(err))
}

func lastClause(err error) string {
	text := err.Error()
	if i := strings.LastIndex(text, ": "); i >= 0 {
		return text[i+2:]
	}
	return text
}
