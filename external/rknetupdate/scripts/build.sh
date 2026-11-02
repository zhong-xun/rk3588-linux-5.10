#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
OUT="${OUT:-$ROOT/output/bin}"
GO_BIN="${GO:-go}"

mkdir -p "$OUT"
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-linux-amd64" "$ROOT/cmd/rknetupdate"
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-linux-arm64" "$ROOT/cmd/rknetupdate"
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-control-linux-amd64" "$ROOT/cmd/rknetupdate-control"
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-control-linux-arm64" "$ROOT/cmd/rknetupdate-control"
CGO_ENABLED=0 GOOS=linux GOARCH=amd64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-recovery-linux-amd64" "$ROOT/cmd/rknetupdate-recovery"
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 "$GO_BIN" build -trimpath -ldflags "-s -w" -o "$OUT/rknetupdate-recovery-linux-arm64" "$ROOT/cmd/rknetupdate-recovery"

sha256sum \
	"$OUT/rknetupdate-linux-amd64" \
	"$OUT/rknetupdate-linux-arm64" \
	"$OUT/rknetupdate-control-linux-amd64" \
	"$OUT/rknetupdate-control-linux-arm64" \
	"$OUT/rknetupdate-recovery-linux-amd64" \
	"$OUT/rknetupdate-recovery-linux-arm64"
