package update

import (
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log/slog"
	"net/http"
	"os"
	"runtime"
	"strings"
	"time"

	"rknetupdate/internal/webui"
)

type API struct {
	cfg     Config
	manager *Manager
	logger  *slog.Logger
	handler http.Handler
}

func NewAPI(cfg Config, manager *Manager, logger *slog.Logger) (*API, error) {
	staticFS, err := fs.Sub(webui.Files, "static")
	if err != nil {
		return nil, err
	}
	api := &API{cfg: cfg, manager: manager, logger: logger}
	mux := http.NewServeMux()
	mux.HandleFunc("/api/v1/health", api.health)
	mux.Handle("/api/v1/status", api.auth(http.HandlerFunc(api.status)))
	mux.Handle("/api/v1/device", api.auth(http.HandlerFunc(api.device)))
	mux.Handle("/api/v1/uploads", api.auth(http.HandlerFunc(api.upload)))
	mux.Handle("/api/v1/bundles", api.auth(http.HandlerFunc(api.bundleUpload)))
	mux.Handle("/api/v1/reboot", api.auth(http.HandlerFunc(api.reboot)))
	mux.HandleFunc("/favicon.ico", func(w http.ResponseWriter, _ *http.Request) { w.WriteHeader(http.StatusNoContent) })
	mux.Handle("/", noCache(http.FileServer(http.FS(staticFS))))
	api.handler = api.requestLog(mux)
	return api, nil
}

func (a *API) Handler() http.Handler { return a.handler }

func (a *API) health(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"status": "ok", "time": time.Now().UTC()})
}

func (a *API) status(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w)
		return
	}
	writeJSON(w, http.StatusOK, a.manager.Status())
}

func (a *API) device(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w)
		return
	}
	hostname, _ := os.Hostname()
	writeJSON(w, http.StatusOK, map[string]any{
		"hostname":                   hostname,
		"model":                      readOneLine("/proc/device-tree/model"),
		"os":                         readOSRelease(),
		"runtime":                    runtime.GOOS + "/" + runtime.GOARCH,
		"listen":                     a.cfg.Listen,
		"dry_run":                    a.cfg.DryRun,
		"flash_enabled":              a.cfg.AllowFlash && !a.cfg.DryRun,
		"test_mode":                  a.cfg.TestMode,
		"expected_parent_device":     a.cfg.ExpectedParentDevice,
		"max_upload_bytes":           a.cfg.MaxUploadBytes,
		"rootfs_enabled":             a.cfg.RootfsEnabled,
		"rootfs_live":                a.cfg.RootfsEnabled && a.cfg.RootfsAllowWrite,
		"max_rootfs_upload_bytes":    a.cfg.MaxRootfsUploadBytes,
		"bundle_enabled":             a.cfg.BundleEnabled,
		"bundle_live":                a.cfg.BundleEnabled && a.cfg.BundleAllowWrite,
		"max_bundle_upload_bytes":    a.cfg.MaxBundleUploadBytes,
		"updateimg_enabled":          a.cfg.UpdateImageEnabled,
		"updateimg_live":             a.cfg.UpdateImageEnabled && a.cfg.UpdateImageAllowWrite,
		"max_updateimg_upload_bytes": a.cfg.MaxUpdateImageUploadBytes,
		"targets":                    a.manager.Capabilities(),
	})
}

func (a *API) reboot(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 4<<10)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	var request struct {
		JobID string `json:"job_id"`
	}
	if err := decoder.Decode(&request); err != nil {
		writeError(w, http.StatusBadRequest, "invalid_reboot_request", "JSON body with job_id is required")
		return
	}
	if err := decoder.Decode(&struct{}{}); err != io.EOF {
		writeError(w, http.StatusBadRequest, "invalid_reboot_request", "request body must contain one JSON object")
		return
	}
	request.JobID = strings.TrimSpace(request.JobID)
	if request.JobID == "" {
		writeError(w, http.StatusBadRequest, "invalid_reboot_request", "job_id is required")
		return
	}
	job, err := a.manager.RequestSystemReboot(request.JobID)
	if err != nil {
		status, code := http.StatusConflict, "reboot_rejected"
		if errors.Is(err, ErrBusy) {
			code = "busy"
		}
		writeError(w, status, code, err.Error())
		return
	}
	writeJSON(w, http.StatusAccepted, job)
}

func (a *API) upload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	target := Target(r.URL.Query().Get("target"))
	if !target.ImageTarget() && target != TargetUpdateImage {
		writeError(w, http.StatusBadRequest, "invalid_target", "target must be uboot, boot, rootfs, or updateimg")
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, a.manager.uploadLimit(target)+(1<<20))
	multipartReader, err := r.MultipartReader()
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_multipart", err.Error())
		return
	}
	part, err := multipartReader.NextPart()
	if err != nil {
		writeError(w, http.StatusBadRequest, "missing_image", "multipart field image is required")
		return
	}
	defer part.Close()
	if part.FormName() != "image" || part.FileName() == "" {
		writeError(w, http.StatusBadRequest, "missing_image", "first multipart field must be the image file")
		return
	}
	job, err := a.manager.BeginUpload(r.Context(), target, part.FileName(), part)
	if err != nil {
		status := http.StatusBadRequest
		code := "upload_rejected"
		if errors.Is(err, ErrBusy) {
			status, code = http.StatusConflict, "busy"
		} else if strings.Contains(err.Error(), "exceeds maximum") || strings.Contains(err.Error(), "request body too large") {
			status, code = http.StatusRequestEntityTooLarge, "image_too_large"
		}
		writeError(w, status, code, err.Error())
		return
	}
	if extra, nextErr := multipartReader.NextPart(); nextErr == nil {
		_ = extra.Close()
		_ = a.manager.Cancel(job.ID)
		writeError(w, http.StatusBadRequest, "extra_part", "only one image part is allowed")
		return
	} else if !errors.Is(nextErr, io.EOF) {
		_ = a.manager.Cancel(job.ID)
		writeError(w, http.StatusBadRequest, "invalid_multipart", nextErr.Error())
		return
	}
	job, err = a.manager.ExecuteStaged(r.Context(), job.ID)
	if err != nil {
		writeError(w, http.StatusConflict, "update_start_failed", err.Error())
		return
	}
	writeJSON(w, http.StatusCreated, job)
}

// bundleUpload accepts repeated multipart fields named image.  Unlike a ZIP
// or update.img, each member remains independently streamed, inspected, and
// named before recovery receives the hash-bound manifest.
func (a *API) bundleUpload(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, a.manager.uploadLimit(TargetBundle)+(1<<20))
	multipartReader, err := r.MultipartReader()
	if err != nil {
		writeError(w, http.StatusBadRequest, "invalid_multipart", err.Error())
		return
	}
	job, err := a.manager.BeginBundleUpload(r.Context(), func(add func(filename string, reader io.Reader) error) error {
		count := 0
		for {
			part, nextErr := multipartReader.NextPart()
			if errors.Is(nextErr, io.EOF) {
				break
			}
			if nextErr != nil {
				return fmt.Errorf("read multipart bundle: %w", nextErr)
			}
			if part.FormName() != "image" || part.FileName() == "" {
				_ = part.Close()
				return errors.New("each bundle multipart field must be an image file named image")
			}
			count++
			err := add(part.FileName(), part)
			closeErr := part.Close()
			if err != nil {
				return err
			}
			if closeErr != nil {
				return fmt.Errorf("close bundle image part: %w", closeErr)
			}
		}
		if count == 0 {
			return errors.New("bundle requires at least rootfs.img")
		}
		return nil
	})
	if err != nil {
		status := http.StatusBadRequest
		code := "bundle_rejected"
		if errors.Is(err, ErrBusy) {
			status, code = http.StatusConflict, "busy"
		} else if strings.Contains(err.Error(), "exceeds maximum") || strings.Contains(err.Error(), "request body too large") {
			status, code = http.StatusRequestEntityTooLarge, "bundle_too_large"
		}
		writeError(w, status, code, err.Error())
		return
	}
	job, err = a.manager.ExecuteStaged(r.Context(), job.ID)
	if err != nil {
		writeError(w, http.StatusConflict, "update_start_failed", err.Error())
		return
	}
	writeJSON(w, http.StatusCreated, job)
}

func (a *API) auth(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		provided := strings.TrimSpace(strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer "))
		if provided == "" {
			provided = r.Header.Get("X-API-Token")
		}
		if subtle.ConstantTimeCompare([]byte(provided), []byte(a.cfg.Token)) != 1 {
			w.Header().Set("WWW-Authenticate", `Bearer realm="rknetupdate"`)
			writeError(w, http.StatusUnauthorized, "unauthorized", "valid bearer token required")
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *API) requestLog(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		recorder := &statusRecorder{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(recorder, r)
		if strings.HasPrefix(r.URL.Path, "/api/") {
			a.logger.Info("http request", "method", r.Method, "path", r.URL.Path, "status", recorder.status, "duration_ms", time.Since(start).Milliseconds(), "remote", r.RemoteAddr)
		}
	})
}

type statusRecorder struct {
	http.ResponseWriter
	status int
}

func (r *statusRecorder) WriteHeader(status int) {
	r.status = status
	r.ResponseWriter.WriteHeader(status)
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func writeError(w http.ResponseWriter, status int, code, message string) {
	writeJSON(w, status, map[string]any{"error": map[string]string{"code": code, "message": message}})
}

func methodNotAllowed(w http.ResponseWriter) {
	writeError(w, http.StatusMethodNotAllowed, "method_not_allowed", "method not allowed")
}

func noCache(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("X-Frame-Options", "DENY")
		next.ServeHTTP(w, r)
	})
}

func readOneLine(path string) string {
	b, err := os.ReadFile(path)
	if err != nil {
		return ""
	}
	return strings.TrimSpace(strings.TrimRight(string(b), "\x00"))
}

func readOSRelease() string {
	b, err := os.ReadFile("/etc/os-release")
	if err != nil {
		return runtime.GOOS
	}
	for _, line := range strings.Split(string(b), "\n") {
		if strings.HasPrefix(line, "PRETTY_NAME=") {
			return strings.Trim(strings.TrimPrefix(line, "PRETTY_NAME="), `"`)
		}
	}
	return fmt.Sprintf("%s/%s", runtime.GOOS, runtime.GOARCH)
}
