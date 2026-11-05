package main

import (
	_ "embed"
	"encoding/json"
)

//go:embed schemas.json
var schemasJSON []byte

var schemas = func() (s struct {
	Library   int `json:"library_schema"`
	Workspace int `json:"workspace_schema"`
}) {
	if err := json.Unmarshal(schemasJSON, &s); err != nil || s.Library <= 0 || s.Workspace <= 0 {
		panic("schemas.json must name positive library_schema and workspace_schema")
	}
	return
}()
