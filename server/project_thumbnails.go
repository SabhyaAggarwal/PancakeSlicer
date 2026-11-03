package main

import (
	"bytes"
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"net/http"
	"strings"
	"time"
)

const thumbnailRoute = "/api/project-thumbnails/"

func thumbnailVersion(data string) string {
	sum := sha256.Sum256([]byte(data))
	return hex.EncodeToString(sum[:6])
}

func thumbnailURL(id, data string) string {
	return thumbnailRoute + id + "?v=" + thumbnailVersion(data)
}

func thumbnailImage(record json.RawMessage) (data, contentType string, image []byte, ok bool) {
	var r struct {
		Meta struct {
			Thumbnail *string `json:"thumbnail"`
		} `json:"meta"`
	}
	if json.Unmarshal(record, &r) != nil || r.Meta.Thumbnail == nil {
		return "", "", nil, false
	}
	data = *r.Meta.Thumbnail
	header, payload, found := strings.Cut(data, ",")
	contentType, isBase64 := strings.CutSuffix(strings.TrimPrefix(header, "data:"), ";base64")
	if !found || !strings.HasPrefix(header, "data:image/") || !isBase64 {
		return "", "", nil, false
	}
	image, err := base64.StdEncoding.DecodeString(payload)
	return data, contentType, image, err == nil
}

func (st *workspaceStore) thumbnailRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /api/project-thumbnails/{id}", func(w http.ResponseWriter, r *http.Request) {
		var data, contentType string
		var image []byte
		found, read := false, false
		st.locked(w, func(d *workspaceData) {
			read = true
			id, err := canonicalWorkspaceProject(d, r.PathValue("id"))
			if p, ok := d.Projects[id]; err == nil && ok && p.Record != nil {
				data, contentType, image, found = thumbnailImage(p.Record)
			}
		})
		if !read {
			return
		}
		if !found {
			workspaceFailure(w, 404, "thumbnail_missing", "This project has no thumbnail.", "open_projects", 0)
			return
		}
		version := thumbnailVersion(data)
		if r.URL.Query().Get("v") == version {
			w.Header().Set("Cache-Control", "private, max-age=31536000, immutable")
		} else {
			w.Header().Set("Cache-Control", "private, no-cache")
		}
		w.Header().Set("Content-Type", contentType)
		w.Header().Set("ETag", `"`+version+`"`)
		http.ServeContent(w, r, "", time.Time{}, bytes.NewReader(image))
	})
}
