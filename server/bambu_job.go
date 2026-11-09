package main

import (
	"archive/zip"
	"encoding/xml"
	"errors"
	"io"
	"net/http"
	"strconv"
	"strings"
)

type BambuJobMapping struct {
	Filaments         []int                 `json:"filament_map"`
	Volumes           []int                 `json:"filament_volume_map"`
	HasFilamentSwitch *bool                 `json:"has_filament_switch,omitempty"`
	Materials         []BambuSlicedMaterial `json:"materials"`
}

type BambuSlicedMaterial struct {
	Slot     int     `json:"slot" xml:"id,attr"`
	Type     string  `json:"type" xml:"type,attr"`
	Color    string  `json:"color" xml:"color,attr"`
	ID       string  `json:"id" xml:"tray_info_idx,attr"`
	Flow     string  `json:"flow" xml:"volume_type,attr"`
	Diameter float64 `json:"diameter" xml:"nozzle_diameter,attr"`
	Object   bool    `json:"object" xml:"used_for_object,attr"`
	Support  bool    `json:"support" xml:"used_for_support,attr"`
}

func readBambuJobMapping(path string) (*BambuJobMapping, error) {
	z, err := zip.OpenReader(path)
	if err != nil {
		return nil, err
	}
	defer z.Close()
	out := &BambuJobMapping{}
	for _, file := range z.File {
		if file.Name != "Metadata/model_settings.config" && file.Name != "Metadata/slice_info.config" {
			continue
		}
		r, err := file.Open()
		if err != nil {
			return nil, err
		}
		defer r.Close()
		var config struct {
			Plates []struct {
				Metadata []struct {
					Key   string `xml:"key,attr"`
					Value string `xml:"value,attr"`
				} `xml:"metadata"`
				Filaments []BambuSlicedMaterial `xml:"filament"`
			} `xml:"plate"`
		}
		if err := xml.NewDecoder(io.LimitReader(r, 4<<20)).Decode(&config); err != nil {
			return nil, err
		}
		if len(config.Plates) != 1 {
			return nil, errors.New("Please select a single sliced plate.")
		}
		if file.Name == "Metadata/slice_info.config" {
			out.Materials = config.Plates[0].Filaments
			for _, row := range config.Plates[0].Metadata {
				if row.Key == "has_filament_switcher" {
					value, err := strconv.ParseBool(row.Value)
					if err != nil {
						return nil, err
					}
					out.HasFilamentSwitch = &value
				}
			}
			continue
		}
		for _, row := range config.Plates[0].Metadata {
			var target *[]int
			switch row.Key {
			case "filament_maps":
				target = &out.Filaments
			case "filament_volume_maps":
				target = &out.Volumes
			default:
				continue
			}
			for _, text := range strings.Fields(row.Value) {
				value, err := strconv.Atoi(text)
				if err != nil || value < 0 {
					return nil, errors.New("Oops! The filament mapping for this slice isn't valid.")
				}
				*target = append(*target, value)
			}
		}
	}
	if len(out.Filaments) == 0 {
		return nil, errors.New("This slice doesn't have filament slots assigned yet.")
	}
	return out, nil
}

func (s *Server) bambuJobMapping(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	if j.Request.Output != "gcode.3mf" || j.AwaitFinished(r.Context()) != StateDone {
		fail(w, 409, "No finished Bambu .gcode.3mf package was found for this job.")
		return
	}
	mapping, err := readBambuJobMapping(j.OutputPath("gcode.3mf"))
	if err != nil {
		fail(w, 422, "Couldn't read the nozzle assignment from the sliced file: " + err.Error())
		return
	}
	writeJSON(w, 200, mapping)
}
