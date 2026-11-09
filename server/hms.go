package main

import (
	"bytes"
	"compress/gzip"
	_ "embed"
	"encoding/json"
	"io"
	"strings"
	"sync"
)

//go:embed hms_en.json.gz
var hmsTableGz []byte

var hmsText = sync.OnceValue(func() map[string]string {
	out := map[string]string{}
	r, err := gzip.NewReader(bytes.NewReader(hmsTableGz))
	if err == nil {
		var b []byte
		if b, err = io.ReadAll(r); err == nil {
			err = json.Unmarshal(b, &out)
		}
	}
	if err != nil {
		panic("hms_en.json.gz: " + err.Error())
	}
	return out
})

func describeHMS(messages []PrinterMessage) []PrinterMessage {
	if messages == nil {
		return nil
	}
	out := make([]PrinterMessage, 0, len(messages))
	for _, m := range messages {
		if m.Message == "" {
			m.Message = hmsText()[strings.ToUpper(strings.ReplaceAll(m.Code, "-", ""))]
		}
		out = append(out, m)
	}
	return out
}
