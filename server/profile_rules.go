package main

import (
	"bytes"
	"encoding/json"
	"errors"
	"maps"
	"math"
	"slices"
	"sort"
	"strconv"
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

func worksWithFromSchema1(raw json.RawMessage) (json.RawMessage, bool) {
	var a struct {
		Schema     int      `json:"schema"`
		Mode       string   `json:"mode"`
		PrinterIDs []string `json:"printerIds"`
	}
	if json.Unmarshal(raw, &a) != nil || a.Schema != 1 {
		return nil, false
	}
	if a.Mode == "only" && len(a.PrinterIDs) > 0 {
		out, _ := json.Marshal(map[string]any{"schema": 2, "printers": map[string]any{"printerIds": a.PrinterIDs}})
		return out, true
	}
	return nil, true
}

type dynamicWhen struct {
	PrinterModel *string  `json:"printerModel,omitempty"`
	Nozzle       *float64 `json:"nozzle,omitempty"`
	LayerHeight  *float64 `json:"layerHeight,omitempty"`
}
type dynamicLayer struct {
	ID     string                     `json:"id"`
	When   dynamicWhen                `json:"when"`
	Values map[string]json.RawMessage `json:"values"`
}
type dynamicProfile struct {
	Schema    int `json:"schema"`
	Reference struct {
		Nozzle      float64 `json:"nozzle"`
		LayerHeight float64 `json:"layerHeight"`
	} `json:"reference"`
	Layers []dynamicLayer `json:"layers"`
}
type dynamicContext struct {
	PrinterModel string  `json:"printerModel"`
	Nozzle       float64 `json:"nozzle"`
	LayerHeight  float64 `json:"layerHeight"`
	PrinterID    string  `json:"printerId,omitempty"`
}

var lineWidthKeys = []string{"line_width", "initial_layer_line_width", "outer_wall_line_width", "inner_wall_line_width",
	"top_surface_line_width", "sparse_infill_line_width", "internal_solid_infill_line_width", "support_line_width",
	"bridge_line_width", "skin_infill_line_width", "skeleton_infill_line_width"}

func parseDynamic(raw json.RawMessage, typ string) (*dynamicProfile, error) {
	invalid := func(why string) error { return errors.New("dynamic profile: " + why) }
	var d dynamicProfile
	dec := json.NewDecoder(bytes.NewReader(raw))
	dec.DisallowUnknownFields()
	if dec.Decode(&d) != nil || d.Schema != 1 || d.Layers == nil {
		return nil, invalid("schema 1 with reference and layers is required")
	}
	if !(d.Reference.Nozzle > 0) || !(d.Reference.LayerHeight > 0) {
		return nil, invalid("reference nozzle and layer height must be positive")
	}
	ids := map[string]bool{}
	for i, l := range d.Layers {
		if !strings.HasPrefix(l.ID, "layer:") || len(l.ID) <= 6 || ids[l.ID] {
			return nil, invalid("every layer needs its own layer: ID")
		}
		ids[l.ID] = true
		w := l.When
		if w.PrinterModel == nil && w.Nozzle == nil && w.LayerHeight == nil || w.PrinterModel != nil && *w.PrinterModel == "" ||
			w.Nozzle != nil && !(*w.Nozzle > 0) || w.LayerHeight != nil && !(*w.LayerHeight > 0) {
			return nil, invalid("a layer names at least one valid axis")
		}
		if w.LayerHeight != nil && typ != "process" || w.PrinterModel != nil && typ == "machine" {
			return nil, invalid("layer height applies to print profiles and printer model to print and filament profiles")
		}
		if l.Values == nil || len(l.Values) == 0 && w.LayerHeight == nil {
			return nil, invalid("a layer holds at least one value unless it offers a layer height")
		}
		for _, other := range d.Layers[:i] {
			if sameWhen(other.When, w) {
				return nil, invalid("two layers have the same condition")
			}
		}
	}
	return &d, nil
}

func sameWhen(a, b dynamicWhen) bool { return covers(a, b) && covers(b, a) }

func covers(a, b dynamicWhen) bool {
	return (b.PrinterModel == nil || a.PrinterModel != nil && *a.PrinterModel == *b.PrinterModel) &&
		(b.Nozzle == nil || a.Nozzle != nil && sameMm(*a.Nozzle, *b.Nozzle)) &&
		(b.LayerHeight == nil || a.LayerHeight != nil && sameMm(*a.LayerHeight, *b.LayerHeight))
}
func contextWhen(ctx dynamicContext, typ string) dynamicWhen {
	w := dynamicWhen{PrinterModel: &ctx.PrinterModel, Nozzle: &ctx.Nozzle}
	if typ == "process" {
		w.LayerHeight = &ctx.LayerHeight
	}
	return w
}

func rank(w dynamicWhen) int {
	var named []int
	if w.Nozzle != nil {
		named = append(named, 3)
	}
	if w.LayerHeight != nil {
		named = append(named, 2)
	}
	if w.PrinterModel != nil {
		named = append(named, 1)
	}
	r := 0
	for _, p := range named {
		r = r*4 + p
	}
	return r + len(named)*1000
}
func round3(n float64) float64  { return math.Round(n*1000) / 1000 }
func formatMm(n float64) string { return strconv.FormatFloat(round3(n), 'f', -1, 64) }

func (d *dynamicProfile) resolve(typ string, ctx dynamicContext, below map[string]json.RawMessage) map[string]json.RawMessage {
	layers := slices.Clone(d.Layers)
	sort.SliceStable(layers, func(i, j int) bool { return rank(layers[i].When) < rank(layers[j].When) })
	here := contextWhen(ctx, typ)
	out, source := map[string]json.RawMessage{}, map[string]dynamicWhen{}
	for _, l := range layers {
		if covers(here, l.When) {
			for k, v := range l.Values {
				out[k], source[k] = v, l.When
			}
		}
	}
	if typ != "process" {
		return out
	}
	out["layer_height"], _ = json.Marshal(formatMm(ctx.LayerHeight))
	if sameMm(ctx.Nozzle, d.Reference.Nozzle) {
		return out
	}
	for _, key := range lineWidthKeys {
		if w, ok := source[key]; ok && w.Nozzle != nil {
			continue
		}
		value, ok := out[key]
		if !ok {
			value = below[key]
		}
		var number float64
		var text string
		isText := json.Unmarshal(value, &text) == nil
		if isText {
			if strings.HasSuffix(strings.TrimSpace(text), "%") {
				continue
			}
			parsed, err := strconv.ParseFloat(strings.TrimSpace(text), 64)
			if err != nil {
				continue
			}
			number = parsed
		} else if json.Unmarshal(value, &number) != nil {
			continue
		}
		if !(number > 0) {
			continue
		}
		scaled := round3(number * ctx.Nozzle / d.Reference.Nozzle)
		if isText {
			out[key], _ = json.Marshal(strconv.FormatFloat(scaled, 'f', -1, 64))
		} else {
			out[key], _ = json.Marshal(scaled)
		}
	}
	return out
}

func (d *dynamicProfile) availableHeights(rule *worksWithRule, ctx dynamicContext) []float64 {
	if !(ctx.Nozzle > 0) || rule != nil && !rule.matchesPrinter(printerFacts{ctx.PrinterID, ctx.PrinterModel, ctx.Nozzle}) {
		return nil
	}
	var heights []float64
	add := func(h float64, declared bool) {
		if (declared || h >= 0.25*ctx.Nozzle-0.0005 && h <= 0.75*ctx.Nozzle+0.0005) && !slices.ContainsFunc(heights, func(x float64) bool { return sameMm(x, h) }) {
			heights = append(heights, round3(h))
		}
	}
	add(d.Reference.LayerHeight, sameMm(d.Reference.Nozzle, ctx.Nozzle))
	here := dynamicWhen{PrinterModel: &ctx.PrinterModel, Nozzle: &ctx.Nozzle}
	for _, l := range d.Layers {
		if l.When.LayerHeight != nil {
			w := l.When
			w.LayerHeight = nil
			if covers(here, w) {
				add(*l.When.LayerHeight, l.When.Nozzle != nil)
			}
		}
	}
	slices.Sort(heights)
	return heights
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

func chainDynamic(chain []map[string]json.RawMessage, typ string) *dynamicProfile {
	var merged *dynamicProfile
	for i := len(chain) - 1; i >= 0; i-- {
		raw, ok := chain[i]["dynamic"]
		if !ok {
			continue
		}
		d, err := parseDynamic(raw, typ)
		if err != nil {
			continue
		}
		if merged == nil {
			merged = &dynamicProfile{Schema: 1}
		}
		merged.Reference = d.Reference
		for _, l := range d.Layers {
			at := slices.IndexFunc(merged.Layers, func(m dynamicLayer) bool { return sameWhen(m.When, l.When) })
			if at < 0 {
				merged.Layers = append(merged.Layers, dynamicLayer{ID: l.ID, When: l.When, Values: maps.Clone(l.Values)})
				continue
			}
			for k, v := range l.Values {
				merged.Layers[at].Values[k] = v
			}
		}
	}
	return merged
}

type chainLink struct {
	Overlay map[string]json.RawMessage `json:"overlay"`
	Dynamic *dynamicProfile            `json:"dynamic"`
}

func resolveChain(links []chainLink, typ string, ctx dynamicContext, below map[string]json.RawMessage) map[string]json.RawMessage {
	out, nozzleSourced := map[string]json.RawMessage{}, map[string]bool{}
	var reference *dynamicProfile
	for _, link := range links {
		for k, v := range link.Overlay {
			out[k], nozzleSourced[k] = v, false
		}
		if link.Dynamic == nil {
			continue
		}
		reference = link.Dynamic
		layers := slices.Clone(link.Dynamic.Layers)
		sort.SliceStable(layers, func(i, j int) bool { return rank(layers[i].When) < rank(layers[j].When) })
		here := contextWhen(ctx, typ)
		for _, l := range layers {
			if covers(here, l.When) {
				for k, v := range l.Values {
					out[k], nozzleSourced[k] = v, l.When.Nozzle != nil
				}
			}
		}
	}
	if reference == nil || typ != "process" {
		return out
	}
	base := maps.Clone(below)
	maps.Copy(base, out)
	derived := (&dynamicProfile{Reference: reference.Reference}).resolve(typ, ctx, base)
	for k, v := range derived {
		if !nozzleSourced[k] {
			out[k] = v
		}
	}
	return out
}
