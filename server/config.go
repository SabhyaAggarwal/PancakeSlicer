package main

import (
	_ "embed"
	"encoding/json"
	"errors"
	"fmt"
	"regexp"
	"sort"
	"strings"
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

var hiddenSetting = regexp.MustCompile(`print_host|printhost|printer_agent|host_type|api_key|bbl_use_printhost|upload|cloud|device|octoprint|_plugin_config_overrides`)

func overrides(raw string) ([][2]string, error) {
	var values map[string]json.RawMessage
	if err := json.Unmarshal([]byte(raw), &values); err != nil || values == nil {
		return nil, errors.New("overrides must be a JSON object")
	}
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	out := make([][2]string, 0, len(keys))
	for _, key := range keys {
		if !settingKey.MatchString(key) || hiddenSetting.MatchString(key) || configKinds[key] == "" {
			return nil, fmt.Errorf("invalid override key %q", key)
		}
		value, err := configValue(key, values[key])
		if err != nil {
			return nil, fmt.Errorf("override %s: %w", key, err)
		}
		out = append(out, [2]string{key, value})
	}
	return out, nil
}

func escapeConfigString(value string) string {
	return strings.NewReplacer("\\", "\\\\", "\"", "\\\"", "\r", "\\r", "\n", "\\n").Replace(value)
}

func scalarConfigValue(value any) (string, error) {
	switch v := value.(type) {
	case string:
		return v, nil
	case json.Number:
		return string(v), nil
	case bool:
		if v {
			return "1", nil
		}
		return "0", nil
	default:
		return "", errors.New("expected string, number, boolean or flat array")
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
			return "", errors.New("this string option expects a scalar value")
		}
		parts := make([]string, len(array))
		for i, element := range array {
			part, err := scalarConfigValue(element)
			if err != nil {
				return "", err
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
	return scalar, nil
}
