package main

import (
	"fmt"
	"net/url"
	"runtime/debug"
	"strings"
)

var buildRevision string
var buildImage string

type About struct {
	Version               string `json:"version"`
	Revision              string `json:"revision"`
	Image                 string `json:"image"`
	License               string `json:"license"`
	SourceURL             string `json:"source_url"`
	SourceConfigured      bool   `json:"source_configured"`
	SourceSHA256          string `json:"source_sha256"`
	ProfileRecoverySchema int    `json:"profile_recovery_schema"`
}

func validateSourceURL(source string) error {
	if source == "" {
		return nil
	}
	u, err := url.Parse(strings.ReplaceAll(source, "{revision}", "revision"))
	if err != nil || u.Host == "" || (u.Scheme != "https" && u.Scheme != "http") || u.User != nil {
		return fmt.Errorf("source-url must be an HTTP(S) Corresponding Source repository or {revision} URL template")
	}
	return nil
}

func buildAbout(c Config) About {
	source := c.SourceURL
	revision := env("ORCA_BUILD_REVISION", buildRevision)
	if revision == "" {
		if info, ok := debug.ReadBuildInfo(); ok {
			for _, setting := range info.Settings {
				if setting.Key == "vcs.revision" {
					revision = setting.Value
				}
			}
			for _, setting := range info.Settings {
				if setting.Key == "vcs.modified" && setting.Value == "true" {
					revision += "-dirty"
				}
			}
		}
	}
	if revision == "" {
		revision = "unknown"
	}
	version := revision
	if len(version) > 12 {
		version = version[:12]
	}
	if strings.HasSuffix(revision, "-dirty") {
		version += "-dirty"
	}
	if source == "" {
		if file, err := openSourceArchive(c); err == nil {
			file.Close()
			source = "/api/source"
		}
	} else if strings.Contains(source, "{revision}") {
		source = strings.ReplaceAll(source, "{revision}", url.PathEscape(revision))
	} else {
		source = strings.TrimRight(source, "/") + "/tree/" + url.PathEscape(revision)
	}
	return About{Version: version, Revision: revision, Image: env("ORCA_BUILD_IMAGE", buildImage), License: "AGPL-3.0", SourceURL: source, SourceConfigured: source != "", SourceSHA256: sourceSHA256(), ProfileRecoverySchema: 1}
}
