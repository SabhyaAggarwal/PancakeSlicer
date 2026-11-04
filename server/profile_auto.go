package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"regexp"
)

type autoScale struct {
	Key    string  `json:"key"`
	Basis  string  `json:"basis"`
	Factor float64 `json:"factor"`
}
type autoProfile struct {
	Schema          int         `json:"schema"`
	Scales          []autoScale `json:"scales"`
	PreferredHeight *float64    `json:"preferredHeight,omitempty"`
}

var settingKeyShape = regexp.MustCompile(`^[a-z][a-z0-9_]{0,63}$`)

func validateAuto(p map[string]json.RawMessage, typ string) error {
	raw, has := p["auto"]
	if _, linked := p["parentId"]; linked && typ == "process" {
		return errors.New("a print profile has no base profile: duplicate is a plain copy")
	}
	if !has {
		return nil
	}
	if typ != "process" {
		return errors.New("auto belongs to print profiles only")
	}
	var a autoProfile
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.DisallowUnknownFields()
	if dec.Decode(&a) != nil || a.Schema != 3 || a.Scales == nil {
		return errors.New("auto must be {schema:3, scales, preferredHeight?}")
	}
	if len(a.Scales) > 64 {
		return errors.New("auto.scales holds at most 64 rows")
	}
	seen := map[string]bool{}
	for _, row := range a.Scales {
		if !settingKeyShape.MatchString(row.Key) || seen[row.Key] || (row.Basis != "nozzle" && row.Basis != "layer" && row.Basis != "perLayer") || !(row.Factor > 0) || row.Factor > 1000 {
			return fmt.Errorf("auto.scales row %q needs a setting key used once, a basis of nozzle, layer or perLayer, and a factor above 0 and at most 1000", row.Key)
		}
		seen[row.Key] = true
	}
	if a.PreferredHeight != nil && (*a.PreferredHeight < 0.04 || *a.PreferredHeight > 1.2) {
		return errors.New("preferredHeight is a layer height from 0.04 to 1.2 mm")
	}
	return nil
}

func isDynamic(p map[string]json.RawMessage) bool {
	var a autoProfile
	return json.Unmarshal(p["auto"], &a) == nil && len(a.Scales) > 0
}

var defaultScales = func() []autoScale {
	rows := []autoScale{{"outer_wall_line_width", "nozzle", 1}}
	for _, key := range []string{"line_width", "inner_wall_line_width", "top_surface_line_width", "sparse_infill_line_width", "internal_solid_infill_line_width", "support_line_width"} {
		rows = append(rows, autoScale{key, "nozzle", 1.125})
	}
	return append(rows, autoScale{"initial_layer_line_width", "nozzle", 1.25}, autoScale{"initial_layer_print_height", "nozzle", 0.5},
		autoScale{"support_top_z_distance", "layer", 1}, autoScale{"support_bottom_z_distance", "layer", 1},
		autoScale{"top_shell_layers", "perLayer", 1}, autoScale{"bottom_shell_layers", "perLayer", 0.6})
}()

var starterProfiles = []struct {
	id, name string
	intent   map[string]string
	auto     autoProfile
}{
	{"everyday", "Everyday", map[string]string{}, autoProfile{Schema: 3, Scales: defaultScales}},
	{"strong", "Strong", map[string]string{"wall_loops": "4", "sparse_infill_density": "25%"}, autoProfile{Schema: 3, Scales: defaultScales}},
	{"fine-detail", "Fine detail", map[string]string{"outer_wall_speed": "120", "inner_wall_speed": "200", "top_surface_speed": "150"},
		autoProfile{Schema: 3, Scales: defaultScales, PreferredHeight: ptr(0.12)}},
	{"draft", "Draft", map[string]string{"sparse_infill_density": "10%", "outer_wall_speed": "250", "inner_wall_speed": "350", "sparse_infill_speed": "350", "internal_solid_infill_speed": "300"},
		autoProfile{Schema: 3, Scales: defaultScales, PreferredHeight: ptr(0.28)}},
}

var starterIntent = map[string]string{"wall_loops": "2", "sparse_infill_density": "15%", "sparse_infill_pattern": "grid",
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

func upgradeAutos(doc profileLibraryDocument) int {
	var profiles []map[string]json.RawMessage
	if json.Unmarshal(doc["profiles"], &profiles) != nil {
		return 0
	}
	changed := 0
	for _, p := range profiles {
		raw, ok := p["auto"]
		if !ok || stringValue(p["type"]) != "process" {
			continue
		}
		var old struct {
			Schema          int      `json:"schema"`
			Dynamic         *bool    `json:"dynamic"`
			PreferredHeight *float64 `json:"preferredHeight"`
		}
		if json.Unmarshal(raw, &old) != nil || old.Schema == 3 {
			continue
		}
		changed++
		if old.Schema == 2 && old.Dynamic != nil && !*old.Dynamic {
			delete(p, "auto")
			continue
		}
		next := autoProfile{Schema: 3, Scales: defaultScales}
		if h := old.PreferredHeight; h != nil && *h >= 0.04 && *h <= 1.2 {
			next.PreferredHeight = h
		}
		p["auto"], _ = json.Marshal(next)
	}
	if changed > 0 {
		doc["profiles"], _ = json.Marshal(profiles)
	}
	return changed
}

func (s *ProfileLibraryStore) upgradeStoredAutos() error {
	envelope, err := s.readLibrary()
	if err != nil {
		return err
	}
	n := upgradeAutos(envelope.Document)
	if n == 0 {
		return nil
	}
	envelope.Document["revision"] = json.RawMessage(fmt.Sprint(libraryRevision(envelope.Document) + 1))
	encoded, err := json.Marshal(envelope)
	if err == nil {
		err = s.writeLibrary(encoded)
	}
	if err == nil {
		log.Printf("profile library: rewrote %d print profiles' auto as schema 3", n)
	}
	return err
}
