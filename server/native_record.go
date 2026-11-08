package main

import (
	"bufio"
	"context"
	"encoding/hex"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

func nativeOutputHashes(raw []byte) (hashes map[string]bool, failed int) {
	var manifest map[string]json.RawMessage
	json.Unmarshal(raw, &manifest)
	var schema int
	json.Unmarshal(manifest["schema"], &schema)
	hashes = map[string]bool{}
	for _, name := range []string{"benchy", "dome", "wave"} {
		var proof struct {
			Hits     *int `json:"clearance_hit_moves"`
			Verifier *struct {
				Hits        *int `json:"hit_moves"`
				MachineHits *int `json:"machine_hit_moves"`
			} `json:"verifier"`
			Native  bool   `json:"native_qualified"`
			Audited bool   `json:"full_audit_qualified"`
			Passed  int    `json:"audit_checks_passed"`
			Total   int    `json:"audit_checks_total"`
			Gcode   string `json:"gcode_sha256"`
			Package string `json:"native_package_sha256"`
		}
		digest := func(h string) bool { b, err := hex.DecodeString(h); return err == nil && len(b) == 32 }
		zero := func(n *int) bool { return n != nil && *n == 0 }
		passed := json.Unmarshal(manifest[name], &proof) == nil && proof.Native && proof.Audited && proof.Total > 0 && proof.Passed == proof.Total
		if schema >= 3 && !(passed && digest(proof.Gcode) && digest(proof.Package) && zero(proof.Hits) && proof.Verifier != nil &&
			zero(proof.Verifier.Hits) && zero(proof.Verifier.MachineHits)) {
			failed++
			continue
		}
		if !passed {
			continue
		}
		for _, hash := range []string{proof.Gcode, proof.Package} {
			if digest(hash) {
				hashes[hash] = true
			}
		}
	}
	return hashes, failed
}

func nativeWaveResults(path string) []map[string]any {
	f, err := os.Open(path)
	if err != nil {
		return nil
	}
	defer f.Close()
	var results []map[string]any
	scan := bufio.NewScanner(f)
	scan.Buffer(make([]byte, 4096), 1<<20)
	for scan.Scan() {
		value, ok := strings.CutPrefix(scan.Text(), "; nonplanar_wave_result = ")
		if !ok {
			continue
		}
		var wave map[string]any
		if json.Unmarshal([]byte(strings.TrimSpace(value)), &wave) == nil && wave != nil {
			results = append(results, wave)
		}
	}
	return results
}

func bindNativeRecord(raw []byte, c Config, j *Job) []byte {
	var record map[string]json.RawMessage
	if json.Unmarshal(raw, &record) != nil || record == nil {
		return raw
	}
	if j.Request.Submission != nil {
		record["submission"], _ = json.Marshal(j.Request.Submission)
	}
	if c.NativeHealth != nil {
		snapshot := c.NativeHealth.snapshot(c.Tool)
		if artifact, ok := snapshot["artifact_manifest"].(map[string]any); ok && artifact != nil {
			var inputs map[string]any
			if json.Unmarshal(record["inputs"], &inputs) == nil && inputs != nil {
				if _, exists := inputs["checkout_source_sha"]; !exists {
					checkout, exists := inputs["checkout_sha"]
					if !exists {
						checkout = inputs["source_sha"]
					}
					if checkout != nil {
						inputs["checkout_source_sha"] = checkout
					}
				}
				inputs["source_sha"] = artifact["source_sha"]
				inputs["artifact"] = map[string]any{"backend": artifact["backend"], "source_sha": artifact["source_sha"], "binary_sha256": artifact["binary_sha256"], "resources_sha256": artifact["resources_sha256"]}
				record["inputs"], _ = json.Marshal(inputs)
			}
			if j.Request.Output == "gcode" || j.Request.Output == "gcode.3mf" {
				delete(record, "print_qualification")
				if hash, err := bundleFileHash(j.OutputPath(j.Request.Output)); err == nil && c.NativeHealth.qualifiedOutput(hash) {
					record["print_qualification"], _ = json.Marshal(map[string]any{"passed": true, "output_sha256": hash})
				}
			}
		}
	}
	if j.Request.Output == "gcode" || j.Request.Output == "gcode.3mf" {
		waves := nativeWaveResults(j.OutputPath("gcode"))
		if len(waves) > 0 {
			record["wave_results"], _ = json.Marshal(waves)
			var report map[string]any
			if json.Unmarshal(record["nonplanar_report"], &report) != nil || report == nil {
				report = map[string]any{}
			}
			report["wave_results"] = waves
			if objects, ok := report["objects"].([]any); ok {
				for _, wave := range waves {
					index, ok := wave["object"].(float64)
					if !ok || index < 0 || index >= float64(len(objects)) || index != float64(int(index)) {
						continue
					}
					if object, ok := objects[int(index)].(map[string]any); ok {
						object["wave_result"] = wave
						if object["mode"] == "off" {
							object["curved_share"] = nil
						}
					}
				}
			}
			var summary map[string]string
			json.Unmarshal(record["text_summary"], &summary)
			if summary["clearance_checked"] == "1" {
				clearance := map[string]any{"checked": true}
				if previous, ok := report["clearance"].(map[string]any); ok && previous["clearance_override"] == true {
					clearance["clearance_override"], clearance["duct_graze_moves"] = true, previous["duct_graze_moves"]
				}
				for target, key := range map[string]string{"hits": "clearance_hit_moves", "body_hits": "clearance_body_hit_moves", "machine_hits": "clearance_machine_hit_moves", "cone_graze_moves": "clearance_cone_graze_moves"} {
					if n, err := strconv.ParseUint(summary[key], 10, 64); err == nil {
						clearance[target] = n
					}
				}
				report["clearance"] = clearance
			}
			if value, err := strconv.ParseFloat(summary["estimated_time_s"], 64); err == nil {
				report["time_s"] = value
			}
			extrusion, e1 := strconv.ParseFloat(summary["nonplanar_extrude_time_s"], 64)
			other, e2 := strconv.ParseFloat(summary["nonplanar_other_time_s"], 64)
			if e1 == nil && e2 == nil {
				report["curved_time_s"] = extrusion + other
			}
			record["nonplanar_report"], _ = json.Marshal(report)
		}
	}
	failures := uncurvedObjects(record["nonplanar_report"], record["inputs"])
	failures = append(failures, collidingObjects(record["nonplanar_report"], c, j)...)
	if len(failures) > 0 {
		record["could_not_curve"], _ = json.Marshal(failures)
	} else {
		delete(record, "could_not_curve")
	}
	encoded, err := json.Marshal(record)
	if err != nil {
		return raw
	}
	return encoded
}

var methodLabels = map[string]string{"nonplanar_top": "Smoothest tops", "curved_tops": "Fast curved tops", "smooth_morph": "Smooth morph",
	"slant": "Slant", "conformal_layers": "Conformal layers", "multiplanar": "Multiplanar", "field": "Rotary curved layers"}

func uncurvedObjects(raw, rawInputs json.RawMessage) []map[string]any {
	var report struct {
		Objects   []map[string]any `json:"objects"`
		Clearance struct {
			Override bool `json:"clearance_override"`
		} `json:"clearance"`
	}
	if json.Unmarshal(raw, &report) != nil {
		return nil
	}
	var inputs struct {
		Head struct {
			File string `json:"file"`
		} `json:"head"`
	}
	json.Unmarshal(rawInputs, &inputs)
	marked := false
	if inputs.Head.File != "" {
		var head struct {
			Boxes []struct {
				Block bool `json:"block"`
			} `json:"boxes"`
		}
		if b, err := os.ReadFile(inputs.Head.File); err == nil && json.Unmarshal(b, &head) == nil {
			for _, box := range head.Boxes {
				marked = marked || box.Block
			}
		}
	}
	var out []map[string]any
	for i, o := range report.Objects {
		mode, _ := o["mode"].(string)
		share, measured := o["curved_share"].(float64)
		limit, _ := o["planar_limit"].(string)
		reason, _ := o["planar_fallback_reason"].(string)
		if mode == "" || mode == "off" || !measured || share > 0 || (limit == "" && reason == "") {
			continue
		}
		method := methodLabels[mode]
		if method == "" {
			method = mode
		}
		entry := map[string]any{"object": i, "name": o["name"], "mode": mode, "method": method, "planar_limit": limit, "reason": reason,
			"try_ignore_duct": marked && !report.Clearance.Override && o["access_by"] != "nozzle" && (limit == "access" || strings.Contains(reason, "print head"))}
		for _, k := range []string{"curved_max_angle_deg", "reachable_slope_deg"} {
			if v, ok := o[k].(float64); ok {
				entry[k] = v
			}
		}
		out = append(out, entry)
	}
	return out
}

var verifiedModes = map[string]bool{"smooth_morph": true, "slant": true}

func collidingObjects(raw json.RawMessage, c Config, j *Job) []map[string]any {
	var report struct {
		Objects []map[string]any `json:"objects"`
	}
	if j == nil || !previewableRequest(j.Request) || json.Unmarshal(raw, &report) != nil {
		return nil
	}
	var curved []int
	for i, o := range report.Objects {
		mode, _ := o["mode"].(string)
		if share, _ := o["curved_share"].(float64); verifiedModes[mode] && share > 0 {
			curved = append(curved, i)
		}
	}
	if len(curved) == 0 {
		return nil
	}
	hits, checked := j.morphClearanceHits(c)
	if checked && hits <= 0 {
		return nil
	}
	var out []map[string]any
	for _, i := range curved {
		o := report.Objects[i]
		mode, _ := o["mode"].(string)
		method := methodLabels[mode]
		reason := "Could not check " + method + " for collisions; try again or slice flat"
		if checked {
			reason = method + " would hit the part in " + strconv.Itoa(hits) + " moves"
		}
		out = append(out, map[string]any{"object": i, "name": o["name"], "mode": mode, "method": method, "planar_limit": "clearance",
			"reason": reason, "try_ignore_duct": false})
	}
	return out
}

func (j *Job) morphClearanceHits(c Config) (int, bool) {
	j.clearanceOnce.Do(func() {
		gcode := j.OutputPath("gcode")
		if _, err := os.Stat(gcode); err != nil {
			j.log("server", "clearance verification found no G-code")
			return
		}
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Minute)
		defer cancel()
		out, err := exec.CommandContext(ctx, c.Tool, "--verify-gcode", gcode, "--profiles", filepath.Join(c.Resources, "profiles")).Output()
		for _, line := range strings.Split(string(out), "\n") {
			if f := strings.Fields(line); len(f) == 2 && f[0] == "clearance_hit_moves" {
				if n, perr := strconv.Atoi(f[1]); perr == nil {
					j.clearanceHits, j.clearanceChecked = n, true
					j.log("server", "verified clearance hit moves "+f[1])
					return
				}
			}
		}
		why := "no clearance_hit_moves line"
		if err != nil {
			why = sanitizeToolError(err)
		}
		j.log("server", "clearance verification gave no count: "+why)
	})
	return j.clearanceHits, j.clearanceChecked
}

func (j *Job) curveRefusal() string {
	var record struct {
		Failures []struct {
			Name   string `json:"name"`
			Method string `json:"method"`
			Reason string `json:"reason"`
			Limit  string `json:"planar_limit"`
		} `json:"could_not_curve"`
	}
	if json.Unmarshal(j.Record(), &record) != nil || len(record.Failures) == 0 {
		return ""
	}
	f := record.Failures[0]
	why := f.Reason
	if why == "" {
		why = "no top surface could be curved (" + f.Limit + ")"
	}
	return "Could not slice " + f.Name + " with " + f.Method + ": " + why
}
