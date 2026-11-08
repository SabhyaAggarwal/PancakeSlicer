package main

import (
	"encoding/json"
	"os"
	"slices"
	"testing"
)

func TestProfileSpec(t *testing.T) {
	raw, err := os.ReadFile("profile_spec.json")
	if err != nil {
		t.Fatal(err)
	}
	var spec struct {
		Scales []struct {
			Nozzle, LayerHeight, Clamped float64
			Values                       map[string]string
		}
		ScaleRows []struct {
			Nozzle, LayerHeight float64
			Rows                []autoScale
			Values              map[string]string
		}
		Heights []struct {
			Nozzle, Min, Max float64
			Heights          []float64
		}
	}
	if err := json.Unmarshal(raw, &spec); err != nil {
		t.Fatal(err)
	}
	check := func(rows []autoScale, n, h float64, want map[string]string) {
		got := scaleValues(rows, n, h)
		if len(got) != len(want) {
			t.Errorf("%v/%v: %v, want %v", n, h, got, want)
		}
		for k, v := range want {
			if got[k] != v {
				t.Errorf("%v/%v %s = %q, want %q", n, h, k, got[k], v)
			}
		}
	}
	for _, c := range spec.Scales {
		if got := clampHeight(c.LayerHeight, c.Nozzle); got != c.Clamped {
			t.Errorf("clamp %v/%v = %v, want %v", c.Nozzle, c.LayerHeight, got, c.Clamped)
		}
		check(profileRules.DefaultScales, c.Nozzle, c.LayerHeight, c.Values)
	}
	for _, c := range spec.ScaleRows {
		check(c.Rows, c.Nozzle, c.LayerHeight, c.Values)
	}
	for _, c := range spec.Heights {
		if got := heightsFor(c.Nozzle, c.Min, c.Max); !slices.Equal(got, c.Heights) {
			t.Errorf("heights %v [%v,%v] = %v, want %v", c.Nozzle, c.Min, c.Max, got, c.Heights)
		}
	}
}
