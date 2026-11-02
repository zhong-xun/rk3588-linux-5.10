package webui

import "embed"

// Files contains the complete browser application shipped in the static binary.
//
//go:embed static/*
var Files embed.FS
