package main

import (
	_ "embed"
	"encoding/json"
	"math"
	"strconv"
	"strings"
)

//go:embed profile_rules.json
var profileRulesData []byte

var profileRules = func() (r struct {
	Round             float64              `json:"round"`
	MaxLayerOfNozzle  float64              `json:"maxLayerOfNozzle"`
	LineWidthOfNozzle [2]float64           `json:"lineWidthOfNozzle"`
	DefaultScales     []autoScale          `json:"defaultScales"`
	PrintDefaultKeys  []string             `json:"printDefaultKeys"`
	SourceOrder       []string             `json:"sourceOrder"`
	NotScalable       []string             `json:"notScalable"`
	SpeedFeatures     []speedFeature       `json:"speedFeatures"`
	SpeedSeeds        map[string]speedSeed `json:"speedIntents"`
	Heights           struct {
		Step, MinOfNozzle, MinFloor, MaxOfNozzle float64
	} `json:"heights"`
}) {
	if err := json.Unmarshal(profileRulesData, &r); err != nil || r.Round == 0 || len(r.DefaultScales) == 0 {
		panic("profile_rules.json is invalid")
	}
	return r
}()

func roundRule(x float64) float64 { return math.Round(x*profileRules.Round) / profileRules.Round }

func clampHeight(layerHeight, nozzle float64) float64 {
	if nozzle > 0 {
		return math.Min(layerHeight, roundRule(nozzle*profileRules.MaxLayerOfNozzle))
	}
	return layerHeight
}
func isLineWidth(key string) bool {
	return key == "line_width" || strings.HasSuffix(key, "_line_width")
}

func scaledValue(row autoScale, nozzle, layerHeight float64) float64 {
	h := clampHeight(layerHeight, nozzle)
	switch row.Basis {
	case "nozzle":
		v := row.Factor * nozzle
		if isLineWidth(row.Key) {
			v = math.Min(profileRules.LineWidthOfNozzle[1]*nozzle, math.Max(profileRules.LineWidthOfNozzle[0]*nozzle, v))
		}
		return roundRule(v)
	case "layer":
		v := row.Factor * h
		if isLineWidth(row.Key) {
			v = math.Min(profileRules.LineWidthOfNozzle[1]*nozzle, math.Max(profileRules.LineWidthOfNozzle[0]*nozzle, v))
		}
		return roundRule(v)
	}
	return math.Ceil(row.Factor/h - 1e-6)
}

func scaleValues(rows []autoScale, nozzle, layerHeight float64) map[string]string {
	out := map[string]string{}
	if !(nozzle > 0) || !(layerHeight > 0) {
		return out
	}
	for _, row := range rows {
		out[row.Key] = strconv.FormatFloat(scaledValue(row, nozzle, layerHeight), 'f', -1, 64)
	}
	return out
}

func heightsFor(nozzle, min, max float64) []float64 {
	hr := profileRules.Heights
	lo := min
	if !(lo > 0) {
		lo = math.Max(hr.MinFloor, math.Round(nozzle*hr.MinOfNozzle*100)/100)
	}
	hi := nozzle * hr.MaxOfNozzle
	if max > 0 {
		hi = max
	}
	hi = math.Min(hi, nozzle*profileRules.MaxLayerOfNozzle)
	out := []float64{}
	for h := lo; h <= hi+1e-6; h += hr.Step {
		out = append(out, math.Round(h*100)/100)
	}
	if len(out) == 0 {
		out = append(out, lo)
	}
	return out
}
