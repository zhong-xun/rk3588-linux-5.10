#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
GO="${GO:-/home/topeet/.cache/codex-toolchains/go1.24.4/bin/go}"
TMP="$(mktemp -d /tmp/rknetupdate-rootfs-v2.XXXXXX)"
LOOP=""
SOURCE_MOUNTED=0
VERIFY_MOUNTED=0

cleanup()
{
	if [ "$VERIFY_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/verify" >/dev/null 2>&1 || true
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

for tool in cc sudo sgdisk partprobe losetup mkfs.ext4 e2fsck resize2fs tune2fs sha256sum jq; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "missing required tool: $tool" >&2
		exit 1
	}
done
sudo -n true

"$GO" build -o "$TMP/rknetupdate-control" ./cmd/rknetupdate-control
"$GO" build -o "$TMP/rknetupdate-recovery" ./cmd/rknetupdate-recovery
cc -shared -fPIC -o "$TMP/libsgdisk-nosync.so" "$ROOT/scripts/sgdisk-nosync.c"

truncate -s 768M "$TMP/disk.img"
LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --clear \
	--new=1:2048:+8M --typecode=1:8300 --change-name=1:uboot \
	--new=2:0:+8M --typecode=2:8300 --change-name=2:boot \
	--new=3:0:+8M --typecode=3:8300 --change-name=3:misc \
	--new=4:0:+8M --typecode=4:8300 --change-name=4:recovery \
	--new=5:0:+8M --typecode=5:8300 --change-name=5:metadata \
	--new=6:0:0 --typecode=6:8300 --change-name=6:rootfs \
	"$TMP/disk.img" >/dev/null

LOOP="$(sudo -n losetup --find --show --partscan "$TMP/disk.img")"
P3="${LOOP}p3"
P6="${LOOP}p6"
i=0
while [ ! -b "$P3" ] || [ ! -b "$P6" ]; do
	i=$((i + 1))
	[ "$i" -lt 50 ] || { echo "loop partition nodes did not appear" >&2; exit 1; }
	sleep 0.1
done
if command -v udevadm >/dev/null 2>&1; then
	sudo -n udevadm settle
fi

sudo -n mkfs.ext4 -q -F -L rootfs "$P6"
truncate -s 96M "$TMP/rootfs.img"
mkfs.ext4 -q -F -L rootfs-new "$TMP/rootfs.img"
mkdir "$TMP/image" "$TMP/source" "$TMP/stage" "$TMP/verify"
sudo -n mount -o loop "$TMP/rootfs.img" "$TMP/image"
sudo -n sh -c "printf '%s\n' rootfs-v2-loop-ok > '$TMP/image/rknetupdate-target-marker'"
sudo -n umount "$TMP/image"

SHA="$(sha256sum "$TMP/rootfs.img" | awk '{print $1}')"
SIZE="$(stat -c %s "$TMP/rootfs.img")"
UUID="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Filesystem UUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
BLOCK_SIZE="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Block size/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
BLOCK_COUNT="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Block count/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"

sudo -n mount "$P6" "$TMP/source"
SOURCE_MOUNTED=1
sudo -n mkdir -p "$TMP/source/var/lib/rknetupdate/rootfs-v2"
sudo -n cp "$TMP/rootfs.img" "$TMP/source/var/lib/rknetupdate/rootfs-v2/rootfs.img"
sudo -n tee "$TMP/source/var/lib/rknetupdate/rootfs-v2/rootfs.manifest.json" >/dev/null <<EOF
{"schema":2,"image":"rootfs.img","size":$SIZE,"sha256":"$SHA","filesystem":"ext4","filesystem_uuid":"$UUID","block_size":$BLOCK_SIZE,"block_count":$BLOCK_COUNT,"target_partlabel":"rootfs","expected_parent":"/dev/mmcblk0"}
EOF
sudo -n sync "$TMP/source/var/lib/rknetupdate/rootfs-v2"
sudo -n umount "$TMP/source"
SOURCE_MOUNTED=0

printf '%s' KEEP | sudo -n dd of="$P3" bs=1 seek=$((16384 + 836)) conv=notrunc status=none
CONFIRM="PREPARE_ROOTFS_V2:$SHA"
sudo -n "$TMP/rknetupdate-control" prepare-rootfs \
	--test-mode --allow-write --confirm "$CONFIRM" \
	--manifest "$TMP/stage/rootfs.manifest.json" \
	--disk "$LOOP" --rootfs "$P6" --misc "$P3" --parent "$LOOP" \
	--e2fsck "$(command -v e2fsck)" >/dev/null 2>&1 && {
		echo "control unexpectedly accepted a missing external manifest" >&2
		exit 1
	}
sudo -n mount "$P6" "$TMP/source"
SOURCE_MOUNTED=1
sudo -n cp "$TMP/source/var/lib/rknetupdate/rootfs-v2/rootfs.manifest.json" "$TMP/bad-hash.manifest.json"
sudo -n sed -i "s/$SHA/0000000000000000000000000000000000000000000000000000000000000000/" "$TMP/bad-hash.manifest.json"
if sudo -n "$TMP/rknetupdate-control" prepare-rootfs \
	--test-mode --allow-write --confirm "PREPARE_ROOTFS_V2:0000000000000000000000000000000000000000000000000000000000000000" \
	--manifest "$TMP/bad-hash.manifest.json" \
	--disk "$LOOP" --rootfs "$P6" --misc "$P3" --parent "$LOOP" \
	--e2fsck "$(command -v e2fsck)" >/dev/null 2>&1; then
	echo "incorrect image hash was not rejected" >&2
	exit 1
fi
sudo -n "$TMP/rknetupdate-control" prepare-rootfs \
	--test-mode --allow-write --confirm "$CONFIRM" \
	--manifest "$TMP/source/var/lib/rknetupdate/rootfs-v2/rootfs.manifest.json" \
	--disk "$LOOP" --rootfs "$P6" --misc "$P3" --parent "$LOOP" \
	--e2fsck "$(command -v e2fsck)" > "$TMP/prepare.json"
sudo -n umount "$TMP/source"
SOURCE_MOUNTED=0

printf '%s\n' rknetupdate-recovery-v1 > "$TMP/recovery-marker"
P6_TYPE_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition GUID code/ {gsub(/^[[:space:]]+/, "", $2); print substr($2, 1, 36)}')"
P6_GUID_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition unique GUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
P6_START_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/First sector/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"

run_recovery()
{
	STOP_PHASE="${1:-}"
	MARGIN="${MIGRATION_MARGIN:-$((16 * 1024 * 1024))}"
	set --
	if [ -n "$STOP_PHASE" ]; then
		set -- --migration-stop-after "$STOP_PHASE"
	fi
	sudo -n "$TMP/rknetupdate-recovery" \
	--test-mode --resume-rootfs-v2 \
	--migration-disk "$LOOP" --migration-rootfs "$P6" --migration-misc "$P3" \
	--migration-userdata "${LOOP}p7" --migration-parent "$LOOP" \
	--migration-mountinfo /proc/self/mountinfo --migration-marker "$TMP/recovery-marker" \
	--migration-source-mount "$TMP/source" --migration-userdata-mount "$TMP/stage" \
	--migration-status "$TMP/status.json" --migration-sgdisk "$(command -v sgdisk)" \
	--migration-partprobe "$(command -v partprobe)" --migration-e2fsck "$(command -v e2fsck)" \
	--migration-resize2fs "$(command -v resize2fs)" --migration-mkfs-ext4 "$(command -v mkfs.ext4)" \
	--migration-stage-margin "$MARGIN" \
	--migration-tool-ld-preload "$TMP/libsgdisk-nosync.so" \
	"$@" > "$TMP/recovery.json" 2>> "$TMP/recovery.log"
}

sudo -n mount "$P6" "$TMP/verify"
VERIFY_MOUNTED=1
if run_recovery; then
	echo "mounted p6 was not rejected" >&2
	exit 1
fi
sudo -n umount "$TMP/verify"
VERIFY_MOUNTED=0

if MIGRATION_MARGIN=$((1024 * 1024 * 1024)) run_recovery; then
	echo "insufficient temporary stage space was not rejected" >&2
	exit 1
fi

sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --change-name=6:not-rootfs "$LOOP" >/dev/null
sudo -n partprobe "$LOOP"
if run_recovery; then
	echo "wrong p6 GPT name was not rejected" >&2
	exit 1
fi
sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --change-name=6:rootfs "$LOOP" >/dev/null
sudo -n partprobe "$LOOP"

[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
	echo "BCB was cleared by a rejected migration" >&2
	exit 1
}

for phase in rootfs-shrunk rootfs-p7-ready; do
	run_recovery "$phase"
	[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
		echo "BCB was cleared at test stop $phase" >&2
		exit 1
	}
done

sudo -n mkfs.ext4 -q -F -L userdata "${LOOP}p7"
sudo -n mount "${LOOP}p7" "$TMP/verify"
VERIFY_MOUNTED=1
if run_recovery; then
	echo "p7 mounted at an unexpected path was not rejected" >&2
	exit 1
fi
sudo -n umount "$TMP/verify"
VERIFY_MOUNTED=0

run_recovery rootfs-staged
grep -q 'RKNETUPDATE-RECOVERY rootfs-v2 phase=rootfs-p7-ready: formatting the new temporary p7 userdata filesystem' "$TMP/recovery.log" || {
	echo "recovery did not report temporary p7 formatting progress" >&2
	exit 1
}
sudo -n sed -i 's/"phase": "rootfs-staged"/"phase": "rootfs-prepared"/' "$TMP/stage/rknetupdate-rootfs-v2/state.json"
if run_recovery; then
	echo "invalid staged state phase was not rejected" >&2
	exit 1
fi
sudo -n sed -i 's/"phase": "rootfs-prepared"/"phase": "rootfs-staged"/' "$TMP/stage/rknetupdate-rootfs-v2/state.json"

# Model a power loss during the p6 raw write. The next invocation must use the
# durable p7 copy to rewrite and verify the complete image.
sudo -n dd if=/dev/zero of="$P6" bs=1M count=8 conv=notrunc status=none
run_recovery rootfs-p6-verified
grep -q 'RKNETUPDATE-RECOVERY rootfs-v2 phase=rootfs-writing progress=100%' "$TMP/recovery.log" || {
	echo "recovery did not report completed rootfs write progress" >&2
	exit 1
}
run_recovery rootfs-gpt-restored
[ ! -b "${LOOP}p7" ] || { echo "p7 exists after gpt-restored stop" >&2; exit 1; }
run_recovery rootfs-expanded
[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
	echo "BCB was cleared at rootfs-expanded test stop" >&2
	exit 1
}
run_recovery

sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --verify "$LOOP" >/dev/null
[ ! -b "${LOOP}p7" ] || { echo "temporary p7 still exists" >&2; exit 1; }
[ "$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --print "$LOOP" | awk '$1 ~ /^7$/ {print $1}')" = "" ] || {
	echo "GPT still contains partition 7" >&2
	exit 1
}
[ "$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --print "$LOOP" | awk '$1 ~ /^6$/ {print $2}')" != "" ] || {
	echo "GPT partition 6 is missing" >&2
	exit 1
}
P6_TYPE_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition GUID code/ {gsub(/^[[:space:]]+/, "", $2); print substr($2, 1, 36)}')"
P6_GUID_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition unique GUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
P6_START_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/First sector/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
P6_LAST_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Last sector/ {gsub(/^[[:space:]]+/, "", $2); split($2, value, " "); print value[1]}')"
GPT_LAST_USABLE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --print "$LOOP" | sed -n 's/.*last usable sector is \([0-9][0-9]*\).*/\1/p')"
[ "$P6_TYPE_AFTER" = "$P6_TYPE_BEFORE" ] || { echo "p6 type GUID changed" >&2; exit 1; }
[ "$P6_GUID_AFTER" = "$P6_GUID_BEFORE" ] || { echo "p6 unique GUID changed" >&2; exit 1; }
[ "$P6_START_AFTER" = "$P6_START_BEFORE" ] || { echo "p6 start changed" >&2; exit 1; }
[ "$P6_LAST_AFTER" = "$GPT_LAST_USABLE" ] || { echo "p6 does not end at GPT last usable LBA" >&2; exit 1; }
[ "$(sudo -n blkid -s UUID -o value "$P6")" = "$UUID" ] || { echo "target UUID mismatch" >&2; exit 1; }
sudo -n mount "$P6" "$TMP/verify"
VERIFY_MOUNTED=1
[ "$(sudo -n cat "$TMP/verify/rknetupdate-target-marker")" = rootfs-v2-loop-ok ] || {
	echo "target marker missing" >&2
	exit 1
}
sudo -n umount "$TMP/verify"
VERIFY_MOUNTED=0
[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=836 status=none | tr -d '\000')" = "" ] || {
	echo "BCB request fields were not cleared" >&2
	exit 1
}
[ "$(sudo -n dd if="$P3" bs=1 skip=$((16384 + 836)) count=4 status=none)" = KEEP ] || {
	echo "BCB systemFlag was not preserved" >&2
	exit 1
}
sudo -n jq -e '.phase == "succeeded" and .success == true and .bcb_cleared == true' "$TMP/status.json" >/dev/null

echo "rootfs v2 loop migration passed: disk=$LOOP sha256=$SHA uuid=$UUID"
