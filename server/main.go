package main

import (
	"context"
	"errors"
	"flag"
	"log"
	"math"
	"net/http"
	"net/netip"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)

type Config struct {
	FallbackBackend string
	SourceURL       string
	SourceArchive   string
	DataDir         string
	Listen          string
	Tool            string
	Resources       string
	JobRoot         string
	StaticDir       string
	MaxJobs         int
	JobTimeout      time.Duration
	JobTTL          time.Duration
	MaxUploadSize   int64
	UploadMinRate   int64
	SubmitRate      float64
	SubmitBurst     int
	MaxClientJobs   int
	MaxQueue        int
	BearerToken     string
	DiskQuota       int64
	RequestLog      bool
	Metrics         bool
	TrustedProxies  []netip.Prefix
	PrintersFile    string
	Printers        []*PrinterConfig
	PrinterEdit     string
	HTTPPrinters    string
	PrinterStore    *printerStore
	Features        ToolFeatures
	Options         *cachedResponse
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

func envBool(key string) bool {
	v, _ := strconv.ParseBool(env(key, "false"))
	return v
}

func parseConfig() Config {
	var c Config
	flag.StringVar(&c.SourceArchive, "source-archive", env("ORCA_WEB_SOURCE_ARCHIVE", ""), "mounted Corresponding Source archive matching build metadata (ORCA_WEB_SOURCE_ARCHIVE)")
	flag.StringVar(&c.SourceURL, "source-url", env("ORCA_WEB_SOURCE_URL", ""), "Corresponding Source repository URL or exact URL containing {revision} (ORCA_WEB_SOURCE_URL)")
	flag.StringVar(&c.DataDir, "data-dir", env("ORCA_WEB_DATA_DIR", "data"), "persistent user preset directory (ORCA_WEB_DATA_DIR)")
	flag.StringVar(&c.FallbackBackend, "fallback-backend", env("ORCA_WEB_FALLBACK_BACKEND", ""), "comma-separated previous loopback backends for read-only historical jobs")
	flag.StringVar(&c.Listen, "listen", env("ORCA_WEB_LISTEN", "127.0.0.1:8780"), "listen address (ORCA_WEB_LISTEN)")
	flag.StringVar(&c.Tool, "tool", env("ORCA_WEB_TOOL", "build-nogui/tests/fff_print/nonplanar_slice_tool"), "nonplanar_slice_tool path (ORCA_WEB_TOOL)")
	flag.StringVar(&c.Resources, "resources", env("ORCA_WEB_RESOURCES", "resources"), "resources directory holding profiles/ and nonplanar/ (ORCA_WEB_RESOURCES)")
	flag.StringVar(&c.JobRoot, "job-root", env("ORCA_WEB_JOB_ROOT", filepath.Join(os.TempDir(), "orca-webapp-jobs")), "scratch directory for job files (ORCA_WEB_JOB_ROOT)")
	flag.StringVar(&c.StaticDir, "static", env("ORCA_WEB_STATIC", "web/dist"), "static front end to serve at / when it exists (ORCA_WEB_STATIC)")
	flag.IntVar(&c.MaxJobs, "max-jobs", envInt("ORCA_WEB_MAX_JOBS", 2), "slices running at once; more jobs queue (ORCA_WEB_MAX_JOBS)")
	flag.DurationVar(&c.JobTimeout, "job-timeout", envDuration("ORCA_WEB_JOB_TIMEOUT", 15*time.Minute), "wall-clock limit of one slice (ORCA_WEB_JOB_TIMEOUT)")
	flag.DurationVar(&c.JobTTL, "job-ttl", envDuration("ORCA_WEB_JOB_TTL", 2*time.Hour), "finished jobs and their files are deleted after this (ORCA_WEB_JOB_TTL)")
	flag.Int64Var(&c.MaxUploadSize, "max-upload", int64(envInt("ORCA_WEB_MAX_UPLOAD_MB", 512))<<20, "largest accepted upload in bytes (ORCA_WEB_MAX_UPLOAD_MB in MB)")
	flag.Int64Var(&c.UploadMinRate, "upload-min-rate", int64(envInt("ORCA_WEB_UPLOAD_MIN_RATE", 256<<10)), "minimum multipart bytes per second after ten-second grace (ORCA_WEB_UPLOAD_MIN_RATE)")
	rate, _ := strconv.ParseFloat(env("ORCA_WEB_SUBMIT_RATE", "0"), 64)
	flag.Float64Var(&c.SubmitRate, "submit-rate", rate, "job submission tokens per second per IP; 0 disables (ORCA_WEB_SUBMIT_RATE)")
	flag.IntVar(&c.SubmitBurst, "submit-burst", envInt("ORCA_WEB_SUBMIT_BURST", 1), "submission bucket capacity (ORCA_WEB_SUBMIT_BURST)")
	flag.IntVar(&c.MaxClientJobs, "max-client-jobs", envInt("ORCA_WEB_MAX_CLIENT_JOBS", 0), "active jobs per IP; 0 disables (ORCA_WEB_MAX_CLIENT_JOBS)")
	flag.IntVar(&c.MaxQueue, "max-queue", envInt("ORCA_WEB_MAX_QUEUE", -1), "waiting jobs; -1 unlimited, 0 no waiting (ORCA_WEB_MAX_QUEUE)")
	flag.StringVar(&c.BearerToken, "bearer-token", env("ORCA_WEB_BEARER_TOKEN", ""), "mutation bearer token; empty disables (ORCA_WEB_BEARER_TOKEN)")
	quota, _ := strconv.ParseInt(env("ORCA_WEB_DISK_QUOTA", "0"), 10, 64)
	flag.Int64Var(&c.DiskQuota, "disk-quota", quota, "job directory quota in bytes; 0 disables (ORCA_WEB_DISK_QUOTA)")
	flag.BoolVar(&c.RequestLog, "request-log", envBool("ORCA_WEB_REQUEST_LOG"), "JSON request logging (ORCA_WEB_REQUEST_LOG)")
	flag.BoolVar(&c.Metrics, "metrics", envBool("ORCA_WEB_METRICS"), "expose GET /metrics (ORCA_WEB_METRICS)")
	flag.StringVar(&c.PrintersFile, "printers", env("ORCA_WEB_PRINTERS", ""), "administrator's printer list (JSON) for Send to printer; empty disables sending (ORCA_WEB_PRINTERS)")
	flag.StringVar(&c.PrinterEdit, "printer-edit", env("ORCA_WEB_PRINTER_EDIT", "auto"), "add Bambu printers in the app: auto (on when a bearer token is set), on or off (ORCA_WEB_PRINTER_EDIT)")
	flag.StringVar(&c.HTTPPrinters, "http-printers", env("ORCA_WEB_HTTP_PRINTERS", ""), "CIDRs and host names where PrusaLink/OctoPrint hosts may be added in the app (needs a bearer token); empty hides the feature (ORCA_WEB_HTTP_PRINTERS)")
	proxies := flag.String("trusted-proxies", env("ORCA_WEB_TRUSTED_PROXIES", ""), "comma-separated trusted proxy CIDRs (ORCA_WEB_TRUSTED_PROXIES)")
	flag.Parse()
	if _, err := fallbackOrigins(c.FallbackBackend); err != nil {
		log.Fatal(err)
	}
	if err := validateSourceURL(c.SourceURL); err != nil {
		log.Fatal(err)
	}
	if c.SubmitRate < 0 || math.IsNaN(c.SubmitRate) || math.IsInf(c.SubmitRate, 0) || c.SubmitBurst < 1 || c.MaxClientJobs < 0 || c.MaxQueue < -1 || c.DiskQuota < 0 {
		log.Fatal("invalid public deployment limits")
	}
	if *proxies != "" {
		for _, cidr := range strings.Split(*proxies, ",") {
			prefix, err := netip.ParsePrefix(strings.TrimSpace(cidr))
			if err != nil {
				log.Fatal(err)
			}
			c.TrustedProxies = append(c.TrustedProxies, prefix)
		}
	}
	if c.MaxJobs < 1 {
		log.Fatal("max-jobs must be positive")
	}
	if c.JobTimeout <= 0 || c.JobTTL <= 0 || c.MaxUploadSize <= 0 || c.UploadMinRate <= 0 {
		log.Fatal("job-timeout, job-ttl, max-upload and upload-min-rate must be positive")
	}
	if c.SourceArchive != "" {
		if abs, err := filepath.Abs(c.SourceArchive); err == nil {
			c.SourceArchive = abs
		}
	}
	for _, p := range []*string{&c.Tool, &c.Resources, &c.JobRoot, &c.StaticDir, &c.DataDir} {
		if abs, err := filepath.Abs(*p); err == nil {
			*p = abs
		}
	}
	return c
}

func main() {
	if len(os.Args) == 3 && os.Args[1] == "printer-fingerprint" {
		if err := printerFingerprint(os.Args[2], env("ORCA_WEB_RESOURCES", "resources")); err != nil {
			log.Fatal(err)
		}
		return
	}
	cfg := parseConfig()
	if cfg.SourceURL == "" && cfg.SourceArchive == "" {
		log.Printf("WARNING: no Corresponding Source offer configured; operator must set ORCA_WEB_SOURCE_URL or ORCA_WEB_SOURCE_ARCHIVE (AGPL-3.0 section 13)")
	}
	if cfg.SourceArchive != "" {
		if file, err := openSourceArchive(cfg); err != nil {
			log.Printf("WARNING: Corresponding Source archive rejected: %v (AGPL-3.0 section 13)", err)
		} else {
			file.Close()
		}
	}
	if st, err := os.Stat(cfg.Tool); err != nil || st.IsDir() {
		log.Printf("warning: tool %s not found; jobs will fail until it is built", cfg.Tool)
	}
	if st, err := os.Stat(filepath.Join(cfg.Resources, "profiles")); err != nil || !st.IsDir() {
		log.Fatalf("no profiles directory under %s", cfg.Resources)
	}
	printers, err := loadPrinters(cfg.PrintersFile, cfg.Resources)
	if err != nil {
		log.Fatal(err)
	}
	cfg.Printers = printers
	if len(printers) > 0 {
		log.Printf("send to printer: %d configured printer(s)", len(printers))
		if cfg.BearerToken == "" {
			log.Printf("WARNING: printers are configured without a bearer token; anyone who can reach this service can start prints")
		}
	}
	bambu := false
	switch cfg.PrinterEdit {
	case "auto", "on":
		if cfg.PrinterEdit == "auto" && cfg.BearerToken == "" {
			log.Printf("adding printers in the app is off: set a bearer token, or -printer-edit on for a private network")
			break
		}
		if cfg.BearerToken == "" {
			log.Printf("WARNING: printers can be added without a bearer token; anyone who can reach this service can add printers and start prints")
		}
		bambu = true
	case "off":
	default:
		log.Fatal("printer-edit must be auto, on or off")
	}
	hosts, err := parseHostPolicy(cfg.HTTPPrinters, cfg.Listen)
	if err != nil {
		log.Fatal(err)
	}
	if hosts != nil && cfg.BearerToken == "" {
		log.Printf("adding PrusaLink/OctoPrint hosts in the app is off: it needs a bearer token")
		hosts = nil
	}
	if bambu || hosts != nil {
		if cfg.PrinterStore, err = loadPrinterStore(filepath.Join(cfg.DataDir, "printers"), cfg.Resources, bambu, hosts); err != nil {
			log.Fatal(err)
		}
		log.Printf("adding printers in the app: Bambu %v, PrusaLink/OctoPrint %v (%d saved)", bambu, hosts != nil, len(cfg.PrinterStore.printers))
		if hosts != nil {
			log.Printf("PrusaLink/OctoPrint hosts may be added at: %s", hosts)
		}
	}
	cfg.Features, cfg.Options = probeTool(cfg)
	if err := probePlatePlacement(cfg); err != nil {
		log.Printf("R238 placement unavailable; plate z/printable and loaded project saves require an upgraded tool: %v", err)
	} else {
		cfg.Features.PlatePlacement = true
	}
	if err := probePlateIndex(cfg); err != nil {
		log.Printf("selected project plates unavailable: %v", err)
	} else {
		cfg.Features.PlateIndex = true
	}
	cfg.Features.ColourChange = probeColourChange(cfg)
	probeUsage(cfg, &cfg.Features)
	log.Printf("tool operations: step %v, edit %v, force_planar %v, cut %v, volumes %v, patch_preview %v, surface_grid %v, calib %v", cfg.Features.Step, cfg.Features.Edit, cfg.Features.ForcePlanar, cfg.Features.Cut, cfg.Features.Volumes, cfg.Features.PatchPreview, cfg.Features.SurfaceGrid, cfg.Features.Calib)
	jobs, err := NewJobManager(cfg)
	if err != nil {
		log.Fatal(err)
	}
	users, err := NewUserPresetStore(filepath.Join(cfg.DataDir, "user-presets"))
	if err != nil {
		log.Fatal(err)
	}
	presets := NewPresetIndex(filepath.Join(cfg.Resources, "profiles"))
	if cfg.Features.DumpPresets {
		presets.tool = &cfg
	}
	if _, err := presets.names(); err != nil {
		log.Fatal(err)
	}
	for _, vendor := range []string{"BBL", "Prusa"} {
		if _, err := presets.vendor(vendor); err != nil {
			log.Printf("cannot preload %s presets: %v", vendor, err)
		}
	}
	srv := &http.Server{Addr: cfg.Listen, Handler: NewServer(cfg, jobs, presets, users), ReadHeaderTimeout: 10 * time.Second, ReadTimeout: 10 * time.Second, MaxHeaderBytes: 2 << 20}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	shutdownDone := make(chan struct{})
	go func() {
		defer close(shutdownDone)
		<-ctx.Done()
		jobs.Shutdown()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		if err := srv.Shutdown(shutdownCtx); err != nil {
			log.Printf("HTTP shutdown: %v", err)
			srv.Close()
		}
		jobs.Cleanup()
	}()
	log.Printf("listening on %s (tool %s, resources %s, jobs %s, %d at once)", cfg.Listen, cfg.Tool, cfg.Resources, cfg.JobRoot, cfg.MaxJobs)
	if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
	<-shutdownDone
}
