package main

import (
	"context"
	"errors"
	"flag"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"rknetupdate/internal/update"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{Level: slog.LevelInfo}))
	cfg, err := update.ConfigFromArgs(os.Args[1:])
	if err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		logger.Error("invalid configuration", "error", err)
		os.Exit(2)
	}
	manager, err := update.NewManager(cfg, logger)
	if err != nil {
		logger.Error("initialize update manager", "error", err)
		os.Exit(2)
	}
	defer manager.Close()
	api, err := update.NewAPI(cfg, manager, logger)
	if err != nil {
		logger.Error("initialize HTTP API", "error", err)
		os.Exit(2)
	}
	server := &http.Server{
		Addr:              cfg.Listen,
		Handler:           api.Handler(),
		ReadHeaderTimeout: 10 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    1 << 20,
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), cfg.ShutdownTimeout)
		defer cancel()
		if err := server.Shutdown(shutdownCtx); err != nil {
			logger.Error("HTTP shutdown", "error", err)
		}
	}()

	logger.Info("rknetupdate started", "listen", cfg.Listen, "dry_run", cfg.DryRun, "flash_enabled", cfg.AllowFlash && !cfg.DryRun, "rootfs_enabled", cfg.RootfsEnabled, "rootfs_live", cfg.RootfsEnabled && cfg.RootfsAllowWrite, "test_mode", cfg.TestMode)
	if err := server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		logger.Error("HTTP server stopped", "error", err)
		os.Exit(1)
	}
	logger.Info("rknetupdate stopped")
}
