package main

import (
	"compress/gzip"
	"io"
	"log"
	"net/http"
	"strings"
)

const gzipMinBytes = 1024

func gzipJSON(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch encoding := strings.ToLower(strings.TrimSpace(r.Header.Get("Content-Encoding"))); encoding {
		case "", "identity":
		case "gzip":
			z, err := gzip.NewReader(r.Body)
			if err != nil {
				log.Printf("%s %s 400: unreadable gzip request body", logPrefix(r.Method, 32), logPrefix(r.URL.Path, 256))
				fail(w, 400, "gzip request body is unreadable")
				return
			}
			r.Body = struct {
				io.Reader
				io.Closer
			}{z, r.Body}
			r.Header.Del("Content-Encoding")
			r.Header.Del("Content-Length")
			r.ContentLength = -1
		default:
			log.Printf("%s %s 415: request Content-Encoding %q", logPrefix(r.Method, 32), logPrefix(r.URL.Path, 256), logPrefix(encoding, 64))
			fail(w, http.StatusUnsupportedMediaType, "request bodies may be gzip or identity")
			return
		}
		if !strings.HasPrefix(r.URL.Path, "/api/") || !acceptsGzip(r) {
			next.ServeHTTP(w, r)
			return
		}
		g := &gzipWriter{ResponseWriter: w}
		defer g.close()
		next.ServeHTTP(g, r)
	})
}

type gzipWriter struct {
	http.ResponseWriter
	status  int
	decided bool
	plain   bool
	pending []byte
	z       *gzip.Writer
}

func (g *gzipWriter) WriteHeader(status int) {
	if g.status == 0 {
		g.status = status
	}
	h := g.Header()
	if status >= 200 && status != http.StatusNoContent && status != http.StatusNotModified && status != http.StatusPartialContent &&
		h.Get("Content-Encoding") == "" && strings.HasPrefix(h.Get("Content-Type"), "application/json") {
		return
	}
	g.decide(true)
}

func (g *gzipWriter) decide(plain bool) {
	if g.decided {
		return
	}
	g.decided, g.plain = true, plain
	if g.status == 0 {
		g.status = http.StatusOK
	}
	if !plain {
		h := g.Header()
		h.Del("Content-Length")
		h.Set("Content-Encoding", "gzip")
		h.Add("Vary", "Accept-Encoding")
		g.z, _ = gzip.NewWriterLevel(g.ResponseWriter, gzip.BestSpeed)
	}
	g.ResponseWriter.WriteHeader(g.status)
}

func (g *gzipWriter) Write(b []byte) (int, error) {
	if g.status == 0 {
		g.WriteHeader(http.StatusOK)
	}
	if !g.decided {
		g.pending = append(g.pending, b...)
		if len(g.pending) < gzipMinBytes {
			return len(b), nil
		}
		g.decide(false)
		if _, err := g.z.Write(g.pending); err != nil {
			return 0, err
		}
		g.pending = nil
		return len(b), nil
	}
	if g.plain {
		return g.ResponseWriter.Write(b)
	}
	return g.z.Write(b)
}

func (g *gzipWriter) close() {
	if !g.decided && g.status != 0 {
		g.decide(true)
		g.ResponseWriter.Write(g.pending)
	}
	if g.z != nil {
		g.z.Close()
	}
}

func (g *gzipWriter) Flush() {
	if !g.decided && g.status != 0 {
		g.decide(len(g.pending) < gzipMinBytes)
		if g.plain {
			g.ResponseWriter.Write(g.pending)
		} else {
			g.z.Write(g.pending)
		}
		g.pending = nil
	}
	if g.z != nil {
		g.z.Flush()
	}
	http.NewResponseController(g.ResponseWriter).Flush()
}

func (g *gzipWriter) Unwrap() http.ResponseWriter { return g.ResponseWriter }
