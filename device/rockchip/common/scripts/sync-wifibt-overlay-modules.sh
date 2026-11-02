#!/bin/bash -e

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
SDK_DIR="${SDK_DIR:-$(realpath "$SCRIPT_DIR/../../../..")}"
RKWIFIBT_DIR="$SDK_DIR/external/rkwifibt"

detect_kernel_release()
{
	local candidate release

	for candidate in \
		"$SDK_DIR/kernel/include/config/kernel.release" \
		"$SDK_DIR"/kernel-*/include/config/kernel.release
	do
		[ -r "$candidate" ] || continue
		read -r release < "$candidate" || true
		[ -n "$release" ] || continue
		printf '%s\n' "$release"
		return 0
	done

	for candidate in \
		"$SDK_DIR/kernel/include/generated/utsrelease.h" \
		"$SDK_DIR"/kernel-*/include/generated/utsrelease.h
	do
		[ -r "$candidate" ] || continue
		release="$(sed -n 's/^#define UTS_RELEASE "\([^"]*\)"/\1/p' "$candidate" | head -n 1)"
		[ -n "$release" ] || continue
		printf '%s\n' "$release"
		return 0
	done

	return 1
}

module_kernel_release()
{
	local module="$1"

	command -v modinfo >/dev/null 2>&1 || return 1
	modinfo "$module" 2>/dev/null | sed -n 's/^vermagic:[[:space:]]*\([^[:space:]]*\).*/\1/p' | head -n 1
}

module_usable_for_kernel()
{
	local module="$1"
	local expected="$2"
	local release

	[ -n "$expected" ] || return 0
	release="$(module_kernel_release "$module" || true)"
	[ -n "$release" ] || return 0
	[ "$release" = "$expected" ]
}

find_module()
{
	local module="$1"
	local expected="$2"
	local dir found release

	for dir in \
		${TOPEET_MODULE_SEARCH_DIRS:-} \
		"$SDK_DIR/output/kernel-modules" \
		"$SDK_DIR/output/target/lib/modules" \
		"$SDK_DIR/kernel" \
		"$SDK_DIR"/kernel-* \
		"$RKWIFIBT_DIR/drivers"
	do
		[ -d "$dir" ] || continue
		while IFS= read -r found; do
			[ -n "$found" ] || continue
			if module_usable_for_kernel "$found" "$expected"; then
				printf '%s\n' "$found"
				return 0
			fi
			release="$(module_kernel_release "$found" || echo unknown)"
			echo ">>> Ignoring $module from ${found#"$SDK_DIR"/}: vermagic $release != kernel $expected" >&2
		done < <(find "$dir" -type f -name "$module" ! -path '*/buildroot/*' | sort)
	done

	return 1
}

sync_overlay_dir()
{
	local overlay_dir="$1"
	local expected="$2"
	local list_file="$overlay_dir/topeet-wifibt-modules.list"
	local module src rel release
	local updated=0 missing=0 removed=0 skipped=0

	[ -s "$list_file" ] || return 0
	echo ">>> Syncing Wi-Fi/Bluetooth overlay modules: ${overlay_dir#"$SDK_DIR"/}"

	while IFS= read -r line || [ -n "$line" ]; do
		module="$(printf '%s' "$line" | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//')"
		[ -n "$module" ] || continue

		case "$module" in
			*.ko) ;;
			*)
				echo ">>> Skipping invalid module list entry: $module"
				skipped=$((skipped + 1))
				continue
				;;
		esac

		src="$(find_module "$module" "$expected" || true)"
		if [ -n "$src" ]; then
			install -m 0644 "$src" "$overlay_dir/$module"
			rel="$src"
			case "$rel" in
				"$SDK_DIR"/*) rel="${rel#"$SDK_DIR"/}" ;;
			esac
			echo ">>> Updated $module from $rel"
			updated=$((updated + 1))
			continue
		fi

		if [ -e "$overlay_dir/$module" ] && ! module_usable_for_kernel "$overlay_dir/$module" "$expected"; then
			release="$(module_kernel_release "$overlay_dir/$module" || echo unknown)"
			echo ">>> Removing stale $module from ${overlay_dir#"$SDK_DIR"/}: vermagic $release != kernel $expected"
			rm -f "$overlay_dir/$module"
			removed=$((removed + 1))
		else
			echo ">>> Missing requested $module; no matching kernel build output found"
			missing=$((missing + 1))
		fi
	done < "$list_file"

	echo ">>> Overlay module sync complete: updated=$updated missing=$missing removed=$removed skipped=$skipped"
}

main()
{
	local expected overlay_dir found=0

	expected="$(detect_kernel_release || true)"
	if [ -z "$expected" ]; then
		echo ">>> ERROR: unable to detect kernel release for $SDK_DIR" >&2
		return 1
	fi

	if [ "$#" -gt 0 ]; then
		for overlay_dir in "$@"; do
			[ -d "$overlay_dir" ] || {
				echo ">>> ERROR: overlay module dir not found: $overlay_dir" >&2
				return 1
			}
			sync_overlay_dir "$(realpath "$overlay_dir")" "$expected"
			found=1
		done
	else
		while IFS= read -r overlay_dir; do
			sync_overlay_dir "$overlay_dir" "$expected"
			found=1
		done < <(find "$SDK_DIR/buildroot/board/rockchip" \
			-path '*/fs-overlay/usr/local/modules' -type d | sort)
	fi

	if [ "$found" -eq 0 ]; then
		echo ">>> No Wi-Fi/Bluetooth overlay module directories found"
	fi
}

main "$@"
