#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SDK_ROOT="$(CDPATH= cd -- "$SCRIPT_DIR/../../../.." && pwd)"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

extract_rootfs_loader()
{
	awk '
		/topeet-wifibt-load\.sh/ && /<<'\''EOF'\''/ { capture = 1; next }
		capture && /^EOF$/ { exit }
		capture { print }
	' "$SDK_ROOT/device/rockchip/common/scripts/rootfs-wifibt-sync.sh" > "$1"
}

extract_functions()
{
	local script="$1"
	local output="$2"
	local stop_pattern="$3"

	sed "/$stop_pattern/,\$d" "$script" > "$output"
}

assert_grouped_usb_poll()
{
	local script="$1"
	local stop_pattern="$2"
	local funcs="$TMPDIR/$(basename "$script").funcs"
	local output calls

	extract_functions "$script" "$funcs" "$stop_pattern"
	output="$(
		TOPEET_WIFIBT_USB_POLL_RETRIES=3
		TOPEET_WIFIBT_USB_POLL_INTERVAL=0
		. "$funcs"
		calls=0
		usb_device_present_once()
		{
			calls=$((calls + 1))
			return 1
		}
		sleep()
		{
			:
		}
		usb_device_present_any a69c:8800 0bda:d723 7392:d611 >/dev/null 2>&1 || true
		printf '%s\n' "$calls"
	)"
	calls="$(printf '%s\n' "$output" | tail -n 1)"
	[ "$calls" -le 3 ] || {
		echo "$script polls each USB ID independently: calls=$calls" >&2
		exit 1
	}

	grep -q '^detect_usb_family()' "$script" || {
		echo "$script does not use grouped AIC/Realtek detection" >&2
		exit 1
	}
}

ROOTFS_LOADER="$TMPDIR/topeet-wifibt-load.sh"
S50_SCRIPT="$(find "$SCRIPT_DIR/.." -path '*/fs-overlay/etc/init.d/S50wifiblue.sh' | sort | head -n 1)"
[ -n "$S50_SCRIPT" ] || {
	echo "missing S50wifiblue.sh" >&2
	exit 1
}
extract_rootfs_loader "$ROOTFS_LOADER"

assert_grouped_usb_poll "$ROOTFS_LOADER" '^case "\${1:-start}" in'
assert_grouped_usb_poll "$S50_SCRIPT" '^case "\$1" in'
