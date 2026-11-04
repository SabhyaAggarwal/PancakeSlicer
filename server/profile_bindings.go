package main

import "encoding/json"

type jobProfileIDs struct {
	Schema    int      `json:"schema"`
	Printer   string   `json:"printer"`
	Process   string   `json:"process"`
	Filaments []string `json:"filaments"`
	Revisions struct {
		Printer   uint64   `json:"printer"`
		Process   uint64   `json:"process"`
		Filaments []uint64 `json:"filaments"`
	} `json:"revisions"`
}

func (s *Server) savedProfileBindings() (map[string]map[string]json.RawMessage, error) {
	s.library.mu.Lock()
	defer s.library.mu.Unlock()
	envelope, err := s.library.readLibrary()
	if err != nil {
		return nil, err
	}
	return libraryProfiles(envelope.Document)
}
func (s *Server) validateProfileBindings(req *JobRequest) *profileProblem {
	ids := req.ProfileIDs
	if ids == nil {
		return nil
	}
	profiles, err := s.savedProfileBindings()
	if err != nil {
		return profileCatalogProblem(req.Vendor, err)
	}
	problem := func(code, recovery, message, kind, name string, slot int) *profileProblem {
		if kind == "machine" {
			kind = "printer"
		}
		return &profileProblem{Message: message, Code: code, Recovery: recovery, Profile: profileReference{Kind: kind, Name: name, Vendor: req.Vendor, Slot: slot}, status: 409}
	}
	if ids.Schema != 1 || len(ids.Filaments) != len(req.Filaments) || len(ids.Revisions.Filaments) != len(req.Filaments) {
		return problem("profile_metadata_invalid", "reload_profiles", "Reload the profile library before slicing; the submitted profile identities are incomplete.", "printer", req.Printer, 0)
	}
	type selection struct {
		id, kind, native string
		revision         uint64
		slot             int
	}
	selections := []selection{{ids.Printer, "machine", req.Printer, ids.Revisions.Printer, 0}, {ids.Process, "process", req.Process, ids.Revisions.Process, 0}}
	for i, id := range ids.Filaments {
		selections = append(selections, selection{id, "filament", req.Filaments[i], ids.Revisions.Filaments[i], i + 1})
	}
	snapshots := map[string]map[string]json.RawMessage{}
	var printer printerFacts
	if p := profiles[ids.Printer]; p != nil {
		printer = s.printerFactsOf(ids.Printer, p)
	}
	for _, selected := range selections {
		p := profiles[selected.id]
		if p == nil || stringValue(p["type"]) != selected.kind {
			return problem("profile_unknown", "save_profile", "The selected owned profile is unavailable. Save or recover it before slicing.", selected.kind, selected.native, selected.slot)
		}
		if len(p["archived"]) > 0 && string(p["archived"]) != "null" {
			return problem("profile_archived", "restore_profile", "This profile was removed from the library. Undo removal or choose another profile before slicing.", selected.kind, selected.native, selected.slot)
		}
		var revision uint64
		json.Unmarshal(p["revision"], &revision)
		if revision != selected.revision {
			return problem("profile_revision_conflict", "reload_profiles", "This profile changed after the document was prepared. Reload it and review the saved settings before slicing.", selected.kind, selected.native, selected.slot)
		}
		var base struct {
			Name       string `json:"name"`
			EngineName string `json:"engineName"`
		}
		json.Unmarshal(p["base"], &base)
		anchor := base.EngineName
		if anchor == "" {
			anchor = base.Name
		}
		if anchor == "" {
			anchor = stringValue(p["inherits"])
		}
		_, auto := p["auto"]
		if anchor == "" || anchor != selected.native && !auto {
			return problem("profile_anchor_mismatch", "repair_profile", "Repair the profile's native base before slicing; an owned ID or label cannot replace its engine profile name.", selected.kind, selected.native, selected.slot)
		}
		chain, chainCode := baseChain(profiles, selected.id)
		if chainCode != "" {
			return problem(chainCode, "edit_base", "Repair this profile's base before slicing.", selected.kind, selected.native, selected.slot)
		}
		if rule := chainRule(chain, selected.kind); rule != nil {
			ok := selected.kind == "machine" || rule.matchesPrinter(printer)
			if selected.kind == "filament" {
				ok = ok && rule.matchesPrint(ids.Process)
			} else {
				for _, fid := range ids.Filaments {
					ok = ok && rule.matchesFilament(fid, firstConfigText(s.profileValue(profiles[fid], "filament_type")))
				}
			}
			if !ok {
				return problem("profile_not_enabled", "edit_applicability", "This profile's Works with rule does not include this printer, nozzle or filament. Edit Works with or choose another profile.", selected.kind, selected.native, selected.slot)
			}
		}
		snapshots[selected.id] = p
	}
	req.ProfileSnapshot = snapshots
	return nil
}
