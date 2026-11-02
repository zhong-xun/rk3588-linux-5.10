#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
GO="${GO:-/home/topeet/.cache/codex-toolchains/go1.24.4/bin/go}"
TMP="$(mktemp -d /tmp/rknetupdate-updateimg-v1.XXXXXX)"
LOOP=""
SOURCE_MOUNTED=0
VERIFY_MOUNTED=0
USERDATA_MOUNTED=0

cleanup()
{
	if [ "$VERIFY_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/verify" >/dev/null 2>&1 || true
	fi
	if [ "$USERDATA_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/userdata" >/dev/null 2>&1 || true
	fi
	if [ "$SOURCE_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/source" >/dev/null 2>&1 || true
	fi
	if [ -n "$LOOP" ]; then
		sudo -n losetup -d "$LOOP" >/dev/null 2>&1 || true
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

for tool in cc sudo sgdisk partprobe losetup mkfs.ext4 e2fsck resize2fs jq; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "missing required tool: $tool" >&2
		exit 1
	}
done
sudo -n true

"$GO" build -o "$TMP/rknetupdate-control" ./cmd/rknetupdate-control
"$GO" build -o "$TMP/rknetupdate-recovery" ./cmd/rknetupdate-recovery
"$GO" build -o "$TMP/make-updateimg" ./scripts/testdata/make-updateimg.go
cc -shared -fPIC -o "$TMP/libsgdisk-nosync.so" "$ROOT/scripts/sgdisk-nosync.c"

truncate -s 512M "$TMP/disk.img"
LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --clear \
	--new=1:2048:+8M --typecode=1:8300 --change-name=1:uboot \
	--new=2:0:+8M --typecode=2:8300 --change-name=2:misc \
	--new=3:0:+16M --typecode=3:8300 --change-name=3:boot \
	--new=4:0:+16M --typecode=4:8300 --change-name=4:recovery \
	--new=5:0:+8M --typecode=5:8300 --change-name=5:backup \
	--new=6:0:0 --typecode=6:8300 --change-name=6:rootfs \
	"$TMP/disk.img" >/dev/null

LOOP="$(sudo -n losetup --find --show --partscan "$TMP/disk.img")"
P1="${LOOP}p1"
P2="${LOOP}p2"
P3="${LOOP}p3"
P4="${LOOP}p4"
P5="${LOOP}p5"
P6="${LOOP}p6"
i=0
while [ ! -b "$P2" ] || [ ! -b "$P4" ] || [ ! -b "$P6" ]; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "loop partition nodes did not appear" >&2; exit 1; }
	sleep 0.1
done

part_start()
{
	sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info="$1" "$LOOP" |
		awk -F: '/First sector/ {gsub(/^[[:space:]]+/, "", $2); split($2, value, " "); print value[1]}'
}

part_last()
{
	sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info="$1" "$LOOP" |
		awk -F: '/Last sector/ {gsub(/^[[:space:]]+/, "", $2); split($2, value, " "); print value[1]}'
}

parameter_entry()
{
	number="$1"
	name="$2"
	start="$(part_start "$number")"
	last="$(part_last "$number")"
	size=$((last - start + 1))
	printf '0x%08x@0x%08x(%s)' "$size" "$start" "$name"
}

P6_START="$(part_start 6)"
{
	printf '%s\n' 'TYPE: GPT'
	printf 'CMDLINE: mtdparts=:'
	parameter_entry 1 uboot
	printf ','
	parameter_entry 2 misc
	printf ','
	parameter_entry 3 boot
	printf ','
	parameter_entry 4 recovery
	printf ','
	parameter_entry 5 backup
	printf ',-@0x%08x(rootfs:grow)\n' "$P6_START"
} > "$TMP/parameter.txt"

sudo -n mkfs.ext4 -q -F -L rootfs "$P6"
mkdir "$TMP/source" "$TMP/userdata" "$TMP/verify" "$TMP/package"
"$TMP/make-updateimg" \
	--image "$TMP/package/update.img" \
	--manifest "$TMP/package/updateimg.manifest.json" \
	--parameter "$TMP/parameter.txt" \
	--parent "$LOOP"

sudo -n mount "$P6" "$TMP/source"
SOURCE_MOUNTED=1
sudo -n mkdir -p "$TMP/source/var/lib/rknetupdate/updateimg-v1"
sudo -n cp "$TMP/package/update.img" "$TMP/source/var/lib/rknetupdate/updateimg-v1/update.img"
sudo -n cp "$TMP/package/updateimg.manifest.json" "$TMP/source/var/lib/rknetupdate/updateimg-v1/updateimg.manifest.json"
PACKAGE_SHA="$(jq -r .sha256 "$TMP/package/updateimg.manifest.json")"
printf '%s' KEEP | sudo -n dd of="$P2" bs=1 seek=$((16384 + 836)) conv=notrunc status=none
sudo -n "$TMP/rknetupdate-control" prepare-updateimg \
	--test-mode --allow-write --confirm "PREPARE_UPDATEIMG_V1:$PACKAGE_SHA" \
	--updateimg-manifest "$TMP/source/var/lib/rknetupdate/updateimg-v1/updateimg.manifest.json" \
	--updateimg-disk "$LOOP" --updateimg-rootfs "$P6" \
	--updateimg-recovery "$P4" --updateimg-misc "$P2" --updateimg-parent "$LOOP" \
	> "$TMP/prepare.json"
sudo -n umount "$TMP/source"
SOURCE_MOUNTED=0
printf '%s\n' rknetupdate-recovery-v1 > "$TMP/recovery-marker"

run_recovery()
{
	stop_phase="${1:-}"
	set --
	if [ "$stop_phase" = stop-after-shrunk ]; then
		set -- --updateimg-migration-stop-after updateimg-shrunk
	fi
	sudo -n unshare --mount --propagation private "$TMP/rknetupdate-recovery" \
		--test-mode --resume-updateimg-v1 --stay-in-recovery \
		--updateimg-migration-disk "$LOOP" --updateimg-migration-rootfs "$P6" \
		--updateimg-migration-misc "$P2" --updateimg-migration-userdata "${LOOP}p7" \
		--updateimg-migration-parent "$LOOP" --updateimg-migration-mountinfo /proc/self/mountinfo \
		--updateimg-migration-marker "$TMP/recovery-marker" \
		--updateimg-migration-source-mount "$TMP/source" \
		--updateimg-migration-userdata-mount "$TMP/userdata" \
		--updateimg-migration-status "$TMP/status.json" \
		--updateimg-migration-sgdisk "$(command -v sgdisk)" \
		--updateimg-migration-partprobe "$(command -v partprobe)" \
		--updateimg-migration-e2fsck "$(command -v e2fsck)" \
		--updateimg-migration-resize2fs "$(command -v resize2fs)" \
		--updateimg-migration-mkfs-ext4 "$(command -v mkfs.ext4)" \
		--updateimg-migration-stage-margin $((16 * 1024 * 1024)) \
		--updateimg-migration-tool-ld-preload "$TMP/libsgdisk-nosync.so" \
		"$@" > "$TMP/recovery.json" 2>> "$TMP/recovery.log"
}

run_recovery stop-after-shrunk
[ -b "${LOOP}p7" ] || { echo "temporary p7 was not created at the shrunk stop" >&2; exit 1; }
BCB_PHASE="$(sudo -n dd if="$P2" bs=1 skip=$((16384 + 32)) count=32 status=none | tr -d '\000')"
[ "$BCB_PHASE" = updateimg-shrunk ] || { echo "unexpected stopped BCB phase: $BCB_PHASE" >&2; exit 1; }

run_recovery
sudo -n jq -e '.phase == "stock-handoff" and .success == true and .handoff_ready == true and .bcb_cleared == false' "$TMP/status.json" >/dev/null
sudo -n mount "${LOOP}p7" "$TMP/verify"
VERIFY_MOUNTED=1
sudo -n test -r "$TMP/verify/rknetupdate/updateimg-v1/update.img" || { echo "staged update.img is missing" >&2; exit 1; }
sudo -n test "$TMP/verify/update.img" -ef "$TMP/verify/rknetupdate/updateimg-v1/update.img" || { echo "canonical update.img is not the staged hardlink" >&2; exit 1; }
sudo -n jq -e '.phase == "stock-pending"' "$TMP/verify/rknetupdate/updateimg-v1/state.json" >/dev/null
sudo -n umount "$TMP/verify"
VERIFY_MOUNTED=0

BCB_RECOVERY="$(sudo -n dd if="$P2" bs=1 skip=$((16384 + 64)) count=768 status=none | tr -d '\000')"
printf '%s' "$BCB_RECOVERY" | grep -q -- '--update_package=/userdata/update.img'
[ "$(sudo -n dd if="$P2" bs=1 skip=$((16384 + 836)) count=4 status=none)" = KEEP ] || {
	echo "BCB systemFlag was not preserved" >&2
	exit 1
}

echo "update.img v1 loop migration resume passed: disk=$LOOP package_sha256=$PACKAGE_SHA"
