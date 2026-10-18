package main

import (
	"encoding/json"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
)

type Preset struct {
	Name               string                     `json:"name"`
	Vendor             string                     `json:"vendor"`
	Inherits           []string                   `json:"inherits"`
	CompatiblePrinters []string                   `json:"compatible_printers"`
	Config             map[string]json.RawMessage `json:"config"`
	Model              string                     `json:"model,omitempty"`
	Variant            string                     `json:"variant,omitempty"`
	BedModel           string                     `json:"bed_model,omitempty"`
	BedTexture         string                     `json:"bed_texture,omitempty"`
	DefaultBedType     string                     `json:"default_bed_type,omitempty"`
	NotSupportBedType  []string                   `json:"not_support_bed_type,omitempty"`
	DefaultMaterials   []string                   `json:"default_materials,omitempty"`
}
type VendorPresets struct {
	Vendor                         string   `json:"vendor"`
	Machines                       []Preset `json:"machines"`
	Processes                      []Preset `json:"processes"`
	Filaments                      []Preset `json:"filaments"`
	machines, processes, filaments map[string]*Preset
}
type rawPreset map[string]json.RawMessage

func stringValue(raw json.RawMessage) string { var s string; json.Unmarshal(raw, &s); return s }
func stringList(raw json.RawMessage) []string {
	var list []string
	if json.Unmarshal(raw, &list) == nil && list != nil {
		return list
	}
	if s := stringValue(raw); s != "" {
		return strings.Split(s, ";")
	}
	return []string{}
}

type vendorCache struct {
	once     sync.Once
	presets  *VendorPresets
	response *cachedResponse
	err      error
}
type PresetIndex struct {
	root           string
	indexOnce      sync.Once
	indexError     error
	vendorNames    []string
	printerVendors map[string]string
	known          map[string]map[string]map[string]bool
	tool           *Config
	cache          sync.Map
	allOnce        sync.Once
	allResponse    *cachedResponse
	allError       error
}

func NewPresetIndex(root string) *PresetIndex { return &PresetIndex{root: root} }

func (p *PresetIndex) names() ([]string, error) {
	p.indexOnce.Do(func() {
		p.known = map[string]map[string]map[string]bool{}
		p.printerVendors = map[string]string{}
		entries, err := os.ReadDir(p.root)
		if err != nil {
			p.indexError = err
			return
		}
		for _, e := range entries {
			if !e.IsDir() {
				continue
			}
			if st, err := os.Stat(filepath.Join(p.root, e.Name(), "machine")); err != nil || !st.IsDir() {
				continue
			}
			p.vendorNames = append(p.vendorNames, e.Name())
		}
		sort.Slice(p.vendorNames, func(i, j int) bool {
			if (p.vendorNames[i] == "BBL") != (p.vendorNames[j] == "BBL") {
				return p.vendorNames[i] == "BBL"
			}
			return p.vendorNames[i] < p.vendorNames[j]
		})
		for _, vendor := range p.vendorNames {
			kinds := map[string]map[string]bool{}
			for _, kind := range []string{"machine", "process", "filament"} {
				raw, err := readKind(filepath.Join(p.root, vendor, kind))
				if err != nil {
					p.indexError = err
					return
				}
				names := map[string]bool{}
				for name, preset := range raw {
					if stringValue(preset["instantiation"]) != "true" {
						continue
					}
					names[name] = true
					if kind == "machine" && p.printerVendors[name] == "" {
						p.printerVendors[name] = vendor
					}
				}
				kinds[kind] = names
			}
			p.known[vendor] = kinds
		}
	})
	return p.vendorNames, p.indexError
}
func (p *PresetIndex) knownPreset(vendor, kind, name string) bool {
	_, err := p.names()
	return err == nil && p.known[vendor][kind][name]
}
func (p *PresetIndex) vendor(name string) (*VendorPresets, error) {
	if _, err := p.names(); err != nil {
		return nil, err
	}
	if p.known[name] == nil {
		return nil, os.ErrNotExist
	}
	entry, _ := p.cache.LoadOrStore(name, &vendorCache{})
	v := entry.(*vendorCache)
	v.once.Do(func() {
		if p.tool != nil {
			v.presets, v.err = p.dumpVendor(name)
		} else {
			v.presets, v.err = p.loadVendor(name)
		}
		if v.err == nil {
			v.presets.machines = presetMap(v.presets.Machines)
			v.presets.processes = presetMap(v.presets.Processes)
			v.presets.filaments = presetMap(v.presets.Filaments)
			v.response, v.err = cacheJSON(v.presets)
		}
	})
	return v.presets, v.err
}
func (p *PresetIndex) Vendor(name string) (*VendorPresets, bool) {
	v, err := p.vendor(name)
	return v, err == nil
}
func (p *PresetIndex) Vendors() ([]VendorPresets, error) {
	names, err := p.names()
	if err != nil {
		return nil, err
	}
	vs := []VendorPresets{}
	for _, name := range names {
		v, err := p.vendor(name)
		if err != nil {
			return nil, err
		}
		vs = append(vs, *v)
	}
	return vs, nil
}
func (p *PresetIndex) VendorOfPrinter(printer string) string {
	if _, err := p.names(); err != nil {
		return ""
	}
	return p.printerVendors[printer]
}
func presetMap(list []Preset) map[string]*Preset {
	out := make(map[string]*Preset, len(list))
	for i := range list {
		out[list[i].Name] = &list[i]
	}
	return out
}

func readKind(dir string) (map[string]rawPreset, error) {
	out := map[string]rawPreset{}
	err := filepath.WalkDir(dir, func(path string, d fs.DirEntry, err error) error {
		if os.IsNotExist(err) {
			return nil
		}
		if err != nil {
			return err
		}
		if d.IsDir() || filepath.Ext(path) != ".json" {
			return nil
		}
		b, err := os.ReadFile(path)
		if err != nil {
			return err
		}
		var r rawPreset
		if json.Unmarshal(b, &r) == nil {
			if name := stringValue(r["name"]); name != "" {
				out[name] = r
			}
		}
		return nil
	})
	return out, err
}
func chain(all map[string]rawPreset, name string) []string {
	out := []string{}
	seen := map[string]bool{name: true}
	for parent := stringValue(all[name]["inherits"]); parent != "" && !seen[parent]; parent = stringValue(all[parent]["inherits"]) {
		seen[parent] = true
		out = append(out, parent)
	}
	return out
}

func configKey(key string) bool {
	switch key {
	case "name", "type", "from", "inherits", "setting_id", "instantiation":
		return false
	}
	return !hiddenSetting.MatchString(key)
}
func resolvedConfig(all map[string]rawPreset, name string) map[string]json.RawMessage {
	names := append([]string{name}, chain(all, name)...)
	config := map[string]json.RawMessage{}
	for i := len(names) - 1; i >= 0; i-- {
		for key, value := range all[names[i]] {
			if configKey(key) {
				config[key] = value
			}
		}
	}
	return config
}
func (p *PresetIndex) loadVendor(vendor string) (*VendorPresets, error) {
	dir := filepath.Join(p.root, vendor)
	machines, err := readKind(filepath.Join(dir, "machine"))
	if err != nil {
		return nil, err
	}
	processes, err := readKind(filepath.Join(dir, "process"))
	if err != nil {
		return nil, err
	}
	filaments, err := readKind(filepath.Join(dir, "filament"))
	if err != nil {
		return nil, err
	}
	library, err := readKind(filepath.Join(p.root, "OrcaFilamentLibrary", "filament"))
	if err != nil {
		return nil, err
	}
	ownFilaments := map[string]bool{}
	for name := range filaments {
		ownFilaments[name] = true
	}
	for name, r := range library {
		if _, ok := filaments[name]; !ok {
			filaments[name] = r
		}
	}
	list := func(all map[string]rawPreset) []Preset {
		out := []Preset{}
		for name, r := range all {
			if stringValue(r["instantiation"]) != "true" {
				continue
			}
			config := resolvedConfig(all, name)
			out = append(out, Preset{Name: name, Vendor: vendor, Inherits: chain(all, name), CompatiblePrinters: stringList(config["compatible_printers"]), Config: config})
		}
		sort.Slice(out, func(i, j int) bool {
			ai, aj := strings.HasPrefix(out[i].Name, "Bambu Lab A1 mini "), strings.HasPrefix(out[j].Name, "Bambu Lab A1 mini ")
			if ai != aj {
				return ai
			}
			return out[i].Name < out[j].Name
		})
		return out
	}
	v := &VendorPresets{Vendor: vendor, Machines: list(machines), Processes: list(processes), Filaments: []Preset{}}
	for _, f := range list(filaments) {
		if ownFilaments[f.Name] {
			v.Filaments = append(v.Filaments, f)
		}
	}
	for i := range v.Machines {
		m := &v.Machines[i]
		m.Model = stringValue(m.Config["printer_model"])
		m.Variant = stringValue(m.Config["printer_variant"])
		model := machines[m.Model]
		m.BedModel = stringValue(model["bed_model"])
		m.BedTexture = stringValue(model["bed_texture"])
		m.DefaultBedType = stringValue(model["default_bed_type"])
		m.NotSupportBedType = stringList(model["not_support_bed_type"])
		m.DefaultMaterials = stringList(model["default_materials"])
	}
	return v, nil
}

func (p *PresetIndex) response(name string) (*cachedResponse, error) {
	if _, err := p.vendor(name); err != nil {
		return nil, err
	}
	entry, _ := p.cache.Load(name)
	return entry.(*vendorCache).response, nil
}

func (p *PresetIndex) allResponseJSON() (*cachedResponse, error) {
	p.allOnce.Do(func() {
		vs, err := p.Vendors()
		if err != nil {
			p.allError = err
			return
		}
		p.allResponse, p.allError = cacheJSON(vs)
	})
	return p.allResponse, p.allError
}
