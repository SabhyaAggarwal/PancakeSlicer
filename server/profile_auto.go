package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
)

type autoProfile struct {
	Schema          int                `json:"schema"`
	Dynamic         *bool              `json:"dynamic"`
	Rules           map[string]float64 `json:"rules,omitempty"`
	PreferredHeight *float64           `json:"preferredHeight,omitempty"`
}

var ruleID = regexp.MustCompile(`^[a-z][a-z0-9_]{0,63}$`)

func validateAuto(p map[string]json.RawMessage, typ, kind string) error {
	raw, has := p["auto"]
	if _, linked := p["parentId"]; linked && typ == "process" {
		return errors.New("a print profile has no base profile: duplicate is a plain copy")
	}
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
	if json.Unmarshal(raw, &a) != nil || a.Schema != 2 || a.Dynamic == nil {
		return errors.New("auto must be {schema:2, dynamic, rules?, preferredHeight?}")
	}
	if len(a.Rules) > 64 {
		return errors.New("auto.rules holds at most 64 rules")
	}
	for id, coefficient := range a.Rules {
		if !ruleID.MatchString(id) || !(coefficient > 0) || coefficient > 1000 {
			return fmt.Errorf("auto.rules.%s must be a positive coefficient for a rule id", id)
		}
	}
	if a.PreferredHeight != nil && (*a.PreferredHeight < 0.04 || *a.PreferredHeight > 1.2) {
		return errors.New("preferredHeight is a layer height from 0.04 to 1.2 mm")
	}
	return nil
}

func isDynamic(p map[string]json.RawMessage) bool {
	var a autoProfile
	return json.Unmarshal(p["auto"], &a) == nil && a.Dynamic != nil && *a.Dynamic
}

var starterProfiles = []struct {
	id, name string
	intent   map[string]string
	auto     autoProfile
}{
	{"everyday", "Everyday", map[string]string{}, autoProfile{Schema: 2, Dynamic: ptr(true)}},
	{"strong", "Strong", map[string]string{"wall_loops": "4", "sparse_infill_density": "25%", "top_shell_thickness": "1.2", "bottom_shell_thickness": "1"}, autoProfile{Schema: 2, Dynamic: ptr(true)}},
	{"fine-detail", "Fine detail", map[string]string{}, autoProfile{Schema: 2, Dynamic: ptr(true), PreferredHeight: ptr(0.12), Rules: map[string]float64{"speed": 0.7}}},
	{"draft", "Draft", map[string]string{"sparse_infill_density": "10%", "top_shell_thickness": "0.8", "bottom_shell_thickness": "0.4"}, autoProfile{Schema: 2, Dynamic: ptr(true), PreferredHeight: ptr(0.28), Rules: map[string]float64{"speed": 1.3}}},
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
