#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/rknetupdate-recovery-notice-sdk.XXXXXX)"
SDK="$TMP/rk3588/rk3588-linux-5.10"
RECOVERY="$SDK/external/recovery"
COMMON="$SDK/device/rockchip/common"
NOTICE_SDK="$TMP/rk3562/rk3562-linux-6.1"
NOTICE_RECOVERY="$NOTICE_SDK/external/recovery"

cleanup()
{
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

mkdir -p "$RECOVERY" \
	"$SDK/buildroot/configs" \
	"$SDK/buildroot/package/gptfdisk" \
	"$COMMON/scripts" \
	"$COMMON/post-hooks" \
	"$COMMON/overlays/recovery"
: > "$SDK/build.sh"
printf '%s\n' 'install_overlay "$POST_OS"' > "$COMMON/scripts/post-overlay.sh"
printf '%s\n' 'BR2_PACKAGE_BUSYBOX=y' \
	> "$SDK/buildroot/configs/rockchip_rk3588_recovery_defconfig"
printf '%s\n' \
	'ifneq ($(BR2_STATIC_LIBS)$(BR2_PACKAGE_GPTFDISK_STATIC),)' \
	'GPTFDISK_CXX += $(TARGET_CXX) -static' \
	'endif' > "$SDK/buildroot/package/gptfdisk/gptfdisk.mk"
printf '%s\n' \
	'OBJ = recovery.o \' \
	'	roots.o' > "$RECOVERY/Makefile"
printf '%s\n' \
	'#include "roots.h"' \
	'#include "recovery_notice.h"' \
	'void completion(void)' \
	'{' \
	'	recovery_notice_wait_for_removal("SD card", sd_device);' \
	'	recovery_notice_wait_for_removal("USB drive", udisk_device);' \
	'}' > "$RECOVERY/recovery.c"

"$ROOT/scripts/integrate-recovery-sdks.sh" --apply --sdk "$SDK"
"$ROOT/scripts/integrate-recovery-sdks.sh" --check --sdk "$SDK"

cmp "$ROOT/packaging/recovery/sdk/recovery-notice/recovery_notice.c" \
	"$RECOVERY/recovery_notice.c"
cmp "$ROOT/packaging/recovery/sdk/recovery-notice/recovery_notice.h" \
	"$RECOVERY/recovery_notice.h"
grep -Fqx '	recovery_notice.o \' "$RECOVERY/Makefile"
grep -Fq 'SND_TONE' "$RECOVERY/recovery_notice.c"
if grep -Fq 'SND_BELL' "$RECOVERY/recovery_notice.c"; then
	echo "managed recovery notice must not use SND_BELL" >&2
	exit 1
fi

mkdir -p "$NOTICE_RECOVERY"
printf '%s\n' \
	'OBJ = recovery.o \' \
	'	roots.o' > "$NOTICE_RECOVERY/Makefile"
printf '%s\n' \
	'#include "recovery_notice.h"' \
	'void completion(void)' \
	'{' \
	'	recovery_notice_wait_for_removal("SD card", sd_device);' \
	'	recovery_notice_wait_for_removal("USB drive", udisk_device);' \
	'}' > "$NOTICE_RECOVERY/recovery.c"
printf '%s\n' \
	'#include "recovery_notice.h"' \
	'int recovery_notice_wait_for_removal(const char *name, const char *path)' \
	'{' \
	'	(void)name;' \
	'	(void)path;' \
	'	return 0;' \
	'}' > "$NOTICE_RECOVERY/recovery_notice.c"
printf '%s\n' \
	'#ifndef RECOVERY_NOTICE_H' \
	'#define RECOVERY_NOTICE_H' \
	'int recovery_notice_wait_for_removal(const char *name, const char *path);' \
	'#endif' > "$NOTICE_RECOVERY/recovery_notice.h"

"$ROOT/scripts/integrate-recovery-sdks.sh" --apply --notice-only \
	--sdk "$NOTICE_SDK"
"$ROOT/scripts/integrate-recovery-sdks.sh" --check --notice-only \
	--sdk "$NOTICE_SDK"
cmp "$ROOT/packaging/recovery/sdk/recovery-notice/recovery_notice.c" \
	"$NOTICE_RECOVERY/recovery_notice.c"
cmp "$ROOT/packaging/recovery/sdk/recovery-notice/recovery_notice.h" \
	"$NOTICE_RECOVERY/recovery_notice.h"
[ ! -e "$NOTICE_SDK/device" ] || {
	echo "notice-only integration must not create SDK overlay files" >&2
	exit 1
}

echo "recovery notice SDK integration checks passed"
