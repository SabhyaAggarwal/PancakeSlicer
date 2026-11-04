package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"slices"
)

type autoProfile struct {
	Schema          int      `json:"schema"`
	PreferredHeight *float64 `json:"preferredHeight,omitempty"`
	PreferredSpeed  string   `json:"preferredSpeed,omitempty"`
}

func validateAuto(p map[string]json.RawMessage, typ, kind string) error {
	raw, has := p["auto"]
	if typ != "process" {
		if has {
			return errors.New("auto belongs to print profiles only")
		}
		return nil
	}
	if !has {
		if kind == "profile" {
			return errors.New("every print profile needs its auto field")
		}
		return nil
	}
	var a autoProfile
	if json.Unmarshal(raw, &a) != nil || a.Schema != 1 {
		return errors.New("auto must be {schema:1, preferredHeight?, preferredSpeed?}")
	}
	if a.PreferredHeight != nil && (*a.PreferredHeight < 0.04 || *a.PreferredHeight > 1.2) {
		return errors.New("preferredHeight is a layer height from 0.04 to 1.2 mm")
	}
	if a.PreferredSpeed != "" && !slices.Contains([]string{"quality", "balanced", "fast"}, a.PreferredSpeed) {
		return errors.New("preferredSpeed is quality, balanced or fast")
	}
	return nil
}

var starterProfiles = []struct {
	id, name string
	intent   map[string]string
	auto     autoProfile
}{
	{"everyday", "Everyday", map[string]string{}, autoProfile{Schema: 1}},
	{"strong", "Strong", map[string]string{"wall_loops": "4", "sparse_infill_density": "25%", "top_shell_thickness": "1.2", "bottom_shell_thickness": "1"}, autoProfile{Schema: 1}},
	{"fine-detail", "Fine detail", map[string]string{}, autoProfile{Schema: 1, PreferredHeight: ptr(0.12), PreferredSpeed: "quality"}},
	{"draft", "Draft", map[string]string{"sparse_infill_density": "10%", "top_shell_thickness": "0.8", "bottom_shell_thickness": "0.4"}, autoProfile{Schema: 1, PreferredHeight: ptr(0.28), PreferredSpeed: "fast"}},
}

var starterIntent = map[string]string{"wall_loops": "2", "sparse_infill_density": "15%", "sparse_infill_pattern": "grid", "top_shell_thickness": "1", "bottom_shell_thickness": "0.6",
	"seam_position": "aligned", "support_type": "tree(auto)", "support_style": "default"}

func seededLibrary(epoch string) profileLibraryDocument {
	profiles := []map[string]any{}
	for _, s := range starterProfiles {
		overlay := map[string]string{}
		for k, v := range starterIntent {
			overlay[k] = v
		}
		for k, v := range s.intent {
			overlay[k] = v
		}
		profiles = append(profiles, map[string]any{"id": "profile:starter-" + s.id, "type": "process", "kind": "profile", "name": s.name, "revision": 1,
			"base": map[string]string{"name": "Auto", "version": "auto:1", "engineName": "Auto"}, "baseline": map[string]string{}, "overlay": overlay, "auto": s.auto})
	}
	doc := profileLibraryDocument{"schema": json.RawMessage(`1`), "revision": json.RawMessage(`1`), "printers": json.RawMessage(`{"schema":1,"revision":0,"setups":{}}`)}
	doc["profiles"], _ = json.Marshal(profiles)
	if epoch != "" {
		doc["epoch"], _ = json.Marshal(epoch)
	}
	return doc
}

func init() {
	if _, err := libraryProfiles(seededLibrary("")); err != nil {
		panic(fmt.Sprint("starter profiles: ", err))
	}
}
