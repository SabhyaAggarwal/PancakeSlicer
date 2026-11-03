package main

import (
	"bytes"
	"crypto/sha256"
	"fmt"
	"net/http"
	"strings"
)

const privateRevalidate = "private, no-cache"

func revalidate(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.URL.Path, "/api/") {
			next.ServeHTTP(w, r)
			return
		}
		v := &revalidateWriter{ResponseWriter: w, r: r}
		next.ServeHTTP(v, r)
		v.finish()
	})
}

type revalidateWriter struct {
	http.ResponseWriter
	r         *http.Request
	status    int
	buffering bool
	body      bytes.Buffer
}

func (v *revalidateWriter) WriteHeader(status int) {
	if v.status != 0 {
		return
	}
	v.status = status
	h := v.Header()
	if h.Get("Cache-Control") == "" {
		h.Set("Cache-Control", "no-store")
	}
	if status == http.StatusOK && (v.r.Method == http.MethodGet || v.r.Method == http.MethodHead) &&
		h.Get("Cache-Control") == privateRevalidate && h.Get("ETag") == "" && h.Get("Content-Encoding") == "" {
		v.buffering = true
		return
	}
	v.ResponseWriter.WriteHeader(status)
}

func (v *revalidateWriter) Write(b []byte) (int, error) {
	if v.status == 0 {
		v.WriteHeader(http.StatusOK)
	}
	if v.buffering {
		return v.body.Write(b)
	}
	return v.ResponseWriter.Write(b)
}

func (v *revalidateWriter) finish() {
	if !v.buffering {
		return
	}
	sum := sha256.Sum256(v.body.Bytes())
	tag := fmt.Sprintf(`W/"%x"`, sum[:16])
	h := v.Header()
	h.Set("ETag", tag)
	for _, candidate := range strings.Split(v.r.Header.Get("If-None-Match"), ",") {
		if c := strings.TrimSpace(candidate); c == "*" || strings.TrimPrefix(c, "W/") == strings.TrimPrefix(tag, "W/") {
			h.Del("Content-Length")
			h.Del("Content-Type")
			v.ResponseWriter.WriteHeader(http.StatusNotModified)
			return
		}
	}
	v.ResponseWriter.WriteHeader(http.StatusOK)
	v.ResponseWriter.Write(v.body.Bytes())
}

func (v *revalidateWriter) Flush() {
	if !v.buffering {
		http.NewResponseController(v.ResponseWriter).Flush()
	}
}

func (v *revalidateWriter) Unwrap() http.ResponseWriter { return v.ResponseWriter }
