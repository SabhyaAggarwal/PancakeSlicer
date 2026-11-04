package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

func (s *Server) migrateLibraryV4() {
	l := s.libraryV4
	if _, err := os.Stat(filepath.Join(l.dir, libraryV4File)); !errors.Is(err, os.ErrNotExist) {
		return
	}
	old, err := s.library.readLibrary()
	if err != nil {
		return
	}
	var records []map[string]json.RawMessage
	if json.Unmarshal(old.Document["profiles"], &records) != nil || len(records) == 0 {
		return
	}
	doc, kept, skipped := s.convertLibrary(old.Document, records)
	if _, err := v4Profiles(doc); err != nil {
		log.Printf("library v4: conversion invalid, starting from the seeds: %v", err)
		return
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	if err := l.write(&libraryV4Envelope{Document: doc, Operations: map[string]libraryReceipt{}}); err != nil {
		log.Printf("library v4: conversion not written: %v", err)
		return
	}
	log.Printf("library v4: converted %d profiles from profile-library.json; left out %v", kept, skipped)
}

func stripCatalogSuffix(name string) string {
	stem, _, _ := strings.Cut(name, " @")
	return strings.TrimSpace(stem)
}

func (s *Server) convertLibrary(oldDoc profileLibraryDocument, records []map[string]json.RawMessage) (map[string]json.RawMessage, int, []string) {
	var skipped []string
	type anchor struct{ Name, Vendor, EngineName string }
	anchorOf := func(p map[string]json.RawMessage) anchor {
		var a anchor
		json.Unmarshal(p["base"], &a)
		if a.EngineName == "" {
			a.EngineName = a.Name
		}
		return a
	}
	values := func(raw json.RawMessage) map[string]json.RawMessage {
		m := map[string]json.RawMessage{}
		json.Unmarshal(raw, &m)
		for k := range m {
			if !configKey(k) || strings.HasPrefix(k, "compatible_") || k == "inherits" {
				delete(m, k)
			}
		}
		return m
	}
	common := func(p map[string]json.RawMessage, typ string) map[string]any {
		out := map[string]any{"id": stringValue(p["id"]), "type": typ, "name": stripCatalogSuffix(stringValue(p["name"])), "revision": 0}
		var revision uint64
		if json.Unmarshal(p["revision"], &revision) == nil {
			out["revision"] = revision
		}
		if a, ok := p["archived"]; ok && string(a) != "null" {
			out["archived"] = a
		}
		return out
	}
	models := map[string]string{}
	var out []map[string]any
	for _, pass := range []string{"machine", "filament", "process"} {
		for _, p := range records {
			if stringValue(p["type"]) != pass {
				continue
			}
			a := anchorOf(p)
			vendor, _ := s.presets.vendor(a.Vendor)
			overlay := values(p["overlay"])
			native := func(kind map[string]*Preset) map[string]json.RawMessage {
				if vendor == nil || kind == nil || kind[a.EngineName] == nil {
					return map[string]json.RawMessage{}
				}
				return kind[a.EngineName].Config
			}
			pick := func(key string, base map[string]json.RawMessage) string {
				if v, ok := overlay[key]; ok {
					return firstConfigText(v)
				}
				return firstConfigText(base[key])
			}
			switch pass {
			case "machine":
				var base map[string]json.RawMessage
				if vendor != nil {
					base = native(vendor.machines)
				}
				model, nozzle := pick("printer_model", base), pick("nozzle_diameter", base)
				n, err := strconv.ParseFloat(nozzle, 64)
				if model == "" || err != nil || !(n > 0) || a.Vendor == "" {
					skipped = append(skipped, stringValue(p["name"])+" (no catalog model)")
					continue
				}
				r := common(p, "printer")
				delete(overlay, "printer_model")
				delete(overlay, "nozzle_diameter")
				r["vendor"], r["model"], r["nozzle"] = a.Vendor, model, n
				if len(overlay) > 0 {
					r["machine"] = overlay
				}
				if po := values(p["printOverrides"]); len(po) > 0 {
					delete(po, "layer_height")
					r["printOverrides"] = po
				}
				models[stringValue(p["id"])] = model
				out = append(out, r)
			case "filament":
				var base map[string]json.RawMessage
				if vendor != nil {
					base = native(vendor.filaments)
				}
				r := common(p, "filament")
				r["family"] = a.Vendor + "/" + stripCatalogSuffix(a.Name)
				for field, key := range map[string]string{"brand": "filament_vendor", "material": "filament_type", "colour": "default_filament_colour"} {
					if v := pick(key, base); v != "" {
						r[field] = v
					}
				}
				if len(overlay) > 0 {
					r["settings"] = overlay
				}
				var rule worksWithRule
				if json.Unmarshal(p["applicability"], &rule) == nil && rule.Schema == 2 {
					fits := map[string]any{}
					if rule.Printers != nil {
						var ms []string
						ms = append(ms, rule.Printers.Models...)
						for _, id := range rule.Printers.PrinterIDs {
							if m := models[id]; m != "" {
								ms = append(ms, m)
							}
						}
						if len(ms) > 0 {
							fits["models"] = ms
						}
					}
					if rule.Nozzle != nil && len(rule.Nozzle.Sizes) > 0 {
						fits["nozzles"] = rule.Nozzle.Sizes
					}
					if len(fits) > 0 {
						r["fits"] = fits
					}
				}
				out = append(out, r)
			case "process":
				r := common(p, "print")
				var auto autoProfile
				if json.Unmarshal(p["auto"], &auto) == nil && auto.Schema == 3 {
					if len(auto.Scales) > 0 {
						r["scales"] = auto.Scales
					}
					if auto.PreferredHeight != nil {
						r["preferredHeight"] = *auto.PreferredHeight
					}
				} else if vendor != nil && vendor.processes[a.EngineName] != nil {
					base := vendor.processes[a.EngineName]
					intent := map[string]json.RawMessage{}
					var standard *Preset
					for _, m := range base.CompatiblePrinters {
						if machine := vendor.machines[m]; machine != nil {
							if standard = standardProcess(vendor, machine); standard != nil {
								break
							}
						}
					}
					for k, v := range base.Config {
						if configKey(k) && !strings.HasPrefix(k, "compatible_") && k != "inherits" && k != "layer_height" && (standard == nil || settingText(standard.Config[k]) != settingText(v)) {
							intent[k] = v
						}
					}
					for k, v := range overlay {
						intent[k] = v
					}
					overlay = intent
					if h, err := strconv.ParseFloat(firstConfigText(base.Config["layer_height"]), 64); err == nil && h > 0 {
						r["preferredHeight"] = h
					}
				}
				if h, ok := overlay["layer_height"]; ok {
					if v, err := strconv.ParseFloat(firstConfigText(h), 64); err == nil && v > 0 {
						r["preferredHeight"] = v
					}
					delete(overlay, "layer_height")
				}
				if len(overlay) > 0 {
					r["intent"] = overlay
				}
				out = append(out, r)
			}
		}
	}
	var printers struct {
		Setups json.RawMessage `json:"setups"`
	}
	json.Unmarshal(oldDoc["printers"], &printers)
	if len(printers.Setups) == 0 {
		printers.Setups = json.RawMessage(`{}`)
	}
	doc := map[string]json.RawMessage{"schema": json.RawMessage(`4`), "revision": json.RawMessage(fmt.Sprint(libraryRevision(oldDoc))), "setups": printers.Setups}
	if epoch, ok := oldDoc["epoch"]; ok {
		doc["epoch"] = epoch
	}
	doc["profiles"], _ = json.Marshal(out)
	return doc, len(out), skipped
}
