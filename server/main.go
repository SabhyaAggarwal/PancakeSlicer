package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"syscall"
	"time"
)

type Config struct {
	DataDir       string
	Listen        string
	Tool          string
	Resources     string
	JobRoot       string
	StaticDir     string
	MaxJobs       int
	JobTimeout    time.Duration
	JobTTL        time.Duration
	MaxUploadSize int64
}

func env(key, def string) string {
	if v, ok := os.LookupEnv(key); ok {
		return v
	}
	return def
}

func envInt(key string, def int) int {
	if v, err := strconv.Atoi(env(key, "")); err == nil {
		return v
	}
	return def
}

func envDuration(key string, def time.Duration) time.Duration {
	if v, err := time.ParseDuration(env(key, "")); err == nil {
		return v
	}
	return def
}

func parseConfig() Config {
	var c Config
	flag.StringVar(&c.DataDir, "data-dir", env("ORCA_WEB_DATA_DIR", "data"), "persistent user preset directory (ORCA_WEB_DATA_DIR)")
	flag.StringVar(&c.Listen, "listen", env("ORCA_WEB_LISTEN", "127.0.0.1:8780"), "listen address (ORCA_WEB_LISTEN)")
	flag.StringVar(&c.Tool, "tool", env("ORCA_WEB_TOOL", "build-nogui/tests/fff_print/nonplanar_slice_tool"), "nonplanar_slice_tool path (ORCA_WEB_TOOL)")
	flag.StringVar(&c.Resources, "resources", env("ORCA_WEB_RESOURCES", "resources"), "resources directory holding profiles/ and nonplanar/ (ORCA_WEB_RESOURCES)")
	flag.StringVar(&c.JobRoot, "job-root", env("ORCA_WEB_JOB_ROOT", filepath.Join(os.TempDir(), "orca-webapp-jobs")), "scratch directory for job files (ORCA_WEB_JOB_ROOT)")
	flag.StringVar(&c.StaticDir, "static", env("ORCA_WEB_STATIC", "web/dist"), "static front end to serve at / when it exists (ORCA_WEB_STATIC)")
	flag.IntVar(&c.MaxJobs, "max-jobs", envInt("ORCA_WEB_MAX_JOBS", 2), "slices running at once; more jobs queue (ORCA_WEB_MAX_JOBS)")
	flag.DurationVar(&c.JobTimeout, "job-timeout", envDuration("ORCA_WEB_JOB_TIMEOUT", 15*time.Minute), "wall-clock limit of one slice (ORCA_WEB_JOB_TIMEOUT)")
	flag.DurationVar(&c.JobTTL, "job-ttl", envDuration("ORCA_WEB_JOB_TTL", 2*time.Hour), "finished jobs and their files are deleted after this (ORCA_WEB_JOB_TTL)")
	flag.Int64Var(&c.MaxUploadSize, "max-upload", int64(envInt("ORCA_WEB_MAX_UPLOAD_MB", 512))<<20, "largest accepted upload in bytes (ORCA_WEB_MAX_UPLOAD_MB in MB)")
	flag.Parse()
	if c.MaxJobs < 1 {
		log.Fatal("max-jobs must be positive")
	}
	if c.JobTimeout <= 0 || c.JobTTL <= 0 || c.MaxUploadSize <= 0 {
		log.Fatal("job-timeout, job-ttl and max-upload must be positive")
	}
	for _, p := range []*string{&c.Tool, &c.Resources, &c.JobRoot, &c.StaticDir, &c.DataDir} {
		if abs, err := filepath.Abs(*p); err == nil {
			*p = abs
		}
	}
	return c
}

func main() {
	cfg := parseConfig()
	if st, err := os.Stat(cfg.Tool); err != nil || st.IsDir() {
		log.Printf("warning: tool %s not found; jobs will fail until it is built", cfg.Tool)
	}
	if st, err := os.Stat(filepath.Join(cfg.Resources, "profiles")); err != nil || !st.IsDir() {
		log.Fatalf("no profiles directory under %s", cfg.Resources)
	}
	jobs, err := NewJobManager(cfg)
	if err != nil {
		log.Fatal(err)
	}
	users, err := NewUserPresetStore(filepath.Join(cfg.DataDir, "user-presets"))
	if err != nil {
		log.Fatal(err)
	}
	presets := NewPresetIndex(filepath.Join(cfg.Resources, "profiles"))
	for _, vendor := range []string{"BBL", "Prusa"} {
		if _, err := presets.vendor(vendor); err != nil {
			log.Printf("cannot preload %s presets: %v", vendor, err)
		}
	}
	srv := &http.Server{Addr: cfg.Listen, Handler: NewServer(cfg, jobs, presets, users), ReadHeaderTimeout: 10 * time.Second}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	shutdownDone := make(chan struct{})
	go func() {
		defer close(shutdownDone)
		<-ctx.Done()
		jobs.Shutdown()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		srv.Shutdown(shutdownCtx)
	}()
	log.Printf("listening on %s (tool %s, resources %s, jobs %s, %d at once)", cfg.Listen, cfg.Tool, cfg.Resources, cfg.JobRoot, cfg.MaxJobs)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
	<-shutdownDone
}
