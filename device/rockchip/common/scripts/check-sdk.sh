#!/bin/bash -e

RK_SCRIPTS_DIR="${RK_SCRIPTS_DIR:-$(dirname "$(realpath "$0")")}"
RK_SDK_DIR="${RK_SDK_DIR:-$RK_SCRIPTS_DIR/../../../..}"
RK_OWNER="${RK_OWNER:-$(stat --format %U "$RK_SDK_DIR")}"
RK_OWNER_UID="${RK_OWNER_UID:-$(stat --format %u "$RK_SDK_DIR")}"

if [ "$(id -u)" -ne 0 ] && [ "$RK_OWNER_UID" -ne "$(id -u)" ]; then
	echo -e "\e[35m"
	echo "ERROR: Current user is not the owner of SDK source!"
	echo "Please change owner of SDK code:"
	echo "sudo chown -h -R $(id -un):$(id -un) $RK_SDK_DIR/"
	if ! [ "$RK_OWNER" = UNKNOWN ]; then
		echo "Or switch to user($RK_OWNER):"
		echo "su - $RK_OWNER"
	fi
	echo -e "\e[0m"
	exit 1
fi

case "$(findmnt -fnu -o FSTYPE -T "$RK_SCRIPTS_DIR")" in
	ext* | f2fs | btrfs) ;;
	*)
		echo -e "\e[35m"
		echo "Please move SDK source code into an ext4 partition."
		echo -e "\e[0m"
		exit 1
		;;
esac

# Host packages are prepared by the Topeet preflight before build stages.
# Keep this entry point for the SDK ownership/filesystem checks above.
exit 0
