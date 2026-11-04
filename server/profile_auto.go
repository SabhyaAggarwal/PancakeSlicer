package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
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

var defaultScales = profileRules.DefaultScales

var starterProfiles = []struct {
	id, name string
	intent   map[string]string
	auto     autoProfile
}{
	{"everyday", "Everyday", map[string]string{"nonplanar_mode": "curved_tops"}, autoProfile{Schema: 3, Scales: defaultScales}},
	{"strong", "Strong", map[string]string{"nonplanar_mode": "curved_tops", "wall_loops": "4", "sparse_infill_density": "25%"}, autoProfile{Schema: 3, Scales: defaultScales}},
	{"fine-detail", "Fine detail", map[string]string{"nonplanar_mode": "curved_tops", "outer_wall_speed": "120", "inner_wall_speed": "200", "top_surface_speed": "150"},
		autoProfile{Schema: 3, Scales: defaultScales, PreferredHeight: ptr(0.12)}},
	{"draft", "Draft", map[string]string{"nonplanar_mode": "off", "sparse_infill_density": "10%", "outer_wall_speed": "250", "inner_wall_speed": "350", "sparse_infill_speed": "350", "internal_solid_infill_speed": "300"},
		autoProfile{Schema: 3, Scales: defaultScales, PreferredHeight: ptr(0.28)}},
}

var starterIntent = map[string]string{"wall_loops": "2", "sparse_infill_density": "15%", "sparse_infill_pattern": "grid",
	"seam_position": "aligned", "support_type": "tree(auto)", "support_style": "default"}

func init() {
	if _, err := v4Profiles(seededLibraryV4("")); err != nil {
		panic(fmt.Sprint("starter profiles: ", err))
	}
}
