package main

import (
	"bytes"
	"encoding/json"
	"sort"
	"strings"
)

const compactPresetsSchema = "orca.presets.compact.v1"

type compactPreset struct {
	Preset
	Config []int `json:"config"`
	Base   int   `json:"base"`
}

type compactVendor struct {
	Schema    string            `json:"schema"`
	Vendor    string            `json:"vendor"`
	Keys      []string          `json:"keys"`
	Values    []json.RawMessage `json:"values"`
	ValueKeys []int             `json:"value_keys"`
	Bases     [][]int           `json:"bases"`
	Machines  []compactPreset   `json:"machines"`
	Processes []compactPreset   `json:"processes"`
	Filaments []compactPreset   `json:"filaments"`
}

func compactPresets(v *VendorPresets) compactVendor {
	out := compactVendor{Schema: compactPresetsSchema, Vendor: v.Vendor, Keys: []string{}, Values: []json.RawMessage{}, ValueKeys: []int{}, Bases: [][]int{}}
	keys := map[string]int{}
	values := map[string]int{}
	pair := func(key string, raw json.RawMessage) (int, int) {
		k, ok := keys[key]
		if !ok {
			k = len(out.Keys)
			keys[key] = k
			out.Keys = append(out.Keys, key)
		}
		id := key + "\x00" + string(raw)
		i, ok := values[id]
		if !ok {
			i = len(out.Values)
			values[id] = i
			out.Values = append(out.Values, raw)
			out.ValueKeys = append(out.ValueKeys, k)
		}
		return k, i
	}
	kind := func(presets []Preset) []compactPreset {
		packed := make([]compactPreset, len(presets))
		groups := map[string][]int{}
		var order []string
		for i, p := range presets {
			names := make([]string, 0, len(p.Config))
			for key := range p.Config {
				names = append(names, key)
			}
			sort.Strings(names)
			set := strings.Join(names, "\x00")
			if _, ok := groups[set]; !ok {
				order = append(order, set)
			}
			groups[set] = append(groups[set], i)
		}
		for _, set := range order {
			members := groups[set]
			names := []string{}
			if set != "" {
				names = strings.Split(set, "\x00")
			}
			base := make(map[string]json.RawMessage, len(names))
			pairs := make([]int, 0, 2*len(names))
			for _, key := range names {
				counts := map[string]int{}
				best, bestCount := json.RawMessage(nil), 0
				for _, i := range members {
					raw := presets[i].Config[key]
					counts[string(raw)]++
					if c := counts[string(raw)]; c > bestCount {
						best, bestCount = raw, c
					}
				}
				base[key] = best
				k, v := pair(key, best)
				pairs = append(pairs, k, v)
			}
			index := len(out.Bases)
			out.Bases = append(out.Bases, pairs)
			for _, i := range members {
				p := presets[i]
				diff := []int{}
				for _, key := range names {
					if raw := p.Config[key]; !bytes.Equal(raw, base[key]) {
						k, v := pair(key, raw)
						diff = append(diff, k, v)
					}
				}
				p.Config = nil
				packed[i] = compactPreset{Preset: p, Config: diff, Base: index}
			}
		}
		return packed
	}
	out.Machines, out.Processes, out.Filaments = kind(v.Machines), kind(v.Processes), kind(v.Filaments)
	for i := range out.Processes {
		out.Processes[i].Internal = true
	}
	return out
}
