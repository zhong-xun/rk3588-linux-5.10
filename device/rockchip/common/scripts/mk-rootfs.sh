#!/bin/bash -e

build_buildroot()
{
	check_config RK_BUILDROOT || false

	IMAGE_DIR="${1:-$RK_OUTDIR/buildroot}"

	BUILDROOT_VERSION=$(grep "export BR2_VERSION := " \
		"$RK_SDK_DIR/buildroot/Makefile" | xargs -n 1 | tail -n 1)

	message "=========================================="
	message "          Start building buildroot($BUILDROOT_VERSION)"
	message "=========================================="

	"$RK_SCRIPTS_DIR/mk-buildroot.sh" $RK_BUILDROOT_CFG "$IMAGE_DIR"

	if [ -r "$RK_LOG_DIR/post-rootfs.log" ]; then
		cat "$RK_LOG_DIR/post-rootfs.log"
	else
		warning "Building without post-rootfs stage!"
	fi

	if [ "$RK_SECURITY" ]; then
		if [ "$RK_ROOTFS_TYPE" == "ubi" ]; then
			# UBIFS DOES NOT support R/W block device,
			# so it only support RO encrypted image.
			SUFFIX=squashfs
		else
			SUFFIX=$RK_ROOTFS_TYPE
		fi

		"$RK_SCRIPTS_DIR/mk-security.sh" system \
			$RK_SECURITY_CHECK_METHOD $IMAGE_DIR/rootfs.$SUFFIX

		if [ "$RK_ROOTFS_TYPE" == "ubi" ]; then
			# Force to using dynamic to faster bootup.
			sed -e "s;BR2_ROOTFS_UBI_PATH;${IMAGE_DIR}/security_system.img;" \
			    -e "s;UBI_VOL_TYPE;dynamic;" \
			    ${RK_SDK_DIR}/buildroot/fs/ubi/ubinize.cfg > \
				${RK_OUTDIR}/security/ubinize.cfg

			# Repack ubifs, source variables from buildroot configuration
			(
				eval $(cat "$RK_SDK_DIR/buildroot/output/$RK_BUILDROOT_CFG/.config" | \
				       grep ^BR2_TARGET_ROOTFS_UBI_)
				ubinize -o ${RK_OUTDIR}/security/security-system.ubi \
					-m ${BR2_TARGET_ROOTFS_UBI_MINIOSIZE} \
					-p ${BR2_TARGET_ROOTFS_UBI_PEBSIZE} \
					-s ${BR2_TARGET_ROOTFS_UBI_SUBSIZE} \
					${RK_OUTDIR}/security/ubinize.cfg
			)
		fi
	fi

	finish_build build_buildroot $@
}

topeet_kernel_version_from_tree()
{
	[ -r "$RK_SDK_DIR/kernel/Makefile" ] || return 0

	awk '
		$1 == "VERSION" { version = $3 }
		$1 == "PATCHLEVEL" { patchlevel = $3 }
		END {
			if (version != "" && patchlevel != "")
				printf "%s.%s\n", version, patchlevel
		}
	' "$RK_SDK_DIR/kernel/Makefile"
}

topeet_rootfs_systems()
{
	case "$(topeet_kernel_version_from_tree)" in
		6.1*) echo "buildroot debian12" ;;
		*) echo "buildroot debian11 debian12 ubuntu20 ubuntu22" ;;
	esac
}

topeet_rootfs_supported()
{
	local rootfs="$1"
	local supported

	for supported in $(topeet_rootfs_systems); do
		[ "$rootfs" = "$supported" ] && return 0
	done

	return 1
}

topeet_build_external_rootfs()
{
	local rootfs="$1"
	local image_dir="$2"
	local source_dir

	if [ "$RK_ROOTFS_TYPE" != "ext4" ]; then
		error "$rootfs rootfs only supports ext4, current type is $RK_ROOTFS_TYPE"
		return 1
	fi

	case "$rootfs" in
		debian*) source_dir="$RK_SDK_DIR/debian" ;;
		ubuntu*) source_dir="$RK_SDK_DIR/ubuntu" ;;
		*) error "Unsupported external rootfs: $rootfs"; return 1 ;;
	esac

	if [ ! -x "$source_dir/mk-rootfs.sh" ] || [ ! -x "$source_dir/mk-image.sh" ]; then
		error "Missing rootfs scripts under $source_dir"
		return 1
	fi

	(
		cd "$source_dir"
		VERSION="$rootfs" ./mk-rootfs.sh
		./mk-image.sh
	)

	if [ ! -r "$source_dir/rootfs.img" ]; then
		error "$source_dir/rootfs.img was not generated"
		return 1
	fi

	ln -rsf "$source_dir/rootfs.img" "$image_dir/rootfs.ext4"
}

# Hooks

usage_hook()
{
	usage_oneline "buildroot-config[:<config>]" "modify buildroot defconfig"
	usage_oneline "bconfig[:<config>]" "alias of buildroot-config"
	usage_oneline "buildroot-make[:<arg1>:<arg2>]" "run buildroot make"
	usage_oneline "bmake[:<arg1>:<arg2>]" "alias of buildroot-make"
	usage_oneline "buildroot-sdk" "build the buildroot SDK tarball"
	usage_oneline "bsdk" "alias of buildroot-sdk"
	usage_oneline "rootfs[:<rootfs type>]" "build default rootfs"
	for rootfs in $(topeet_rootfs_systems); do
		usage_oneline "$rootfs" "build $rootfs rootfs"
	done
}

clean_hook()
{
	for dir in debian ubuntu; do
		[ ! -e "$RK_SDK_DIR/$dir/binary" ] || \
			sudo rm -rf "$RK_SDK_DIR/$dir/binary"
		rm -f "$RK_SDK_DIR/$dir/rootfs.img"
		[ ! -e "$RK_SDK_DIR/$dir/overlay_header" ] || \
			sudo rm -rf "$RK_SDK_DIR/$dir/overlay_header"
	done

	if check_config RK_BUILDROOT &>/dev/null; then
		rm -rf "$RK_SDK_DIR/buildroot/output/$RK_BUILDROOT_CFG"
		[ -z "$RK_DEFCONFIG" ] || \
			rm -rf "$RK_SDK_DIR/buildroot/output/${RK_DEFCONFIG%_defconfig}/$RK_BUILDROOT_CFG"
	fi

	rm -rf "$RK_OUTDIR/buildroot"
	rm -rf "$RK_OUTDIR"/debian*
	rm -rf "$RK_OUTDIR"/ubuntu*
	rm -rf "$RK_OUTDIR/rootfs"
	rm -rf "$RK_FIRMWARE_DIR/rootfs.img"
}

TOPEET_ROOTFS_CMDS="$(topeet_rootfs_systems)"
INIT_CMDS="default $TOPEET_ROOTFS_CMDS"
init_hook()
{
	load_config RK_ROOTFS
	check_config RK_ROOTFS &>/dev/null || return 0
	[ -z "$DRY_RUN" ] || return 0

	# Priority: cmdline > custom env
	if [ "$1" != default ]; then
		export RK_ROOTFS_SYSTEM=$1
		notice "Using rootfs system($RK_ROOTFS_SYSTEM) from cmdline"
	elif [ "$RK_ROOTFS_SYSTEM" ]; then
		export RK_ROOTFS_SYSTEM=${RK_ROOTFS_SYSTEM//\"/}
		notice "Using rootfs system($RK_ROOTFS_SYSTEM) from environment"
	else
		return 0
	fi

	ROOTFS_CONFIG="RK_ROOTFS_SYSTEM=\"$RK_ROOTFS_SYSTEM\""
	ROOTFS_UPPER=$(echo $RK_ROOTFS_SYSTEM | tr 'a-z' 'A-Z')
	ROOTFS_CHOICE="RK_ROOTFS_SYSTEM_$ROOTFS_UPPER"
	if ! grep -q "^$ROOTFS_CONFIG$" "$RK_CONFIG"; then
		if ! topeet_rootfs_supported "$RK_ROOTFS_SYSTEM"; then
			error "$RK_ROOTFS_SYSTEM not supported!"
			return 1
		fi

		sed -i -e "/RK_ROOTFS_SYSTEM/d" "$RK_CONFIG"
		echo "$ROOTFS_CONFIG" >> "$RK_CONFIG"
		echo "$ROOTFS_CHOICE=y" >> "$RK_CONFIG"
		"$RK_SCRIPTS_DIR/mk-config.sh" olddefconfig &>/dev/null
	fi
}

PRE_BUILD_CMDS="buildroot-config bconfig buildroot-make bmake"
pre_build_hook()
{
	check_config RK_ROOTFS || false

	case "$1" in
		buildroot-make | bmake)
			check_config RK_BUILDROOT || false

			shift
			"$RK_SCRIPTS_DIR/mk-buildroot.sh" \
				$RK_BUILDROOT_CFG make $@
			finish_build buildroot-make $@
			;;
		buildroot-config | bconfig)
			BUILDROOT_BOARD="${2:-"$RK_BUILDROOT_CFG"}"
			[ "$BUILDROOT_BOARD" ] || return 0

			TEMP_DIR=$(mktemp -d)
			unset BUILDROOT_OUTPUT_DIR
			make -C "$RK_SDK_DIR/buildroot" O="$TEMP_DIR" \
				"${BUILDROOT_BOARD}_defconfig" menuconfig
			"$RK_SDK_DIR/buildroot/scripts/update_defconfig.sh" \
				"$BUILDROOT_BOARD" "$TEMP_DIR"
			rm -rf "$TEMP_DIR"

			finish_build $@
			;;
	esac
}

BUILD_CMDS="rootfs $TOPEET_ROOTFS_CMDS"
build_hook_dry()
{
	local rootfs="${1:-rootfs}"

	[ "$rootfs" = rootfs ] && rootfs="${RK_ROOTFS_SYSTEM:-buildroot}"
	notice "DRY-RUN: would build rootfs($rootfs)"
}

build_hook()
{
	check_config RK_ROOTFS || false

	if [ -z "$1" -o "$1" = rootfs ]; then
		ROOTFS=${RK_ROOTFS_SYSTEM:-buildroot}
	else
		ROOTFS=$1
	fi

	ROOTFS_IMG=rootfs.${RK_ROOTFS_TYPE}
	ROOTFS_DIR="$RK_OUTDIR/$ROOTFS"
	IMAGE_DIR="$ROOTFS_DIR/images"

	message "=========================================="
	message "          Start building rootfs($ROOTFS)"
	message "=========================================="
	message "Defconfig: ${RK_DEFCONFIG:-unknown}"
	message "Rootfs type: ${RK_ROOTFS_TYPE:-unknown}, image: $ROOTFS_IMG"
	message "Rootfs output: $ROOTFS_DIR"
	message "Firmware rootfs link: $RK_FIRMWARE_DIR/rootfs.img"

	if ! topeet_rootfs_supported "$ROOTFS"; then
		error "$ROOTFS not supported for kernel $(topeet_kernel_version_from_tree)"
		usage
	fi

	rm -rf "$ROOTFS_DIR" "$RK_OUTDIR/rootfs"
	mkdir -p "$IMAGE_DIR"
	ln -rsf "$ROOTFS_DIR" "$RK_OUTDIR/rootfs"

	touch "$ROOTFS_DIR/.stamp_build_start"
	case "$ROOTFS" in
		buildroot) build_buildroot "$IMAGE_DIR" ;;
		*) topeet_build_external_rootfs "$ROOTFS" "$IMAGE_DIR" ;;
	esac
	touch "$ROOTFS_DIR/.stamp_build_finish"

	if [ ! -f "$IMAGE_DIR/$ROOTFS_IMG" ]; then
		error "There's no $ROOTFS_IMG generated..."
		exit 1
	fi

	if [ "$RK_ROOTFS_INITRD" ]; then
		"$RK_SCRIPTS_DIR/mk-ramboot.sh" "$ROOTFS_DIR" \
			"$IMAGE_DIR/$ROOTFS_IMG" "$RK_BOOT_FIT_ITS"
		ln -rsf "$ROOTFS_DIR/ramboot.img" "$RK_FIRMWARE_DIR/boot.img"
	elif [ "$RK_SECURITY_CHECK_SYSTEM_ENCRYPTION" -o \
		"$RK_SECURITY_CHECK_SYSTEM_VERITY" ]; then
		if [ "$RK_ROOTFS_TYPE" == "ubi" ]; then
			ln -rsf "${RK_OUTDIR}/security/security-system.ubi" \
				"$RK_FIRMWARE_DIR/rootfs.img"
		else
			ln -rsf "$IMAGE_DIR/security_system.img" \
				"$RK_FIRMWARE_DIR/rootfs.img"
		fi



	else
		ln -rsf "$IMAGE_DIR/$ROOTFS_IMG" "$RK_FIRMWARE_DIR/rootfs.img"
	fi

	rm -rf "$RK_OUTDIR/update"
	rm -f "$RK_FIRMWARE_DIR/update.img" "$RK_FIRMWARE_DIR/update-ab.img" \
		"$RK_FIRMWARE_DIR/ota.img" "$RK_FIRMWARE_DIR/ota-ab.img"
	message "Removed stale update images; run ./build.sh updateimg after rootfs changes."

	finish_build build_rootfs $@
}

POST_BUILD_CMDS="buildroot-sdk bsdk"
post_build_hook()
{
	check_config RK_ROOTFS || false

	build_hook buildroot
	pre_build_hook bmake sdk

	BUILDROOT_SDK_TARBALL="$RK_OUTDIR/buildroot/buildroot-sdk.tar.gz"
	ln -rsf "$RK_OUTDIR/buildroot/images/"*sdk-buildroot.tar.gz \
		"$BUILDROOT_SDK_TARBALL"

	message "Generated buildroot SDK tarball at: $BUILDROOT_SDK_TARBALL"

	finish_build $@
}

source "${RK_BUILD_HELPER:-$(dirname "$(realpath "$0")")/build-helper}"

case "${1:-rootfs}" in
	buildroot-config | bconfig | buildroot-make | bmake) pre_build_hook $@ ;;
	buildroot-sdk | bsdk) post_build_hook $@ ;;
	buildroot | debian11 | debian12 | ubuntu20 | ubuntu22) init_hook $@ ;&
	*) build_hook $@ ;;
esac
