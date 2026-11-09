package main

import (
	"archive/zip"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io"
	"strings"
)

func savedProjectMethods(path string) (map[string]any, []map[string]any, error) {
	archive, err := zip.OpenReader(path)
	if err != nil {
		return nil, nil, err
	}
	defer archive.Close()
	global := map[string]any{}
	scopes := []map[string]any{}
	for _, file := range archive.File {
		name := strings.ToLower(file.Name)
		if name != "metadata/project_settings.config" && name != "metadata/model_settings.config" && name != "metadata/layer_config_ranges.xml" {
			continue
		}
		if file.UncompressedSize64 > 32<<20 {
			return nil, nil, fmt.Errorf("project method metadata exceeds 32 MiB")
		}
		stream, err := file.Open()
		if err != nil {
			return nil, nil, err
		}
		raw, err := io.ReadAll(io.LimitReader(stream, (32<<20)+1))
		stream.Close()
		if err != nil {
			return nil, nil, err
		}
		if len(raw) > 32<<20 {
			return nil, nil, fmt.Errorf("project method metadata exceeds 32 MiB")
		}
		if name == "metadata/project_settings.config" {
			var config map[string]any
			if err := json.Unmarshal(raw, &config); err != nil {
				return nil, nil, err
			}
			for _, key := range []string{"nonplanar_mode", "nonplanar_wave"} {
				if value, ok := config[key]; ok {
					if text, ok := value.(string); ok {
						value = migratedMethod(text)
					}
					global[key] = value
				}
			}
			continue
		}
		decoder := xml.NewDecoder(strings.NewReader(string(raw)))
		for {
			token, err := decoder.Token()
			if err == io.EOF {
				break
			}
			if err != nil {
				return nil, nil, err
			}
			element, ok := token.(xml.StartElement)
			if !ok {
				continue
			}
			key, value := "", ""
			for _, attr := range element.Attr {
				switch attr.Name.Local {
				case "key", "opt_key":
					key = attr.Value
				case "value":
					value = attr.Value
				}
			}
			if key != "nonplanar_mode" && key != "nonplanar_wave" {
				continue
			}
			if element.Name.Local == "option" {
				if err := decoder.DecodeElement(&value, &element); err != nil {
					return nil, nil, err
				}
			}
			scopes = append(scopes, map[string]any{key: migratedMethod(strings.TrimSpace(value))})
		}
	}
	return global, scopes, nil
}
