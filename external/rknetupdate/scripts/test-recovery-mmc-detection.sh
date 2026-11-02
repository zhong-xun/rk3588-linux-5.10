#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/rknetupdate-recovery-mmc.XXXXXX)"
SYSFS="$TMP/sys"
DEVICES="$TMP/dev"

cleanup()
{
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

mkdir -p "$SYSFS/mmc7:0001/block" \
    "$SYSFS/mmc0:0001/block" \
    "$SYSFS/mmc2:0001/block" \
    "$DEVICES"
printf 'SD\n' > "$SYSFS/mmc7:0001/type"
printf 'MMC\n' > "$SYSFS/mmc0:0001/type"
printf 'UNKNOWN\n' > "$SYSFS/mmc2:0001/type"
: > "$SYSFS/mmc7:0001/block/mmcblk2"
: > "$SYSFS/mmc0:0001/block/mmcblk9"
: > "$SYSFS/mmc2:0001/block/mmcblk4"

if [ "$#" -eq 0 ]; then
    set -- \
        /home/topeet/Linux/rk3562/rk3562-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3568/rk3568-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3568_mini/rk3568-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3588/rk3588-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3588_mini/rk3588-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3588s/rk3588s-linux-5.10/external/recovery \
        /home/topeet/Linux/rk3562/rk3562-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3568/rk3568-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3568_mini/rk3568-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3576/rk3576-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3588/rk3588-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3588_mini/rk3588-linux-6.1/external/recovery \
        /home/topeet/Linux/rk3588s/rk3588s-linux-6.1/external/recovery \
        /home/topeet/Linux/topeet-extboot-sdk-6.1/external/recovery
fi

count=0
for recovery in "$@"; do
    [ -r "$recovery/rktools.c" ] || {
        echo "missing recovery source: $recovery/rktools.c" >&2
        exit 1
    }
    count=$((count + 1))
    binary="$TMP/test-$count"
    cc -std=gnu11 -Wall -Wextra -Werror \
        -Wno-unused-but-set-variable \
        -I"$recovery" \
        -DRKTOOLS_SOURCE="\"$recovery/rktools.c\"" \
        "$ROOT/scripts/testdata/recovery-mmc-contract.c" \
        -o "$binary"
    "$binary" "$SYSFS" "$DEVICES"
done

echo "recovery MMC mapping checks passed for $count source tree(s)"
