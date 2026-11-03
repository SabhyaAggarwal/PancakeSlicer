package main

import (
	"bufio"
	"encoding/hex"
	"encoding/json"
	"os"
	"strconv"
	"strings"
)

func nativeOutputHashes(raw []byte) map[string]bool {
	var manifest map[string]json.RawMessage
	json.Unmarshal(raw, &manifest)
	hashes := map[string]bool{}
	for _, name := range []string{"benchy", "dome", "wave"} {
		var proof struct {
			Native  bool   `json:"native_qualified"`
			Audited bool   `json:"full_audit_qualified"`
			Passed  int    `json:"audit_checks_passed"`
			Total   int    `json:"audit_checks_total"`
			Gcode   string `json:"gcode_sha256"`
			Package string `json:"native_package_sha256"`
		}
		if json.Unmarshal(manifest[name], &proof) != nil || !proof.Native || !proof.Audited || proof.Total == 0 || proof.Passed != proof.Total {
			continue
		}
		for _, hash := range []string{proof.Gcode, proof.Package} {
			if b, err := hex.DecodeString(hash); err == nil && len(b) == 32 {
				hashes[hash] = true
			}
		}
	}
	return hashes
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
				if hash, err := bundleFileHash(j.OutputPath(j.Request.Output)); err == nil && c.NativeHealth.outputs[hash] {
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
	encoded, err := json.Marshal(record)
	if err != nil {
		return raw
	}
	return encoded
}
