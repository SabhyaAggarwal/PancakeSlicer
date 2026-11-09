package main

import (
	"net/http"
	"slices"
	"strings"
	"sync"
	"time"
)

type filamentFamily struct {
	ID       string            `json:"id"`
	Name     string            `json:"name"`
	Brand    string            `json:"brand,omitempty"`
	Material string            `json:"material,omitempty"`
	Colour   string            `json:"colour,omitempty"`
	Models   []string          `json:"models"`
	Variants map[string]string `json:"variants"`
	Fallback string            `json:"fallback"`
}

func filamentFamilyOf(name string) string { family, _, _ := strings.Cut(name, " @"); return family }

func filamentFamilies(v *VendorPresets) []filamentFamily {
	modelOf := map[string]string{}
	for _, m := range v.Machines {
		modelOf[m.Name] = firstConfigText(m.Config["printer_model"])
	}
	byName := map[string]*filamentFamily{}
	var order []string
	for _, f := range v.Filaments {
		name := filamentFamilyOf(f.Name)
		fam := byName[name]
		if fam == nil {
			fam = &filamentFamily{ID: v.Vendor + "/" + name, Name: name, Brand: firstConfigText(f.Config["filament_vendor"]),
				Material: firstConfigText(f.Config["filament_type"]), Colour: firstConfigText(f.Config["default_filament_colour"]), Models: []string{}}
			byName[name] = fam
			order = append(order, name)
		}
		for _, printer := range f.CompatiblePrinters {
			if model := modelOf[printer]; model != "" && !slices.Contains(fam.Models, model) {
				fam.Models = append(fam.Models, model)
			}
		}
	}
	out := make([]filamentFamily, 0, len(order))
	for _, name := range order {
		fam := byName[name]
		fam.Variants = map[string]string{}
		for _, m := range v.Machines {
			if f := familyVariant(v, name, m.Name); f != nil && profileCompatible(f, m.Name, "", false) {
				fam.Variants[m.Name] = f.Name
			}
		}
		if f := familyVariant(v, name, ""); f != nil {
			fam.Fallback = f.Name
		}
		slices.Sort(byName[name].Models)
		out = append(out, *byName[name])
	}
	slices.SortFunc(out, func(a, b filamentFamily) int { return strings.Compare(a.Name, b.Name) })
	return out
}

var filamentCatalogs sync.Map

func (s *Server) filamentCatalog(w http.ResponseWriter, r *http.Request) {
	vendor, err := s.presets.vendor(r.URL.Query().Get("vendor"))
	if err != nil {
		fail(w, 404, "Unrecognized printer brand.")
		return
	}
	body, err := filamentCatalogResponse(vendor)
	if err != nil {
		fail(w, 500, err.Error())
		return
	}
	body.serve(w, r, "filament-catalog.json", "application/json", catalogCache, time.Time{})
}

func filamentCatalogResponse(vendor *VendorPresets) (*cachedResponse, error) {
	if body, ok := filamentCatalogs.Load(vendor); ok {
		return body.(*cachedResponse), nil
	}
	built, err := cacheJSON(map[string]any{"vendor": vendor.Vendor, "families": filamentFamilies(vendor)})
	if err != nil {
		return nil, err
	}
	body, _ := filamentCatalogs.LoadOrStore(vendor, built)
	return body.(*cachedResponse), nil
}
