package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"time"
)

type BambuControl struct {
	Index  int  `json:"index"`
	Min    int  `json:"min"`
	Max    int  `json:"max"`
	Modern bool `json:"modern,omitempty"`
}

type BambuControls struct {
	Fans    map[string]BambuControl `json:"fans,omitempty"`
	Chamber *BambuControl           `json:"chamber,omitempty"`
}

func bambuFanName(index int) string {
	return map[int]string{1: "part", 2: "auxiliary", 3: "chamber", 6: "internal", 10: "left auxiliary"}[index]
}

func (b *bambuReport) controls() *BambuControls {
	c := &BambuControls{Fans: map[string]BambuControl{}}
	if b.ChamberEditable {
		low, high := 0, 60
		if len(b.ChamberRange) >= 2 {
			low, high = b.ChamberRange[0], b.ChamberRange[1]
		}
		c.Chamber = &BambuControl{Min: low, Max: high}
	}
	if b.Device != nil && b.Device.Airduct != nil {
		a := b.Device.Airduct
		allowed := map[int]bool{}
		for _, mode := range a.ModeList {
			if mode.ID == a.ModeCur {
				for _, id := range mode.Control {
					allowed[id>>4] = true
				}
			}
		}
		for _, part := range a.Parts {
			index := part.ID >> 4 & 255
			if name := bambuFanName(part.Function); name != "" && part.ID&15 == 0 && allowed[index] {
				c.Fans[name] = BambuControl{Index: index, Min: int(part.Range & 65535), Max: int(part.Range >> 16), Modern: true}
			}
		}
	} else {
		for index, name := range []string{"part", "auxiliary", "chamber"} {
			if _, reported := b.fans()[name]; !reported {
				continue
			}
			if name == "auxiliary" && b.AuxSupported != nil && !*b.AuxSupported {
				continue
			}
			if name == "chamber" && b.ChamberFanSupported != nil && !*b.ChamberFanSupported {
				continue
			}
			c.Fans[name] = BambuControl{Index: index + 1, Max: 100}
		}
	}
	return c
}

type bambuControlRequest struct {
	Control string `json:"control"`
	Fan     string `json:"fan,omitempty"`
	Value   *int   `json:"value"`
}

func (c *BambuControls) command(in bambuControlRequest) (map[string]any, error) {
	if c == nil || in.Value == nil {
		return nil, errors.New("a reported control and integer value are required")
	}
	value := *in.Value
	switch in.Control {
	case "chamber":
		if c.Chamber == nil || value < c.Chamber.Min || value > c.Chamber.Max {
			return nil, errors.New("chamber target is outside the printer's supported range")
		}
		return map[string]any{"command": "set_ctt", "ctt_val": value}, nil
	case "fan":
		fan, ok := c.Fans[in.Fan]
		if !ok || value < fan.Min || value > fan.Max || value < 0 || value > 100 {
			return nil, errors.New("fan is not adjustable at that speed in the current air-duct mode")
		}
		if fan.Modern {
			return map[string]any{"command": "set_fan", "fan_index": fan.Index, "speed": value}, nil
		}
		return map[string]any{"command": "gcode_line", "param": fmt.Sprintf("M106 P%d S%d\n", fan.Index, value*255/100)}, nil
	}
	return nil, errors.New("unknown device control")
}

func (s *Server) bambuDeviceControl(w http.ResponseWriter, r *http.Request) {
	p := s.sends.printer(r.PathValue("printer"))
	if p == nil || p.Kind != "bambu-lan" {
		fail(w, 404, "this printer has no Bambu LAN connection")
		return
	}
	var in bambuControlRequest
	if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1024)).Decode(&in); err != nil {
		fail(w, 400, "expected a device control and integer value")
		return
	}
	status, ok := s.sends.hub.current(p.Printer)
	if !ok || !status.Online {
		fail(w, 409, "wait for the printer's live status before changing its controls")
		return
	}
	command, err := status.Controls.command(in)
	if err != nil {
		fail(w, 422, err.Error())
		return
	}
	secret, err := p.secret()
	if err != nil {
		fail(w, 409, err.Error())
		return
	}
	if ok, retry := s.sends.allowBurst("control "+s.clientIP(r), 10); !ok {
		w.Header().Set("Retry-After", fmt.Sprint(min(retry, 86400)))
		fail(w, 429, "too many printer commands; wait before trying again")
		return
	}
	ctx, cancel := context.WithTimeout(r.Context(), 20*time.Second)
	defer cancel()
	err = bambuCommand(ctx, p, secret, command)
	audit(map[string]any{"event": "printer_device_control", "printer": p.Printer, "control": in.Control, "fan": in.Fan, "value": *in.Value, "client": s.clientIP(r), "error": fmt.Sprint(err)})
	if err != nil {
		fail(w, 502, redact(err.Error(), secret))
		return
	}
	s.sends.hub.poke(p.Printer)
	w.WriteHeader(204)
}
