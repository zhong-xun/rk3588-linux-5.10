#!/bin/sh
set -eu

# End-to-end host test for the v1 offline multi-image recovery bundle.  It
# uses a disposable GPT loop disk and never touches a real board device.

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
GO="${GO:-/home/topeet/.cache/codex-toolchains/go1.24.4/bin/go}"
TMP="$(mktemp -d /tmp/rknetupdate-bundle-v1.XXXXXX)"
LOOP=""
SOURCE_MOUNTED=0
STAGE_MOUNTED=0
VERIFY_MOUNTED=0

cleanup()
{
	if [ "$VERIFY_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/verify" >/dev/null 2>&1 || true
	fi
	if [ "$STAGE_MOUNTED" -eq 1 ]; then
		sudo -n umount "$TMP/stage" >/dev/null 2>&1 || true
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

for tool in cc sudo sgdisk partprobe losetup mkfs.ext4 e2fsck resize2fs tune2fs sha256sum jq dd; do
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
P1="${LOOP}p1"
P2="${LOOP}p2"
P3="${LOOP}p3"
P6="${LOOP}p6"
i=0
while [ ! -b "$P1" ] || [ ! -b "$P2" ] || [ ! -b "$P3" ] || [ ! -b "$P6" ]; do
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
sudo -n sh -c "printf '%s\n' bundle-v1-loop-ok > '$TMP/image/rknetupdate-target-marker'"
sudo -n umount "$TMP/image"

dd if=/dev/urandom of="$TMP/boot.img" bs=1M count=2 status=none
dd if=/dev/urandom of="$TMP/uboot.img" bs=1M count=1 status=none
ROOTFS_SHA="$(sha256sum "$TMP/rootfs.img" | awk '{print $1}')"
BOOT_SHA="$(sha256sum "$TMP/boot.img" | awk '{print $1}')"
UBOOT_SHA="$(sha256sum "$TMP/uboot.img" | awk '{print $1}')"
ROOTFS_SIZE="$(stat -c %s "$TMP/rootfs.img")"
BOOT_SIZE="$(stat -c %s "$TMP/boot.img")"
UBOOT_SIZE="$(stat -c %s "$TMP/uboot.img")"
ROOTFS_UUID="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Filesystem UUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
BLOCK_SIZE="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Block size/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
BLOCK_COUNT="$(tune2fs -l "$TMP/rootfs.img" 2>/dev/null | awk -F: '/Block count/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"

sudo -n mount "$P6" "$TMP/source"
SOURCE_MOUNTED=1
sudo -n mkdir -p "$TMP/source/var/lib/rknetupdate/bundle-v1"
sudo -n cp "$TMP/rootfs.img" "$TMP/source/var/lib/rknetupdate/bundle-v1/rootfs.img"
sudo -n cp "$TMP/boot.img" "$TMP/source/var/lib/rknetupdate/bundle-v1/boot.img"
sudo -n cp "$TMP/uboot.img" "$TMP/source/var/lib/rknetupdate/bundle-v1/uboot.img"
sudo -n tee "$TMP/source/var/lib/rknetupdate/bundle-v1/bundle.manifest.json" >/dev/null <<EOF
{"schema":1,"expected_parent":"$LOOP","artifacts":[{"target":"rootfs","image":"rootfs.img","size":$ROOTFS_SIZE,"sha256":"$ROOTFS_SHA","filesystem":"ext4","filesystem_uuid":"$ROOTFS_UUID","block_size":$BLOCK_SIZE,"block_count":$BLOCK_COUNT},{"target":"boot","image":"boot.img","size":$BOOT_SIZE,"sha256":"$BOOT_SHA"},{"target":"uboot","image":"uboot.img","size":$UBOOT_SIZE,"sha256":"$UBOOT_SHA"}]}
EOF
sudo -n sync "$TMP/source/var/lib/rknetupdate/bundle-v1"
BUNDLE_SHA="$(sudo -n sha256sum "$TMP/source/var/lib/rknetupdate/bundle-v1/bundle.manifest.json" | awk '{print $1}')"
sudo -n umount "$TMP/source"
SOURCE_MOUNTED=0

printf '%s' KEEP | sudo -n dd of="$P3" bs=1 seek=$((16384 + 836)) conv=notrunc status=none
sudo -n "$TMP/rknetupdate-control" prepare-bundle --test-mode --allow-write \
	--confirm "PREPARE_BUNDLE_V1:$BUNDLE_SHA" \
	--bundle-manifest "$TMP/source/var/lib/rknetupdate/bundle-v1/bundle.manifest.json" \
	--bundle-disk "$LOOP" --bundle-rootfs "$P6" --bundle-boot "$P2" --bundle-uboot "$P1" \
	--bundle-misc "$P3" --bundle-parent "$LOOP" > "$TMP/prepare.json" 2>&1 && {
	echo "control unexpectedly accepted unmounted source path" >&2
	exit 1
}
sudo -n mount "$P6" "$TMP/source"
SOURCE_MOUNTED=1
sudo -n "$TMP/rknetupdate-control" prepare-bundle --test-mode --allow-write \
	--confirm "PREPARE_BUNDLE_V1:$BUNDLE_SHA" \
	--bundle-manifest "$TMP/source/var/lib/rknetupdate/bundle-v1/bundle.manifest.json" \
	--bundle-disk "$LOOP" --bundle-rootfs "$P6" --bundle-boot "$P2" --bundle-uboot "$P1" \
	--bundle-misc "$P3" --bundle-parent "$LOOP" > "$TMP/prepare.json"
sudo -n umount "$TMP/source"
SOURCE_MOUNTED=0

printf '%s\n' rknetupdate-recovery-v1 > "$TMP/recovery-marker"
P6_TYPE_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition GUID code/ {gsub(/^[[:space:]]+/, "", $2); print substr($2, 1, 36)}')"
P6_GUID_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition unique GUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
P6_START_BEFORE="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/First sector/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"

run_recovery()
{
	STOP_PHASE="${1:-}"
	MARGIN="${BUNDLE_MARGIN:-$((16 * 1024 * 1024))}"
	set --
	if [ -n "$STOP_PHASE" ]; then
		set -- --bundle-migration-stop-after "$STOP_PHASE"
	fi
	sudo -n "$TMP/rknetupdate-recovery" --test-mode --resume-bundle-v1 \
		--bundle-migration-disk "$LOOP" --bundle-migration-rootfs "$P6" \
		--bundle-migration-boot "$P2" --bundle-migration-uboot "$P1" --bundle-migration-misc "$P3" \
		--bundle-migration-userdata "${LOOP}p7" --bundle-migration-parent "$LOOP" \
		--bundle-migration-mountinfo /proc/self/mountinfo --bundle-migration-marker "$TMP/recovery-marker" \
		--bundle-migration-source-mount "$TMP/source" --bundle-migration-userdata-mount "$TMP/stage" \
		--bundle-migration-status "$TMP/status.json" --bundle-migration-sgdisk "$(command -v sgdisk)" \
		--bundle-migration-partprobe "$(command -v partprobe)" --bundle-migration-e2fsck "$(command -v e2fsck)" \
		--bundle-migration-resize2fs "$(command -v resize2fs)" --bundle-migration-mkfs-ext4 "$(command -v mkfs.ext4)" \
		--bundle-migration-stage-margin "$MARGIN" --bundle-migration-tool-ld-preload "$TMP/libsgdisk-nosync.so" \
		"$@" > "$TMP/recovery.json"
}

if BUNDLE_MARGIN=$((1024 * 1024 * 1024)) run_recovery; then
	echo "insufficient temporary bundle stage space was not rejected" >&2
	exit 1
fi
[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
	echo "BCB was cleared by a rejected bundle migration" >&2
	exit 1
}

for phase in bundle-shrunk bundle-p7-ready bundle-staged; do
	run_recovery "$phase"
	[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
		echo "BCB was cleared at test stop $phase" >&2
		exit 1
	}
done

# Model a power loss during raw rootfs writing.  The next invocation must use
# the durable p7 source and independently write boot then uboot.
sudo -n dd if=/dev/zero of="$P6" bs=1M count=8 conv=notrunc status=none
run_recovery bundle-rootfs-ok
run_recovery bundle-payload-ok
run_recovery bundle-gpt-restored
[ ! -b "${LOOP}p7" ] || { echo "p7 exists after bundle-gpt-restored stop" >&2; exit 1; }
run_recovery bundle-expanded
[ "$(sudo -n dd if="$P3" bs=1 skip=16384 count=16 status=none | tr -d '\000')" != "" ] || {
	echo "BCB was cleared at bundle-expanded test stop" >&2
	exit 1
}
run_recovery

sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --verify "$LOOP" >/dev/null
[ ! -b "${LOOP}p7" ] || { echo "temporary p7 still exists" >&2; exit 1; }
P6_TYPE_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition GUID code/ {gsub(/^[[:space:]]+/, "", $2); print substr($2, 1, 36)}')"
P6_GUID_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/Partition unique GUID/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
P6_START_AFTER="$(sudo -n env LD_PRELOAD="$TMP/libsgdisk-nosync.so" sgdisk --info=6 "$LOOP" | awk -F: '/First sector/ {gsub(/^[[:space:]]+/, "", $2); print $2}')"
[ "$P6_TYPE_AFTER" = "$P6_TYPE_BEFORE" ] || { echo "p6 type GUID changed" >&2; exit 1; }
[ "$P6_GUID_AFTER" = "$P6_GUID_BEFORE" ] || { echo "p6 unique GUID changed" >&2; exit 1; }
[ "$P6_START_AFTER" = "$P6_START_BEFORE" ] || { echo "p6 start changed" >&2; exit 1; }
[ "$(sudo -n dd if="$P2" bs=1 count="$BOOT_SIZE" status=none | sha256sum | awk '{print $1}')" = "$BOOT_SHA" ] || { echo "boot readback mismatch" >&2; exit 1; }
[ "$(sudo -n dd if="$P1" bs=1 count="$UBOOT_SIZE" status=none | sha256sum | awk '{print $1}')" = "$UBOOT_SHA" ] || { echo "uboot readback mismatch" >&2; exit 1; }
[ "$(sudo -n blkid -s UUID -o value "$P6")" = "$ROOTFS_UUID" ] || { echo "rootfs UUID mismatch" >&2; exit 1; }
sudo -n mount "$P6" "$TMP/verify"
VERIFY_MOUNTED=1
[ "$(sudo -n cat "$TMP/verify/rknetupdate-target-marker")" = bundle-v1-loop-ok ] || {
	echo "rootfs target marker missing" >&2
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

echo "bundle v1 loop migration passed: disk=$LOOP manifest=$BUNDLE_SHA"
