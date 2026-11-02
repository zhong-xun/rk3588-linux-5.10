#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
S39="$ROOT/packaging/recovery/common/etc/init.d/S39rknetupdate-rootfs"
TMP="$(mktemp -d /tmp/rknetupdate-sd-handoff.XXXXXX)"

cleanup()
{
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

[ -r "$S39" ] || {
	echo "missing recovery init script: $S39" >&2
	exit 1
}
sh -n "$S39"

# The cmdline path must short-circuit before the script probes misc or userdata.
printf '%s\n' 'console=ttyS2,1500000 sdfwupdate root=/dev/mmcblk1p1' > "$TMP/cmdline"
SUPPRESS="$TMP/suppressed"
OUTPUT="$TMP/output"
RKNETUPDATE_CMDLINE_PATH="$TMP/cmdline" \
RKNETUPDATE_MISC_PATH="$TMP/missing-misc" \
RKNETUPDATE_SUPPRESS_PATH="$SUPPRESS" \
RKNETUPDATE_SD_EXPECTED_PATH="$TMP/sd-expected" \
RKNETUPDATE_SD_ACTUAL_PATH="$TMP/sd-actual" \
sh "$S39" start > "$OUTPUT"

[ ! -e "$SUPPRESS" ] || {
	echo "SD boot handoff created the stock-recovery suppression marker" >&2
	exit 1
}
grep -Fq 'handing off to stock recovery' "$OUTPUT"

# The BCB fallback must also hand off without a cmdline token.  This fixture
# only needs the canonical Rockchip marker at misc+16 KiB; the stock recovery
# remains responsible for parsing the complete message and mounting the card.
truncate -s 32768 "$TMP/misc"
printf '%s' 'boot-recovery' | dd of="$TMP/misc" bs=1 seek=16384 conv=notrunc status=none
printf 'recovery\n--rk_fwupdate\n' | dd of="$TMP/misc" bs=1 seek=$((16384 + 64)) conv=notrunc status=none
rm -f "$SUPPRESS" "$OUTPUT"
printf '%s\n' 'console=ttyS2,1500000 root=/dev/mmcblk0p1' > "$TMP/normal-cmdline"
RKNETUPDATE_CMDLINE_PATH="$TMP/normal-cmdline" \
RKNETUPDATE_MISC_PATH="$TMP/misc" \
RKNETUPDATE_SUPPRESS_PATH="$SUPPRESS" \
RKNETUPDATE_SD_EXPECTED_PATH="$TMP/sd-expected" \
RKNETUPDATE_SD_ACTUAL_PATH="$TMP/sd-actual" \
sh "$S39" start > "$OUTPUT"

[ ! -e "$SUPPRESS" ] || {
	echo "BCB SD boot handoff created the stock-recovery suppression marker" >&2
	exit 1
}
grep -Fq 'handing off to stock recovery' "$OUTPUT"

# A marker outside an otherwise empty canonical 836-byte request region must
# not trigger the handoff. With a zero wait budget this fixture reaches the
# normal recovery UI path immediately after the strict probe and does not touch
# the host system.
truncate -s 32768 "$TMP/malformed-misc"
printf '%s' '--rk_fwupdate' | dd of="$TMP/malformed-misc" bs=1 seek=$((16384 + 836)) conv=notrunc status=none
rm -f "$SUPPRESS" "$OUTPUT"
RKNETUPDATE_CMDLINE_PATH="$TMP/normal-cmdline" \
RKNETUPDATE_MISC_PATH="$TMP/malformed-misc" \
RKNETUPDATE_SUPPRESS_PATH="$SUPPRESS" \
RKNETUPDATE_DEVICE_WAIT_SECONDS=0 \
RKNETUPDATE_USERDATA_PATH="$TMP/missing-userdata" \
RKNETUPDATE_SD_EXPECTED_PATH="$TMP/sd-expected" \
RKNETUPDATE_SD_ACTUAL_PATH="$TMP/sd-actual" \
sh "$S39" start > "$OUTPUT"

[ ! -e "$SUPPRESS" ] || {
	echo "non-canonical BCB created the stock-recovery suppression marker" >&2
	exit 1
}
if grep -Fq 'handing off to stock recovery' "$OUTPUT"; then
	echo "non-canonical BCB incorrectly triggered the stock recovery handoff" >&2
	exit 1
fi

echo "recovery SD/TF handoff checks passed"
