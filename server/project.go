package main

import (
	"archive/zip"
	"bytes"
	"encoding/xml"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

type projectInstance struct {
	Printable *bool     `json:"printable"`
	Transform string    `json:"-"`
	Delta     []float64 `json:"transform"`
}

func projectTransformsWorkaround(path string, edits []projectInstance) error {
	source, err := savedProjectInstances(path)
	if err != nil {
		return err
	}
	if len(source) != len(edits) {
		return fmt.Errorf("project build item count changed")
	}
	for i := range edits {
		if edits[i].Delta == nil {
			continue
		}
		if len(edits[i].Delta) != 12 {
			return fmt.Errorf("project transform needs 12 numbers")
		}
		var delta modelTransform
		for k, v := range edits[i].Delta {
			if math.IsNaN(v) || math.IsInf(v, 0) {
				return fmt.Errorf("non-finite project transform")
			}
			delta[k] = v
		}
		t, err := transformValue(source[i].Transform)
		if err != nil {
			return err
		}
		values := make([]string, 12)
		for r := 0; r < 4; r++ {
			for c := 0; c < 3; c++ {
				v := 0.0
				if r == 3 {
					v = delta[9+c]
				}
				for k := 0; k < 3; k++ {
					v += t[r*3+k] * delta[k*3+c]
				}
				values[r*3+c] = strconv.FormatFloat(v, 'g', -1, 64)
			}
		}
		edits[i].Transform = strings.Join(values, " ")
	}
	return patchProject(path, edits)
}

var buildPattern = regexp.MustCompile(`(?s)<build\b[^>]*>.*?</build>`)
var itemPattern = regexp.MustCompile(`<item\b[^>]*>`)
var printablePattern = regexp.MustCompile(`\sprintable="[^"]*"`)
var transformPattern = regexp.MustCompile(`\stransform="([^"]*)"`)

func patchProject(path string, instances []projectInstance) error {
	if len(instances) == 0 {
		return nil
	}
	src, err := zip.OpenReader(path)
	if err != nil {
		return err
	}
	defer src.Close()
	root := "3D/3dmodel.model"
	for _, f := range src.File {
		if strings.EqualFold(f.Name, "_rels/.rels") {
			r, err := f.Open()
			if err != nil {
				return err
			}
			var rels struct {
				Items []struct {
					Type   string `xml:"Type,attr"`
					Target string `xml:"Target,attr"`
				} `xml:"Relationship"`
			}
			err = xml.NewDecoder(io.LimitReader(r, 1<<20)).Decode(&rels)
			r.Close()
			if err != nil {
				return err
			}
			for _, rel := range rels.Items {
				if strings.HasSuffix(rel.Type, "/3dmodel") {
					root = strings.TrimPrefix(rel.Target, "/")
				}
			}
		}
	}
	tmp, err := os.CreateTemp(filepath.Dir(path), ".project-*.3mf")
	if err != nil {
		return err
	}
	name := tmp.Name()
	defer os.Remove(name)
	dst := zip.NewWriter(tmp)
	found := false
	for _, f := range src.File {
		if !strings.EqualFold(f.Name, root) {
			if err = dst.Copy(f); err != nil {
				break
			}
			continue
		}
		found = true
		var r io.ReadCloser
		r, err = f.Open()
		if err != nil {
			break
		}
		var data []byte
		data, err = io.ReadAll(io.LimitReader(r, 256<<20))
		r.Close()
		if err != nil {
			break
		}
		n := 0
		data = buildPattern.ReplaceAllFunc(data, func(build []byte) []byte {
			return itemPattern.ReplaceAllFunc(build, func(item []byte) []byte {
				if n >= len(instances) {
					n++
					return item
				}
				state := instances[n]
				n++
				if state.Printable != nil {
					value := "0"
					if *state.Printable {
						value = "1"
					}
					item = printablePattern.ReplaceAll(item, nil)
					item = bytes.Replace(item, []byte("<item"), []byte(`<item printable="`+value+`"`), 1)
				}
				if state.Transform != "" {
					item = transformPattern.ReplaceAll(item, nil)
					item = bytes.Replace(item, []byte("<item"), []byte(`<item transform="`+state.Transform+`"`), 1)
				}
				return item
			})
		})
		if n != len(instances) {
			err = fmt.Errorf("project has %d build items, expected %d", n, len(instances))
			break
		}
		var w io.Writer
		w, err = dst.CreateHeader(&f.FileHeader)
		if err != nil {
			break
		}
		_, err = w.Write(data)
		if err != nil {
			break
		}
	}
	if closeErr := dst.Close(); err == nil {
		err = closeErr
	}
	if closeErr := tmp.Close(); err == nil {
		err = closeErr
	}
	if err != nil {
		return err
	}
	if !found {
		return fmt.Errorf("project has no root model part")
	}
	if err = src.Close(); err != nil {
		return err
	}
	return os.Rename(name, path)
}

func savedProjectInstances(path string) ([]projectInstance, error) {
	src, err := zip.OpenReader(path)
	if err != nil {
		return nil, err
	}
	defer src.Close()
	for _, f := range src.File {
		if !strings.HasSuffix(strings.ToLower(f.Name), ".model") {
			continue
		}
		r, err := f.Open()
		if err != nil {
			return nil, err
		}
		decoder := xml.NewDecoder(r)
		var out []projectInstance
		inBuild := false
		for {
			token, parseErr := decoder.Token()
			if parseErr == io.EOF {
				break
			}
			if parseErr != nil {
				r.Close()
				return nil, parseErr
			}
			switch t := token.(type) {
			case xml.StartElement:
				if t.Name.Local == "build" {
					inBuild = true
				}
				if inBuild && t.Name.Local == "item" {
					state := projectInstance{}
					for _, a := range t.Attr {
						if a.Name.Local == "transform" {
							state.Transform = a.Value
						}
						if a.Name.Local == "printable" {
							v := a.Value != "0"
							state.Printable = &v
						}
					}
					if state.Transform == "" {
						state.Transform = "1 0 0 0 1 0 0 0 1 0 0 0"
					}
					out = append(out, state)
				}
			case xml.EndElement:
				if t.Name.Local == "build" {
					inBuild = false
				}
			}
		}
		r.Close()
		if len(out) > 0 {
			return out, nil
		}
	}
	return nil, fmt.Errorf("project has no build items")
}
