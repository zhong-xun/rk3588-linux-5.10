#!/bin/bash -e

# RKNETUPDATE-MANAGED-RECOVERY-HOOK-V1

source "${RK_POST_HELPER:-$(dirname "$(realpath "$0")")/post-helper}"

# This hook is shared by the Rockchip 5.10 and 6.1 SDK build frameworks.  It
# runs for every post-rootfs stage, so never put managed files in a normal
# rootfs.
[ "$POST_OS" = recovery ] || exit 0

# Do not rely on the vendor post-overlay argument filter here. Some 5.10 SDKs
# pass the Recovery defconfig name as an overlay allow-list, which silently
# suppresses overlays/recovery. Keep the source files in the SDK's native
# flat/nested overlay layout, but install the managed files explicitly.
RKNETUPDATE_RECOVERY_OVERLAY="$RK_COMMON_DIR/overlays/recovery/rknetupdate-rootfs"
if [ ! -r "$RKNETUPDATE_RECOVERY_OVERLAY/etc/rknetupdate-recovery" ]; then
	RKNETUPDATE_RECOVERY_OVERLAY="$RK_COMMON_DIR/overlays/recovery"
fi
if ! grep -qx 'rknetupdate-recovery-v1' \
	"$RKNETUPDATE_RECOVERY_OVERLAY/etc/rknetupdate-recovery" 2>/dev/null; then
	error "Missing managed RKNetUpdate recovery overlay: $RKNETUPDATE_RECOVERY_OVERLAY"
	exit 1
fi

# Keep the source and generated helper inside the SDK. This makes a copied or
# archived SDK self-contained and avoids depending on a sibling checkout on
# the build host. RKNETUPDATE_PROJECT_DIR remains an explicit override for
# development and CI.
RKNETUPDATE_PROJECT_DIR="${RKNETUPDATE_PROJECT_DIR:-$RK_SDK_DIR/external/rknetupdate}"
RKNETUPDATE_RECOVERY_BIN="${RKNETUPDATE_RECOVERY_BIN:-$RKNETUPDATE_PROJECT_DIR/output/bin/rknetupdate-recovery-linux-arm64}"
if [ ! -x "$RKNETUPDATE_RECOVERY_BIN" ]; then
	if [ ! -r "$RKNETUPDATE_PROJECT_DIR/Makefile" ]; then
		error "Missing SDK-local RKNetUpdate source: $RKNETUPDATE_PROJECT_DIR"
		error "Install the SDK-local component before building recovery"
		exit 1
	fi
	message "Building SDK-local RKNetUpdate recovery helper..."
	make -C "$RKNETUPDATE_PROJECT_DIR" recovery-arm64
fi

if [ ! -x "$RKNETUPDATE_RECOVERY_BIN" ]; then
	error "Missing RKNetUpdate recovery helper after local build: $RKNETUPDATE_RECOVERY_BIN"
	exit 1
fi

if ! command -v readelf >/dev/null 2>&1; then
	error "readelf is required to validate the RKNetUpdate recovery helper"
	exit 1
fi

if ! LC_ALL=C readelf -h "$RKNETUPDATE_RECOVERY_BIN" 2>/dev/null | \
	grep -q 'Machine:.*AArch64'; then
	error "RKNetUpdate recovery helper is not an AArch64 ELF: $RKNETUPDATE_RECOVERY_BIN"
	exit 1
fi

message "Installing RKNetUpdate recovery scripts and helper..."
install -D -m 0755 \
	"$RKNETUPDATE_RECOVERY_OVERLAY/etc/init.d/S39rknetupdate-rootfs" \
	"$TARGET_DIR/etc/init.d/S39rknetupdate-rootfs"
install -D -m 0755 \
	"$RKNETUPDATE_RECOVERY_OVERLAY/etc/init.d/S40recovery" \
	"$TARGET_DIR/etc/init.d/S40recovery"
install -D -m 0644 \
	"$RKNETUPDATE_RECOVERY_OVERLAY/etc/rknetupdate-recovery" \
	"$TARGET_DIR/etc/rknetupdate-recovery"
install -D -m 0755 "$RKNETUPDATE_RECOVERY_BIN" \
	"$TARGET_DIR/usr/bin/rknetupdate-recovery"
