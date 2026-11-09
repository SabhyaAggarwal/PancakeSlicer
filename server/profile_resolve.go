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
	Config          map[string]json.RawMessage `json:"config"`
	Sources         map[string]string          `json:"sources"`
	Speeds          map[string]resolvedSpeed   `json:"speeds,omitempty"`
	engineOverrides map[string]bool
	Engine          struct {
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
	if best == nil {
		for j := range v.Processes {
			p := &v.Processes[j]
			if !compatibleWith(p, machine.Name) {
				continue
			}
			height, _ := strconv.ParseFloat(firstConfigText(p.Config["layer_height"]), 64)
			gap := math.Abs(height - nozzle/2)
			if gap < bestGap || gap == bestGap && best != nil && p.Name < best.Name {
				best, bestGap = p, gap
			}
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
	out := &sliceConfig{Config: map[string]json.RawMessage{}, Sources: map[string]string{}, engineOverrides: map[string]bool{}}
	out.Engine.Vendor, out.Engine.Printer, out.Engine.Process = vendor.Vendor, machine.Name, process.Name
	set := func(values map[string]json.RawMessage, source string) {
		for k, v := range values {
			if to := canonicalKey(k); to != k {
				if _, both := values[to]; both {
					continue
				}
				k = to
			}
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
	if v, ok := machine.Config["default_nozzle_volume_type"]; ok {
		if _, set := machine.Config["nozzle_volume_type"]; !set {
			out.Config["nozzle_volume_type"], out.Sources["nozzle_volume_type"] = v, "catalog"
			out.engineOverrides["nozzle_volume_type"] = true
		}
	}
	set(printer.Machine, "printer")
	set(process.Config, "catalog")
	for _, k := range []string{"nonplanar_mode", "nonplanar_wave"} {
		out.Config[k], out.Sources[k] = json.RawMessage(`"off"`), "catalog"
		out.engineOverrides[k] = true
	}
	set(print.Values, "print")
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
	filamentConfigs := make([]map[string]json.RawMessage, 0, len(in.Filaments))
	filamentOwned := map[string]bool{}
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
		config := map[string]json.RawMessage{}
		for k, v := range variant.Config {
			config[k] = v
		}
		for k, v := range f.Settings {
			config[k], filamentOwned[k] = v, true
		}
		if f.Colour != "" {
			config["filament_colour"], _ = json.Marshal([]string{f.Colour})
			filamentOwned["filament_colour"] = true
		}
		filamentConfigs = append(filamentConfigs, config)
	}
	dictionary := optionData
	if s.cfg.Options != nil {
		dictionary = s.cfg.Options.plain
	}
	filamentConfig, err := assembleFilamentConfig(filamentConfigs, dictionary)
	if err != nil {
		return nil, err
	}
	for k, v := range filamentConfig {
		source := "catalog"
		if filamentOwned[k] {
			source = "filament"
		}
		out.Config[k], out.Sources[k] = v, source
		if len(v) > 0 && v[0] == '[' {
			out.engineOverrides[k] = true
		}
	}
	set(in.Project, "project")
	if len(print.Scales) > 0 || print.SpeedModel != nil {
		model := print.SpeedModel
		if model == nil {
			return nil, errors.New("Dynamic profile has no speed multipliers. Reload profiles.")
		}
		base := dynamicSpeedBase(printer.Vendor, printer.Model, process.Config)
		if base == nil {
			return nil, errors.New("Dynamic speeds have no reference for " + printer.Model + ". Choose a regular print profile.")
		}
		for _, f := range profileRules.SpeedFeatures {
			if v, ok := printer.PrintOverrides[f.Key]; ok {
				base[f.Key] = v
			}
			if _, own := print.Values[f.Acceleration]; !own {
				if _, project := in.Project[f.Acceleration]; !project {
					if v, printerOwn := printer.PrintOverrides[f.Acceleration]; printerOwn {
						out.Config[f.Acceleration] = v
					} else if v, ok := base[f.Acceleration]; ok {
						out.Config[f.Acceleration] = v
					}
				}
			}
		}
		overrides, sources := map[string]json.RawMessage{}, map[string]string{}
		for _, f := range profileRules.SpeedFeatures {
			for _, row := range print.Scales {
				if row.Key == f.Key {
					raw, _ := json.Marshal(scaleValues([]autoScale{row}, printer.Nozzle, height)[f.Key])
					overrides[f.Key], sources[f.Key] = raw, "legacy formula override"
				}
			}
			if v, ok := print.Values[f.Key]; ok {
				overrides[f.Key], sources[f.Key] = v, "profile"
			}
			if v, ok := in.Project[f.Key]; ok {
				overrides[f.Key], sources[f.Key] = v, "project"
			}
		}
		values, details := resolveDynamicSpeeds(model, base, out.Config, overrides, sources, printer.Nozzle, speedNumber(out.Config, "layer_height", height))
		set(values, "dynamic speed")
		out.Speeds = details
		for k := range values {
			out.engineOverrides[k] = true
		}
	}
	if firstConfigText(out.Config["nonplanar_mode"]) == fastCurvedTops {
		out.Config["nonplanar_mode"] = json.RawMessage(`"` + curvedTops + `"`)
		out.Config["nonplanar_curved_top_layers"] = json.RawMessage(`1`)
		out.Sources["nonplanar_curved_top_layers"] = out.Sources["nonplanar_mode"]
		out.engineOverrides["nonplanar_curved_top_layers"] = true
	}
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
	dictionary := optionData
	if s.cfg.Options != nil {
		dictionary = s.cfg.Options.plain
	}
	vectors := vectorOptions(dictionary)
	for k, v := range out.Config {
		var text string
		if vectors[k] && json.Unmarshal(v, &text) == nil {
			out.Config[k], _ = json.Marshal(strings.Split(text, ","))
		}
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

func vectorOptions(dictionary []byte) map[string]bool {
	var defs map[string]struct {
		Type string `json:"type"`
	}
	json.Unmarshal(dictionary, &defs)
	out := map[string]bool{}
	for key, def := range defs {
		switch strings.TrimPrefix(def.Type, "co") {
		case "Floats", "Ints", "Bools", "Percents", "FloatsOrPercents", "Points":
			out[key] = true
		}
	}
	return out
}
