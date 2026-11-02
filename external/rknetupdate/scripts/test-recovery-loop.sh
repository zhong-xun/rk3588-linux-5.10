#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BINARY="${1:-$ROOT/output/bin/rknetupdate-recovery-linux-amd64}"
TMP="$(mktemp -d /tmp/rknetupdate-loop.XXXXXX)"
LOOP=""
MOUNTED=0

cleanup()
{
	if [ "$MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/mnt" >/dev/null 2>&1 || true
	fi
	if [ -n "$LOOP" ]; then
		sudo -n losetup -d "$LOOP" >/dev/null 2>&1 || true
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

for tool in sudo sgdisk losetup mkfs.ext4 e2fsck sha256sum jq; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "missing required tool: $tool" >&2
		exit 1
	}
done
[ -x "$BINARY" ] || {
	echo "recovery helper not found: $BINARY" >&2
	exit 1
}
sudo -n true

truncate -s 160M "$TMP/disk.img"
sgdisk --clear \
	--new=1:2048:+112M --typecode=1:8300 --change-name=1:rootfs \
	--new=2:0:+8M --typecode=2:8300 --change-name=2:misc \
	"$TMP/disk.img" >/dev/null
LOOP="$(sudo -n losetup --find --show --partscan "$TMP/disk.img")"
ROOTFS_PART="${LOOP}p1"
MISC_PART="${LOOP}p2"

i=0
while [ ! -b "$ROOTFS_PART" ] || [ ! -b "$MISC_PART" ]; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || {
		echo "loop partition nodes did not appear" >&2
		exit 1
	}
	sleep 0.1
done

truncate -s 64M "$TMP/rootfs.img"
mkfs.ext4 -q -F "$TMP/rootfs.img"
SHA="$(sha256sum "$TMP/rootfs.img" | awk '{print $1}')"
SIZE="$(stat -c %s "$TMP/rootfs.img")"
CONFIRM="WRITE_ROOTFS:$SHA"

printf '%s\n' 'rknetupdate-recovery-v1' > "$TMP/recovery-marker"
: > "$TMP/mountinfo"
printf '%s\n' "$CONFIRM" > "$TMP/rootfs.apply"
printf '{"schema":1,"image":"rootfs.img","size":%s,"sha256":"%s","filesystem":"ext4","target_partlabel":"rootfs","expected_parent":"%s"}\n' \
	"$SIZE" "$SHA" "$LOOP" > "$TMP/rootfs.manifest.json"
printf '%s' 'boot-recovery' | sudo -n dd of="$MISC_PART" bs=1 seek=16384 conv=notrunc status=none

sudo -n "$BINARY" \
	--test-mode \
	--apply \
	--allow-write \
	--confirm "$CONFIRM" \
	--manifest "$TMP/rootfs.manifest.json" \
	--status "$TMP/rootfs.status.json" \
	--request "$TMP/rootfs.apply" \
	--target "$ROOTFS_PART" \
	--misc "$MISC_PART" \
	--expected-parent "$LOOP" \
	--mountinfo "$TMP/mountinfo" \
	--recovery-marker "$TMP/recovery-marker" \
	--e2fsck "$(command -v e2fsck)" > "$TMP/apply-output.json"

READBACK_SHA="$(sudo -n dd if="$ROOTFS_PART" bs=1M count=64 status=none | sha256sum | awk '{print $1}')"
[ "$READBACK_SHA" = "$SHA" ] || {
	echo "readback mismatch: $READBACK_SHA != $SHA" >&2
	exit 1
}
[ "$(sudo -n dd if="$MISC_PART" bs=1 skip=16384 count=32 status=none | tr -d '\000')" = "" ] || {
	echo "BCB command was not cleared" >&2
	exit 1
}
sudo -n jq -e '.phase == "succeeded" and .applied == true and .bcb_cleared == true and .readback_sha256 == .sha256' \
	"$TMP/rootfs.status.json" >/dev/null

# Exercise the no-rootfs-write smoke path against the same loop-backed GPT.
dd if=/dev/zero of="$TMP/smoke.bcb" bs=1088 count=1 status=none
printf '%s' 'boot-recovery' | dd of="$TMP/smoke.bcb" bs=1 seek=0 conv=notrunc status=none
printf 'recovery\n--rknetupdate-smoke-v1\n' | dd of="$TMP/smoke.bcb" bs=1 seek=64 conv=notrunc status=none
printf '%s' 'KEEP' | dd of="$TMP/smoke.bcb" bs=1 seek=836 conv=notrunc status=none
sudo -n dd if="$TMP/smoke.bcb" of="$MISC_PART" bs=1 seek=16384 conv=notrunc status=none
ROOTFS_BEFORE_SMOKE="$(sudo -n sha256sum "$ROOTFS_PART" | awk '{print $1}')"
sudo -n "$BINARY" \
	--test-mode \
	--smoke \
	--smoke-status "$TMP/smoke.status.json" \
	--target "$ROOTFS_PART" \
	--misc "$MISC_PART" \
	--expected-parent "$LOOP" \
	--mountinfo "$TMP/mountinfo" \
	--recovery-marker "$TMP/recovery-marker" > "$TMP/smoke-output.json"
ROOTFS_AFTER_SMOKE="$(sudo -n sha256sum "$ROOTFS_PART" | awk '{print $1}')"
[ "$ROOTFS_AFTER_SMOKE" = "$ROOTFS_BEFORE_SMOKE" ] || {
	echo "rootfs changed during smoke mode" >&2
	exit 1
}
[ "$(sudo -n dd if="$MISC_PART" bs=1 skip=16384 count=836 status=none | tr -d '\000')" = "" ] || {
	echo "smoke BCB command fields were not cleared" >&2
	exit 1
}
[ "$(sudo -n dd if="$MISC_PART" bs=1 skip=$((16384 + 836)) count=4 status=none)" = "KEEP" ] || {
	echo "smoke BCB systemFlag was not preserved" >&2
	exit 1
}
sudo -n jq -e '.mode == "smoke" and .phase == "succeeded" and .applied == false and .bcb_cleared == true and .bytes_written == null' \
	"$TMP/smoke.status.json" >/dev/null

mkdir "$TMP/mnt"
sudo -n mount "$ROOTFS_PART" "$TMP/mnt"
MOUNTED=1
if sudo -n "$BINARY" \
	--test-mode \
	--manifest "$TMP/rootfs.manifest.json" \
	--status "$TMP/mounted.status.json" \
	--request "$TMP/rootfs.apply" \
	--target "$ROOTFS_PART" \
	--misc "$MISC_PART" \
	--expected-parent "$LOOP" \
	--mountinfo /proc/self/mountinfo \
	--recovery-marker "$TMP/recovery-marker" \
	--e2fsck "$(command -v e2fsck)" >/dev/null 2>&1; then
	echo "mounted rootfs target was not rejected" >&2
	exit 1
fi
sudo -n jq -e '.phase == "failed" and (.error | contains("mounted at"))' "$TMP/mounted.status.json" >/dev/null

echo "loop recovery apply passed: target=$ROOTFS_PART sha256=$SHA"
