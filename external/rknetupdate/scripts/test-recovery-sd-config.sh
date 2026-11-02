#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/rknetupdate-recovery-sd-config.XXXXXX)"

cleanup()
{
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

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
    [ -r "$recovery/sdboot.c" ] || {
        echo "missing recovery source: $recovery/sdboot.c" >&2
        exit 1
    }
    count=$((count + 1))
    binary="$TMP/test-$count"
    cc -std=gnu11 -Wall -Wextra -Werror \
        -I"$recovery" \
        -DSDBOOT_SOURCE="\"$recovery/sdboot.c\"" \
        "$ROOT/scripts/testdata/recovery-sd-config-contract.c" \
        -o "$binary"
    "$binary" "$TMP/config-$count"
done

echo "recovery SD configuration checks passed for $count source tree(s)"
