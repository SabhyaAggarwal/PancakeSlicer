package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"crypto/sha256"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"
)

//go:embed options.json
var optionData []byte

type cachedResponse struct {
	plain, zipped []byte
	etag          string
}

func cacheResponse(b []byte) *cachedResponse {
	var zipped bytes.Buffer
	z := gzip.NewWriter(&zipped)
	z.Write(b)
	z.Close()
	return &cachedResponse{b, zipped.Bytes(), fmt.Sprintf(`"%x"`, sha256.Sum256(b))}
}
func cacheJSON(value any) (*cachedResponse, error) {
	b, err := json.Marshal(value)
	if err != nil {
		return nil, err
	}
	return cacheResponse(append(b, '\n')), nil
}

func acceptsGzip(r *http.Request) bool {
	wildcard := false
	for _, part := range strings.Split(r.Header.Get("Accept-Encoding"), ",") {
		fields := strings.Split(strings.TrimSpace(part), ";")
		name := strings.ToLower(strings.TrimSpace(fields[0]))
		quality := 1.0
		for _, parameter := range fields[1:] {
			key, value, ok := strings.Cut(strings.TrimSpace(parameter), "=")
			if ok && strings.EqualFold(key, "q") {
				q, err := strconv.ParseFloat(value, 64)
				if err != nil {
					quality = 0
				} else {
					quality = q
				}
			}
		}
		if name == "gzip" {
			return quality > 0
		}
		if name == "*" {
			wildcard = quality > 0
		}
	}
	return wildcard
}
func (c *cachedResponse) serve(w http.ResponseWriter, r *http.Request, name, contentType, policy string, modified time.Time) {
	w.Header().Set("Vary", "Accept-Encoding")
	w.Header().Set("Cache-Control", policy)
	w.Header().Set("Content-Type", contentType)
	data, etag := c.plain, c.etag
	if acceptsGzip(r) {
		data = c.zipped
		etag = strings.TrimSuffix(etag, `"`) + `-gzip"`
		w.Header().Set("Content-Encoding", "gzip")
	}
	w.Header().Set("ETag", etag)
	w.Header().Set("Content-Length", strconv.Itoa(len(data)))
	http.ServeContent(w, r, name, modified, bytes.NewReader(data))
}

const catalogCache = "public, max-age=300, must-revalidate"

func serveStream(w http.ResponseWriter, r *http.Request, f *os.File, st os.FileInfo, policy string) {
	w.Header().Set("Vary", "Accept-Encoding")
	w.Header().Set("Cache-Control", policy)
	etag := fmt.Sprintf(`W/"%x-%x"`, st.ModTime().UnixNano(), st.Size())
	w.Header().Set("ETag", etag)
	if header := r.Header.Get("If-None-Match"); header != "" {
		for _, tag := range strings.Split(header, ",") {
			if strings.TrimSpace(tag) == "*" || strings.TrimPrefix(strings.TrimSpace(tag), "W/") == strings.TrimPrefix(etag, "W/") {
				w.WriteHeader(http.StatusNotModified)
				return
			}
		}
	}
	if !acceptsGzip(r) {
		http.ServeContent(w, r, st.Name(), st.ModTime(), f)
		return
	}
	w.Header().Set("Content-Encoding", "gzip")
	w.Header().Set("Last-Modified", st.ModTime().UTC().Format(http.TimeFormat))
	if r.Method == http.MethodHead {
		return
	}
	z := gzip.NewWriter(w)
	defer z.Close()
	io.Copy(z, f)
}

func serveGzipped(w http.ResponseWriter, r *http.Request, path string, st os.FileInfo, suffix, etag string, encode func(src io.Reader, dst io.Writer, flush func() error) error) {
	tag := fmt.Sprintf(etag, st.ModTime().UnixNano(), st.Size())
	h := w.Header()
	h.Set("Cache-Control", "private, max-age=31536000, immutable")
	h.Set("ETag", tag)
	h.Set("Vary", "Accept-Encoding")
	if match := r.Header.Get("If-None-Match"); match == tag || match == "*" {
		w.WriteHeader(http.StatusNotModified)
		return
	}
	cached := path + suffix
	if c, err := os.Stat(cached); err != nil || c.ModTime().Before(st.ModTime()) {
		streamed, err := writeGzipped(w, r, path, cached, encode)
		if streamed {
			return
		}
		if err != nil {
			fail(w, 500, "the output could not be compressed: "+err.Error())
			return
		}
	}
	f, err := os.Open(cached)
	if err != nil {
		fail(w, 500, "the output could not be opened")
		return
	}
	defer f.Close()
	if acceptsGzip(r) {
		c, _ := f.Stat()
		h.Set("Content-Encoding", "gzip")
		h.Set("Content-Length", fmt.Sprint(c.Size()))
		if r.Method != http.MethodHead {
			io.Copy(w, f)
		}
		return
	}
	z, err := gzip.NewReader(f)
	if err != nil {
		fail(w, 500, "the output could not be opened")
		return
	}
	if r.Method != http.MethodHead {
		io.Copy(w, z)
	}
}

func writeGzipped(w http.ResponseWriter, r *http.Request, path, cached string, encode func(io.Reader, io.Writer, func() error) error) (streamed bool, err error) {
	in, err := os.Open(path)
	if err != nil {
		return false, err
	}
	defer in.Close()
	tmp, err := os.CreateTemp(filepath.Dir(cached), filepath.Base(cached)+".*.tmp")
	if err != nil {
		return false, err
	}
	defer os.Remove(tmp.Name())
	defer tmp.Close()
	file := bufio.NewWriterSize(tmp, 1<<20)
	streamed = acceptsGzip(r) && r.Method != http.MethodHead
	out := io.Writer(file)
	if streamed {
		w.Header().Set("Content-Encoding", "gzip")
		out = io.MultiWriter(file, w)
	}
	z := gzip.NewWriter(out)
	controller := http.NewResponseController(w)
	flush := func() error {
		if !streamed {
			return nil
		}
		if err := z.Flush(); err != nil {
			return err
		}
		return controller.Flush()
	}
	err = encode(in, z, flush)
	if err == nil {
		err = z.Close()
	}
	if err == nil {
		err = file.Flush()
	}
	if cerr := tmp.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp.Name(), cached)
	}
	return streamed, err
}

type staticEntry struct {
	modified time.Time
	size     int64
	response *cachedResponse
}
type staticCache struct {
	mu    sync.Mutex
	files map[string]staticEntry
}

var hashedAsset = regexp.MustCompile(`-[a-zA-Z0-9_-]{8,}\.[a-zA-Z0-9]+$`)

func mimeType(path string) string {
	t := mime.TypeByExtension(filepath.Ext(path))
	if t == "" {
		return "application/octet-stream"
	}
	return t
}
func (c *staticCache) serve(w http.ResponseWriter, r *http.Request, path string, st os.FileInfo) {
	policy := "public, max-age=300, must-revalidate"
	if filepath.Base(path) == "index.html" {
		policy = "no-cache"
	} else if hashedAsset.MatchString(filepath.Base(path)) {
		policy = "public, max-age=31536000, immutable"
	}
	contentType := mimeType(path)
	if st.Size() > 8<<20 {
		f, err := os.Open(path)
		if err != nil {
			http.NotFound(w, r)
			return
		}
		defer f.Close()
		w.Header().Set("Content-Type", contentType)
		serveStream(w, r, f, st, policy)
		return
	}
	c.mu.Lock()
	entry, ok := c.files[path]
	if !ok || entry.size != st.Size() || !entry.modified.Equal(st.ModTime()) {
		b, err := os.ReadFile(path)
		if err != nil {
			c.mu.Unlock()
			http.NotFound(w, r)
			return
		}
		entry = staticEntry{st.ModTime(), st.Size(), cacheResponse(b)}
		c.files[path] = entry
	}
	c.mu.Unlock()
	entry.response.serve(w, r, st.Name(), contentType, policy, st.ModTime())
}
