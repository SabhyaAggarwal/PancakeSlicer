package main

import (
	"encoding/json"
	"errors"
	"math"
	"net/http"
	"strconv"
	"strings"
)

type resolveRequest struct {
	Printer     string                     `json:"printer"`
	Filaments   []string                   `json:"filaments"`
	Print       string                     `json:"print"`
	LayerHeight float64                    `json:"layerHeight"`
	Project     map[string]json.RawMessage `json:"project,omitempty"`
}
type sliceConfig struct {
	Config  map[string]json.RawMessage `json:"config"`
	Sources map[string]string          `json:"sources"`
	Engine  struct {
		Vendor    string   `json:"vendor"`
		Printer   string   `json:"printer"`
		Process   string   `json:"process"`
		Filaments []string `json:"filaments"`
	} `json:"engine"`
}

func catalogMachine(v *VendorPresets, model string, nozzle float64) *Preset {
	var best *Preset
	for i := range v.Machines {
		m := &v.Machines[i]
		n, _ := strconv.ParseFloat(firstConfigText(m.Config["nozzle_diameter"]), 64)
		if firstConfigText(m.Config["printer_model"]) == model && sameMm(n, nozzle) && (best == nil || m.Name < best.Name) {
			best = m
		}
	}
	return best
}

func standardProcess(v *VendorPresets, machine *Preset) *Preset {
	nozzle, _ := strconv.ParseFloat(firstConfigText(machine.Config["nozzle_diameter"]), 64)
	var best *Preset
	bestGap := math.Inf(1)
	for j := range v.Processes {
		p := &v.Processes[j]
		if !standardPreset.MatchString(p.Name) || !compatibleWith(p, machine.Name) {
			continue
		}
		height, _ := strconv.ParseFloat(firstConfigText(p.Config["layer_height"]), 64)
		if gap := math.Abs(height - nozzle/2); gap < bestGap || gap == bestGap && best != nil && p.Name < best.Name {
			best, bestGap = p, gap
		}
	}
	return best
}

func familyVariant(v *VendorPresets, family string, machine string) *Preset {
	var fit, any *Preset
	for i := range v.Filaments {
		f := &v.Filaments[i]
		if filamentFamilyOf(f.Name) != family {
			continue
		}
		if profileCompatible(f, machine, "", false) && (fit == nil || f.Name < fit.Name) {
			fit = f
		}
		if any == nil || f.Name < any.Name {
			any = f
		}
	}
	if fit != nil {
		return fit
	}
	return any
}

func (s *Server) resolveProfiles(profiles map[string]*v4Record, in resolveRequest) (*sliceConfig, error) {
	printer, print := profiles[in.Printer], profiles[in.Print]
	if printer == nil || printer.Type != "printer" || !printer.live() {
		return nil, errors.New("choose a printer")
	}
	if print == nil || print.Type != "print" || !print.live() {
		return nil, errors.New("choose a print profile")
	}
	vendor, err := s.presets.vendor(printer.Vendor)
	if err != nil {
		return nil, errors.New("the printer's catalog is unavailable: " + printer.Vendor)
	}
	machine := catalogMachine(vendor, printer.Model, printer.Nozzle)
	if machine == nil {
		return nil, errors.New("the catalog has no " + printer.Model + " with a " + strconv.FormatFloat(printer.Nozzle, 'f', -1, 64) + " mm nozzle")
	}
	process := standardProcess(vendor, machine)
	if process == nil {
		return nil, errors.New("the catalog has no print settings for " + printer.Model)
	}
	out := &sliceConfig{Config: map[string]json.RawMessage{}, Sources: map[string]string{}}
	out.Engine.Vendor, out.Engine.Printer, out.Engine.Process = vendor.Vendor, machine.Name, process.Name
	set := func(values map[string]json.RawMessage, source string) {
		for k, v := range values {
			out.Config[k], out.Sources[k] = v, source
		}
	}
	text := func(values map[string]string) map[string]json.RawMessage {
		m := map[string]json.RawMessage{}
		for k, v := range values {
			m[k], _ = json.Marshal(v)
		}
		return m
	}
	set(machine.Config, "catalog")
	set(printer.Machine, "printer")
	set(process.Config, "catalog")
	set(print.Intent, "print")
	height := in.LayerHeight
	if !(height > 0) && print.PreferredHeight != nil {
		height = *print.PreferredHeight
	}
	if !(height > 0) {
		height, _ = strconv.ParseFloat(firstConfigText(process.Config["layer_height"]), 64)
	}
	height = clampHeight(height, printer.Nozzle)
	set(text(scaleValues(print.Scales, printer.Nozzle, height)), "scale")
	set(text(map[string]string{"layer_height": strconv.FormatFloat(height, 'f', -1, 64)}), "print")
	set(printer.PrintOverrides, "printer override")
	for i, id := range in.Filaments {
		f := profiles[id]
		if f == nil || f.Type != "filament" || !f.live() {
			return nil, errors.New("choose a filament for slot " + strconv.Itoa(i+1))
		}
		family := strings.TrimPrefix(f.Family, vendor.Vendor+"/")
		variant := familyVariant(vendor, family, machine.Name)
		if variant == nil {
			return nil, errors.New("the catalog has no " + family + " filament for " + printer.Vendor)
		}
		out.Engine.Filaments = append(out.Engine.Filaments, variant.Name)
		if i == 0 {
			set(variant.Config, "catalog")
			set(f.Settings, "filament")
		}
	}
	set(in.Project, "project")
	return out, nil
}

func (s *Server) postResolve(w http.ResponseWriter, r *http.Request) {
	var in resolveRequest
	if !workspaceDecode(w, r, &in) {
		return
	}
	profiles, err := s.libraryV4Profiles()
	if err != nil {
		libraryFailure(w, 503, "library_unavailable", "Profiles could not be read.", "retry_storage", 0)
		return
	}
	out, err := s.resolveProfiles(profiles, in)
	if err != nil {
		libraryFailure(w, 422, "resolve_failed", err.Error(), "choose_profiles", 0)
		return
	}
	writeJSON(w, 200, out)
}

func (s *Server) libraryV4Profiles() (map[string]*v4Record, error) {
	l := s.libraryV4
	l.mu.Lock()
	e, err := l.read()
	l.mu.Unlock()
	if err != nil {
		return nil, err
	}
	return v4Profiles(e.Document)
}
