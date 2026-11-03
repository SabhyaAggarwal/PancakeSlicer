package main

import (
	"context"
	"fmt"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"strings"
	"time"
)

func fallbackOrigins(value string) ([]*url.URL, error) {
	var origins []*url.URL
	for _, entry := range strings.Split(value, ",") {
		if strings.TrimSpace(entry) == "" {
			continue
		}
		origin, err := url.Parse(strings.TrimSpace(entry))
		if err != nil {
			return nil, err
		}
		ip := net.ParseIP(origin.Hostname())
		if origin.Scheme != "http" || (origin.Hostname() != "localhost" && (ip == nil || !ip.IsLoopback())) || origin.User != nil || (origin.Path != "" && origin.Path != "/") || origin.RawQuery != "" || origin.Fragment != "" {
			return nil, fmt.Errorf("fallback backend must be an HTTP loopback origin")
		}
		origin.Path = ""
		origins = append(origins, origin)
	}
	return origins, nil
}

func (s *Server) fallbackJobReads(next http.Handler) http.Handler {
	origins, _ := fallbackOrigins(s.cfg.FallbackBackend)
	if len(origins) == 0 {
		return next
	}
	client := &http.Client{CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		path := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/jobs/"), "/")
		if r.Method != http.MethodGet || !strings.HasPrefix(r.URL.Path, "/api/jobs/") || !jobIDPattern.MatchString(path[0]) || s.jobs.Get(path[0]) != nil || r.Header.Get("X-Orca-Job-Fallback") != "" {
			next.ServeHTTP(w, r)
			return
		}
		unavailable := false
		for _, origin := range origins {
			ctx, cancel := context.WithTimeout(r.Context(), 3*time.Second)
			lookup, _ := http.NewRequestWithContext(ctx, http.MethodGet, origin.String()+"/api/jobs/"+path[0], nil)
			lookup.Header.Set("X-Orca-Job-Fallback", "1")
			response, err := client.Do(lookup)
			code := 0
			if err == nil {
				code = response.StatusCode
				response.Body.Close()
			}
			cancel()
			if code == http.StatusOK {
				proxy := httputil.NewSingleHostReverseProxy(origin)
				director := proxy.Director
				proxy.Director = func(request *http.Request) {
					director(request)
					request.Header.Set("X-Orca-Job-Fallback", "1")
					request.Header.Del("Authorization")
					request.Header.Del("Cookie")
				}
				proxy.FlushInterval = -1
				proxy.ServeHTTP(w, r)
				return
			}
			unavailable = unavailable || err != nil || code >= 500
		}
		if unavailable {
			fail(w, http.StatusBadGateway, "historical job service unavailable; retry shortly")
			return
		}
		next.ServeHTTP(w, r)
	})
}
