package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"net/http"
	"strings"
	"unicode"
)

func validProfileName(name string) error {
	if name == "" || len(name) > 200 || strings.TrimSpace(name) != name {
		return errors.New("Profile names must be between 1 and 200 characters (without leading or trailing spaces).")
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return errors.New("Profile names can't contain control characters.")
		}
	}
	return nil
}

type libraryReceipt struct {
	Digest   string `json:"digest"`
	Revision uint64 `json:"revision"`
}
type libraryError struct {
	Message  string `json:"error"`
	Code     string `json:"code"`
	Recovery string `json:"recovery"`
	Revision uint64 `json:"revision,omitempty"`
}

func libraryFailure(w http.ResponseWriter, status int, code, message, recovery string, revision uint64) {
	writeJSON(w, status, libraryError{message, code, recovery, revision})
}
func libraryRevision(doc map[string]json.RawMessage) uint64 {
	var revision uint64
	json.Unmarshal(doc["revision"], &revision)
	return revision
}

func firstConfigText(raw json.RawMessage) string {
	var list []any
	if json.Unmarshal(raw, &list) == nil {
		if len(list) == 0 {
			return ""
		}
		return strings.TrimSpace(fmt.Sprint(list[0]))
	}
	var scalar any
	json.Unmarshal(raw, &scalar)
	if scalar == nil {
		return ""
	}
	text, _, _ := strings.Cut(fmt.Sprint(scalar), ",")
	return strings.Trim(strings.TrimSpace(text), `"`)
}

func sameMm(a, b float64) bool { return math.Abs(a-b) < 0.0005 }
