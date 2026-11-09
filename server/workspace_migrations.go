package main

import (
	"bytes"
	"encoding/json"
	"time"
)

const migrationSlantMorph = "slant-morph-default-0"

func migrateWorkspace(d *workspaceData, now time.Time) bool {
	if _, done := d.Migrations[migrationSlantMorph]; done {
		return false
	}
	for id, p := range d.Projects {
		if p.Record == nil {
			continue
		}
		if raw, ok := slantMorphRecord(p.Record); ok {
			p.Record = raw
			d.Projects[id] = p
		}
	}
	for id, dr := range d.Drafts {
		if raw, ok := slantMorphRecord(dr.Document); ok {
			dr.Document = raw
			d.Drafts[id] = dr
		}
	}
	if d.Migrations == nil {
		d.Migrations = map[string]int64{}
	}
	d.Migrations[migrationSlantMorph] = now.UnixMilli()
	return true
}

func migrateVersionRecord(d *workspaceData, raw json.RawMessage, at int64) json.RawMessage {
	if ran, ok := d.Migrations[migrationSlantMorph]; ok && at < ran {
		if out, changed := slantMorphRecord(raw); changed {
			return out
		}
	}
	return raw
}

func slantMorphRecord(raw json.RawMessage) (json.RawMessage, bool) {
	var record map[string]json.RawMessage
	if json.Unmarshal(raw, &record) != nil || record == nil {
		return raw, false
	}
	var doc map[string]any
	decoder := json.NewDecoder(bytes.NewReader(record["doc"]))
	decoder.UseNumber()
	if decoder.Decode(&doc) != nil || doc == nil || !choosesSlant(doc) {
		return raw, false
	}
	dirty, _ := doc["dirty"].(map[string]any)
	if dirty == nil {
		dirty = map[string]any{}
	}
	process, _ := dirty["process"].(map[string]any)
	if process == nil {
		process = map[string]any{}
	}
	if _, set := process["nonplanar_slant_morph_layers"]; set {
		return raw, false
	}
	process["nonplanar_slant_morph_layers"] = "10"
	dirty["process"] = process
	doc["dirty"] = dirty
	encoded, err := json.Marshal(doc)
	if err != nil {
		return raw, false
	}
	record["doc"] = encoded
	out, err := json.Marshal(record)
	if err != nil {
		return raw, false
	}
	return out, true
}

func choosesSlant(v any) bool {
	switch x := v.(type) {
	case map[string]any:
		for k, child := range x {
			if k == "nonplanar_mode" && child == "slant" {
				return true
			}
			if choosesSlant(child) {
				return true
			}
		}
	case []any:
		for _, child := range x {
			if choosesSlant(child) {
				return true
			}
		}
	}
	return false
}
