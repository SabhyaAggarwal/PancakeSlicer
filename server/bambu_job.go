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
	Filaments []int `json:"filament_map"`
	Volumes   []int `json:"filament_volume_map"`
}

func readBambuJobMapping(path string) (*BambuJobMapping, error) {
	z, err := zip.OpenReader(path)
	if err != nil {
		return nil, err
	}
	defer z.Close()
	for _, file := range z.File {
		if file.Name != "Metadata/model_settings.config" {
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
			} `xml:"plate"`
		}
		if err := xml.NewDecoder(io.LimitReader(r, 4<<20)).Decode(&config); err != nil {
			return nil, err
		}
		if len(config.Plates) != 1 {
			return nil, errors.New("expected one sliced plate")
		}
		out := &BambuJobMapping{}
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
					return nil, errors.New("invalid sliced filament assignment")
				}
				*target = append(*target, value)
			}
		}
		if len(out.Filaments) == 0 {
			return nil, errors.New("slice has no resolved filament assignment")
		}
		return out, nil
	}
	return nil, errors.New("slice has no plate settings")
}

func (s *Server) bambuJobMapping(w http.ResponseWriter, r *http.Request) {
	j := s.job(w, r)
	if j == nil {
		return
	}
	if j.Request.Output != "gcode.3mf" || j.AwaitFinished(r.Context()) != StateDone {
		fail(w, 409, "job has no completed Bambu G-code package")
		return
	}
	mapping, err := readBambuJobMapping(j.OutputPath("gcode.3mf"))
	if err != nil {
		fail(w, 422, "cannot read the sliced nozzle assignment: "+err.Error())
		return
	}
	writeJSON(w, 200, mapping)
}
