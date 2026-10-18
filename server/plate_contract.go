package main

import (
	"archive/zip"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"strings"
	"time"
)

func validatePlatePlacement(raw []byte, saving, native bool) error {
	type placement struct {
		Printable *bool   `json:"printable"`
		Z         float64 `json:"z"`
	}
	var plate struct {
		Objects []struct {
			placement
			Instances []placement `json:"instances"`
		} `json:"objects"`
	}
	if err := json.Unmarshal(raw, &plate); err != nil {
		return err
	}
	live := 0
	for _, object := range plate.Objects {
		places := object.Instances
		if places == nil {
			places = []placement{object.placement}
		}
		if len(places) == 0 {
			return fmt.Errorf("instances must be a non-empty array")
		}
		for _, p := range places {
			printable := p.Printable == nil || *p.Printable
			if !native && (!printable || p.Z != 0) {
				return fmt.Errorf("plate z and printable:false require the R238 tool (native plate placement)")
			}
			if printable {
				live++
			}
		}
	}
	if !saving && live == 0 {
		return fmt.Errorf("plate has no printable instances")
	}
	return nil
}

func probePlatePlacement(c Config) error {
	dir, err := os.MkdirTemp("", "orca-placement-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(dir)
	files := map[string]string{
		"probe.obj":  "v 0 0 0\nv 4 0 0\nv 0 4 0\nv 0 0 4\nf 1 3 2\nf 1 2 4\nf 2 3 4\nf 3 1 4\n",
		"plate.json": `{"objects":[{"file":"probe.obj","instances":[{"x":43,"y":57,"z":-2},{"x":83,"y":57,"z":-2,"printable":false}]}]}`,
	}
	for name, data := range files {
		if err := os.WriteFile(filepath.Join(dir, name), []byte(data), 0600); err != nil {
			return err
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	save := func(input []string, output string) error {
		args := []string{"--profiles", filepath.Join(c.Resources, "profiles"), "--datadir", filepath.Join(dir, "data"), "--vendor", "BBL", "--printer", "Bambu Lab A1 mini 0.4 nozzle", "--process", "0.20mm Non-planar top @BBL A1M", "--filament", "Bambu PLA Basic @BBL A1M", "--save-3mf", output}
		cmd := exec.CommandContext(ctx, c.Tool, append(args, input...)...)
		configureKill(cmd)
		cmd.WaitDelay = 5 * time.Second
		if out, err := cmd.CombinedOutput(); err != nil {
			return fmt.Errorf("placement probe: %w: %.500s", err, out)
		}
		return nil
	}
	first, second := filepath.Join(dir, "first.3mf"), filepath.Join(dir, "second.3mf")
	if err := save([]string{"--plate", filepath.Join(dir, "plate.json")}, first); err != nil {
		return err
	}
	instances, err := savedProjectInstances(first)
	if err != nil {
		return err
	}
	if len(instances) != 2 || instances[1].Printable == nil || *instances[1].Printable {
		return fmt.Errorf("tool ignores plate printable:false")
	}
	before, err := projectBounds(first, true)
	if err != nil {
		return err
	}
	if len(before) != 2 || math.Abs(before[0].box.min[2]+2) > 1e-5 {
		return fmt.Errorf("tool ignores plate z")
	}
	if err := save([]string{first}, second); err != nil {
		return err
	}
	after, err := projectBounds(second, true)
	if err != nil {
		return err
	}
	if len(after) != len(before) {
		return fmt.Errorf("tool changes saved instance count")
	}
	for i := range before {
		for axis := 0; axis < 3; axis++ {
			if math.Abs(before[i].box.min[axis]-after[i].box.min[axis]) > 1e-5 || math.Abs(before[i].box.max[axis]-after[i].box.max[axis]) > 1e-5 {
				return fmt.Errorf("tool changes loaded placement on save")
			}
		}
	}
	identities := func(path string) (string, error) {
		archive, err := zip.OpenReader(path)
		if err != nil {
			return "", err
		}
		defer archive.Close()
		for _, part := range archive.File {
			if !strings.EqualFold(part.Name, "Metadata/model_settings.config") {
				continue
			}
			r, err := part.Open()
			if err != nil {
				return "", err
			}
			data, err := io.ReadAll(io.LimitReader(r, 1<<20))
			r.Close()
			if err != nil {
				return "", err
			}
			ids := regexp.MustCompile(`<metadata key="identify_id" value="[^"]*"`).FindAllString(string(data), -1)
			if len(ids) != 2 {
				return "", fmt.Errorf("missing native instance identities")
			}
			return strings.Join(ids, "\n"), nil
		}
		return "", fmt.Errorf("missing native instance metadata")
	}
	a, err := identities(first)
	if err != nil {
		return err
	}
	b, err := identities(second)
	if err != nil {
		return err
	}
	if a != b {
		return fmt.Errorf("tool changes loaded identify IDs on save")
	}
	return nil
}
