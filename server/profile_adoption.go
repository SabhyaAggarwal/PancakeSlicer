package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"sort"
	"strings"
)

type profileAdoption struct {
	Scope               string                     `json:"scope"`
	SourceID            string                     `json:"sourceId"`
	Digest              string                     `json:"digest"`
	Source              string                     `json:"source"`
	IdentityResolutions []serviceProfileResolution `json:"identityResolutions,omitempty"`
}
type serviceProfileResolution struct {
	Path      string `json:"path"`
	SHA256    string `json:"sha256"`
	ProfileID string `json:"profileId"`
}
type profileAdoptionReceipt struct {
	profileAdoption
	OperationID string `json:"operationId"`
	Revision    uint64 `json:"revision"`
	Durable     bool   `json:"durable"`
}

func (a *profileAdoption) validate() error {
	prefix := "browser-profile-library:v1:"
	if strings.HasPrefix(a.SourceID, serviceProfilePrefix) {
		prefix = serviceProfilePrefix
	}
	if a.Scope != "profile-library-and-working-setup" || !strings.HasPrefix(a.SourceID, prefix) || len(a.SourceID) <= len(prefix) || len(a.SourceID) > 500 || a.Source == "" {
		return errors.New("adoption requires scope, stable sourceId and the exact readonly source string")
	}
	sum := sha256.Sum256([]byte(a.Source))
	if hex.EncodeToString(sum[:]) != a.Digest {
		return errors.New("legacy source string does not match its digest")
	}
	var capture map[string]json.RawMessage
	if json.Unmarshal([]byte(a.Source), &capture) != nil || capture == nil {
		return errors.New("legacy source must encode a JSON source capture object")
	}
	if raw := capture["complete"]; string(raw) != "true" {
		return errors.New("an incomplete or unknown source capture cannot be acknowledged")
	}
	if prefix == serviceProfilePrefix && (stringValue(capture["kind"]) != "service-user-presets" || a.SourceID != prefix+stringValue(capture["origin"])) {
		return errors.New("service profile capture requires its original service provenance")
	}
	if prefix != serviceProfilePrefix && len(a.IdentityResolutions) > 0 {
		return errors.New("service file identity resolutions cannot alter a browser capture")
	}
	return nil
}

func (a *profileAdoption) sameSource(b *profileAdoption) bool {
	return a != nil && b != nil && a.Scope == b.Scope && a.SourceID == b.SourceID && a.Digest == b.Digest && a.Source == b.Source
}

func (s *Server) profileAdoptionRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /api/profile-library/adoptions", func(w http.ResponseWriter, r *http.Request) {
		s.library.mu.Lock()
		defer s.library.mu.Unlock()
		w.Header().Set("Cache-Control", "no-store")
		value, err := s.library.readLibrary()
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "Adoption acknowledgements could not be read. Keep legacy sources and retry.", "retry_storage", 0)
			return
		}
		receipts := []profileAdoptionReceipt{}
		for _, receipt := range value.Adoptions {
			if source := r.URL.Query().Get("sourceId"); source == "" || receipt.SourceID == source {
				receipts = append(receipts, receipt)
			}
		}
		sort.Slice(receipts, func(i, j int) bool { return receipts[i].Revision < receipts[j].Revision })
		writeJSON(w, 200, receipts)
	})
	mux.HandleFunc("GET /api/profile-library/adoptions/{digest}", func(w http.ResponseWriter, r *http.Request) {
		s.library.mu.Lock()
		defer s.library.mu.Unlock()
		w.Header().Set("Cache-Control", "no-store")
		value, err := s.library.readLibrary()
		if err != nil {
			libraryFailure(w, 503, "library_unavailable", "Adoption acknowledgements could not be read. Keep legacy sources and retry.", "retry_storage", 0)
			return
		}
		receipt, ok := value.Adoptions[r.PathValue("digest")]
		if !ok {
			libraryFailure(w, 404, "adoption_missing", "This legacy source capture has not been acknowledged.", "adopt_inventory", 0)
			return
		}
		writeJSON(w, 200, receipt)
	})
}
