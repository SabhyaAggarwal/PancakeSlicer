package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

type slotTray struct {
	Type       string `json:"type"`
	Colour     string `json:"colour"`
	Name       string `json:"name"`
	FilamentID string `json:"filament_id"`
}

type slotMatch struct {
	Slot      string   `json:"slot"`
	Label     string   `json:"label"`
	Tray      slotTray `json:"tray"`
	Filament  *string  `json:"filament"`
	Catalog   string   `json:"catalog,omitempty"`
	Name      string   `json:"name"`
	Rule      string   `json:"rule"`
	Colour    string   `json:"colour"`
	Remaining *int     `json:"remaining"`
	Spool     *string  `json:"spool"`
}

type printerSlots struct {
	Online  bool        `json:"online"`
	Updated time.Time   `json:"updated"`
	Slots   []slotMatch `json:"slots"`
	Error string `json:"error,omitempty"`
}

type slotChoice struct {
	Filament string    `json:"filament"`
	Type     string    `json:"type"`
	Colour   string    `json:"colour"`
	At       time.Time `json:"at"`
}

type spoolChoice struct {
	Filament string    `json:"filament"`
	At       time.Time `json:"at"`
}

type slotDoc struct {
	Schema int                              `json:"schema"`
	Last   map[string]*printerSlots         `json:"last"`
	Chosen map[string]map[string]slotChoice `json:"chosen"`
	Spools map[string]spoolChoice           `json:"spools"`
}

type filamentSlotStore struct {
	mu   sync.Mutex
	path string
	doc  *slotDoc
}

func newFilamentSlotStore(dir string) *filamentSlotStore {
	return &filamentSlotStore{path: filepath.Join(dir, "printer-slots.json")}
}

func (st *filamentSlotStore) load() (*slotDoc, error) {
	if st.doc != nil {
		return st.doc, nil
	}
	doc := &slotDoc{Schema: 1, Last: map[string]*printerSlots{}, Chosen: map[string]map[string]slotChoice{}, Spools: map[string]spoolChoice{}}
	b, err := os.ReadFile(st.path)
	if err != nil && !errors.Is(err, os.ErrNotExist) {
		return nil, err
	}
	if err == nil {
		if err := json.Unmarshal(b, doc); err != nil || doc.Schema != 1 {
			return nil, fmt.Errorf("%s: couldn't read printer slot configuration.", st.path)
		}
		if doc.Last == nil {
			doc.Last = map[string]*printerSlots{}
		}
		if doc.Chosen == nil {
			doc.Chosen = map[string]map[string]slotChoice{}
		}
		if doc.Spools == nil {
			doc.Spools = map[string]spoolChoice{}
		}
	}
	st.doc = doc
	return doc, nil
}

func (st *filamentSlotStore) save() error {
	b, err := json.MarshalIndent(st.doc, "", " ")
	if err != nil {
		return err
	}
	return writeFileAtomic(st.path, b)
}

func slotID(t AMSTray) string {
	if t.Kind == "external" {
		if t.Extruder != nil {
			return map[bool]string{true: "ext.L", false: "ext.R"}[*t.Extruder == 0]
		}
		return "ext"
	}
	return fmt.Sprintf("ams%d.%d", t.AMS, t.Slot)
}

func slotLabel(t AMSTray, units int) string {
	switch {
	case t.Kind == "external" && t.Extruder != nil:
		return map[bool]string{true: "Left external spool", false: "Right external spool"}[*t.Extruder == 0]
	case t.Kind == "external":
		return "External spool"
	case t.Kind == "ams-ht":
		return fmt.Sprintf("AMS HT %d", t.AMS-127)
	case units > 1:
		return fmt.Sprintf("AMS %d slot %d", t.AMS+1, t.Slot+1)
	}
	return fmt.Sprintf("Slot %d", t.Slot+1)
}

type libraryFilament struct {
	key, name, typ, id, colour string
}

func firstString(raw json.RawMessage) string {
	var s string
	if json.Unmarshal(raw, &s) == nil {
		return strings.TrimSpace(s)
	}
	var list []string
	if json.Unmarshal(raw, &list) == nil && len(list) > 0 {
		return strings.TrimSpace(list[0])
	}
	return ""
}

func fits(r *v4Record, model string, nozzle float64) bool {
	if r.Fits == nil {
		return true
	}
	modelOK := len(r.Fits.Models) == 0
	for _, m := range r.Fits.Models {
		modelOK = modelOK || m == model
	}
	nozzleOK := len(r.Fits.Nozzles) == 0
	for _, n := range r.Fits.Nozzles {
		nozzleOK = nozzleOK || math.Abs(n-nozzle) < 1e-6
	}
	return modelOK && nozzleOK
}

func sliceable(profiles map[string]*v4Record, printer *v4Record, vendor *VendorPresets, machine *Preset) []libraryFilament {
	out := []libraryFilament{}
	for _, r := range profiles {
		if r.Type != "filament" || !r.live() || !fits(r, printer.Model, printer.Nozzle) {
			continue
		}
		variant := familyVariant(vendor, strings.TrimPrefix(r.Family, vendor.Vendor+"/"), machine.Name)
		if variant == nil || !profileCompatible(variant, machine.Name, "", false) {
			continue
		}
		f := libraryFilament{key: r.ID, name: r.Name, id: firstString(r.Settings["filament_id"]), typ: firstString(r.Settings["filament_type"]), colour: r.Colour}
		if f.id == "" {
			f.id = variant.FilamentID
		}
		if f.typ == "" {
			f.typ = r.Material
		}
		if f.typ == "" {
			f.typ = firstString(variant.Config["filament_type"])
		}
		if f.colour == "" {
			f.colour = firstString(r.Settings["filament_colour"])
		}
		if f.colour == "" {
			f.colour = firstString(variant.Config["filament_colour"])
		}
		out = append(out, f)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].name < out[j].name })
	return out
}

func colourDistance(a, b string) float64 {
	la, oka := lab(a)
	lb, okb := lab(b)
	if !oka || !okb {
		return 1000
	}
	return math.Sqrt((la[0]-lb[0])*(la[0]-lb[0]) + (la[1]-lb[1])*(la[1]-lb[1]) + (la[2]-lb[2])*(la[2]-lb[2]))
}

func lab(hex string) ([3]float64, bool) {
	hex = strings.TrimPrefix(hex, "#")
	if len(hex) < 6 {
		return [3]float64{}, false
	}
	var rgb [3]float64
	for i := range rgb {
		v, err := strconv.ParseUint(hex[2*i:2*i+2], 16, 8)
		if err != nil {
			return [3]float64{}, false
		}
		c := float64(v) / 255
		if c > 0.04045 {
			c = math.Pow((c+0.055)/1.055, 2.4)
		} else {
			c /= 12.92
		}
		rgb[i] = c
	}
	f := func(t float64) float64 {
		if t > 0.008856 {
			return math.Cbrt(t)
		}
		return 7.787*t + 16.0/116
	}
	x := f((0.4124*rgb[0] + 0.3576*rgb[1] + 0.1805*rgb[2]) / 0.9505)
	y := f(0.2126*rgb[0] + 0.7152*rgb[1] + 0.0722*rgb[2])
	z := f((0.0193*rgb[0] + 0.1192*rgb[1] + 0.9505*rgb[2]) / 1.089)
	return [3]float64{116*y - 16, 500 * (x - y), 200 * (y - z)}, true
}

var nonWord = regexp.MustCompile(`[^A-Z0-9]+`)

func sameType(filament, tray string) (exact, word bool) {
	f, t := strings.ToUpper(strings.TrimSpace(filament)), strings.ToUpper(strings.TrimSpace(tray))
	if f == "" || t == "" {
		return false, false
	}
	if f == t {
		return true, true
	}
	padded := " " + nonWord.ReplaceAllString(t, " ") + " "
	return false, strings.Contains(padded, " "+nonWord.ReplaceAllString(f, " ")+" ")
}

func nearest(list []libraryFilament, colour string) *libraryFilament {
	var best *libraryFilament
	for i := range list {
		if best == nil || colourDistance(list[i].colour, colour) < colourDistance(best.colour, colour) {
			best = &list[i]
		}
	}
	return best
}

func genericFor(vendor *VendorPresets, machine *Preset, trayType string) *Preset {
	t := strings.ToUpper(strings.TrimSpace(trayType))
	words := strings.Fields(nonWord.ReplaceAllString(t, " "))
	candidates := []string{t}
	for n := len(words) - 1; n >= 1; n-- {
		candidates = append(candidates, strings.Join(words[:n], " "))
	}
	for _, c := range candidates {
		for i := range vendor.Filaments {
			p := &vendor.Filaments[i]
			if strings.EqualFold(filamentFamilyOf(p.Name), "Generic "+c) && profileCompatible(p, machine.Name, "", false) {
				return p
			}
		}
	}
	return nil
}

var bambuIDs struct {
	once sync.Once
	ids  map[string]string
	err  error
}

func bambuFilamentIDs(resources string) (map[string]string, error) {
	bambuIDs.once.Do(func() {
		var file struct {
			Filaments map[string]struct {
				BambuID string `json:"bambu_id"`
			} `json:"filaments"`
		}
		b, err := os.ReadFile(filepath.Join(resources, "printers", "bambu_filament_ids.json"))
		if err == nil {
			err = json.Unmarshal(b, &file)
		}
		bambuIDs.ids, bambuIDs.err = map[string]string{}, err
		for id, row := range file.Filaments {
			bambuIDs.ids[id] = row.BambuID
		}
	})
	return bambuIDs.ids, bambuIDs.err
}

const sameColour = 15.0

func matchTray(t AMSTray, units int, mine []libraryFilament, chosen map[string]slotChoice, spools map[string]spoolChoice, vendor *VendorPresets, machine *Preset) slotMatch {
	m := slotMatch{Slot: slotID(t), Label: slotLabel(t, units), Tray: slotTray{Type: t.Type, Colour: t.Color, Name: t.Name, FilamentID: t.FilamentID}, Remaining: t.Remaining}
	if t.Spool != "" {
		spool := t.Spool
		m.Spool = &spool
	}
	if m.Remaining != nil && *m.Remaining < 0 {
		m.Remaining = nil
	}
	byKey := map[string]*libraryFilament{}
	for i := range mine {
		byKey[mine[i].key] = &mine[i]
	}
	use := func(f *libraryFilament, rule string) slotMatch {
		key := f.key
		m.Filament, m.Name, m.Rule, m.Colour = &key, f.name, rule, f.colour
		return m
	}
	if c, ok := spools[t.Spool]; ok && t.Spool != "" && byKey[c.Filament] != nil {
		return use(byKey[c.Filament], "spool")
	}
	if c, ok := chosen[m.Slot]; ok && byKey[c.Filament] != nil && strings.EqualFold(c.Type, t.Type) && colourDistance(c.Colour, t.Color) < sameColour {
		return use(byKey[c.Filament], "remembered")
	}
	if t.FilamentID != "" {
		var exact []libraryFilament
		for _, f := range mine {
			if f.id != "" && f.id == t.FilamentID {
				exact = append(exact, f)
			}
		}
		if f := nearest(exact, t.Color); f != nil {
			return use(f, "exact")
		}
	}
	var typed, worded []libraryFilament
	for _, f := range mine {
		exact, word := sameType(f.typ, t.Type)
		if exact {
			typed = append(typed, f)
		} else if word {
			worded = append(worded, f)
		}
	}
	if f := nearest(typed, t.Color); f != nil {
		return use(f, "type")
	}
	if f := nearest(worded, t.Color); f != nil {
		return use(f, "type")
	}
	m.Rule = "generic"
	if g := genericFor(vendor, machine, t.Type); g != nil {
		m.Catalog, m.Name, m.Colour = g.Name, filamentFamilyOf(g.Name), firstString(g.Config["filament_colour"])
	} else {
		m.Name = strings.TrimSpace("Generic " + t.Type)
	}
	return m
}

func (s *Server) printerSlotsFor(key string, profiles map[string]*v4Record) (*printerSlots, error) {
	st, ok := s.sends.hub.current(key)
	s.filamentSlots.mu.Lock()
	defer s.filamentSlots.mu.Unlock()
	doc, err := s.filamentSlots.load()
	if err != nil {
		return nil, err
	}
	if !ok || !st.Online || st.Trays == nil {
		if last := doc.Last[key]; last != nil {
			out := *last
			out.Online = false
			return &out, nil
		}
		return &printerSlots{Slots: []slotMatch{}}, nil
	}
	printer := profiles[key]
	if printer == nil || printer.Type != "printer" {
		return nil, errors.New("That printer isn't in your library yet.")
	}
	vendor, err := s.presets.vendor(printer.Vendor)
	if err != nil {
		return nil, errors.New("Couldn't load the printer catalog: " + printer.Vendor)
	}
	machine := catalogMachine(vendor, printer.Model, printer.Nozzle)
	if machine == nil {
		return nil, errors.New("Couldn't find " + printer.Model + " in the catalog.")
	}
	mine := sliceable(profiles, printer, vendor, machine)
	ids, err := bambuFilamentIDs(s.cfg.Resources)
	if err != nil {
		return nil, errors.New("Couldn't read the Bambu filament lookup table: " + err.Error())
	}
	for i := range mine {
		if id := ids[mine[i].id]; id != "" {
			mine[i].id = id
		}
	}
	units := map[int]bool{}
	for _, t := range st.Trays {
		if t.Kind != "external" && t.Kind != "ams-ht" {
			units[t.AMS] = true
		}
	}
	out := &printerSlots{Online: true, Slots: []slotMatch{}}
	for _, t := range st.Trays {
		if strings.TrimSpace(t.Type) == "" {
			continue
		}
		out.Slots = append(out.Slots, matchTray(t, len(units), mine, doc.Chosen[key], doc.Spools, vendor, machine))
	}
	prev := doc.Last[key]
	a, _ := json.Marshal(out.Slots)
	var b []byte
	if prev != nil {
		b, _ = json.Marshal(prev.Slots)
	}
	if prev == nil || string(a) != string(b) {
		out.Updated = time.Now().UTC()
		doc.Last[key] = &printerSlots{Updated: out.Updated, Slots: out.Slots}
		if err := s.filamentSlots.save(); err != nil {
			return nil, err
		}
	} else {
		out.Updated = prev.Updated
	}
	return out, nil
}

func (s *Server) printerSlotRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /api/printer-slots", func(w http.ResponseWriter, r *http.Request) {
		profiles, err := s.libraryV4Profiles()
		if err != nil {
			fail(w, 503, "Your profile library isn't available right now: " + err.Error())
			return
		}
		out := map[string]*printerSlots{}
		for _, p := range s.sends.connections() {
			if p.Printer == "" || out[p.Printer] != nil {
				continue
			}
			slots, err := s.printerSlotsFor(p.Printer, profiles)
			if err != nil {
				slots = &printerSlots{Slots: []slotMatch{}, Error: err.Error()}
			}
			out[p.Printer] = slots
		}
		writeJSON(w, 200, map[string]any{"printers": out})
	})
	mux.HandleFunc("GET /api/printer-slots/{printer}", func(w http.ResponseWriter, r *http.Request) {
		profiles, err := s.libraryV4Profiles()
		if err != nil {
			fail(w, 503, "Your profile library isn't available right now: " + err.Error())
			return
		}
		slots, err := s.printerSlotsFor(r.PathValue("printer"), profiles)
		if err != nil {
			fail(w, 409, err.Error())
			return
		}
		writeJSON(w, 200, slots)
	})
	choose := func(w http.ResponseWriter, r *http.Request, filament string) {
		key, slot := r.PathValue("printer"), r.PathValue("slot")
		profiles, err := s.libraryV4Profiles()
		if err != nil {
			fail(w, 503, "Your profile library isn't available right now: " + err.Error())
			return
		}
		if f := profiles[filament]; filament != "" && (f == nil || f.Type != "filament" || !f.live()) {
			fail(w, 400, "Please pick a filament from My filaments.")
			return
		}
		current, err := s.printerSlotsFor(key, profiles)
		if err != nil {
			fail(w, 409, err.Error())
			return
		}
		var tray *slotMatch
		for i := range current.Slots {
			if current.Slots[i].Slot == slot {
				tray = &current.Slots[i]
			}
		}
		if tray == nil {
			fail(w, 404, "This slot doesn't have any filament loaded right now.")
			return
		}
		s.filamentSlots.mu.Lock()
		doc, err := s.filamentSlots.load()
		if err == nil {
			if doc.Chosen[key] == nil {
				doc.Chosen[key] = map[string]slotChoice{}
			}
			if filament == "" {
				delete(doc.Chosen[key], slot)
				if tray.Spool != nil {
					delete(doc.Spools, *tray.Spool)
				}
			} else {
				doc.Chosen[key][slot] = slotChoice{Filament: filament, Type: tray.Tray.Type, Colour: tray.Tray.Colour, At: time.Now().UTC()}
				if tray.Spool != nil {
					doc.Spools[*tray.Spool] = spoolChoice{Filament: filament, At: time.Now().UTC()}
				}
			}
			err = s.filamentSlots.save()
		}
		s.filamentSlots.mu.Unlock()
		if err != nil {
			fail(w, 500, err.Error())
			return
		}
		next, err := s.printerSlotsFor(key, profiles)
		if err != nil {
			fail(w, 409, err.Error())
			return
		}
		writeJSON(w, 200, next)
	}
	mux.HandleFunc("PUT /api/printer-slots/{printer}/{slot}", func(w http.ResponseWriter, r *http.Request) {
		var body struct {
			Filament string `json:"filament"`
		}
		if err := json.NewDecoder(http.MaxBytesReader(w, r.Body, 4096)).Decode(&body); err != nil || body.Filament == "" {
			fail(w, 400, "send {filament}")
			return
		}
		choose(w, r, body.Filament)
	})
	mux.HandleFunc("DELETE /api/printer-slots/{printer}/{slot}", func(w http.ResponseWriter, r *http.Request) { choose(w, r, "") })
}
