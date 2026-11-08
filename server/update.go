package main

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"time"
)

type releaseAsset struct {
	Name string `json:"name"`
	URL  string `json:"browser_download_url"`
}
type release struct {
	Tag    string         `json:"tag_name"`
	URL    string         `json:"html_url"`
	Assets []releaseAsset `json:"assets"`
}

const releaseEvery, releaseRetry = 6 * time.Hour, 15 * time.Minute

type releaseFeed struct {
	repo, token string
	mu          sync.Mutex
	latest      *release
	next        time.Time
}

func (f *releaseFeed) get(ctx context.Context) *release {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.repo == "" || time.Now().Before(f.next) {
		return f.latest
	}
	latest, err := f.read(ctx)
	if err != nil {
		f.next = time.Now().Add(releaseRetry)
		return f.latest
	}
	f.latest, f.next = latest, time.Now().Add(releaseEvery)
	return f.latest
}

func (f *releaseFeed) read(ctx context.Context) (*release, error) {
	ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, "https://api.github.com/repos/"+f.repo+"/releases/latest", nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Accept", "application/vnd.github+json")
	if f.token != "" {
		req.Header.Set("Authorization", "Bearer "+f.token)
	}
	res, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	if res.StatusCode == http.StatusNotFound {
		return nil, nil
	}
	if res.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("release feed answered %s", res.Status)
	}
	var out release
	if err := json.NewDecoder(http.MaxBytesReader(nil, res.Body, 4<<20)).Decode(&out); err != nil {
		return nil, err
	}
	return &out, nil
}

func (s *Server) updateRoutes(mux *http.ServeMux) {
	feed := &releaseFeed{repo: strings.Trim(s.cfg.ReleaseFeed, "/"), token: s.cfg.ReleaseToken}
	mux.HandleFunc("GET /api/update", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		writeJSON(w, 200, map[string]any{"release": feed.get(context.Background())})
	})
}
