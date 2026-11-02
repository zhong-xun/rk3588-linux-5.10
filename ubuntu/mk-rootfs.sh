#!/bin/bash -e
chroot_dir=binary  
ROOTFS_SOC="rk3588"
SDK_DIR="$(cd "$(dirname "$(realpath "$0")")/.." && pwd)"
ROOTFS_DIR="$(dirname "$(realpath "$0")")"
RK_ROOTFS_HEADER_KERNEL_DIR="$SDK_DIR/kernel"
RK_ROOTFS_HEADER_OVERLAY_DIR="$ROOTFS_DIR/overlay_header"
RK_ROOTFS_HEADER_OUTDIR="$SDK_DIR/output"
RK_ROOTFS_HEADER_BUILD_SH="$SDK_DIR/device/rockchip/common/scripts/mk-kernel.sh"
RK_ROOTFS_HEADER_BUILD_ARCH="${RK_KERNEL_ARCH:-arm64}"
case "$RK_ROOTFS_HEADER_BUILD_ARCH" in
	arm64|aarch64) RK_ROOTFS_HEADER_BUILD_ARCH=aarch64 ;;
	arm|armhf) RK_ROOTFS_HEADER_BUILD_ARCH=armhf ;;
	*) RK_ROOTFS_HEADER_BUILD_ARCH=aarch64 ;;
esac

source "$SDK_DIR/device/rockchip/common/scripts/rootfs-header-sync.sh"

extract_rootfs()
{
	for archive in "$@"; do
		if [ -r "$archive" ]; then
			echo "Using rootfs archive: $archive"
			sudo tar -xpf "$archive" --sparse
			return 0
		fi
	done

	echo "Unsupported or missing ubuntu archive for $VERSION"
	echo "Searched directory: $(pwd)"
	echo "Expected archives:"
	printf "  %s\n" "$@"
	echo "Available tar.xz archives:"
	find . -maxdepth 1 -type f -name "*.tar.xz" -printf "  %f\n" | sort || true
	return 1
}

cleanup_mounts()
{
	for mount_dir in dev/pts dev sys proc; do
		if mountpoint -q "$chroot_dir/$mount_dir"; then
			sudo umount -lf "$chroot_dir/$mount_dir" 2> /dev/null || true
		fi
	done
}

VERSION="${VERSION:-ubuntu20}"

echo -e "\033[36m Building for $VERSION\033[0m"

if [ -d "binary" ]; then
  sudo rm -rf binary
  echo "Deleted binary directory."
fi

echo -e "\033[36m Extract image \033[0m"      
case $VERSION in
    "ubuntu20")
        extract_rootfs \
            "ubuntu-focal-${ROOTFS_SOC}-xfce-arm64.tar.xz" \
            "ubuntu-focal-${ROOTFS_SOC}-arm64.tar.xz"
        ;;
    "ubuntu22")
        extract_rootfs \
            "ubuntu-jammy-${ROOTFS_SOC}-xfce-arm64.tar.xz"
        ;;
    *)
        echo "Unsupported version."
        exit 1
        ;;
esac

rootfs_header_sync

sudo cp -a overlay/. "$chroot_dir"/
if [ -d "overlay_header" ] && [ "$(ls -A overlay_header)" ]; then
    sudo cp -a overlay_header/. "$chroot_dir"/
fi

"$SDK_DIR/device/rockchip/common/scripts/rootfs-wifibt-sync.sh" "$chroot_dir"

trap cleanup_mounts EXIT
sudo mount -t proc /proc ${chroot_dir}/proc
sudo mount -t sysfs /sys ${chroot_dir}/sys
sudo mount -o bind /dev ${chroot_dir}/dev
sudo mount -o bind /dev/pts ${chroot_dir}/dev/pts

sudo chroot "${chroot_dir}" /bin/bash -c "apt update"

cleanup_mounts
trap - EXIT
