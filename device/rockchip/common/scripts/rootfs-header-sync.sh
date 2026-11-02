#!/bin/bash -e

rootfs_header_resolve_path()
{
	local name="$1" value="$2" resolved

	if [ -z "$value" ]; then
		echo "ERROR: $name is empty." >&2
		return 1
	fi
	resolved="$(realpath -m -- "$value")" || {
		echo "ERROR: Unable to resolve $name: $value" >&2
		return 1
	}
	if [ "$resolved" = / ]; then
		echo "ERROR: Refusing unsafe root path for $name." >&2
		return 1
	fi
	printf '%s\n' "$resolved"
}

rootfs_header_validate_paths()
{
	RK_ROOTFS_HEADER_KERNEL_DIR="$(rootfs_header_resolve_path \
		RK_ROOTFS_HEADER_KERNEL_DIR "${RK_ROOTFS_HEADER_KERNEL_DIR:-}")" || return 1
	RK_ROOTFS_HEADER_OVERLAY_DIR="$(rootfs_header_resolve_path \
		RK_ROOTFS_HEADER_OVERLAY_DIR "${RK_ROOTFS_HEADER_OVERLAY_DIR:-}")" || return 1
	RK_ROOTFS_HEADER_OUTDIR="$(rootfs_header_resolve_path \
		RK_ROOTFS_HEADER_OUTDIR "${RK_ROOTFS_HEADER_OUTDIR:-}")" || return 1
	RK_ROOTFS_HEADER_BUILD_SH="$(rootfs_header_resolve_path \
		RK_ROOTFS_HEADER_BUILD_SH "${RK_ROOTFS_HEADER_BUILD_SH:-}")" || return 1

	[ -d "$RK_ROOTFS_HEADER_KERNEL_DIR" ] || {
		echo "ERROR: Kernel directory not found: $RK_ROOTFS_HEADER_KERNEL_DIR" >&2
		return 1
	}
	[ -x "$RK_ROOTFS_HEADER_BUILD_SH" ] || {
		echo "ERROR: Kernel build script is not executable: $RK_ROOTFS_HEADER_BUILD_SH" >&2
		return 1
	}
}

rootfs_header_kernelrelease()
{
	rootfs_header_ensure_kernel_ready
	make -s -C "$RK_ROOTFS_HEADER_KERNEL_DIR" --no-print-directory kernelrelease
}

rootfs_header_kernel_ready()
{
	[ -r "$RK_ROOTFS_HEADER_KERNEL_DIR/.config" ] || return 1
	[ -r "$RK_ROOTFS_HEADER_KERNEL_DIR/include/config/auto.conf" ] || return 1
	[ -r "$RK_ROOTFS_HEADER_KERNEL_DIR/include/generated/autoconf.h" ] || return 1
}

rootfs_header_ensure_kernel_ready()
{
	local sdk_dir

	if rootfs_header_kernel_ready; then
		echo "Kernel has been built, skip kernel rebuild." >&2
		return 0
	fi

	echo "Kernel has not been built or prepared. Building kernel before rootfs header sync..." >&2
	sdk_dir="$(cd "$(dirname "$RK_ROOTFS_HEADER_BUILD_SH")/../../../.." && pwd)"
	(cd "$sdk_dir" && "$RK_ROOTFS_HEADER_BUILD_SH" kernel) >&2

	if ! rootfs_header_kernel_ready; then
		echo "ERROR: Kernel is still not prepared after kernel build." >&2
		return 1
	fi
}

rootfs_header_build_arch()
{
	case "${RK_ROOTFS_HEADER_BUILD_ARCH:-${RK_KERNEL_ARCH:-arm64}}" in
		arm64|aarch64) echo aarch64 ;;
		arm|armhf) echo armhf ;;
		*) echo aarch64 ;;
	esac
}

rootfs_header_dir()
{
	echo "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/src/linux-headers-$1"
}

rootfs_header_overlay_valid()
{
	local kernelrelease="$1"
	local headers_dir release

	headers_dir="$(rootfs_header_dir "$kernelrelease")"
	[ -r "$headers_dir/include/config/kernel.release" ] || return 1
	[ -s "$headers_dir/Module.symvers" ] || return 1
	[ -r "$headers_dir/Makefile" ] || return 1

	release="$(tr -d '\n' < "$headers_dir/include/config/kernel.release")"
	[ "$release" = "$kernelrelease" ] || return 1
	[ -L "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/lib/modules/$kernelrelease/build" ] || return 1
	[ -L "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/lib/modules/$kernelrelease/source" ] || return 1
}

rootfs_header_refresh()
{
	local kernelrelease="$1"
	local build_arch headers_tar headers_dir modules_dir

	build_arch="$(rootfs_header_build_arch)"
	headers_tar="$RK_ROOTFS_HEADER_OUTDIR/linux-headers/linux-headers-$build_arch.tar.gz"
	headers_dir="$(rootfs_header_dir "$kernelrelease")"
	modules_dir="$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/lib/modules/$kernelrelease"

	echo "Syncing kernel headers for $kernelrelease..."
	mkdir -p "$RK_ROOTFS_HEADER_OUTDIR/linux-headers"
	rm -f "$RK_ROOTFS_HEADER_OUTDIR/linux-headers/linux-headers-$build_arch.tar" \
		"$headers_tar" 2>/dev/null || true
	(cd "$(cd "$(dirname "$RK_ROOTFS_HEADER_BUILD_SH")/../../../.." && pwd)" && \
		"$RK_ROOTFS_HEADER_BUILD_SH" linux-headers "$build_arch")
	[ -r "$headers_tar" ] || return 1

	sudo mkdir -p "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/src"
	sudo find "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/src" -mindepth 1 -maxdepth 1 \
		-type d -name 'linux-headers-*' -exec rm -rf {} +
	sudo find "$RK_ROOTFS_HEADER_OVERLAY_DIR/lib/modules" -mindepth 1 -maxdepth 1 \
		-type d -exec rm -rf {} + 2>/dev/null || true
	sudo rmdir "$RK_ROOTFS_HEADER_OVERLAY_DIR/lib/modules" "$RK_ROOTFS_HEADER_OVERLAY_DIR/lib" \
		2>/dev/null || true
	sudo find "$RK_ROOTFS_HEADER_OVERLAY_DIR/usr/lib/modules" -mindepth 1 -maxdepth 1 \
		-type d -exec rm -rf {} + 2>/dev/null || true

	sudo mkdir -p "$headers_dir" "$modules_dir"
	sudo tar -xpf "$headers_tar" -C "$headers_dir" --sparse
	sudo ln -snf "/usr/src/linux-headers-$kernelrelease" "$modules_dir/build"
	sudo ln -snf "/usr/src/linux-headers-$kernelrelease" "$modules_dir/source"
}

rootfs_header_sync()
{
	local kernelrelease

	rootfs_header_validate_paths || return 1
	kernelrelease="$(rootfs_header_kernelrelease)"
	if rootfs_header_overlay_valid "$kernelrelease"; then
		return 0
	fi

	rootfs_header_refresh "$kernelrelease"
	rootfs_header_overlay_valid "$kernelrelease"
}
