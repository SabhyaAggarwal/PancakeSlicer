package main

import (
	"encoding/json"
	"errors"
	"math"
	"regexp"
	"slices"
	"strconv"
	"strings"
	"sync"
)

var printDefaultKeys = profileRules.PrintDefaultKeys

var processKeys = struct {
	sync.RWMutex
	set map[string]bool
}{set: map[string]bool{}}

func noteProcessKeys(v *VendorPresets) {
	processKeys.Lock()
	defer processKeys.Unlock()
	for _, p := range v.Processes {
		for key := range p.Config {
			processKeys.set[key] = true
		}
	}
}

func validatePrintOverrides(p map[string]json.RawMessage, typ string) error {
	raw, has := p["printOverrides"]
	if !has {
		return nil
	}
	if typ != "machine" {
		return errors.New("printOverrides belong to printer profiles only")
	}
	var values map[string]json.RawMessage
	if json.Unmarshal(raw, &values) != nil || values == nil {
		return errors.New("printOverrides is an object of print settings")
	}
	if len(values) > 256 {
		return errors.New("printOverrides holds at most 256 settings")
	}
	processKeys.RLock()
	defer processKeys.RUnlock()
	for key := range values {
		if key == "layer_height" || !settingKey.MatchString(key) || len(processKeys.set) > 0 && !processKeys.set[key] {
			return errors.New("printOverrides." + key + " is not a print setting a printer can set")
		}
	}
	return nil
}

var standardPreset = regexp.MustCompile(`^\d+(\.\d+)?mm Standard\b`)

func settingText(raw json.RawMessage) string {
	var v any
	if json.Unmarshal(raw, &v) != nil {
		return string(raw)
	}
	var parts []string
	var walk func(any)
	walk = func(x any) {
		switch t := x.(type) {
		case []any:
			for _, e := range t {
				walk(e)
			}
		case string:
			for _, s := range strings.Split(t, ",") {
				parts = append(parts, strings.TrimSpace(s))
			}
		default:
			parts = append(parts, strings.TrimSpace(strings.Trim(string(must(json.Marshal(t))), `"`)))
		}
	}
	walk(v)
	for i, s := range parts {
		if n, err := strconv.ParseFloat(strings.TrimSuffix(s, "%"), 64); err == nil {
			parts[i] = strconv.FormatFloat(n, 'g', -1, 64)
			if strings.HasSuffix(s, "%") {
				parts[i] += "%"
			}
		}
	}
	return strings.Join(parts, ",")
}
func must(b []byte, _ error) []byte { return b }

func setPrintDefaults(v *VendorPresets, defaults map[string]json.RawMessage) {
	for i := range v.Machines {
		m := &v.Machines[i]
		nozzle, _ := strconv.ParseFloat(firstConfigText(m.Config["nozzle_diameter"]), 64)
		var best *Preset
		bestGap := math.Inf(1)
		for j := range v.Processes {
			p := &v.Processes[j]
			if !standardPreset.MatchString(p.Name) || !compatibleWith(p, m.Name) {
				continue
			}
			height, _ := strconv.ParseFloat(firstConfigText(p.Config["layer_height"]), 64)
			if gap := math.Abs(height - nozzle/2); gap < bestGap || gap == bestGap && best != nil && p.Name < best.Name {
				best, bestGap = p, gap
			}
		}
		if best == nil {
			continue
		}
		out := map[string]json.RawMessage{}
		for _, key := range printDefaultKeys {
			value, ok := best.Config[key]
			if ok && (defaults[key] == nil || settingText(value) != settingText(defaults[key])) {
				out[key] = value
			}
		}
		if len(out) > 0 {
			m.PrintDefaults = out
		}
	}
}

func setLayerHeights(v *VendorPresets) {
	for i := range v.Machines {
		m := &v.Machines[i]
		seen := map[float64]bool{}
		var heights []float64
		for j := range v.Processes {
			p := &v.Processes[j]
			if !compatibleWith(p, m.Name) {
				continue
			}
			height, err := strconv.ParseFloat(firstConfigText(p.Config["layer_height"]), 64)
			if err != nil || !(height > 0) {
				continue
			}
			height = math.Round(height*1000) / 1000
			if !seen[height] {
				seen[height] = true
				heights = append(heights, height)
			}
		}
		slices.Sort(heights)
		m.LayerHeights = heights
	}
}
func compatibleWith(p *Preset, machine string) bool {
	if len(p.CompatiblePrinters) == 0 {
		return !p.Evaluated
	}
	return slices.Contains(p.CompatiblePrinters, machine)
}

func optionDefaults(dictionary []byte) map[string]json.RawMessage {
	var defs map[string]struct {
		Default json.RawMessage `json:"default"`
	}
	json.Unmarshal(dictionary, &defs)
	out := map[string]json.RawMessage{}
	for key, def := range defs {
		out[key] = def.Default
	}
	return out
}
