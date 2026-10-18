package main

import (
	"archive/zip"
	"bytes"
	"context"
	"encoding/json"
	"encoding/xml"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

type projectInstance struct {
	Printable  *bool   `json:"printable"`
	Z          float64 `json:"z"`
	Transform  string  `json:"-"`
	ObjectID   string  `json:"-"`
	IdentifyID string  `json:"-"`
}

var buildPattern = regexp.MustCompile(`(?s)<build\b[^>]*>.*?</build>`)
var itemPattern = regexp.MustCompile(`<item\b[^>]*>`)
var printablePattern = regexp.MustCompile(`\sprintable="[^"]*"`)
var transformPattern = regexp.MustCompile(`\stransform="([^"]*)"`)

func patchProject(path string, instances []projectInstance, shiftZ bool) error {
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
	hasIdentity := false
	for _, i := range instances {
		hasIdentity = hasIdentity || i.IdentifyID != ""
	}
	for _, f := range src.File {
		if hasIdentity && strings.EqualFold(f.Name, "Metadata/model_settings.config") {
			err = rewriteInstanceIdentities(dst, f, instances)
			if err != nil {
				break
			}
			continue
		}
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
				} else if shiftZ && state.Z != 0 {
					match := transformPattern.FindSubmatch(item)
					values := strings.Fields("1 0 0 0 1 0 0 0 1 0 0 0")
					if len(match) > 1 {
						values = strings.Fields(string(match[1]))
					}
					if len(values) != 12 {
						return item
					}
					z, parseErr := strconv.ParseFloat(values[11], 64)
					if parseErr != nil {
						return item
					}
					values[11] = strconv.FormatFloat(z+state.Z, 'g', -1, 64)
					item = transformPattern.ReplaceAll(item, nil)
					item = bytes.Replace(item, []byte("<item"), []byte(`<item transform="`+strings.Join(values, " ")+`"`), 1)
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

func plateInstances(raw []byte) ([]projectInstance, error) {
	var plate struct {
		Objects []struct {
			Instances []projectInstance `json:"instances"`
		} `json:"objects"`
	}
	if err := json.Unmarshal(raw, &plate); err != nil {
		return nil, err
	}
	var out []projectInstance
	for _, o := range plate.Objects {
		out = append(out, o.Instances...)
	}
	return out, nil
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
						if a.Name.Local == "objectid" {
							state.ObjectID = a.Value
						}
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
			for _, part := range src.File {
				if !strings.EqualFold(part.Name, "Metadata/model_settings.config") {
					continue
				}
				r, err := part.Open()
				if err != nil {
					return nil, err
				}
				var settings struct {
					Plates []struct {
						Instances []instanceMetadata `xml:"model_instance"`
					} `xml:"plate"`
				}
				err = xml.NewDecoder(r).Decode(&settings)
				r.Close()
				if err != nil {
					return nil, err
				}
				ids := map[string]string{}
				for _, plate := range settings.Plates {
					for _, item := range plate.Instances {
						values := item.values()
						ids[values["object_id"]+":"+values["instance_id"]] = values["identify_id"]
					}
				}
				counts := map[string]int{}
				for i := range out {
					key := out[i].ObjectID
					out[i].IdentifyID = ids[key+":"+strconv.Itoa(counts[key])]
					counts[key]++
				}
			}
			return out, nil
		}
	}
	return nil, fmt.Errorf("project has no build items")
}

func platePrintableWorkaround(raw []byte, savedPath string) ([]byte, error) {
	if savedPath != "" {
		instances, err := plateInstances(raw)
		if err != nil {
			return nil, err
		}
		for i := range instances {
			instances[i].Z = 0
		}
		return raw, patchProject(savedPath, instances, false)
	}
	var plate map[string]json.RawMessage
	var objects []map[string]json.RawMessage
	if json.Unmarshal(raw, &plate) != nil || json.Unmarshal(plate["objects"], &objects) != nil {
		return nil, fmt.Errorf("invalid plate")
	}
	kept := objects[:0]
	for _, obj := range objects {
		var instances []map[string]json.RawMessage
		if value, ok := obj["instances"]; ok {
			if json.Unmarshal(value, &instances) != nil || len(instances) == 0 {
				return nil, fmt.Errorf("instances must be a non-empty array")
			}
		} else {
			instances = []map[string]json.RawMessage{obj}
		}
		live := instances[:0]
		for _, instance := range instances {
			printable := true
			if value, ok := instance["printable"]; ok && json.Unmarshal(value, &printable) != nil {
				return nil, fmt.Errorf("printable must be boolean")
			}
			if printable {
				live = append(live, instance)
			}
		}
		if len(live) > 0 {
			obj["instances"], _ = json.Marshal(live)
			kept = append(kept, obj)
		}
	}
	if len(kept) == 0 {
		return nil, fmt.Errorf("plate has no printable instances")
	}
	plate["objects"], _ = json.Marshal(kept)
	return json.Marshal(plate)
}

func plateZWorkaround(ctx context.Context, m *JobManager, j *Job, args []string) ([]string, error) {
	if !j.Request.Plate {
		return args, nil
	}
	raw, err := os.ReadFile(filepath.Join(j.Dir, "input", "plate.json"))
	if err != nil {
		return nil, err
	}
	instances, err := plateInstances(raw)
	if err != nil {
		return nil, err
	}
	needsProject := false
	for i := range instances {
		needsProject = needsProject || instances[i].Z != 0
		instances[i].Printable = nil
	}
	if !needsProject {
		return args, nil
	}
	if j.Request.Output != "3mf" {
		save := &Job{Dir: j.Dir, Request: j.Request}
		save.Request.Output = "3mf"
		cmd, err := lineBufferedCommand(ctx, m.cfg.Tool, m.toolArgs(save))
		if err != nil {
			return nil, err
		}
		cmd.Dir = j.Dir
		cmd.Env = append(os.Environ(), "TMPDIR="+filepath.Join(j.Dir, "tmp"))
		configureKill(cmd)
		cmd.WaitDelay = 5 * time.Second
		var stderr bytes.Buffer
		cmd.Stderr = &stderr
		if err = cmd.Run(); err != nil {
			return nil, fmt.Errorf("cannot place plate: %w: %s", err, stderr.String())
		}
		args = append(args[:len(args)-2], j.OutputPath("3mf"))
	}
	return args, patchProject(j.OutputPath("3mf"), instances, true)
}

func loadedProjectSaveWorkaround(input, output string) error {
	source, err := savedProjectInstances(input)
	if err != nil {
		return err
	}
	saved, err := savedProjectInstances(output)
	if err != nil {
		return err
	}
	before, err := projectBounds(input, true)
	if err != nil {
		return err
	}
	after, err := projectBounds(output, true)
	if err != nil {
		return err
	}
	if len(source) != len(saved) || len(before) != len(after) || len(before) != len(saved) {
		return fmt.Errorf("native save changed the build item count")
	}
	for i := range saved {
		t, err := transformValue(saved[i].Transform)
		if err != nil {
			return err
		}
		for axis := 0; axis < 3; axis++ {
			t[9+axis] += before[i].box.min[axis] - after[i].box.min[axis]
		}
		values := make([]string, len(t))
		for k, v := range t {
			values[k] = strconv.FormatFloat(v, 'g', -1, 64)
		}
		saved[i].Transform = strings.Join(values, " ")
		saved[i].IdentifyID = source[i].IdentifyID
	}
	return patchProject(output, saved, false)
}

type instanceMetadata struct {
	Entries []struct {
		Key   string `xml:"key,attr"`
		Value string `xml:"value,attr"`
	} `xml:"metadata"`
}

func (m instanceMetadata) values() map[string]string {
	out := map[string]string{}
	for _, e := range m.Entries {
		out[e.Key] = e.Value
	}
	return out
}

func rewriteInstanceIdentities(dst *zip.Writer, part *zip.File, instances []projectInstance) error {
	r, err := part.Open()
	if err != nil {
		return err
	}
	data, err := io.ReadAll(io.LimitReader(r, 16<<20))
	r.Close()
	if err != nil {
		return err
	}
	pattern := regexp.MustCompile(`<metadata key="identify_id" value="[^"]*"`)
	numeric := regexp.MustCompile(`^[0-9]+$`)
	n := 0
	data = pattern.ReplaceAllFunc(data, func(entry []byte) []byte {
		defer func() { n++ }()
		if n < len(instances) && numeric.MatchString(instances[n].IdentifyID) {
			return []byte(`<metadata key="identify_id" value="` + instances[n].IdentifyID + `"`)
		}
		return entry
	})
	w, err := dst.CreateHeader(&part.FileHeader)
	if err != nil {
		return err
	}
	_, err = w.Write(data)
	return err
}
