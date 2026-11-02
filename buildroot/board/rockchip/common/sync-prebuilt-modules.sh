#!/bin/sh
set -u

TARGET_DIR="${1:-${TARGET_DIR:-}}"
if [ -z "$TARGET_DIR" ]; then
	echo "sync-prebuilt-modules: missing TARGET_DIR" >&2
	exit 1
fi

if [ "${TOPEET_SYNC_KERNEL_MODULES:-1}" = "0" ]; then
	echo ">>> Skipping Topeet kernel module sync (TOPEET_SYNC_KERNEL_MODULES=0)"
	exit 0
fi

MODULE_DIR="$TARGET_DIR/usr/local/modules"
[ -d "$MODULE_DIR" ] || exit 0

LIST_FILE="${TOPEET_MODULE_LIST:-$MODULE_DIR/topeet-wifibt-modules.list}"
if [ ! -s "$LIST_FILE" ]; then
	echo ">>> No Topeet Wi-Fi/Bluetooth module sync list found; skipping module sync"
	exit 0
fi

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
BUILDROOT_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)"
SDK_ROOT="${SDK_ROOT:-$(CDPATH= cd -- "$BUILDROOT_DIR/.." && pwd)}"

SEARCH_DIRS="${TOPEET_MODULE_SEARCH_DIRS:-$SDK_ROOT/output/kernel-modules $SDK_ROOT/output/target/lib/modules $SDK_ROOT/kernel $SDK_ROOT/kernel-* $SDK_ROOT/external}"

find_module()
{
	module="$1"
	for dir in $SEARCH_DIRS; do
		[ -d "$dir" ] || continue
		found="$(find "$dir" -type f -name "$module" ! -path '*/buildroot/*' | sort | head -n 1)"
		if [ -n "$found" ]; then
			printf '%s\n' "$found"
			return 0
		fi
	done
	return 1
}

updated=0
kept=0
missing=0
skipped=0
echo ">>> Syncing listed Wi-Fi/Bluetooth modules from kernel build outputs"
while IFS= read -r line || [ -n "$line" ]; do
	module="$(printf '%s' "$line" | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//')"
	[ -n "$module" ] || continue
	case "$module" in
		*/*|*.ko) ;;
		*)
			echo ">>> Skipping invalid module list entry: $module"
			skipped=$((skipped + 1))
			continue
			;;
	esac
	case "$module" in
		*/*)
			echo ">>> Skipping path-like module list entry: $module"
			skipped=$((skipped + 1))
			continue
			;;
	esac
	dst="$MODULE_DIR/$module"
	src="$(find_module "$module" || true)"
	if [ -n "$src" ]; then
		install -m 0644 "$src" "$dst"
		rel="$src"
		case "$rel" in
			"$SDK_ROOT"/*) rel="${rel#"$SDK_ROOT"/}" ;;
		esac
		echo ">>> Updated $module from $rel"
		updated=$((updated + 1))
	elif [ -e "$dst" ]; then
		echo ">>> Keeping existing $module; no kernel build output found"
		kept=$((kept + 1))
	else
		echo ">>> Missing requested $module; no kernel build output or overlay module found"
		missing=$((missing + 1))
	fi
done < "$LIST_FILE"

echo ">>> Module sync complete: updated=$updated kept=$kept missing=$missing skipped=$skipped"
