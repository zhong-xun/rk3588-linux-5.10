#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMP="$(mktemp -d /tmp/rknetupdate-recovery-flow.XXXXXX)"

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

require_text()
{
    text=$1
    file=$2
    grep -Fq "$text" "$file" || {
        echo "missing recovery contract '$text': $file" >&2
        exit 1
    }
}

require_safe_unmount_before_wait()
{
	file=$1
	awk '
		/static void unmount_update_medium/ { in_helper = 1 }
		in_helper && /^}/ { in_helper = 0 }
		in_helper && /sync\(\)/ { has_sync = 1 }
		in_helper && /for \(;;\)/ { retries_until_success = 1 }
		in_helper && /if \(unmount_path\(mount_path\) == 0\)/ {
			in_success = 1
		}
		in_helper && in_success && /^[[:space:]]*return;/ {
			returns_after_success = 1
			in_success = 0
		}
		/unmount_update_medium\(EX_SDCARD_ROOT,/ { sd_unmount = NR }
		/recovery_notice_wait_for_removal\("SD card",/ {
			if (sd_unmount && NR > sd_unmount) sd_ordered = 1
		}
		/unmount_update_medium\(EX_UDISK_ROOT,/ { usb_unmount = NR }
		/recovery_notice_wait_for_removal\("USB drive",/ {
			if (usb_unmount && NR > usb_unmount) usb_ordered = 1
		}
		END {
			exit !(has_sync && retries_until_success &&
			       returns_after_success && sd_ordered && usb_ordered)
		}
	' "$file" || {
		echo "removal wait can run before a confirmed safe unmount: $file" >&2
		exit 1
	}
}

mkdir -p "$TMP/include"
printf '%s\n' '#define GIT_COMMIT_INFO -ghost-test' > \
    "$TMP/include/recovery_autogenerate.h"

count=0
for recovery in "$@"; do
    for source in recovery.c recovery_notice.c rktools.c sdboot.c; do
        [ -r "$recovery/$source" ] || {
            echo "missing recovery source: $recovery/$source" >&2
            exit 1
        }
    done

    require_text 'getenv(SD_POINT_NAME_2)' "$recovery/recovery.c"
    require_text 'is_update_medium_partition_of' "$recovery/recovery.c"
    require_text 'static void unmount_update_medium' "$recovery/recovery.c"
    require_text 'unmount_update_medium(EX_SDCARD_ROOT,' \
        "$recovery/recovery.c"
    require_text 'unmount_update_medium(EX_UDISK_ROOT,' \
        "$recovery/recovery.c"
	# The helper retries forever and returns only after a successful synced
	# unmount; the removal wait must appear after that blocking call.
    require_safe_unmount_before_wait "$recovery/recovery.c"
    require_text 'Do not remove it; retrying safe unmount.' \
        "$recovery/recovery.c"
    require_text 'run_factory_command("sh "CMD4RECOVERY_FILENAME)' \
        "$recovery/recovery.c"
    require_text 'updateEngine did not produce factory update artifacts.' \
        "$recovery/recovery.c"
    require_text 'NOTICE_REMOVAL_CONFIRM_MS' "$recovery/recovery_notice.c"
    require_text 'strcasecmp(name, selector) != 0' \
        "$recovery/recovery_notice.c"

    if grep -Fq 'SDCARD_DEVICE_FALLBACK' "$recovery/recovery.c" ||
       grep -Eq '^[[:space:]]*system\("(flashcp|flash_erase|sh )' \
           "$recovery/recovery.c"; then
        echo "unsafe recovery fallback or unchecked factory command: $recovery" >&2
        exit 1
    fi

    count=$((count + 1))
    cc -std=gnu11 -I"$TMP/include" -I"$recovery" -DUSE_UPDATEENGINE=ON \
        -c "$recovery/recovery.c" -o "$TMP/recovery-$count.o"
    cc -std=gnu11 -Wall -Wextra -Werror -I"$recovery" \
        -c "$recovery/recovery_notice.c" \
        -o "$TMP/recovery-notice-$count.o"
    cc -std=gnu11 -Wall -Wextra -Werror \
        -Wno-unused-but-set-variable -I"$recovery" \
        -c "$recovery/rktools.c" -o "$TMP/rktools-$count.o"
    cc -std=gnu11 -Wall -Wextra -Werror -I"$recovery" \
        -c "$recovery/sdboot.c" -o "$TMP/sdboot-$count.o"
done

echo "recovery flow and strict compile checks passed for $count source tree(s)"
