package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"slices"
	"strings"
)

type profileProblem struct {
	Message  string           `json:"error"`
	Code     string           `json:"code"`
	Recovery string           `json:"recovery"`
	Profile  profileReference `json:"profile"`
	status   int
}
type profileReference struct {
	Kind   string `json:"kind"`
	Name   string `json:"name"`
	Vendor string `json:"vendor"`
	Slot   int    `json:"slot,omitempty"`
}

func (p *profileProblem) write(w http.ResponseWriter) { writeJSON(w, p.status, p) }
func profileSelectionProblem(code, kind, name, vendor string, slot int) *profileProblem {
	if name != "" {
		name = presetDisplayName(name)
	}
	p := &profileProblem{Code: code, Recovery: "select_profile", Profile: profileReference{kind, name, vendor, slot}, status: 400}
	if code == "profile_missing" {
		p.Message = "Choose a " + kind + " profile before continuing."
	} else {
		p.Message = fmt.Sprintf("The selected %s profile %q is unavailable. Choose another profile or import it again.", kind, presetDisplayName(name))
	}
	return p
}
func profileCatalogProblem(vendor string, err error) *profileProblem {
	log.Printf("profile catalog %s: %v", presetDisplayName(vendor), err)
	return &profileProblem{Message: "Profiles could not be loaded. Reload the profile catalog and try again.", Code: "profile_catalog_unavailable", Recovery: "reload_profiles", Profile: profileReference{Kind: "vendor", Name: vendor, Vendor: vendor}, status: 503}
}
func profileCompatible(p *Preset, printer, process string, filament bool) bool {
	if (p.Evaluated || len(p.CompatiblePrinters) > 0) && !slices.Contains(p.CompatiblePrinters, printer) {
		return false
	}
	prints := stringList(p.Config["compatible_prints"])
	return !filament || len(prints) == 0 || slices.Contains(prints, process)
}

func (s *Server) resolveJobProfiles(req *JobRequest) (*VendorPresets, *profileProblem) {
	for _, selection := range []struct {
		kind, name string
		slot       int
	}{{"printer", req.Printer, 0}, {"process", req.Process, 0}} {
		if selection.name == "" {
			return nil, profileSelectionProblem("profile_missing", selection.kind, "", req.Vendor, selection.slot)
		}
	}
	if len(req.Filaments) == 0 {
		return nil, profileSelectionProblem("profile_missing", "filament", "", req.Vendor, 1)
	}
	for i, name := range req.Filaments {
		if name == "" {
			return nil, profileSelectionProblem("profile_missing", "filament", "", req.Vendor, i+1)
		}
	}
	if _, err := s.presets.names(); err != nil {
		return nil, profileCatalogProblem(req.Vendor, err)
	}
	if req.Vendor == "" {
		req.Vendor = s.presets.VendorOfPrinter(req.Printer)
		if req.Vendor == "" {
			return nil, profileSelectionProblem("profile_unknown", "printer", req.Printer, "", 0)
		}
	}
	if s.presets.known[req.Vendor] == nil {
		return nil, profileSelectionProblem("profile_unknown", "vendor", req.Vendor, req.Vendor, 0)
	}
	for _, selection := range []struct{ kind, label, name string }{{"machine", "printer", req.Printer}, {"process", "process", req.Process}} {
		if !s.presets.knownPreset(req.Vendor, selection.kind, selection.name) {
			return nil, profileSelectionProblem("profile_unknown", selection.label, selection.name, req.Vendor, 0)
		}
	}
	for i, name := range req.Filaments {
		if !s.presets.knownPreset(req.Vendor, "filament", name) {
			return nil, profileSelectionProblem("profile_unknown", "filament", name, req.Vendor, i+1)
		}
	}
	vendor, err := s.presets.vendor(req.Vendor)
	if err != nil {
		return nil, profileCatalogProblem(req.Vendor, err)
	}
	for _, selection := range []struct {
		kind, name string
		preset     *Preset
		slot       int
	}{{"printer", req.Printer, vendor.machines[req.Printer], 0}, {"process", req.Process, vendor.processes[req.Process], 0}} {
		if selection.preset == nil {
			return nil, profileSelectionProblem("profile_unknown", selection.kind, selection.name, req.Vendor, selection.slot)
		}
	}
	incompatible := func(kind, name string, slot int) *profileProblem {
		return &profileProblem{Message: fmt.Sprintf("The %s profile %q is not compatible with the selected printer and process. Choose a compatible profile.", kind, presetDisplayName(name)), Code: "profile_incompatible", Recovery: "select_compatible_profile", Profile: profileReference{kind, presetDisplayName(name), req.Vendor, slot}, status: 409}
	}
	if !profileCompatible(vendor.processes[req.Process], req.Printer, req.Process, false) {
		return nil, incompatible("process", req.Process, 0)
	}
	for i, name := range req.Filaments {
		preset := vendor.filaments[name]
		if preset == nil {
			return nil, profileSelectionProblem("profile_unknown", "filament", name, req.Vendor, i+1)
		}
		if profileCompatible(preset, req.Printer, req.Process, false) {
			continue
		}
		if variant := filamentVariant(vendor, name, req.Printer, req.Process); variant != "" {
			if req.SubmittedFilaments == nil {
				req.SubmittedFilaments = slices.Clone(req.Filaments)
			}
			req.Filaments[i] = variant
		} else if req.ProfileIDs == nil {
			return nil, incompatible("filament", name, i+1)
		}
	}
	return vendor, nil
}

type submittedConfig struct {
	ProfileIDs      *jobProfileIDs                        `json:"profile_ids,omitempty"`
	ProfileSnapshot map[string]map[string]json.RawMessage `json:"profile_snapshot,omitempty"`
	Schema          int                                   `json:"schema"`
	Vendor          string                                `json:"vendor"`
	Printer         string                                `json:"printer"`
	Process         string                                `json:"process"`
	Filaments       []string                              `json:"filaments"`
	Profiles        struct {
		Printer   *Preset   `json:"printer"`
		Process   *Preset   `json:"process"`
		Filaments []*Preset `json:"filaments"`
	} `json:"profiles"`
	Overrides         [][2]string       `json:"overrides"`
	ProjectSettings   [][2]string       `json:"project_settings,omitempty"`
	InputConfigSHA256 map[string]string `json:"input_config_sha256,omitempty"`
	Artifact          map[string]any    `json:"artifact"`
	ConfigSHA256      string            `json:"config_sha256"`
}

func (s *Server) captureSubmission(j *Job, req *JobRequest, vendor *VendorPresets) error {
	snapshot := &submittedConfig{Schema: 1, Vendor: req.Vendor, Printer: req.Printer, Process: req.Process, Filaments: append([]string{}, req.Filaments...), Overrides: append([][2]string{}, req.Overrides...), ProjectSettings: append([][2]string{}, req.ProjectSettings...), InputConfigSHA256: map[string]string{}, Artifact: map[string]any{}}
	snapshot.ProfileIDs, snapshot.ProfileSnapshot = req.ProfileIDs, req.ProfileSnapshot
	snapshot.Profiles.Printer, snapshot.Profiles.Process = vendor.machines[req.Printer], vendor.processes[req.Process]
	for _, name := range req.Filaments {
		snapshot.Profiles.Filaments = append(snapshot.Profiles.Filaments, vendor.filaments[name])
	}
	if health := s.jobs.cfg.NativeHealth; health != nil {
		if artifact, ok := health.snapshot(s.cfg.Tool)["artifact_manifest"].(map[string]any); ok {
			for _, key := range []string{"backend", "source_sha", "binary_sha256", "resources_sha256"} {
				snapshot.Artifact[key] = artifact[key]
			}
		}
	}
	for _, name := range []string{"plate.json", "edits.json"} {
		path := filepath.Join(j.Dir, "input", name)
		if _, err := os.Stat(path); os.IsNotExist(err) {
			continue
		}
		digest, err := bundleFileHash(path)
		if err != nil {
			return err
		}
		snapshot.InputConfigSHA256[name] = digest
	}
	encoded, err := json.Marshal(snapshot)
	if err != nil {
		return err
	}
	digest := sha256.Sum256(encoded)
	snapshot.ConfigSHA256 = hex.EncodeToString(digest[:])
	req.Submission = snapshot
	return nil
}

func filamentVariant(vendor *VendorPresets, name, printer, process string) string {
	family, _, _ := strings.Cut(name, " @")
	best := ""
	for _, f := range vendor.Filaments {
		if other, _, _ := strings.Cut(f.Name, " @"); other == family && f.Name != name && profileCompatible(&f, printer, process, false) && (best == "" || f.Name < best) {
			best = f.Name
		}
	}
	return best
}
