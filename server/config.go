package main

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"sort"
	"strings"
	"unicode"
)

//go:generate python3 generate_config_types.py ../web/src/options.generated.json
//go:embed config_types.json
var configTypeData []byte

var configKinds = func() map[string]string {
	var groups map[string][]string
	if err := json.Unmarshal(configTypeData, &groups); err != nil {
		panic(err)
	}
	kinds := map[string]string{}
	for kind, keys := range groups {
		for _, key := range keys {
			kinds[key] = kind
		}
	}
	return kinds
}()

var settingKey = regexp.MustCompile(`^[a-zA-Z][a-zA-Z0-9_]*$`)

var hiddenSetting = substrings{"print_host", "printhost", "printer_agent", "host_type", "api_key", "bbl_use_printhost", "upload", "cloud", "device", "octoprint", "_plugin_config_overrides"}

type substrings []string

func (s substrings) MatchString(key string) bool {
	for _, part := range s {
		if strings.Contains(key, part) {
			return true
		}
	}
	return false
}

func overrides(raw string) ([][2]string, error) {
	if len(raw) > 256<<10 {
		return nil, errors.New("Settings overrides exceed the 256 KiB limit.")
	}
	var values map[string]json.RawMessage
	if err := json.Unmarshal([]byte(raw), &values); err != nil || values == nil {
		return nil, errors.New("Settings overrides must be a JSON object.")
	}
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	out := make([][2]string, 0, len(keys))
	for _, saved := range keys {
		key := canonicalKey(saved)
		if _, both := values[key]; both && key != saved {
			continue
		}
		if !settingKey.MatchString(key) || hiddenSetting.MatchString(key) || configKinds[key] == "" {
			return nil, fmt.Errorf("Unrecognized setting override key: %q", key)
		}
		value, err := configValue(key, values[saved])
		if err == nil && len(value) > 64<<10 {
			err = errors.New("Setting value exceeds the 64 KiB limit.")
		}
		if err != nil {
			return nil, fmt.Errorf("override %s: %w", key, err)
		}
		out = append(out, [2]string{key, value})
	}
	return migrateFastCurvedTops(out), nil
}

func escapeConfigString(value string) string {
	return strings.NewReplacer("\\", "\\\\", "\"", "\\\"", "\r", "\\r", "\n", "\\n").Replace(value)
}

func scalarConfigValue(value any) (string, error) {
	switch v := value.(type) {
	case string:
		if len(v) > 32<<10 {
			return "", errors.New("Text value exceeds the 32 KiB limit.")
		}
		if strings.IndexFunc(v, func(r rune) bool { return unicode.IsControl(r) && r != '\n' && r != '\r' && r != '\t' }) >= 0 {
			return "", errors.New("Value contains invalid control characters.")
		}
		return v, nil
	case json.Number:
		if len(v) > 32<<10 {
			return "", errors.New("Text value exceeds the 32 KiB limit.")
		}
		return string(v), nil
	case bool:
		if v {
			return "1", nil
		}
		return "0", nil
	default:
		return "", errors.New("Expected a string, number, boolean, or flat list.")
	}
}

func configValue(key string, raw json.RawMessage) (string, error) {
	var value any
	decoder := json.NewDecoder(strings.NewReader(string(raw)))
	decoder.UseNumber()
	if err := decoder.Decode(&value); err != nil {
		return "", err
	}
	kind := configKinds[key]
	if array, ok := value.([]any); ok {
		if kind == "string" {
			return "", errors.New("This text setting expects a single value.")
		}
		parts := make([]string, len(array))
		for i, element := range array {
			part, err := scalarConfigValue(element)
			if err != nil {
				return "", err
			}
			if kind != "strings" && strings.IndexFunc(part, unicode.IsControl) >= 0 {
				return "", errors.New("Value contains invalid control characters.")
			}
			if kind == "strings" && ((len(array) == 1 && part == "") || strings.ContainsAny(part, " \t;\\\"\r\n")) {
				part = "\"" + escapeConfigString(part) + "\""
			}
			parts[i] = part
		}
		separator := ","
		if kind == "" && len(array) > 0 {
			if _, stringsOnly := array[0].(string); stringsOnly {
				separator = ";"
			}
		}
		if kind == "strings" {
			separator = ";"
		}
		if kind == "groups" {
			separator = "#"
		}
		return strings.Join(parts, separator), nil
	}
	scalar, err := scalarConfigValue(value)
	if err != nil {
		return "", err
	}
	if kind != "string" && kind != "strings" && strings.IndexFunc(scalar, unicode.IsControl) >= 0 {
		return "", errors.New("Value contains invalid control characters.")
	}
	return scalar, nil
}

var renamedKeys = map[string]string{"nonplanar_curved_shells": "nonplanar_curved_top_layers"}

func canonicalKey(key string) string {
	if to, ok := renamedKeys[key]; ok {
		return to
	}
	return key
}

const fastCurvedTops, curvedTops = "curved_tops", "nonplanar_top"

func migrateFastCurvedTops(pairs [][2]string) [][2]string {
	fast := false
	for i, kv := range pairs {
		if kv[0] == "nonplanar_mode" && kv[1] == fastCurvedTops {
			pairs[i][1], fast = curvedTops, true
		}
	}
	if !fast {
		return pairs
	}
	out := pairs[:0]
	for _, kv := range pairs {
		if kv[0] != "nonplanar_curved_top_layers" {
			out = append(out, kv)
		}
	}
	return append(out, [2]string{"nonplanar_curved_top_layers", "1"})
}

func migratedMethod(mode string) string {
	if mode == fastCurvedTops {
		return curvedTops
	}
	return mode
}
