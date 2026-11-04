package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"math"
	"slices"
	"strings"
)

type worksWithRule struct {
	Schema   int `json:"schema"`
	Printers *struct {
		Models     []string `json:"models"`
		PrinterIDs []string `json:"printerIds"`
	} `json:"printers"`
	Nozzle *struct {
		Sizes []float64 `json:"sizes"`
		Min   *float64  `json:"min"`
		Max   *float64  `json:"max"`
	} `json:"nozzle"`
	Filaments *struct {
		Types      []string `json:"types"`
		ProfileIDs []string `json:"profileIds"`
	} `json:"filaments"`
	Prints *struct {
		ProfileIDs []string `json:"profileIds"`
	} `json:"prints"`
	Exclude []string `json:"exclude"`
}
type printerFacts struct {
	ID, Model string
	Nozzle    float64
}

func nonEmptyStrings(v []string) bool {
	return len(v) > 0 && !slices.Contains(v, "")
}

func parseWorksWith(raw json.RawMessage, typ string) (*worksWithRule, error) {
	invalid := errors.New("Works with must be a schema 2 rule with one non-empty choice per condition")
	var shape map[string]map[string]json.RawMessage
	var top map[string]json.RawMessage
	if json.Unmarshal(raw, &top) != nil {
		return nil, invalid
	}
	shape = map[string]map[string]json.RawMessage{}
	for key, value := range top {
		switch key {
		case "schema", "exclude":
		case "printers", "nozzle", "filaments", "prints":
			var fields map[string]json.RawMessage
			if json.Unmarshal(value, &fields) != nil || fields == nil {
				return nil, invalid
			}
			shape[key] = fields
		default:
			return nil, invalid
		}
	}
	var r worksWithRule
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.DisallowUnknownFields()
	if dec.Decode(&r) != nil || r.Schema != 2 {
		return nil, invalid
	}
	only := func(fields map[string]json.RawMessage, keys ...string) bool {
		if len(fields) != len(keys) {
			return false
		}
		for _, k := range keys {
			if _, ok := fields[k]; !ok {
				return false
			}
		}
		return true
	}
	if f, ok := shape["printers"]; ok && !(only(f, "models") && nonEmptyStrings(r.Printers.Models) || only(f, "printerIds") && nonEmptyStrings(r.Printers.PrinterIDs)) {
		return nil, invalid
	}
	if f, ok := shape["nozzle"]; ok {
		n := r.Nozzle
		sizes := only(f, "sizes") && len(n.Sizes) > 0 && !slices.ContainsFunc(n.Sizes, func(s float64) bool { return !(s > 0) })
		bounds := only(f, "min", "max") && n.Min != nil && n.Max != nil && *n.Min > 0 && *n.Min <= *n.Max
		if !sizes && !bounds {
			return nil, invalid
		}
	}
	if f, ok := shape["filaments"]; ok && (typ == "filament" || !(only(f, "types") && nonEmptyStrings(r.Filaments.Types) || only(f, "profileIds") && nonEmptyStrings(r.Filaments.ProfileIDs))) {
		return nil, invalid
	}
	if f, ok := shape["prints"]; ok && (typ != "filament" || !only(f, "profileIds") || !nonEmptyStrings(r.Prints.ProfileIDs)) {
		return nil, invalid
	}
	if _, ok := top["exclude"]; ok && !nonEmptyStrings(r.Exclude) {
		return nil, invalid
	}
	if typ == "machine" && (r.Printers != nil || r.Nozzle != nil || r.Exclude != nil) {
		return nil, invalid
	}
	return &r, nil
}

func sameMm(a, b float64) bool { return math.Abs(a-b) < 0.0005 }
func (r *worksWithRule) matchesNozzle(nozzle float64) bool {
	n := r.Nozzle
	if n == nil {
		return true
	}
	if len(n.Sizes) > 0 {
		return slices.ContainsFunc(n.Sizes, func(s float64) bool { return sameMm(s, nozzle) })
	}
	return nozzle >= *n.Min-0.0005 && nozzle <= *n.Max+0.0005
}
func (r *worksWithRule) matchesPrinter(p printerFacts) bool {
	if p.ID != "" && slices.Contains(r.Exclude, p.ID) {
		return false
	}
	if c := r.Printers; c != nil {
		if len(c.Models) > 0 && !slices.Contains(c.Models, p.Model) || len(c.PrinterIDs) > 0 && (p.ID == "" || !slices.Contains(c.PrinterIDs, p.ID)) {
			return false
		}
	}
	return r.matchesNozzle(p.Nozzle)
}
func (r *worksWithRule) matchesFilament(id, filamentType string) bool {
	c := r.Filaments
	if c == nil {
		return true
	}
	if len(c.Types) > 0 {
		return slices.ContainsFunc(c.Types, func(t string) bool { return strings.EqualFold(t, filamentType) })
	}
	return id != "" && slices.Contains(c.ProfileIDs, id)
}
func (r *worksWithRule) matchesPrint(id string) bool {
	return r.Prints == nil || id != "" && slices.Contains(r.Prints.ProfileIDs, id)
}

const maxBaseChain = 4

func baseChain(profiles map[string]map[string]json.RawMessage, id string) ([]map[string]json.RawMessage, string) {
	anchor := func(p map[string]json.RawMessage) string {
		var b struct{ Name, Vendor string }
		json.Unmarshal(p["base"], &b)
		return b.Vendor + "\n" + b.Name
	}
	var chain []map[string]json.RawMessage
	seen := map[string]bool{}
	for p := profiles[id]; p != nil; {
		if seen[stringValue(p["id"])] {
			return chain, "profile_base_cycle"
		}
		seen[stringValue(p["id"])] = true
		chain = append(chain, p)
		if len(chain) > maxBaseChain {
			return chain, "profile_base_depth"
		}
		parentID := stringValue(p["parentId"])
		if parentID == "" {
			break
		}
		parent := profiles[parentID]
		if parent == nil || stringValue(parent["type"]) != stringValue(p["type"]) || anchor(parent) != anchor(p) {
			return chain, "profile_base_invalid"
		}
		p = parent
	}
	return chain, ""
}

func chainRule(chain []map[string]json.RawMessage, typ string) *worksWithRule {
	for _, p := range chain {
		if raw, ok := p["applicability"]; ok {
			rule, _ := parseWorksWith(raw, typ)
			return rule
		}
	}
	return nil
}
