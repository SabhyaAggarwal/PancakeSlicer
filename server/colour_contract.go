package main

import (
	"context"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func probeColourChange(c Config) map[string]bool {
	features := map[string]bool{"BBL": false, "Prusa": false}
	for _, family := range []struct{ vendor, printer, process, filament, command string }{
		{"BBL", "Bambu Lab A1 mini 0.4 nozzle", "0.20mm Non-planar top @BBL A1M", "Bambu PLA Basic @BBL A1M", "M400 U1"},
		{"Prusa", "Prusa MINI 0.4 nozzle", "0.20mm Non-planar top @MINI", "Prusament PLA @MK3S", "M600"},
	} {
		err := func() error {
			dir, err := os.MkdirTemp("", "orca-colour-*")
			if err != nil {
				return err
			}
			defer os.RemoveAll(dir)
			mesh := filepath.Join(dir, "probe.obj")
			if err := os.WriteFile(mesh, []byte("v 0 0 0\nv 4 0 0\nv 0 4 0\nv 0 0 4\nf 1 3 2\nf 1 2 4\nf 2 3 4\nf 3 1 4\n"), 0600); err != nil {
				return err
			}
			output := filepath.Join(dir, "probe.gcode")
			ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
			defer cancel()
			cmd := exec.CommandContext(ctx, c.Tool, "--profiles", filepath.Join(c.Resources, "profiles"),
				"--datadir", filepath.Join(dir, "data"), "--vendor", family.vendor, "--printer", family.printer,
				"--process", family.process, "--filament", family.filament, "--set", "nonplanar_mode=off",
				"--event", "1:colour", "-o", output, mesh)
			configureKill(cmd)
			cmd.WaitDelay = 5 * time.Second
			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("colour probe: %w: %.500s", err, out)
			}
			data, err := os.ReadFile(output)
			if err != nil {
				return err
			}
			atHeight := false
			for _, line := range strings.Split(string(data), "\n") {
				label, value, _ := strings.Cut(strings.TrimSpace(strings.TrimPrefix(line, ";")), ":")
				if strings.HasPrefix(line, ";") && (label == "Z" || label == "Z_HEIGHT") {
					z, err := strconv.ParseFloat(strings.TrimSpace(value), 64)
					atHeight = err == nil && z == 1
				}
				command, _, _ := strings.Cut(line, ";")
				if atHeight && strings.TrimSpace(command) == family.command {
					return nil
				}
			}
			return fmt.Errorf("missing %s at Z=1", family.command)
		}()
		features[family.vendor] = err == nil
		if err != nil {
			log.Printf("%s colour-change emission unavailable: %v", family.vendor, err)
		}
	}
	return features
}
