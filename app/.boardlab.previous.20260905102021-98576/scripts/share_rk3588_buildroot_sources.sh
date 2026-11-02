#!/usr/bin/env bash
#
# Create a shared Buildroot core for the Topeet RK3588 and RK3588S SDKs.
#
# The two products must not point buildroot/ at the same directory directly:
# Buildroot derives paths from TOPDIR, while a number of Rockchip packages use
# TOPDIR/../app and TOPDIR/../external.  This script instead creates a real
# per-SDK wrapper directory.  The common core is symlinked, while board/product
# layers and the Buildroot O= directory stay local to that SDK.  Generic
# package source and patches (including BoardLab, Qt and GStreamer) remain in
# the common core so a source fix is not silently split between the products.
#
# One common core is created per Buildroot release.  RK3588 Linux 5.10 uses
# Buildroot 2021.11 and Linux 6.1 uses Buildroot 2024.02; those two releases
# cannot be combined.
#
# Usage:
#   scripts/share_rk3588_buildroot_sources.sh --check
#   scripts/share_rk3588_buildroot_sources.sh --verify
#   scripts/share_rk3588_buildroot_sources.sh --apply
#   scripts/share_rk3588_buildroot_sources.sh --sync-common
#
# --apply is deliberately non-destructive: the RK3588S original tree is moved
# to a timestamped backup.  Do not remove that backup until normal builds have
# been exercised on both products.

set -euo pipefail
shopt -s nullglob

readonly LINUX_ROOT="${TOPEET_LINUX_ROOT:-/home/topeet/Linux}"
readonly STAMP="${TOPEET_SHARED_BUILDROOT_STAMP:-20260727}"
readonly COMMON_ROOT="$LINUX_ROOT/buildroot-common"

action="${1:---check}"

case "$action" in
	--check|--verify|--apply|--sync-common) ;;
	*)
		echo "usage: $0 [--check|--verify|--apply|--sync-common]" >&2
		exit 2
		;;
esac

declare -a LOCAL_TOPDIRS=(board configs utils archives)
# These entries contain Buildroot source, patches or the shared BoardLab
# package.  They must resolve to the common core for every SDK.  Product
# hardware stays in device/, kernel/, external/, app/, board/ and configs/.
declare -a SHARED_PACKAGE_ENTRIES=(gstreamer1 gptfdisk qt5 rockchip)

sdk_root()
{
	local sku="$1"
	local kernel_line="$2"

	case "$sku:$kernel_line" in
		rk3588:5.10) printf '%s\n' "$LINUX_ROOT/rk3588/rk3588-linux-5.10" ;;
		rk3588s:5.10) printf '%s\n' "$LINUX_ROOT/rk3588s/rk3588s-linux-5.10" ;;
		rk3588:6.1) printf '%s\n' "$LINUX_ROOT/rk3588/rk3588-linux-6.1" ;;
		rk3588s:6.1) printf '%s\n' "$LINUX_ROOT/rk3588s/rk3588s-linux-6.1" ;;
	*)
		echo "unsupported SDK tuple: $sku/$kernel_line" >&2
		return 2
		;;
	esac
}

common_root()
{
	local kernel_line="$1"

	case "$kernel_line" in
	5.10) printf '%s\n' "$COMMON_ROOT/buildroot-2021.11" ;;
	6.1) printf '%s\n' "$COMMON_ROOT/buildroot-2024.02" ;;
	*)
		echo "unsupported kernel line: $kernel_line" >&2
		return 2
		;;
	esac
}

expected_buildroot_version()
{
	case "$1" in
	5.10) printf '%s\n' 2021.11 ;;
	6.1) printf '%s\n' 2024.02 ;;
	*) return 2 ;;
	esac
}

buildroot_version()
{
	awk '/^export BR2_VERSION :=/ { print $NF; exit }' "$1/Makefile"
}

require_tree()
{
	local tree="$1"
	local expected="$2"
	local actual

	[ -d "$tree" ] || {
		echo "missing Buildroot tree: $tree" >&2
		return 1
	}
	[ -r "$tree/Makefile" ] || {
		echo "missing Buildroot Makefile: $tree/Makefile" >&2
		return 1
	}
	actual="$(buildroot_version "$tree")"
	[ "$actual" = "$expected" ] || {
		echo "unexpected Buildroot version in $tree: ${actual:-unknown} (expected $expected)" >&2
		return 1
	}
}

assert_not_building()
{
	local sdk="$1"
	local active

	active="$(pgrep -af "$sdk/buildroot" || true)"
	[ -z "$active" ] || {
		echo "refusing to change a Buildroot tree while it is in use:" >&2
		echo "$active" >&2
		return 1
	}
}

copy_local_tree()
{
	local source="$1"
	local name="$2"
	local destination="$3"

	[ -e "$source/$name" ] || return 0
	cp -a "$source/$name" "$destination/$name"
}

link_common_entries()
{
	local common="$1"
	local wrapper="$2"
	local item name

	for item in "$common"/* "$common"/.[!.]* "$common"/..?*; do
		name="$(basename "$item")"
		case "$name" in
			board|configs|utils|package|archives|dl|output)
				continue
				;;
		esac
		ln -s "$item" "$wrapper/$name"
	done
}

make_package_layer()
{
	local common="$1"
	local local_source="$2"
	local wrapper="$3"
	local item name

	mkdir "$wrapper/package"
	# Keep source and package patches common.  The wrapper remains necessary so
	# TOPDIR stays inside each SDK and ../app / ../external keep resolving to
	# that SDK's own local sources.
	for item in "$common/package"/* "$common/package"/.[!.]* "$common/package"/..?*; do
		name="$(basename "$item")"
		ln -s "$item" "$wrapper/package/$name"
	done

	# Preserve only package entries that exist exclusively in the product SDK.
	# A same-named entry is intentionally shared source code, not a board
	# capability switch.  This keeps BoardLab/GStreamer/Qt identical on
	# RK3588 and RK3588S.
	for item in "$local_source/package"/* "$local_source/package"/.[!.]* "$local_source/package"/..?*; do
		name="$(basename "$item")"
		if [ ! -e "$common/package/$name" ] && [ ! -L "$common/package/$name" ]; then
			cp -a "$item" "$wrapper/package/$name"
		fi
	done
}

sync_common_package_entry()
{
	local sdk="$1"
	local common="$2"
	local name="$3"
	local package_dir="$sdk/buildroot/package"
	local target="$common/package/$name"
	local active="$package_dir/$name"
	local backup="$package_dir/.${name}.pre-shared-${STAMP}"

	[ -e "$target" ] || {
		echo "missing common package entry: $target" >&2
		return 1
	}

	if [ -L "$active" ] && [ "$(readlink -f "$active")" = "$(readlink -f "$target")" ]; then
		return 0
	fi

	if [ -e "$active" ] || [ -L "$active" ]; then
		[ ! -e "$backup" ] && [ ! -L "$backup" ] || {
			echo "refusing to overwrite package backup: $backup" >&2
			return 1
		}
		mv "$active" "$backup"
	fi
	ln -s "$target" "$active"
}

sync_common_packages()
{
	local sdk="$1"
	local common="$2"
	local name

	for name in "${SHARED_PACKAGE_ENTRIES[@]}"; do
		sync_common_package_entry "$sdk" "$common" "$name"
	done
}

verify_shared_packages()
{
	local sdk="$1"
	local common="$2"
	local name active target

	for name in "${SHARED_PACKAGE_ENTRIES[@]}"; do
		active="$sdk/buildroot/package/$name"
		target="$common/package/$name"
		[ -e "$target" ] || {
			echo "missing common package entry: $target" >&2
			return 1
		}
		[ "$(readlink -f "$active")" = "$(readlink -f "$target")" ] || {
			echo "package entry is not shared: $active" >&2
			return 1
		}
	done
}

restore_or_create_output()
{
	local sdk="$1"
	local kernel_line="$2"
	local wrapper="$3"
	local staged="$sdk/.buildroot-output-${kernel_line}-${STAMP}"

	if [ -e "$staged" ]; then
		mv "$staged" "$wrapper/output"
	else
		mkdir "$wrapper/output"
	fi
}

stage_output()
{
	local sdk="$1"
	local kernel_line="$2"
	local source="$3"
	local staged="$sdk/.buildroot-output-${kernel_line}-${STAMP}"

	[ ! -e "$source/output" ] && return 0
	[ ! -e "$staged" ] || {
		echo "staged output already exists: $staged" >&2
		return 1
	}
	mv "$source/output" "$staged"
}

create_wrapper()
{
	local sdk="$1"
	local kernel_line="$2"
	local common="$3"
	local local_source="$4"
	local wrapper="$sdk/buildroot"
	local name

	[ ! -e "$wrapper" ] || {
		echo "wrapper destination already exists: $wrapper" >&2
		return 1
	}
	mkdir "$wrapper"
	link_common_entries "$common" "$wrapper"

	for name in "${LOCAL_TOPDIRS[@]}"; do
		copy_local_tree "$local_source" "$name" "$wrapper"
	done
	make_package_layer "$common" "$local_source" "$wrapper"

	# Downloads are safe to share by release; output is intentionally local.
	ln -s "$common/dl" "$wrapper/dl"
	restore_or_create_output "$sdk" "$kernel_line" "$wrapper"
}

sync_pair()
{
	local kernel_line="$1"
	local standard s common expected

	standard="$(sdk_root rk3588 "$kernel_line")"
	s="$(sdk_root rk3588s "$kernel_line")"
	common="$(common_root "$kernel_line")"
	expected="$(expected_buildroot_version "$kernel_line")"

	assert_not_building "$standard"
	assert_not_building "$s"
	require_tree "$common" "$expected"
	[ -d "$standard/buildroot" ]
	[ -d "$s/buildroot" ]
	sync_common_packages "$standard" "$common"
	sync_common_packages "$s" "$common"
}

print_pair()
{
	local kernel_line="$1"
	local standard s common expected

	standard="$(sdk_root rk3588 "$kernel_line")"
	s="$(sdk_root rk3588s "$kernel_line")"
	common="$(common_root "$kernel_line")"
	expected="$(expected_buildroot_version "$kernel_line")"

	echo "[$kernel_line / Buildroot $expected]"
	echo "  RK3588 : $standard/buildroot"
	echo "  RK3588S: $s/buildroot"
	echo "  common : $common"
	if [ -e "$common" ]; then
		echo "  state  : common core already exists"
	else
		echo "  state  : will promote the RK3588 core to the common baseline"
	fi
}

migrate_pair()
{
	local kernel_line="$1"
	local standard s common expected standard_source s_source s_backup

	standard="$(sdk_root rk3588 "$kernel_line")"
	s="$(sdk_root rk3588s "$kernel_line")"
	common="$(common_root "$kernel_line")"
	expected="$(expected_buildroot_version "$kernel_line")"

	assert_not_building "$standard"
	assert_not_building "$s"
	require_tree "$standard/buildroot" "$expected"
	require_tree "$s/buildroot" "$expected"
	[ ! -e "$common" ] || {
		echo "common core already exists; refusing a second migration: $common" >&2
		return 1
	}

	mkdir -p "$COMMON_ROOT"
	stage_output "$standard" "$kernel_line" "$standard/buildroot"
	mv "$standard/buildroot" "$common"
	standard_source="$common"
	create_wrapper "$standard" "$kernel_line" "$common" "$standard_source"

	s_backup="$s/buildroot.pre-shared-${STAMP}"
	[ ! -e "$s_backup" ] || {
		echo "RK3588S backup already exists: $s_backup" >&2
		return 1
	}
	stage_output "$s" "$kernel_line" "$s/buildroot"
	mv "$s/buildroot" "$s_backup"
	s_source="$s_backup"
	create_wrapper "$s" "$kernel_line" "$common" "$s_source"
}

verify_pair()
{
	local kernel_line="$1"
	local standard s common expected

	standard="$(sdk_root rk3588 "$kernel_line")"
	s="$(sdk_root rk3588s "$kernel_line")"
	common="$(common_root "$kernel_line")"
	expected="$(expected_buildroot_version "$kernel_line")"

	require_tree "$common" "$expected"
	[ -d "$standard/buildroot" ]
	[ -d "$s/buildroot" ]
	[ ! -L "$standard/buildroot" ]
	[ ! -L "$s/buildroot" ]
	[ "$(readlink -f "$standard/buildroot/arch")" = "$common/arch" ]
	[ "$(readlink -f "$s/buildroot/arch")" = "$common/arch" ]
	[ ! -L "$standard/buildroot/board" ]
	[ ! -L "$s/buildroot/board" ]
	[ ! -L "$standard/buildroot/configs" ]
	[ ! -L "$s/buildroot/configs" ]
	[ ! -L "$standard/buildroot/package" ]
	[ ! -L "$s/buildroot/package" ]
	[ ! -L "$standard/buildroot/utils" ]
	[ ! -L "$s/buildroot/utils" ]
	[ ! -L "$standard/buildroot/archives" ]
	[ ! -L "$s/buildroot/archives" ]
	[ ! -L "$standard/buildroot/output" ]
	[ ! -L "$s/buildroot/output" ]
	verify_shared_packages "$standard" "$common"
	verify_shared_packages "$s" "$common"
	echo "verified shared $kernel_line core with local board/configs/output layers and a package wrapper"
}

case "$action" in
	--check)
		print_pair 5.10
		print_pair 6.1
		;;
	--verify)
		verify_pair 5.10
		verify_pair 6.1
		;;
	--apply)
		migrate_pair 5.10
		migrate_pair 6.1
		sync_pair 5.10
		sync_pair 6.1
		verify_pair 5.10
		verify_pair 6.1
		;;
	--sync-common)
		sync_pair 5.10
		sync_pair 6.1
		verify_pair 5.10
		verify_pair 6.1
		;;
esac
