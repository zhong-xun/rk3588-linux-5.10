#!/bin/sh
set -eu

SDK_DIR="${SDK_DIR:-/home/topeet/Linux/rk3588/rk3588-linux-6.1}"
COMMON="$SDK_DIR/device/rockchip/common"
POST_OVERLAY="$COMMON/scripts/post-overlay.sh"
TMP="$(mktemp -d /tmp/rknetupdate-sdk-handoff.XXXXXX)"

cleanup()
{
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

[ -r "$POST_OVERLAY" ] || {
	echo "missing Recovery overlay installer: $POST_OVERLAY" >&2
	exit 1
}

# Rockchip 6.1 installs each recovery overlay child, while 5.10 installs the
# recovery overlay directory itself. Keep this detection aligned with the SDK
# integration script instead of inferring the layout from a version string.
if grep -q 'install_overlays.*\$POST_OS' "$POST_OVERLAY"; then
	OVERLAY="$COMMON/overlays/recovery/rknetupdate-rootfs"
elif grep -q 'install_overlay.*\$POST_OS' "$POST_OVERLAY"; then
	OVERLAY="$COMMON/overlays/recovery"
else
	echo "unknown Recovery overlay framework: $POST_OVERLAY" >&2
	exit 1
fi

S39="$OVERLAY/etc/init.d/S39rknetupdate-rootfs"
S40="$OVERLAY/etc/init.d/S40recovery"

for script in "$S39" "$S40"; do
	[ -r "$script" ] || {
		echo "missing recovery init script: $script" >&2
		exit 1
	}
	sh -n "$script"
done

grep -Fq '"$HELPER" --resume-updateimg-v1 --stay-in-recovery' "$S39"
grep -Fq 'UPDATEIMG_MANIFEST=/userdata/rknetupdate/updateimg-v1/updateimg.manifest.json' "$S39"
grep -Fq 'UPDATEIMG_MANIFEST=/userdata/rknetupdate/updateimg-v1/updateimg.manifest.json' "$S40"
grep -Fq 'touch "$STOCK_READY"' "$S39"
grep -Fq 'rm -f "$SUPPRESS_STOCK"' "$S39"
grep -Fq 'continuing directly to Rockchip updater without a warm reboot' "$S39"
grep -Fq '[ -e "$STOCK_READY" ] || return 0' "$S40"
grep -Fq 'managed update.img handoff ready; starting Rockchip updater in the current recovery boot' "$S40"
grep -Fq 'touch /.rkdebug' "$S40"
grep -Fq '/usr/bin/recovery > /dev/console 2>&1 &' "$S40"

READY_LINE="$(grep -nF 'touch "$STOCK_READY"' "$S39" | head -n 1 | cut -d: -f1)"
RELEASE_LINE="$(grep -nF 'rm -f "$SUPPRESS_STOCK"' "$S39" | head -n 1 | cut -d: -f1)"
[ "$READY_LINE" -lt "$RELEASE_LINE" ] || {
	echo "stock recovery suppression is released before the ready marker is durable" >&2
	exit 1
}

# Keep the shell-side exact comparison aligned with the Go BCB handoff:
# command[32], status[32], recovery[768], needupdate little-endian 0x00bb0000.
dd if=/dev/zero of="$TMP/stock.bcb" bs=836 count=1 status=none
printf '%s' 'boot-recovery' | \
	dd of="$TMP/stock.bcb" bs=1 seek=0 conv=notrunc status=none
printf 'recovery\n--update_package=/userdata/update.img\n' | \
	dd of="$TMP/stock.bcb" bs=1 seek=64 conv=notrunc status=none
printf '\000\000\273\000' | \
	dd of="$TMP/stock.bcb" bs=1 seek=832 conv=notrunc status=none

ACTUAL_SHA256="$(sha256sum "$TMP/stock.bcb" | awk '{print $1}')"
EXPECTED_SHA256=54f832f3397768689ed15a9c0d856af212d062e7322bcdbd0c890d0ebd3bb866
[ "$ACTUAL_SHA256" = "$EXPECTED_SHA256" ] || {
	echo "canonical stock BCB fixture mismatch: $ACTUAL_SHA256" >&2
	exit 1
}

echo "SDK recovery same-boot update.img handoff checks passed"
