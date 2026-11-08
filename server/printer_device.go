package main

import (
	"archive/zip"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"path"
	"strings"
	"time"
)

type deviceJob struct {
	ID        string    `json:"id"`
	Plate     int       `json:"plate"`
	SentAt    time.Time `json:"sent_at"`
	Thumbnail string    `json:"thumbnail,omitempty"`
}

func (s *Server) deviceJob(st PrinterStatus) *deviceJob {
	h := s.sends.history
	if h == nil || st.File == "" {
		return nil
	}
	stem := func(name string) string {
		return strings.TrimSuffix(strings.TrimSuffix(path.Base(name), ".3mf"), ".gcode")
	}
	h.mu.Lock()
	defer h.mu.Unlock()
	for i := len(h.list) - 1; i >= 0; i-- {
		t := h.list[i]
		if t.Printer != st.Printer || !t.Start || t.State != TransferDone || stem(t.Filename) != stem(st.File) {
			continue
		}
		j := s.jobs.Get(t.Job)
		if j == nil {
			return nil
		}
		plate := j.Request.PlateIndex
		if plate < 1 {
			plate = 1
		}
		job := &deviceJob{ID: t.Job, Plate: plate, SentAt: t.Started}
		if j.Request.Output == "gcode.3mf" {
			job.Thumbnail = "/api/printer-connections/" + url.PathEscape(st.Printer) + "/job-thumbnail"
		}
		return job
	}
	return nil
}
func (s *Server) printerJobThumbnail(w http.ResponseWriter, r *http.Request) {
	st, ok := s.sends.hub.current(r.PathValue("printer"))
	if !ok {
		fail(w, 404, "no current job")
		return
	}
	job := s.deviceJob(st)
	if job == nil {
		fail(w, 404, "this job was not sent by this service")
		return
	}
	j := s.jobs.Get(job.ID)
	if j == nil {
		fail(w, 404, "job expired")
		return
	}
	z, err := zip.OpenReader(j.OutputPath("gcode.3mf"))
	if err != nil {
		fail(w, 404, "no job thumbnail")
		return
	}
	defer z.Close()
	want := fmt.Sprintf("Metadata/plate_%d.png", job.Plate)
	for _, file := range z.File {
		if file.Name != want || file.UncompressedSize64 > 4<<20 {
			continue
		}
		reader, err := file.Open()
		if err != nil {
			break
		}
		defer reader.Close()
		w.Header().Set("Content-Type", "image/png")
		w.Header().Set("Cache-Control", "no-store")
		io.Copy(w, io.LimitReader(reader, 4<<20))
		return
	}
	fail(w, 404, "no job thumbnail")
}

func controlHostDevice(ctx context.Context, p *PrinterConfig, secret string, in bambuControlRequest) error {
	if in.Value == nil || in.Control != "nozzle" && in.Control != "bed" {
		return errors.New("this host exposes only nozzle and bed setpoints")
	}
	value := *in.Value
	maxTemp := 120
	gcode := "M140"
	if in.Control == "nozzle" {
		maxTemp = 300
		gcode = "M104"
	}
	if value < 0 || value > maxTemp {
		return errors.New("temperature target is outside the supported range")
	}
	h, base, err := httpSession(ctx, p, secret)
	if err != nil {
		return err
	}
	switch p.Kind {
	case "moonraker":
		body, _ := json.Marshal(map[string]any{"script": fmt.Sprintf("%s S%d", gcode, value)})
		_, err = h.call(ctx, "POST", base.JoinPath("printer/gcode/script").String(), body, nil)
	case "octoprint":
		body := map[string]any{"command": "target", "target": value}
		endpoint := "api/printer/bed"
		if in.Control == "nozzle" {
			endpoint = "api/printer/tool"
			body = map[string]any{"command": "target", "targets": map[string]int{"tool0": value}}
		}
		raw, _ := json.Marshal(body)
		_, err = h.call(ctx, "POST", base.JoinPath(endpoint).String(), raw, nil)
	case "duet":
		if _, err = duetConnect(ctx, h, base); err == nil {
			defer duetDisconnect(h, base)
			err = duetGcode(ctx, h, base, fmt.Sprintf("%s S%d", gcode, value))
		}
	default:
		return errors.New("device setpoints are unavailable for this host")
	}
	return err
}
