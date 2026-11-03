package main

import (
	"crypto/sha256"
	"encoding/base64"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"sort"
	"strings"
)

const serviceProfilePrefix = "service-profile-library:v1:"

var errLegacyProfilesPending = errors.New("original service profiles require lossless adoption before submission")

type legacyServiceFile struct {
	Path   string `json:"path"`
	Size   int64  `json:"size"`
	SHA256 string `json:"sha256"`
	Mode   uint32 `json:"mode"`
	Bytes  string `json:"bytes"`
}
type legacyServiceSource struct {
	Version     int                 `json:"version"`
	Complete    bool                `json:"complete"`
	Kind        string              `json:"kind"`
	Origin      string              `json:"origin"`
	Root        string              `json:"root"`
	Directories []string            `json:"directories"`
	Files       []legacyServiceFile `json:"files"`
}

func sourceHash(raw []byte) string {
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:])
}

func (s *ProfileLibraryStore) captureLegacyProfiles() (*profileAdoption, error) {
	abs, err := filepath.Abs(s.dir)
	if err != nil {
		return nil, err
	}
	origin := "service-data:" + sourceHash([]byte(filepath.Clean(abs)))
	scan := func() (*legacyServiceSource, error) {
		source := &legacyServiceSource{1, true, "service-user-presets", origin, "user-presets", []string{}, []legacyServiceFile{}}
		var total int64
		var visit func(string) error
		visit = func(relative string) error {
			full := filepath.Join(s.dir, filepath.FromSlash(relative))
			before, err := os.Lstat(full)
			if errors.Is(err, os.ErrNotExist) && relative == source.Root {
				return nil
			}
			if err != nil {
				return err
			}
			if before.Mode()&os.ModeSymlink != 0 || (!before.IsDir() && !before.Mode().IsRegular()) {
				return errors.New("legacy profile inventory contains a symbolic link or nonregular entry")
			}
			if before.IsDir() {
				source.Directories = append(source.Directories, relative)
				if len(source.Directories)+len(source.Files) > 4096 {
					return errors.New("legacy profile inventory exceeds 4096 entries; export it before adoption")
				}
				entries, err := os.ReadDir(full)
				if err != nil {
					return err
				}
				for _, entry := range entries {
					if err := visit(relative + "/" + entry.Name()); err != nil {
						return err
					}
				}
			} else {
				total += before.Size()
				if total > 2<<20 || len(source.Directories)+len(source.Files) >= 4096 {
					return errors.New("legacy profile inventory exceeds the bounded adoption size; export it before adoption")
				}
				file, err := os.Open(full)
				if err != nil {
					return err
				}
				opened, statErr := file.Stat()
				if statErr != nil || !os.SameFile(before, opened) {
					file.Close()
					return errors.New("legacy profile file changed while opening")
				}
				raw, readErr := io.ReadAll(io.LimitReader(file, before.Size()+1))
				after, statErr := file.Stat()
				closeErr := file.Close()
				if err := errors.Join(readErr, statErr, closeErr); err != nil {
					return err
				}
				if int64(len(raw)) != before.Size() || !sameLegacyEntry(before, after) {
					return errors.New("legacy profile file changed while reading")
				}
				if strings.EqualFold(filepath.Ext(relative), ".json") {
					if err := legacyProfileIdentity(raw); err != nil {
						return err
					}
				}
				source.Files = append(source.Files, legacyServiceFile{relative, before.Size(), sourceHash(raw), uint32(before.Mode()), base64.StdEncoding.EncodeToString(raw)})
			}
			after, err := os.Lstat(full)
			if err != nil || !sameLegacyEntry(before, after) {
				return errors.New("legacy profile inventory changed while reading")
			}
			return nil
		}
		if err := visit(source.Root); err != nil {
			return nil, err
		}
		sort.Strings(source.Directories)
		sort.Slice(source.Files, func(i, j int) bool { return source.Files[i].Path < source.Files[j].Path })
		return source, nil
	}
	first, err := scan()
	if err != nil {
		return nil, err
	}
	second, err := scan()
	if err != nil || !reflect.DeepEqual(first, second) {
		return nil, errors.New("legacy profile inventory is unreadable or changed between captures")
	}
	if len(first.Files) == 0 {
		return nil, nil
	}
	raw, err := json.Marshal(first)
	if err != nil {
		return nil, err
	}
	return &profileAdoption{Scope: "profile-library-and-working-setup", SourceID: serviceProfilePrefix + origin, Digest: sourceHash(raw), Source: string(raw)}, nil
}

func sameLegacyEntry(a, b os.FileInfo) bool {
	return a != nil && b != nil && os.SameFile(a, b) && a.Mode() == b.Mode() && a.Size() == b.Size() && a.ModTime() == b.ModTime()
}

func legacyProfileIdentity(raw []byte) error {
	var fields map[string]json.RawMessage
	if !json.Valid(raw) {
		return errors.New("legacy profile JSON requires explicit recovery before adoption")
	}
	if json.Unmarshal(raw, &fields) != nil || fields == nil {
		return nil
	}
	if fields["name"] == nil && fields["type"] == nil && fields["id"] == nil && fields["overrides"] == nil {
		return nil
	}
	if validProfileName(stringValue(fields["name"])) != nil || !validProfileType(stringValue(fields["type"])) {
		return errors.New("legacy profile name/type is unresolved; retain and recover the original inventory")
	}
	if id, ok := fields["id"]; ok && (!strings.HasPrefix(stringValue(id), "profile:") || len(stringValue(id)) <= 8 || len(stringValue(id)) > 200 || strings.ContainsAny(stringValue(id), "\r\n\x00")) {
		return errors.New("legacy profile stable identity is malformed; retain and recover the original inventory")
	}
	return nil
}

func (s *ProfileLibraryStore) pendingLegacyProfiles(envelope *libraryEnvelope) (*profileAdoption, error) {
	source, err := s.captureLegacyProfiles()
	if err != nil || source == nil {
		return source, err
	}
	if receipt, ok := envelope.Adoptions[source.Digest]; ok && receipt.Durable && receipt.sameSource(source) {
		return nil, nil
	}
	return source, nil
}

func legacyInventoryFailure(w http.ResponseWriter, revision uint64) {
	libraryFailure(w, 503, "legacy_inventory_unavailable", "The original service profile inventory is unreadable, changing or needs identity recovery. Keep the originals and export them before retrying; no profiles were changed.", "export_profiles", revision)
}
func legacyProfilesPending(w http.ResponseWriter, envelope *libraryEnvelope, adoption *profileAdoption) {
	w.Header().Set("Cache-Control", "no-store")
	writeJSON(w, 409, struct {
		libraryError
		Document profileLibraryDocument `json:"document"`
		Adoption *profileAdoption       `json:"adoption"`
	}{libraryError{"Original service profiles require explicit lossless adoption before new library changes.", "legacy_profiles_pending", "adopt_service_profiles", libraryRevision(envelope.Document)}, envelope.Document, adoption})
}

func (a *profileAdoption) operationID() string {
	if strings.HasPrefix(a.SourceID, serviceProfilePrefix) {
		return "service-adoption:" + a.Digest
	}
	return "browser-adoption:" + a.Digest
}

func (a *profileAdoption) validateIdentityResolutions(profiles map[string]map[string]json.RawMessage, envelope *libraryEnvelope) error {
	var source legacyServiceSource
	if err := json.Unmarshal([]byte(a.Source), &source); err != nil {
		return err
	}
	choices := map[string]serviceProfileResolution{}
	for _, choice := range a.IdentityResolutions {
		if _, duplicate := choices[choice.Path]; duplicate || !strings.HasPrefix(choice.ProfileID, "profile:") || len(choice.ProfileID) <= 8 {
			return errors.New("each identity choice requires a unique source path and an explicit stable profile ID")
		}
		choices[choice.Path] = choice
	}
	usedIDs := map[string]bool{}
	for _, file := range source.Files {
		if !strings.EqualFold(filepath.Ext(file.Path), ".json") {
			continue
		}
		raw, err := base64.StdEncoding.DecodeString(file.Bytes)
		if err != nil {
			return err
		}
		var fields map[string]json.RawMessage
		json.Unmarshal(raw, &fields)
		if !validProfileType(stringValue(fields["type"])) || fields["name"] == nil {
			continue
		}
		id := stringValue(fields["id"])
		acknowledged := a.acknowledgedFile(file, envelope)
		choice, chosen := choices[file.Path]
		if id != "" {
			if chosen {
				return errors.New("an existing stable source ID cannot be rewritten by an identity choice")
			}
			if !acknowledged && (profiles[id] == nil || stringValue(profiles[id]["type"]) != stringValue(fields["type"])) {
				return errors.New("a new or changed identified source profile must be retained with its original stable ID and type")
			}
		} else {
			if !chosen || choice.SHA256 != file.SHA256 {
				return errors.New("every ID-less legacy profile requires an explicit identity bound to its exact file SHA256")
			}
			id = choice.ProfileID
			if !acknowledged && (profiles[id] == nil || stringValue(profiles[id]["type"]) != stringValue(fields["type"])) {
				return errors.New("the confirmed identity must exist in the submitted library with the original profile type")
			}
			for _, receipt := range envelope.Adoptions {
				if receipt.SourceID == a.SourceID {
					for _, prior := range receipt.IdentityResolutions {
						if prior.Path == file.Path && prior.SHA256 == file.SHA256 && prior.ProfileID != id {
							return errors.New("an unchanged acknowledged file already has a different confirmed identity")
						}
					}
				}
			}
			delete(choices, file.Path)
		}
		if usedIDs[id] {
			return errors.New("multiple source records resolve to one identity; reconcile the collision explicitly")
		}
		usedIDs[id] = true
	}
	if len(choices) > 0 {
		return errors.New("an identity choice references a foreign, unknown or already-identified source file")
	}
	return nil
}

func (a *profileAdoption) acknowledgedFile(file legacyServiceFile, envelope *libraryEnvelope) bool {
	for _, receipt := range envelope.Adoptions {
		if !receipt.Durable || receipt.SourceID != a.SourceID {
			continue
		}
		var baseline legacyServiceSource
		if json.Unmarshal([]byte(receipt.Source), &baseline) == nil {
			for _, prior := range baseline.Files {
				if prior.Path == file.Path && prior.SHA256 == file.SHA256 {
					return true
				}
			}
		}
	}
	return false
}
