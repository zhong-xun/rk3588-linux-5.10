#!/bin/bash -e

# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-INIT
# Capture both paths before the vendor script changes its working directory.
RKNETUPDATE_POST_BUILD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RKNETUPDATE_SDK_DIR="$(cd "$RKNETUPDATE_POST_BUILD_DIR/.." && pwd)"
RKNETUPDATE_TARGET_DIR="$(realpath "${1:?target rootfs is required}")"
RKNETUPDATE_ROOTFS_HOOK="$RKNETUPDATE_SDK_DIR/device/rockchip/common/scripts/post-rknetupdate-rootfs.sh"

TARGET_DIR=$1
shift

RK_LEGACY_PARTITIONS=" \
	${RK_OEM_FS_TYPE:+oem:/oem:${RK_OEM_FS_TYPE}}
	${RK_USERDATA_FS_TYPE:+userdata:/userdata:${RK_USERDATA_FS_TYPE}}
"

# <dev>:<mount point>:<fs type>:<source dir>, for example:
# RK_EXTRA_PARTITIONS="oem:/oem:ext2:oem_normal userdata:/userdata:vfat:userdata_empty"
RK_EXTRA_PARTITIONS=${RK_EXTRA_PARTITIONS:-${RK_LEGACY_PARTITIONS}}

function fixup_root()
{
	echo "Fixing up rootfs type: $1"

	FS_TYPE=$1
	sed -i "s#\([[:space:]]/[[:space:]]\+\)\w\+#\1${FS_TYPE}#" \
		${TARGET_DIR}/etc/fstab
}

function fixup_part()
{
	echo "Fixing up partition: $@"

	if echo $1 | grep -qE "^/"; then
		DEV=$1
	else
		DEV="PARTLABEL=$1"
	fi

	MOUNT=${2:-/$1}
	FS_TYPE=${3:-auto}
	OPT=${4:-defaults}

	sed -i "/[[:space:]]${MOUNT//\//\\\/}[[:space:]]/d" ${TARGET_DIR}/etc/fstab

	echo -e "${DEV}\t${MOUNT}\t${FS_TYPE}\t${OPT}\t0 2" >> \
		${TARGET_DIR}/etc/fstab

	mkdir -p ${TARGET_DIR}/${MOUNT} 
}

function fixup_fstab()
{
	echo "Fixing up /etc/fstab..."

	case "${RK_ROOTFS_TYPE}" in
		ext[234])
			fixup_root ${RK_ROOTFS_TYPE}
			;;
		*)
			fixup_root auto
			;;
	esac

	for part in ${RK_EXTRA_PARTITIONS}; do
		fixup_part $(echo "${part}" | xargs -d':')
	done
}

function add_build_info()
{
	[ -f ${TARGET_DIR}/etc/os-release ] && \
		sed -i "/^BUILD_ID=/d" ${TARGET_DIR}/etc/os-release

	echo "Adding build-info to /etc/os-release..."
	echo "BUILD_INFO=\"$(whoami)@$(hostname) $(date)${@:+ - $@}\"" >> \
		${TARGET_DIR}/etc/os-release
}

function add_dirs_and_links()
{
	echo "Adding dirs and links..."

	cd ${TARGET_DIR}
	mkdir -p mnt/sdcard mnt/usb0
	ln -sf media/usb0 udisk
	ln -sf mnt/sdcard sdcard
	ln -sf userdata data
}

echo "Executing $(basename $0)..."

add_build_info $@
[ -f ${TARGET_DIR}/etc/fstab ] && fixup_fstab
add_dirs_and_links



# RKNETUPDATE-MANAGED-FILESYSTEM-POST-BUILD-V2-CALL
if [ ! -x "$RKNETUPDATE_ROOTFS_HOOK" ]; then
  echo "RKNetUpdate: missing rootfs hook: $RKNETUPDATE_ROOTFS_HOOK" >&2
  exit 1
fi
RKNETUPDATE_STANDALONE=1 "$RKNETUPDATE_ROOTFS_HOOK" "$RKNETUPDATE_TARGET_DIR" unknown
exit 0
