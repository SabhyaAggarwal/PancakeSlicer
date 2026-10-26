package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"time"
)

type ToolFeatures struct {
	ColourChange         map[string]bool `json:"colour_change"`
	PlateIndex           bool            `json:"plate_index"`
	PlatePlacement       bool            `json:"plate_placement"`
	DumpOptions          bool            `json:"dump_options"`
	DumpPresets          bool            `json:"dump_presets"`
	Progress             bool            `json:"progress"`
	MovesOut             bool            `json:"moves_out"`
	ObjectSettings       bool            `json:"object_settings"`
	Step                 bool            `json:"step"`
	Edit                 bool            `json:"edit"`
	ForcePlanar          bool            `json:"force_planar"`
	Cut                  bool            `json:"cut"`
	PatchPreview         bool            `json:"patch_preview"`
	SurfaceGrid          bool            `json:"surface_grid"`
	Calib                bool            `json:"calib"`
	Volumes              bool            `json:"volumes"`
	PresetCompat         bool            `json:"preset_compat"`
	OneShot              bool            `json:"oneshot"`
	MeshPlates           bool            `json:"mesh_plates"`
	Paint                bool            `json:"paint"`
	LayerProfile         bool            `json:"layer_profile"`
	Arrange              bool            `json:"arrange"`
	ProjectVolumes       bool            `json:"project_volumes"`
	ProjectPaint         bool            `json:"project_paint"`
	OrientScores         bool            `json:"orient_scores"`
	Floating             bool            `json:"floating"`
	Ranges               bool            `json:"ranges"`
	MovesExtended        bool            `json:"moves_extended"`
	MixedTemperatures    bool            `json:"mixed_temperatures"`
	ArrangeMaterials     bool            `json:"arrange_materials"`
	WipeTower            bool            `json:"wipe_tower"`
	Explode              bool            `json:"explode"`
	CutConnectors        bool            `json:"cut_connectors"`
	MeshRepair           bool            `json:"mesh_repair"`
	MeshSimplify         bool            `json:"mesh_simplify"`
	MeshBoolean          bool            `json:"mesh_boolean"`
	EmbossText           bool            `json:"emboss_text"`
	EmbossSurface        bool            `json:"emboss_surface"`
	ExportGeneric3MF     bool            `json:"export_generic_3mf"`
	ExportDRC            bool            `json:"export_drc"`
	EmbossSurfaceOutside bool            `json:"emboss_surface_outside"`
	BrimEars             bool            `json:"brim_ears"`
	SeamParts            bool            `json:"seam_parts"`
}

type toolOption struct {
	Key  string `json:"key"`
	Type string `json:"type"`
	Mode int    `json:"mode"`
}

func toolDump(c Config, args ...string) ([]byte, error) {
	dir, err := os.MkdirTemp("", "orca-catalog-*")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	path := filepath.Join(dir, "catalog.json")
	args = append(args, path, "--profiles", filepath.Join(c.Resources, "profiles"), "--datadir", filepath.Join(dir, "data"))
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Minute)
	defer cancel()
	cmd := exec.CommandContext(ctx, c.Tool, args...)
	configureKill(cmd)
	cmd.WaitDelay = 5 * time.Second
	if output, err := cmd.CombinedOutput(); err != nil {
		return nil, fmt.Errorf("tool catalog: %w: %.1000s", err, output)
	}
	return os.ReadFile(path)
}

func probeUsage(c Config, f *ToolFeatures) {
	ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, c.Tool, "--help")
	configureKill(cmd)
	cmd.WaitDelay = 5 * time.Second
	out, _ := cmd.CombinedOutput()
	usage := string(out)
	f.Step = strings.Contains(usage, "--step-linear-deflection")
	f.Edit = strings.Contains(usage, "--edit EDITS.json")
	f.ForcePlanar = strings.Contains(usage, "--force-planar")
	f.Cut = strings.Contains(usage, "--cut-only")
	f.PatchPreview = strings.Contains(usage, "--patch-preview FILE")
	f.SurfaceGrid = strings.Contains(usage, "--surface-grid")
	f.Calib = strings.Contains(usage, "--calib-params")
	f.Volumes = f.Cut
	f.MeshPlates = strings.Contains(usage, `plate JSON objects take "plate"`)
	f.Paint = strings.Contains(usage, `plate JSON objects and volumes take "paint"`)
	f.LayerProfile = strings.Contains(usage, `plate JSON objects take "layer_height_profile"`)
	f.Arrange = strings.Contains(usage, "--arrange-only")
	f.ProjectVolumes = strings.Contains(usage, `--edit takes "add_volume"`)
	f.ProjectPaint = strings.Contains(usage, `--edit takes "set_paint"`)
	f.OrientScores = strings.Contains(usage, "--orient-scores")
	f.Floating = strings.Contains(usage, "[--floating]")
	f.Ranges = strings.Contains(usage, `plate JSON objects take "ranges"`)
	f.MovesExtended = strings.Contains(usage, "--moves-extended")
	f.MixedTemperatures = strings.Contains(usage, "--allow-mixed-temperatures")
	f.ArrangeMaterials = strings.Contains(usage, "--arrange-multi-materials")
	f.WipeTower = strings.Contains(usage, "--wipe-tower-only")
	f.Explode = strings.Contains(usage, "--explode-3mf IN.3mf OUTDIR")
	f.SeamParts = strings.Contains(usage, `plate JSON volumes take "type": "seam"`)
	f.BrimEars = strings.Contains(usage, `plate JSON objects take "brim_ears"`)
	f.MeshRepair = strings.Contains(usage, "--repair-mesh IN OUT.stl")
	f.MeshSimplify = strings.Contains(usage, "--simplify-mesh IN OUT.stl")
	f.MeshBoolean = strings.Contains(usage, "--mesh-boolean union|difference|intersection --a A --b B")
	f.EmbossText = strings.Contains(usage, "--emboss-text OUT.stl") && strings.Contains(usage, "--emboss-fonts")
	f.EmbossSurface = f.EmbossText && strings.Contains(usage, "--use-surface --surface TARGET --at X,Y,Z --normal NX,NY,NZ")
	f.EmbossSurfaceOutside = f.EmbossSurface && strings.Contains(usage, "--surface-outside")
	f.ExportGeneric3MF = strings.Contains(usage, "--export-3mf-generic IN.3mf OUT.3mf")
	f.ExportDRC = strings.Contains(usage, "--export-drc IN OUT.drc")
	f.CutConnectors = strings.Contains(usage, "--cut-connectors") && strings.Contains(usage, "--cut-mode planar|dovetail")
	if dump, err := toolDump(c, "--dump-presets", "Prusa"); err == nil {
		f.PresetCompat = bytes.Contains(dump, []byte(`"compatible_printers_evaluated"`))
	}
}

func probeTool(c Config) (ToolFeatures, *cachedResponse) {
	fallback := cacheResponse(optionData)
	b, err := toolDump(c, "--dump-options")
	if err != nil {
		log.Printf("using legacy tool contract: %v", err)
		return ToolFeatures{}, fallback
	}
	var dump struct {
		Schema  string            `json:"schema"`
		Options []json.RawMessage `json:"options"`
	}
	if err := json.Unmarshal(b, &dump); err != nil || dump.Schema != "orca.options.v1" || len(dump.Options) == 0 {
		log.Print("using legacy tool contract: invalid options dump")
		return ToolFeatures{}, fallback
	}
	options := map[string]map[string]any{}
	kinds := map[string]string{}
	for _, raw := range dump.Options {
		var option toolOption
		var value map[string]any
		if json.Unmarshal(raw, &option) != nil || json.Unmarshal(raw, &value) != nil || option.Key == "" || option.Type == "" {
			log.Print("using legacy tool contract: invalid option entry")
			return ToolFeatures{}, fallback
		}
		if hiddenSetting.MatchString(option.Key) {
			continue
		}
		kind := "comma"
		switch option.Type {
		case "string", "strings":
			kind = option.Type
		case "points_groups", "ints_groups":
			kind = "groups"
		}
		kinds[option.Key] = kind
		types := map[string]string{"float_or_percent": "FloatOrPercent", "floats_or_percents": "FloatsOrPercents", "points_groups": "PointsGroups", "ints_groups": "IntsGroups"}
		typ := types[option.Type]
		if typ == "" {
			typ = strings.ToUpper(option.Type[:1]) + option.Type[1:]
		}
		value["type"] = typ
		if option.Type == "strings" {
			if serialized, ok := value["default"].(string); ok {
				value["default"] = nativeStrings(serialized)
			}
		}
		mode := "Develop"
		switch option.Mode {
		case 0:
			mode = "Simple"
		case 1:
			mode = "Advanced"
		case 2:
			mode = "Expert"
		}
		value["mode"] = mode
		options[option.Key] = value
	}
	response, err := cacheJSON(options)
	if err != nil {
		log.Printf("using legacy tool contract: %v", err)
		return ToolFeatures{}, fallback
	}
	configKinds = kinds
	modes, _ := options["nonplanar_mode"]["enum_values"].([]any)
	oneshot := false
	for _, mode := range modes {
		oneshot = oneshot || mode == "oneshot"
	}
	log.Printf("using native v1 tool contract (%d options)", len(options))
	return ToolFeatures{DumpOptions: true, DumpPresets: true, Progress: true, MovesOut: true, ObjectSettings: true, OneShot: oneshot}, response
}

func nativeStrings(value string) []string {
	if value == "" {
		return []string{}
	}
	var result []string
	var part strings.Builder
	quoted, escaped := false, false
	for _, ch := range value {
		if escaped {
			switch ch {
			case 'n':
				ch = '\n'
			case 'r':
				ch = '\r'
			}
			part.WriteRune(ch)
			escaped = false
		} else {
			switch ch {
			case '\\':
				escaped = true
			case '"':
				quoted = !quoted
			case ';':
				if quoted {
					part.WriteRune(ch)
				} else {
					result = append(result, part.String())
					part.Reset()
				}
			default:
				part.WriteRune(ch)
			}
		}
	}
	if escaped {
		part.WriteRune('\\')
	}
	return append(result, part.String())
}

func (p *PresetIndex) dumpVendor(vendor string) (*VendorPresets, error) {
	b, err := toolDump(*p.tool, "--dump-presets", vendor)
	if err != nil {
		return nil, err
	}
	type entry struct {
		Name       string                     `json:"name"`
		Inherits   []string                   `json:"inherits"`
		Values     map[string]json.RawMessage `json:"values"`
		Printers   *[]string                  `json:"compatible_printers_evaluated"`
		Prints     *[]string                  `json:"compatible_prints_evaluated"`
		FilamentID string                     `json:"filament_id"`
	}
	var dump struct {
		Schema   string      `json:"schema"`
		Machine  []entry     `json:"machine"`
		Process  []entry     `json:"process"`
		Filament []entry     `json:"filament"`
		Models   []rawPreset `json:"printer_models"`
	}
	if err := json.Unmarshal(b, &dump); err != nil {
		return nil, err
	}
	if dump.Schema != "orca.presets.v1" {
		return nil, fmt.Errorf("unsupported preset schema %q", dump.Schema)
	}
	list := func(entries []entry) []Preset {
		out := make([]Preset, 0, len(entries))
		for _, e := range entries {
			config := map[string]json.RawMessage{}
			for key, value := range e.Values {
				if configKey(key) {
					if configKinds[key] == "strings" {
						value, _ = json.Marshal(nativeStrings(stringValue(value)))
					}
					config[key] = value
				}
			}
			preset := Preset{Name: e.Name, Vendor: vendor, Inherits: e.Inherits, Config: config, CompatiblePrinters: stringList(config["compatible_printers"]), FilamentID: e.FilamentID}
			if preset.FilamentID == "" {
				preset.FilamentID = stringValue(e.Values["filament_id"])
			}
			if e.Printers != nil {
				preset.Evaluated, preset.CompatiblePrinters = true, *e.Printers
				if e.Prints != nil {
					preset.CompatiblePrints = *e.Prints
				}
			}
			out = append(out, preset)
		}
		return out
	}
	v := &VendorPresets{Vendor: vendor, Machines: list(dump.Machine), Processes: list(dump.Process), Filaments: list(dump.Filament)}
	models := map[string]rawPreset{}
	for _, model := range dump.Models {
		models[stringValue(model["name"])] = model
	}
	for i := range v.Machines {
		m := &v.Machines[i]
		m.Model = stringValue(m.Config["printer_model"])
		m.Variant = stringValue(m.Config["printer_variant"])
		model := models[m.Model]
		m.BedModel = stringValue(model["bed_model"])
		m.BedTexture = stringValue(model["bed_texture"])
		m.DefaultBedType = stringValue(model["default_bed_type"])
		m.NotSupportBedType = stringList(model["not_support_bed_type"])
		m.DefaultMaterials = stringList(model["default_materials"])
	}
	return v, nil
}
