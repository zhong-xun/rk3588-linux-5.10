#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
    set -- \
        /home/topeet/Linux/rk3588/rk3588-linux-6.1/external/recovery \
        /home/topeet/Linux/topeet-extboot-sdk-6.1/external/recovery
fi

reference=
count=0
for recovery in "$@"; do
    for source in Makefile recovery.c roots.c rknetupdate_sha256.c \
                  rknetupdate_sha256.h tests/rknetupdate_sha256_test.c \
                  tests/rknetupdate_state_test.c; do
        [ -r "$recovery/$source" ] || {
            echo "missing RKNetUpdate handoff source: $recovery/$source" >&2
            exit 1
        }
        if [ -n "$reference" ]; then
            cmp -s "$reference/$source" "$recovery/$source" || {
                echo "RKNetUpdate handoff source drift: $recovery/$source" >&2
                exit 1
            }
        fi
    done

    grep -Fq 'result = -1;' "$recovery/recovery.c"
    grep -Fq 'rknetupdate_stock_handoff_candidate' "$recovery/recovery.c"
    grep -Fq 'rknetupdate_handoff_state < 0' "$recovery/recovery.c"
    grep -Fq 'refusing ordinary update fallback' "$recovery/recovery.c"
    grep -Fq 'rknetupdate_sha256_fd(package_fd, package_stat.st_size' \
        "$recovery/recovery.c"
    grep -Fq 'strcmp(package_sha256, request.package_sha256) != 0' \
        "$recovery/recovery.c"
    grep -Fq 'rknetupdate_padded_field_equals(boot.command,' \
        "$recovery/recovery.c"
    grep -Fq 'rknetupdate_padded_field_equals(boot.status,' \
        "$recovery/recovery.c"
    grep -Fq 'rknetupdate_padded_field_equals(boot.recovery,' \
        "$recovery/recovery.c"

    make -s -C "$recovery" test-rknetupdate-sha256
    make -s -C "$recovery" test-rknetupdate-state
    reference=${reference:-$recovery}
    count=$((count + 1))
done

echo "RKNetUpdate fail-closed handoff validation passed for $count source tree(s)"
