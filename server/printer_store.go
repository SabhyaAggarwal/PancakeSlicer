package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"time"
	"unicode"
)

type printerStore struct {
	dir       string
	resources string
	printers  []*PrinterConfig
}

type storedPrinter struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	Host        string `json:"host"`
	Serial      string `json:"serial"`
	Model       string `json:"model,omitempty"`
	TLS         string `json:"tls"`
	Fingerprint string `json:"fingerprint,omitempty"`
}

func loadPrinterStore(dir, resources string) (*printerStore, error) {
	st := &printerStore{dir: dir, resources: resources}
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return nil, err
	}
	b, err := os.ReadFile(filepath.Join(dir, "printers.json"))
	if errors.Is(err, os.ErrNotExist) {
		return st, nil
	}
	if err != nil {
		return nil, err
	}
	var file struct {
		Printers []storedPrinter `json:"printers"`
	}
	if err := json.Unmarshal(b, &file); err != nil {
		return nil, fmt.Errorf("%s: %w", filepath.Join(dir, "printers.json"), err)
	}
	for _, sp := range file.Printers {
		p, err := st.config(sp)
		if err != nil {
			log.Printf("warning: app-added printer %q skipped: %v", sp.Name, err)
			continue
		}
		st.printers = append(st.printers, p)
	}
	return st, nil
}

func (st *printerStore) config(sp storedPrinter) (*PrinterConfig, error) {
	if len(sp.ID) != 16 || strings.Trim(sp.ID, "0123456789abcdef") != "" {
		return nil, errors.New("bad id")
	}
	p := &PrinterConfig{Name: sp.Name, Kind: "bambu-lan", Host: sp.Host, Serial: sp.Serial, TLS: sp.TLS, Fingerprint: sp.Fingerprint,
		CredentialFile: filepath.Join(st.dir, sp.ID+".code"), editable: true, model: sp.Model}
	return p, p.validate(st.resources)
}

func (st *printerStore) save() error {
	var file struct {
		Printers []storedPrinter `json:"printers"`
	}
	file.Printers = []storedPrinter{}
	for _, p := range st.printers {
		file.Printers = append(file.Printers, storedPrinter{printerID(p), p.Name, p.Host, p.Serial, p.model, p.TLS, p.Fingerprint})
	}
	b, _ := json.MarshalIndent(file, "", "  ")
	return writeFileAtomic(filepath.Join(st.dir, "printers.json"), b)
}

func printerID(p *PrinterConfig) string {
	return strings.TrimSuffix(filepath.Base(p.CredentialFile), ".code")
}

func writeFileAtomic(path string, b []byte) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), ".tmp-*")
	if err != nil {
		return err
	}
	defer os.Remove(tmp.Name())
	if _, err := tmp.Write(b); err != nil {
		tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	return os.Rename(tmp.Name(), path)
}

func (m *SendManager) watchTrust(p *PrinterConfig) {
	if p.TLS != "tofu" {
		return
	}
	p.trusted = func(mode string, fp []byte) {
		m.mu.Lock()
		defer m.mu.Unlock()
		for i, q := range m.saved {
			if q != p {
				continue
			}
			next := *p
			next.TLS, next.trusted, next.fingerprint, next.Fingerprint = mode, nil, fp, ""
			if fp != nil {
				next.Fingerprint = "sha256:" + hex.EncodeToString(fp)
			}
			m.saved[i] = &next
			m.store.printers = m.saved
			if err := m.store.save(); err != nil {
				log.Printf("warning: cannot record printer %q certificate: %v", p.Name, err)
			}
			log.Printf("printer %q: trusted its %s certificate on first use", p.Name, map[string]string{"pin": "self-signed", "bambu-ca": "Bambu-signed"}[mode])
		}
	}
}

type printerEdit struct {
	Name       string `json:"name"`
	Host       string `json:"host"`
	Serial     string `json:"serial"`
	AccessCode string `json:"access_code"`
	Model      string `json:"model"`
}

func (s *Server) printerBody(w http.ResponseWriter, r *http.Request) (*printerEdit, bool) {
	if s.sends.store == nil {
		fail(w, 403, "adding printers in the app is off on this server")
		return nil, false
	}
	var body printerEdit
	raw, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 4<<10))
	if err != nil || json.Unmarshal(raw, &body) != nil {
		fail(w, 400, `expected {"name", "host", "serial", "access_code"}`)
		return nil, false
	}
	body.Name, body.Host, body.Serial, body.AccessCode = strings.TrimSpace(body.Name), strings.TrimSpace(body.Host), strings.TrimSpace(body.Serial), strings.TrimSpace(body.AccessCode)
	if body.AccessCode != "" && (len(body.AccessCode) > 64 || strings.IndexFunc(body.AccessCode, func(r rune) bool { return unicode.IsSpace(r) || unicode.IsControl(r) }) >= 0) {
		fail(w, 400, "the access code is the 8 characters under Settings > LAN only on the printer")
		return nil, false
	}
	return &body, true
}

func (s *Server) editPrinter(w http.ResponseWriter, r *http.Request, old string) {
	body, ok := s.printerBody(w, r)
	if !ok {
		return
	}
	m := s.sends
	m.mu.Lock()
	defer m.mu.Unlock()
	index := -1
	for i, p := range m.saved {
		if p.Name == old {
			index = i
		}
	}
	if old != "" && index < 0 {
		fail(w, 404, "no app-added printer named "+old)
		return
	}
	taken := false
	for _, p := range m.admin {
		taken = taken || p.Name == body.Name
	}
	for i, p := range m.saved {
		taken = taken || p.Name == body.Name && i != index
	}
	if taken {
		fail(w, 409, "a printer named "+body.Name+" already exists")
		return
	}
	sp := storedPrinter{Name: body.Name, Host: body.Host, Serial: strings.ToUpper(body.Serial), TLS: "tofu"}
	if len(body.Model) <= 40 && strings.IndexFunc(body.Model, unicode.IsControl) < 0 {
		sp.Model = body.Model
	}
	if index >= 0 {
		prev := m.saved[index]
		sp.ID = printerID(prev)
		if prev.Host == sp.Host && prev.Serial == sp.Serial {
			sp.TLS, sp.Fingerprint = prev.TLS, prev.Fingerprint
			if sp.Model == "" {
				sp.Model = prev.model
			}
		}
	} else {
		id := make([]byte, 8)
		rand.Read(id)
		sp.ID = hex.EncodeToString(id)
		if body.AccessCode == "" {
			fail(w, 400, "enter the printer's access code (Settings > LAN only)")
			return
		}
	}
	p, err := m.store.config(sp)
	if err != nil {
		fail(w, 400, err.Error())
		return
	}
	if body.AccessCode != "" {
		if err := writeFileAtomic(p.CredentialFile, []byte(body.AccessCode+"\n")); err != nil {
			fail(w, 500, "cannot store the access code")
			return
		}
	}
	saved := append([]*PrinterConfig{}, m.saved...)
	if index >= 0 {
		saved[index] = p
	} else {
		saved = append(saved, p)
	}
	m.store.printers = saved
	if err := m.store.save(); err != nil {
		m.store.printers = m.saved
		fail(w, 500, "cannot save the printer list")
		return
	}
	m.saved = saved
	m.watchTrust(p)
	audit(map[string]any{"event": map[bool]string{true: "printer_edited", false: "printer_added"}[index >= 0], "printer": p.Name, "client": s.clientIP(r)})
	writeJSON(w, map[bool]int{true: 200, false: 201}[index >= 0], PrinterSummary{p.Name, p.Kind, true, p.Host, p.Serial, p.model})
}

func (s *Server) addPrinter(w http.ResponseWriter, r *http.Request) { s.editPrinter(w, r, "") }

func (s *Server) updatePrinter(w http.ResponseWriter, r *http.Request) {
	s.editPrinter(w, r, r.PathValue("name"))
}

func (s *Server) removePrinter(w http.ResponseWriter, r *http.Request) {
	m := s.sends
	if m.store == nil {
		fail(w, 403, "adding printers in the app is off on this server")
		return
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	name := r.PathValue("name")
	for i, p := range m.saved {
		if p.Name != name {
			continue
		}
		saved := append(append([]*PrinterConfig{}, m.saved[:i]...), m.saved[i+1:]...)
		m.store.printers = saved
		if err := m.store.save(); err != nil {
			m.store.printers = m.saved
			fail(w, 500, "cannot save the printer list")
			return
		}
		m.saved = saved
		os.Remove(p.CredentialFile)
		audit(map[string]any{"event": "printer_removed", "printer": name, "client": s.clientIP(r)})
		w.WriteHeader(204)
		return
	}
	fail(w, 404, "no app-added printer named "+name)
}

func (s *Server) testPrinter(w http.ResponseWriter, r *http.Request) {
	body, ok := s.printerBody(w, r)
	if !ok {
		return
	}
	p := &PrinterConfig{Name: "test", Kind: "bambu-lan", Host: body.Host, Serial: strings.ToUpper(body.Serial), TLS: "tofu", editable: true, CredentialFile: "-"}
	fresh := true
	secret := body.AccessCode
	if saved := s.sends.printer(body.Name); saved != nil && saved.editable {
		if saved.Host == p.Host && saved.Serial == p.Serial {
			p, fresh = saved, false
		}
		if secret == "" {
			secret, _ = readCredential(saved.CredentialFile)
		}
	}
	if secret == "" {
		fail(w, 400, "enter the printer's access code (Settings > LAN only)")
		return
	}
	if fresh {
		if err := p.validate(s.cfg.Resources); err != nil {
			fail(w, 400, err.Error())
			return
		}
	}
	client := s.clientIP(r)
	if ok, retry := s.sends.allowBurst("test "+client, 10); !ok {
		w.Header().Set("Retry-After", fmt.Sprint(min(retry, 86400)))
		fail(w, 429, "too many tests; wait before testing again")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 45*time.Second)
	defer cancel()
	result := testBambu(ctx, p, secret)
	result.Message = redact(result.Message, secret)
	if result.OK && !fresh && result.Model != "" {
		s.sends.setModel(p, result.Model)
	}
	audit(map[string]any{"event": "printer_test", "printer": body.Name, "client": client, "stage": result.Stage})
	writeJSON(w, 200, result)
}

func (m *SendManager) setModel(p *PrinterConfig, model string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	for _, q := range m.saved {
		if q.Name == p.Name && q.model != model {
			q.model = model
			m.store.save()
		}
	}
}
