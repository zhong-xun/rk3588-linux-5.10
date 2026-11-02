#!/usr/bin/env bash
#
# Topeet-managed host dependency preparation for the Rockchip SDK.
#
# This file is intentionally safe to source from build.sh.  It does not enable
# shell options. Automatic preparation is best effort on Ubuntu 18/20/22/24;
# offline/other hosts and preparation failures continue to the actual build.

topeet_host_init()
{
	if [ -z "${RK_SCRIPTS_DIR:-}" ]; then
		RK_SCRIPTS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
	fi
	if [ -z "${RK_SDK_DIR:-}" ]; then
		RK_SDK_DIR="$(cd -- "$RK_SCRIPTS_DIR/../../../.." && pwd -P)"
	fi
	if [ -z "${TOPEET_HOST_REPORTED_ADVISORIES_INITIALIZED:-}" ]; then
		TOPEET_HOST_REPORTED_ADVISORIES=()
		export TOPEET_HOST_REPORTED_ADVISORIES_INITIALIZED=1
	fi
	export TOPEET_HOST_DEPS_MANAGED=1
}

topeet_host_os_release_value()
{
	local key=$1 file=$2 current value

	# Keep OS detection bootstrappable: awk is installed by the base stage, so
	# this parser deliberately uses only bash builtins.
	while IFS='=' read -r current value; do
		[ "$current" = "$key" ] || continue
		case "$value" in
			\"*\") value=${value#\"}; value=${value%\"} ;;
			\'*) value=${value#\'}; value=${value%\'} ;;
		esac
		printf '%s\n' "$value"
		return 0
	done < "$file"
}

topeet_host_detect_os()
{
	local file=${TOPEET_HOST_OS_RELEASE:-/etc/os-release}
	local id=unknown id_like= version=unknown codename=unknown
	local family=unknown package_manager=unsupported profile=unsupported
	local supported=0
	local runtime=native kernel_release=

	if [ -r "$file" ]; then
		id=$(topeet_host_os_release_value ID "$file")
		id_like=$(topeet_host_os_release_value ID_LIKE "$file")
		version=$(topeet_host_os_release_value VERSION_ID "$file")
		codename=$(topeet_host_os_release_value VERSION_CODENAME "$file")
		[ -n "$codename" ] || \
			codename=$(topeet_host_os_release_value UBUNTU_CODENAME "$file")
	fi
	TOPEET_HOST_OS_ID=${id:-unknown}
	TOPEET_HOST_OS_VERSION=${version:-unknown}
	TOPEET_HOST_OS_CODENAME=${codename:-unknown}
	case " $TOPEET_HOST_OS_ID $id_like " in
		*" debian "* | *" ubuntu "*)
			family=debian
			package_manager=apt
			;;
	esac
	TOPEET_HOST_OS_FAMILY=$family
	TOPEET_HOST_PACKAGE_MANAGER=$package_manager
	case "$TOPEET_HOST_OS_ID:$TOPEET_HOST_OS_VERSION" in
		ubuntu:18.04 | ubuntu:20.04 | ubuntu:22.04 | ubuntu:24.04)
			profile=ubuntu-lts-supported
			supported=1
			;;
		ubuntu:*) profile=ubuntu-best-effort ;;
		debian:*) profile=debian-compatible ;;
		*) ;;
	esac
	TOPEET_HOST_OS_PROFILE=$profile
	TOPEET_HOST_OS_SUPPORTED=$supported
	if [ -r /proc/sys/kernel/osrelease ]; then
		IFS= read -r kernel_release < /proc/sys/kernel/osrelease || true
	fi
	case "${kernel_release,,}" in
		*microsoft* | *wsl*) runtime=wsl2 ;;
		*) [ -e /.dockerenv ] && runtime=container ;;
	esac
	TOPEET_HOST_RUNTIME=$runtime
	export TOPEET_HOST_OS_ID TOPEET_HOST_OS_VERSION
	export TOPEET_HOST_OS_CODENAME TOPEET_HOST_OS_FAMILY
	export TOPEET_HOST_PACKAGE_MANAGER TOPEET_HOST_OS_PROFILE TOPEET_HOST_RUNTIME
	export TOPEET_HOST_OS_SUPPORTED

	if [ "${TOPEET_HOST_OS_REPORTED:-0}" != 1 ]; then
		topeet_host_note "stage=host-detect id=$TOPEET_HOST_OS_ID version=$TOPEET_HOST_OS_VERSION codename=$TOPEET_HOST_OS_CODENAME arch=$(uname -m) runtime=$TOPEET_HOST_RUNTIME package_manager=$TOPEET_HOST_PACKAGE_MANAGER profile=$TOPEET_HOST_OS_PROFILE supported=$TOPEET_HOST_OS_SUPPORTED"
		export TOPEET_HOST_OS_REPORTED=1
	fi
}

topeet_host_sanitize_build_environment()
{
	local entry cleaned path_changed=0
	local -a entries cleaned_entries

	[ "${TOPEET_HOST_ENV_SANITIZED:-0}" = 1 ] && return 0
	case ":${PATH:-}:" in
		*::* | *:.:*) path_changed=1 ;;
	esac
	IFS=: read -r -a entries <<< "${PATH:-}"
	for entry in "${entries[@]}"; do
		if [ -z "$entry" ] || [ "$entry" = . ]; then
			path_changed=1
			continue
		fi
		case "$entry" in
			*[[:space:]]*) path_changed=1; continue ;;
		esac
		cleaned_entries+=("$entry")
	done
	if [ "$path_changed" = 1 ] || [ "${#cleaned_entries[@]}" -eq 0 ]; then
		if [ "${#cleaned_entries[@]}" -eq 0 ]; then
			cleaned=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
		else
			cleaned=$(IFS=:; echo "${cleaned_entries[*]}")
		fi
		PATH=$cleaned
		export PATH
		topeet_host_note 'environment=PATH-sanitized removed-empty-dot-or-whitespace-entry'
	fi

	if [ -n "${LD_LIBRARY_PATH:-}" ]; then
		case ":$LD_LIBRARY_PATH:" in
			*::* | *:.:*)
				unset LD_LIBRARY_PATH
				topeet_host_note 'environment=LD_LIBRARY_PATH-unset removed-current-directory-entry'
				;;
		esac
	fi
	if [ -n "${PERL_MM_OPT:-}" ]; then
		unset PERL_MM_OPT
		topeet_host_note 'environment=PERL_MM_OPT-unset Buildroot-rejects-local-Perl-library-settings'
	fi
	export TOPEET_HOST_ENV_SANITIZED=1
}

topeet_host_color()
{
	[ -t 1 ] && [ -z "${NO_COLOR:-}" ]
}

topeet_host_note()
{
	if topeet_host_color; then
		printf '\033[36m[host-preflight]\033[0m %s\n' "$*"
	else
		printf '[host-preflight] %s\n' "$*"
	fi
}

topeet_host_warn()
{
	if topeet_host_color; then
		printf '\033[33m[host-preflight]\033[0m %s\n' "$*" >&2
	else
		printf '[host-preflight] %s\n' "$*" >&2
	fi
}

topeet_host_contains()
{
	local needle=$1 item
	shift
	for item in "$@"; do
		[ "$item" = "$needle" ] && return 0
	done
	return 1
}

topeet_host_reset_requirements()
{
	TOPEET_HOST_MISSING_IDS=()
	TOPEET_HOST_MISSING_PKGS=()
	TOPEET_HOST_MANUAL_IDS=()
}

topeet_host_add_advisory()
{
	local advisory=$1

	topeet_host_contains "$advisory" \
		"${TOPEET_HOST_REPORTED_ADVISORIES[@]}" && return 0
	TOPEET_HOST_REPORTED_ADVISORIES+=("$advisory")
	topeet_host_warn "advisory=$advisory"
}

topeet_host_add_missing()
{
	local id=$1 packages=$2 package

	topeet_host_contains "$id" "${TOPEET_HOST_MISSING_IDS[@]}" || \
		TOPEET_HOST_MISSING_IDS+=("$id")
	for package in $packages; do
		topeet_host_contains "$package" "${TOPEET_HOST_MISSING_PKGS[@]}" || \
			TOPEET_HOST_MISSING_PKGS+=("$package")
	done
}

topeet_host_add_manual()
{
	local id=$1
	topeet_host_contains "$id" "${TOPEET_HOST_MANUAL_IDS[@]}" || \
		TOPEET_HOST_MANUAL_IDS+=("$id")
}

topeet_host_probe_command()
{
	local id=$1 command=$2 packages=$3
	command -v "$command" >/dev/null 2>&1 || \
		topeet_host_add_missing "$id" "$packages"
}

topeet_host_probe_path()
{
	local id=$1 path=$2 packages=$3
	[ -x "$path" ] || topeet_host_add_missing "$id" "$packages"
}

topeet_host_probe_sdk_executable()
{
	local id=$1 path=$2
	[ -x "$path" ] || topeet_host_add_manual "$id"
}

topeet_host_probe_header()
{
	local id=$1 header=$2 packages=$3
	if ! printf '' | gcc -E -include "$header" - >/dev/null 2>&1; then
		topeet_host_add_missing "$id" "$packages"
	fi
}

topeet_host_probe_python3_elftools()
{
	if ! command -v python3 >/dev/null 2>&1 || \
		! python3 -c 'from elftools.elf.elffile import ELFFile' \
		>/dev/null 2>&1; then
		topeet_host_add_missing python3-pyelftools python3-pyelftools
	fi
}

topeet_host_probe_make4()
{
	local version major minor
	if ! command -v make >/dev/null 2>&1; then
		topeet_host_add_missing make make
		return 0
	fi

	version=$(make --version 2>/dev/null | awk 'NR == 1 { print $3 }')
	major=${version%%.*}
	minor=${version#*.}
	minor=${minor%%.*}
	if ! [[ $major =~ ^[0-9]+$ && $minor =~ ^[0-9]+$ ]] || \
		[ "$major" -lt 4 ]; then
		topeet_host_add_missing make make
	fi
}

topeet_host_probe_git2()
{
	local version major

	if ! command -v git >/dev/null 2>&1; then
		topeet_host_add_missing git git
		return 0
	fi
	version=$(git --version 2>/dev/null)
	version=${version##* }
	major=${version%%.*}
	if ! [[ $major =~ ^[0-9]+$ ]] || [ "$major" -lt 2 ]; then
		topeet_host_add_missing git-version git
	fi
}

topeet_host_probe_patch27()
{
	local version major minor

	if ! command -v patch >/dev/null 2>&1; then
		topeet_host_add_missing patch patch
		return 0
	fi
	version=$(patch --version 2>/dev/null | awk 'NR == 1 { print $NF }')
	major=${version%%.*}
	minor=${version#*.}
	minor=${minor%%.*}
	if ! [[ $major =~ ^[0-9]+$ && $minor =~ ^[0-9]+$ ]] || \
		[ "$major" -lt 2 ] || \
		{ [ "$major" -eq 2 ] && [ "$minor" -lt 7 ]; }; then
		topeet_host_add_missing patch-version patch
	fi
}

topeet_host_probe_lz4()
{
	if ! command -v lz4 >/dev/null 2>&1 || \
		! lz4 -h 2>&1 | grep -q favor-decSpeed; then
		topeet_host_add_missing lz4 lz4
	fi
}

topeet_host_probe_utf8_locale()
{
	if ! command -v locale >/dev/null 2>&1 || \
		! locale -a 2>/dev/null | grep -qiE '(^|[._-])utf-?8$'; then
		topeet_host_add_missing utf8-locale locales
	fi
}

topeet_host_apt_has_candidate()
{
	local package=$1 candidate

	[ "${TOPEET_HOST_PACKAGE_MANAGER:-unsupported}" = apt ] || return 1
	command -v apt-cache >/dev/null 2>&1 || return 1
	candidate=$(LC_ALL=C apt-cache policy "$package" 2>/dev/null | \
		awk '/^[[:space:]]*Candidate:/ { print $2; exit }')
	[ -n "$candidate" ] && [ "$candidate" != "(none)" ]
}

topeet_host_probe_environment()
{
	local arch fstype available_kb available_gb mem_kb mem_gb open_files

	arch=$(uname -m 2>/dev/null || echo unknown)
	case "$arch" in
		x86_64) ;;
		*)
			topeet_host_add_manual "host-arch-$arch"
			topeet_host_warn 'host-arch-requirement=x86_64 action=use-an-amd64-host-for-prebuilt-tools'
			;;
	esac

	case "${RK_SDK_DIR:-}" in
		*[[:space:]]*)
			topeet_host_add_manual sdk-path-whitespace
			topeet_host_warn 'sdk-path=contains-whitespace action=move-sdk-to-a-simple-path-such-as-/opt/topeet/rk3588'
			;;
	esac

	case ":${PATH:-}:" in
		*::* | *:.:*)
			topeet_host_add_manual path-current-directory
			topeet_host_warn 'PATH=contains-empty-or-current-directory-entry action=clean-PATH-before-building'
			;;
	esac
	case "${PATH:-}" in
		*[[:space:]]*)
			topeet_host_add_manual path-whitespace
			topeet_host_warn 'PATH=contains-whitespace action=clean-PATH-before-building'
			;;
	esac
	if [ -n "${LD_LIBRARY_PATH:-}" ]; then
		case ":$LD_LIBRARY_PATH:" in
			*::* | *:.:*)
				topeet_host_add_manual ld-library-path-current-directory
				topeet_host_warn 'LD_LIBRARY_PATH=contains-empty-or-current-directory-entry action=unset-before-building'
				;;
		esac
	fi

	if command -v findmnt >/dev/null 2>&1; then
		fstype=$(findmnt -fnu -o FSTYPE -T "${RK_SDK_DIR:-.}" 2>/dev/null || true)
		case "$fstype" in
			ext* | f2fs | btrfs) ;;
			*)
				topeet_host_add_manual "sdk-filesystem-${fstype:-unknown}"
				topeet_host_warn 'sdk-filesystem=require-ext4-f2fs-or-btrfs action=move-sdk-and-rerun-host-doctor'
				;;
		esac
	fi

	if [ "${TOPEET_HOST_RESOURCES_REPORTED:-0}" != 1 ]; then
		available_gb=unknown
		if command -v df >/dev/null 2>&1 && command -v awk >/dev/null 2>&1; then
			available_kb=$(df -Pk "${RK_SDK_DIR:-.}" 2>/dev/null | \
				awk 'NR == 2 { print $4 }')
			if [[ $available_kb =~ ^[0-9]+$ ]]; then
				available_gb=$((available_kb / 1024 / 1024))
				[ "$available_gb" -ge "${TOPEET_HOST_MIN_DISK_GB:-80}" ] || \
					topeet_host_add_advisory "low-disk-${available_gb}GiB-recommend-at-least-${TOPEET_HOST_MIN_DISK_GB:-80}GiB"
			fi
		fi
		mem_gb=unknown
		if [ -r /proc/meminfo ] && command -v awk >/dev/null 2>&1; then
			mem_kb=$(awk '/^MemTotal:/ { print $2; exit }' /proc/meminfo)
			if [[ $mem_kb =~ ^[0-9]+$ ]]; then
				mem_gb=$((mem_kb / 1024 / 1024))
				[ "$mem_gb" -ge "${TOPEET_HOST_MIN_MEMORY_GB:-8}" ] || \
					topeet_host_add_advisory "low-memory-${mem_gb}GiB-recommend-at-least-${TOPEET_HOST_MIN_MEMORY_GB:-8}GiB"
			fi
		fi
		open_files=$(ulimit -n 2>/dev/null || echo unknown)
		if [[ $open_files =~ ^[0-9]+$ ]] && [ "$open_files" -lt 4096 ]; then
			topeet_host_add_advisory "low-open-file-limit-${open_files}-recommend-at-least-4096"
		fi
		topeet_host_note "stage=resources sdk_free_gib=$available_gb memory_gib=$mem_gb open_files=$open_files"
		export TOPEET_HOST_RESOURCES_REPORTED=1
	fi
}

topeet_host_collect_base()
{
	topeet_host_probe_command python3 python3 python3
	topeet_host_probe_command rsync rsync rsync
	topeet_host_probe_command gcc gcc build-essential
	topeet_host_probe_command g++ g++ build-essential
	topeet_host_probe_command make make make
	topeet_host_probe_command awk awk gawk
	topeet_host_probe_command grep grep grep
	topeet_host_probe_command sed sed sed
	topeet_host_probe_command which which debianutils
	topeet_host_probe_command flock flock util-linux
	topeet_host_probe_command find find findutils
	topeet_host_probe_command xargs xargs findutils
	topeet_host_probe_command findmnt findmnt util-linux
	topeet_host_probe_command realpath realpath coreutils
	topeet_host_probe_command stat stat coreutils
	topeet_host_probe_command mktemp mktemp coreutils
	topeet_host_probe_command timeout timeout coreutils
	topeet_host_probe_command whiptail whiptail whiptail
	topeet_host_probe_command pkg-config pkg-config pkg-config
	topeet_host_probe_header ncurses ncurses.h libncurses-dev
	topeet_host_probe_environment
}

topeet_host_collect_kernel()
{
	topeet_host_probe_command python python python-is-python3
	# Kernel image scripts call host dtc even when U-Boot is not rebuilt.
	topeet_host_probe_command dtc dtc device-tree-compiler
	topeet_host_probe_command gzip gzip gzip
	topeet_host_probe_command flex flex flex
	topeet_host_probe_command bison bison bison
	topeet_host_probe_command bc bc bc
	topeet_host_probe_command openssl-cli openssl openssl
	topeet_host_probe_command pkg-config pkg-config pkg-config
	topeet_host_probe_lz4
	topeet_host_probe_header openssl openssl/ssl.h libssl-dev
	topeet_host_probe_header gmp gmp.h libgmp-dev
	topeet_host_probe_header mpc mpc.h libmpc-dev
	topeet_host_probe_header ncurses ncurses.h libncurses-dev
	topeet_host_probe_command depmod depmod kmod
	topeet_host_probe_command modinfo modinfo kmod
	topeet_host_probe_sdk_linux_toolchain arm64
}

topeet_host_collect_buildroot()
{
	topeet_host_probe_make4
	topeet_host_probe_command bash bash bash
	topeet_host_probe_command awk awk gawk
	topeet_host_probe_command ld ld binutils
	topeet_host_probe_command grep grep grep
	topeet_host_probe_command msgmerge msgmerge gettext
	topeet_host_probe_header libc6 dirent.h libc6-dev
	topeet_host_probe_path file /usr/bin/file file
	topeet_host_probe_command unbuffer unbuffer expect
	topeet_host_probe_command sed sed sed
	topeet_host_probe_git2
	topeet_host_probe_command gzip gzip gzip
	topeet_host_probe_command zcat zcat gzip
	topeet_host_probe_command bzip2 bzip2 bzip2
	topeet_host_probe_command bzcat bzcat bzip2
	topeet_host_probe_command xz xz xz-utils
	topeet_host_probe_command xzcat xzcat xz-utils
	topeet_host_probe_command lzcat lzcat xz-utils
	topeet_host_probe_utf8_locale
	topeet_host_probe_command bc bc bc
	topeet_host_probe_command cpio cpio cpio
	topeet_host_probe_patch27
	topeet_host_probe_command perl perl perl
	topeet_host_probe_command unzip unzip unzip
	topeet_host_probe_command wget wget wget
	topeet_host_probe_command curl curl curl
	topeet_host_probe_command git-lfs git-lfs git-lfs
	topeet_host_probe_command svn svn subversion
	topeet_host_probe_command bzr bzr bzr
	topeet_host_probe_command hg hg mercurial
	topeet_host_probe_command cvs cvs cvs
	topeet_host_probe_command ssh ssh openssh-client
	topeet_host_probe_command scp scp openssh-client
	topeet_host_probe_command find find findutils
	topeet_host_probe_command xargs xargs findutils
	topeet_host_probe_command cmp cmp diffutils
	topeet_host_probe_command tar tar tar
}

topeet_host_collect_loader()
{
	topeet_host_probe_command dtc dtc device-tree-compiler
	topeet_host_probe_command bc bc bc
	topeet_host_probe_command flex flex flex
	topeet_host_probe_command bison bison bison
	topeet_host_probe_command openssl-cli openssl openssl
	topeet_host_probe_header openssl openssl/ssl.h libssl-dev
	topeet_host_probe_command gzip gzip gzip
	topeet_host_probe_command bzip2 bzip2 bzip2
	topeet_host_probe_command xz xz xz-utils
	# Rockchip ships Python FIT generators in both SDK lines.  The 5.10 copies
	# are maintained for Python 3 because Ubuntu 24.04 no longer ships Python 2.
	topeet_host_probe_python3_elftools
	topeet_host_probe_sdk_linux_toolchain arm64
}

topeet_host_external_rootfs_requested()
{
	local options=${TOPEET_HOST_OPTIONS:-${OPTIONS:-}}
	local rootfs=${RK_ROOTFS_SYSTEM:-}

	rootfs=${rootfs//\"/}
	[ "${TOPEET_HOST_SCOPE:-}" = all ] && return 0
	if topeet_host_option_matches "$options" \
		'debian debian11 debian12 debian11_update debian12_update ubuntu ubuntu20 ubuntu22 ubuntu24 ubuntu20_update ubuntu22_update ubuntu24_update'; then
		return 0
	fi
	if topeet_host_option_matches "$options" 'all all-release rootfs'; then
		case "$rootfs" in
			debian* | ubuntu*) return 0 ;;
		esac
	fi
	[ -n "$options" ] && return 1
	case "$rootfs" in
		debian* | ubuntu*) return 0 ;;
	esac
	return 1
}

topeet_host_binfmt_aarch64_enabled()
{
	if [ -r /proc/sys/fs/binfmt_misc/qemu-aarch64 ] && \
		grep -qx enabled /proc/sys/fs/binfmt_misc/qemu-aarch64; then
		return 0
	fi
	command -v update-binfmts >/dev/null 2>&1 && \
		update-binfmts --display qemu-aarch64 2>/dev/null | grep -q enabled
}

topeet_host_collect_filesystem()
{
	local rootfs_type=${1:-ext4}
	rootfs_type=${rootfs_type#ubi-}

	case "$rootfs_type" in
		ext2 | ext3 | ext4)
			topeet_host_probe_command mke2fs mke2fs e2fsprogs
			topeet_host_probe_command tune2fs tune2fs e2fsprogs
			topeet_host_probe_command mkfs.ext4 mkfs.ext4 e2fsprogs
			topeet_host_probe_command e2fsck e2fsck e2fsprogs
			;;
		msdos | fat | vfat)
			topeet_host_probe_command mkfs.vfat mkfs.vfat dosfstools
			topeet_host_probe_command mcopy mcopy mtools
			;;
		ntfs)
			topeet_host_probe_command mkntfs mkntfs ntfs-3g
			topeet_host_probe_command ntfscp ntfscp ntfs-3g
			topeet_host_probe_command mount mount util-linux
			;;
		btrfs) topeet_host_probe_command mkfs.btrfs mkfs.btrfs btrfs-progs ;;
		f2fs)
			topeet_host_probe_command mkfs.f2fs mkfs.f2fs f2fs-tools
			topeet_host_probe_command sload.f2fs sload.f2fs f2fs-tools
			;;
		erofs) topeet_host_probe_command mkfs.erofs mkfs.erofs erofs-utils ;;
		squashfs) topeet_host_probe_command mksquashfs mksquashfs squashfs-tools ;;
		jffs2) topeet_host_probe_command mkfs.jffs2 mkfs.jffs2 mtd-utils ;;
		ubi | ubifs)
			topeet_host_probe_command mkfs.ubifs mkfs.ubifs mtd-utils
			topeet_host_probe_command ubinize ubinize mtd-utils
			;;
		*)
			topeet_host_add_manual "filesystem-$rootfs_type"
			topeet_host_warn "filesystem=unsupported type=$rootfs_type action=choose-a-supported-mk-image-filesystem"
			;;
	esac
}

topeet_host_collect_image()
{
	local rootfs_type=${RK_ROOTFS_TYPE:-ext4}
	local extra_num=${RK_EXTRA_PARTITION_NUM:-0}
	local index extra_type var

	topeet_host_probe_command tar tar tar
	topeet_host_probe_command dd dd coreutils
	topeet_host_probe_command truncate truncate coreutils
	topeet_host_probe_command du du coreutils
	topeet_host_probe_command cut cut coreutils
	topeet_host_probe_command wc wc coreutils
	topeet_host_probe_command rev rev coreutils
	topeet_host_probe_command readlink readlink coreutils
	topeet_host_probe_command ls ls coreutils
	topeet_host_probe_command stat stat coreutils
	topeet_host_collect_filesystem "$rootfs_type"
	if [[ $extra_num =~ ^[0-9]+$ ]]; then
		for ((index = 1; index <= extra_num; index++)); do
			var="RK_EXTRA_PARTITION_${index}_FSTYPE"
			extra_type=${!var:-}
			[ -n "$extra_type" ] || extra_type=ext4
			topeet_host_collect_filesystem "$extra_type"
		done
	fi
	topeet_host_probe_command fakeroot fakeroot fakeroot
	topeet_host_probe_sdk_executable sdk-afptool \
		"$RK_SDK_DIR/tools/linux/Linux_Pack_Firmware/rockdev/afptool"
	topeet_host_probe_sdk_executable sdk-rkImageMaker \
		"$RK_SDK_DIR/tools/linux/Linux_Pack_Firmware/rockdev/rkImageMaker"
}

topeet_host_collect_rootfs()
{
	local rootfs_type=${RK_ROOTFS_TYPE:-ext4}

	topeet_host_probe_command tar tar tar
	topeet_host_probe_command gzip gzip gzip
	topeet_host_probe_command dpkg dpkg dpkg
	topeet_host_probe_command dpkg-deb dpkg-deb dpkg
	topeet_host_collect_filesystem "$rootfs_type"
	topeet_host_probe_command findmnt findmnt util-linux
	topeet_host_probe_command sha256sum sha256sum coreutils
	topeet_host_probe_command fakeroot fakeroot fakeroot
	topeet_host_external_rootfs_requested || return 0
	[ "$(id -u)" -eq 0 ] || topeet_host_probe_command sudo sudo sudo
	topeet_host_probe_command chroot chroot coreutils
	topeet_host_probe_command apt apt apt
	topeet_host_probe_command xz xz xz-utils
	topeet_host_probe_command mount mount util-linux
	topeet_host_probe_command umount umount util-linux
	topeet_host_probe_command mountpoint mountpoint util-linux
	topeet_host_probe_command dd dd coreutils
	topeet_host_probe_command du du coreutils
	topeet_host_probe_command cut cut coreutils
	case "$(uname -m)" in
		aarch64 | arm64) ;;
		*)
			topeet_host_probe_command qemu-aarch64-static \
				qemu-aarch64-static qemu-user-static
			topeet_host_probe_command update-binfmts update-binfmts binfmt-support
			if command -v qemu-aarch64-static >/dev/null 2>&1 && \
				command -v update-binfmts >/dev/null 2>&1 && \
				! topeet_host_binfmt_aarch64_enabled; then
				topeet_host_warn 'stage=rootfs binfmt_aarch64=disabled action=prepare-will-enable'
			fi
			;;
	esac
}

topeet_host_config_enabled()
{
	local config=$1 symbol=$2
	[ -r "$config" ] && grep -qx "$symbol=y" "$config"
}

topeet_host_config_any_enabled()
{
	local config=$1 symbol
	shift
	for symbol in "$@"; do
		topeet_host_config_enabled "$config" "$symbol" && return 0
	done
	return 1
}

topeet_host_gcc_plugin_ready()
{
	local compiler=$1 plugin_dir

	[ -n "$compiler" ] || return 1
	plugin_dir=$("$compiler" -print-file-name=plugin 2>/dev/null)
	[ -r "$plugin_dir/include/gcc-plugin.h" ] || return 1
	[ -r "$plugin_dir/include/plugin-version.h" ] || return 1
	command -v g++ >/dev/null 2>&1 || return 1
	printf '#include <gcc-plugin.h>\n' | \
		g++ -I"$plugin_dir/include" -x c++ -c -o /dev/null - \
		>/dev/null 2>&1
}

topeet_host_compiler_is_linux()
{
	local compiler=$1 machine

	[ -x "$compiler" ] || command -v "$compiler" >/dev/null 2>&1 || return 1
	machine=$("$compiler" -dumpmachine 2>/dev/null) || return 1
	case "$machine" in
		*-linux-* | *-linux) return 0 ;;
		*) return 1 ;;
	esac
}

topeet_host_find_sdk_linux_compiler()
{
	local arch=${1:-arm64} pattern compiler

	[ -d "${RK_SDK_DIR:-}/prebuilts/gcc" ] || return 1
	case "$arch" in
		arm | armhf) pattern='arm*-linux-gnueabihf-gcc' ;;
		*) pattern='aarch64*-linux-*-gcc' ;;
	esac
	while IFS= read -r compiler; do
		topeet_host_compiler_is_linux "$compiler" && {
			printf '%s\n' "$compiler"
			return 0
		}
	done < <(find "$RK_SDK_DIR/prebuilts/gcc" -type f -name "$pattern" \
		-perm -u+x -print 2>/dev/null | sort)
	return 1
}

topeet_host_probe_sdk_linux_toolchain()
{
	local arch=${1:-arm64} compiler

	[ "$(uname -m 2>/dev/null)" = x86_64 ] || return 0
	compiler=$(topeet_host_find_sdk_linux_compiler "$arch" || true)
	if [ -z "$compiler" ]; then
		topeet_host_add_manual "sdk-${arch}-linux-toolchain"
		topeet_host_warn "toolchain=missing arch=$arch expected=aarch64-none-linux-gnu-or-arm-none-linux-gnueabihf"
		return 0
	fi
	if ! printf 'int main(void) { return 0; }\n' | \
		"$compiler" -x c -c -o /dev/null - >/dev/null 2>&1; then
		topeet_host_add_manual "sdk-${arch}-linux-toolchain-unusable"
		topeet_host_warn "toolchain=unusable compiler=$compiler action=check-sdk-prebuilts-and-executable-mount"
	fi
}

topeet_host_find_kernel_compiler()
{
	local prefix compiler arch

	for prefix in "${TOPEET_HOST_KERNEL_CC:-}" \
		"${RK_KERNEL_TOOLCHAIN:-}" "${CROSS_COMPILE:-}"; do
		[ -n "$prefix" ] || continue
		case "$prefix" in
			*gcc) compiler=$prefix ;;
			*) compiler="${prefix}gcc" ;;
		esac
		if topeet_host_compiler_is_linux "$compiler"; then
			printf '%s\n' "$compiler"
			return 0
		fi
	done

	arch=${RK_KERNEL_ARCH:-arm64}
	topeet_host_find_sdk_linux_compiler "$arch"
}

topeet_host_probe_host_gcc_plugin()
{
	local major package

	if topeet_host_gcc_plugin_ready gcc; then
		return 0
	fi
	major=$(gcc -dumpfullversion -dumpversion 2>/dev/null)
	major=${major%%.*}
	package="gcc-${major}-plugin-dev"
	if [ -z "$major" ]; then
		package=gcc-plugin-dev
	fi
	topeet_host_add_missing gcc-plugin "$package"
}

topeet_host_probe_kernel_gcc_plugin()
{
	local compiler

	compiler=$(topeet_host_find_kernel_compiler)
	if [ -z "$compiler" ]; then
		# Native kernel builds still use the host compiler's plugin headers.
		topeet_host_probe_host_gcc_plugin
		return 0
	fi
	if ! topeet_host_gcc_plugin_ready "$compiler"; then
		topeet_host_add_manual cross-gcc-plugin
	fi
}

topeet_host_collect_kernel_config()
{
	local config=$1

	[ -r "$config" ] || return 0
	if topeet_host_config_any_enabled "$config" \
		CONFIG_STACK_VALIDATION CONFIG_UNWINDER_ORC \
		CONFIG_FTRACE_MCOUNT_USE_OBJTOOL CONFIG_DEBUG_INFO_BTF; then
		topeet_host_probe_command pkg-config pkg-config pkg-config
		topeet_host_probe_header libelf libelf.h libelf-dev
	fi
	if topeet_host_config_enabled "$config" CONFIG_DEBUG_INFO_BTF; then
		topeet_host_probe_command pahole pahole dwarves
		topeet_host_probe_header zlib zlib.h zlib1g-dev
	fi
	topeet_host_config_enabled "$config" CONFIG_GCC_PLUGINS && \
		topeet_host_probe_kernel_gcc_plugin

	topeet_host_config_any_enabled "$config" CONFIG_KERNEL_GZIP \
		CONFIG_MODULE_COMPRESS_GZIP CONFIG_INITRAMFS_COMPRESSION_GZIP && \
		topeet_host_probe_command gzip gzip gzip
	topeet_host_config_any_enabled "$config" CONFIG_KERNEL_BZIP2 \
		CONFIG_INITRAMFS_COMPRESSION_BZIP2 && \
		topeet_host_probe_command bzip2 bzip2 bzip2
	topeet_host_config_any_enabled "$config" CONFIG_KERNEL_XZ \
		CONFIG_MODULE_COMPRESS_XZ CONFIG_INITRAMFS_COMPRESSION_XZ && \
		topeet_host_probe_command xz xz xz-utils
	topeet_host_config_any_enabled "$config" CONFIG_KERNEL_LZ4 \
		CONFIG_INITRAMFS_COMPRESSION_LZ4 && topeet_host_probe_lz4
	topeet_host_config_any_enabled "$config" CONFIG_KERNEL_ZSTD \
		CONFIG_MODULE_COMPRESS_ZSTD CONFIG_INITRAMFS_COMPRESSION_ZSTD && \
		topeet_host_probe_command zstd zstd zstd
	return 0
}

topeet_host_probe_perl_module()
{
	local module=$1
	perl -e "require $module" >/dev/null 2>&1 || \
		topeet_host_add_missing "perl-$module" perl
}

topeet_host_probe_buildroot_tool()
{
	local tool=$1

	case "$tool" in
		git) topeet_host_probe_git2 ;;
		git-lfs) topeet_host_probe_command git-lfs git-lfs git-lfs ;;
		svn) topeet_host_probe_command svn svn subversion ;;
		bzr) topeet_host_probe_command bzr bzr bzr ;;
		hg) topeet_host_probe_command hg hg mercurial ;;
		cvs) topeet_host_probe_command cvs cvs cvs ;;
		ssh | scp) topeet_host_probe_command "$tool" "$tool" openssh-client ;;
		curl) topeet_host_probe_command curl curl curl ;;
		gzip | zcat) topeet_host_probe_command "$tool" "$tool" gzip ;;
		bzip2 | bzcat) topeet_host_probe_command "$tool" "$tool" bzip2 ;;
		xz | xzcat | lzcat) topeet_host_probe_command "$tool" "$tool" xz-utils ;;
		lz4) topeet_host_probe_lz4 ;;
		zstd | unzstd) topeet_host_probe_command "$tool" "$tool" zstd ;;
		tar | wget | cpio | unzip | rsync | bc)
			topeet_host_probe_command "$tool" "$tool" "$tool"
			;;
		'') ;;
		*)
			if ! command -v "$tool" >/dev/null 2>&1; then
				topeet_host_add_manual "buildroot-tool-$tool"
			fi
			;;
	esac
}

topeet_host_collect_buildroot_config()
{
	local config=$1 buildroot_dir=${2:-} output_dir=${3:-}
	local module tools tool buildroot_version

	[ -r "$config" ] || return 0
	topeet_host_config_enabled "$config" BR2_NEEDS_HOST_UTF8_LOCALE && \
		topeet_host_probe_utf8_locale
	topeet_host_config_enabled "$config" BR2_NEEDS_HOST_JAVA && \
		topeet_host_probe_command java java default-jre-headless
	if topeet_host_config_enabled "$config" BR2_HOSTARCH_NEEDS_IA32_LIBS && \
		[ ! -e /lib/ld-linux.so.2 ]; then
		topeet_host_add_missing ia32-libs 'libc6-i386 lib32stdc++6 lib32z1'
	fi
	if topeet_host_config_enabled "$config" BR2_HOSTARCH_NEEDS_IA32_COMPILER; then
		printf 'int main(void) {}\n' | gcc -m32 -x c - -o /dev/null 2>/dev/null || \
			topeet_host_add_missing gcc-multilib gcc-multilib
		printf 'int main(void) {}\n' | g++ -m32 -x c++ - -o /dev/null 2>/dev/null || \
			topeet_host_add_missing g++-multilib g++-multilib
	fi
	topeet_host_config_enabled "$config" BR2_NEEDS_HOST_GCC_PLUGIN_SUPPORT && \
		topeet_host_probe_host_gcc_plugin

	for module in Data::Dumper English ExtUtils::MakeMaker Thread::Queue \
		FindBin IPC::Cmd; do
		topeet_host_probe_perl_module "$module"
	done
	topeet_host_config_enabled "$config" BR2_PACKAGE_MPV && {
		topeet_host_probe_perl_module Math::BigInt
		topeet_host_probe_perl_module Math::BigRat
	}
	topeet_host_config_enabled "$config" BR2_PACKAGE_MOSH && \
		topeet_host_probe_perl_module diagnostics
	topeet_host_config_enabled "$config" BR2_PACKAGE_NETSURF && \
		topeet_host_probe_perl_module Digest::MD5
	topeet_host_config_enabled "$config" BR2_PACKAGE_WHOIS && \
		topeet_host_probe_perl_module autodie
	if topeet_host_config_any_enabled "$config" BR2_PACKAGE_WEBKITGTK \
		BR2_PACKAGE_WPEWEBKIT; then
		topeet_host_probe_perl_module JSON::PP
	fi
	if topeet_host_config_any_enabled "$config" BR2_PACKAGE_ACE \
		BR2_TARGET_SYSLINUX; then
		topeet_host_probe_perl_module FileHandle
	fi
	if topeet_host_config_enabled "$config" BR2_PACKAGE_LIBXCRYPT && \
		! perl -e "use open ':std'" >/dev/null 2>&1; then
		topeet_host_add_missing perl-open perl
	fi

	[ -n "$buildroot_dir" ] && [ -n "$output_dir" ] || return 0
	case "${TOPEET_HOST_BUILDROOT_TOOLS:-auto}" in
		never | off) return 0 ;;
		force) ;;
		auto)
			buildroot_version=$(sed -n \
				's/^[[:space:]]*export[[:space:]]\+BR2_VERSION[[:space:]]*:=[[:space:]]*//p' \
				"$buildroot_dir/Makefile" | head -n 1)
			case "$buildroot_version" in
				2021.11)
					# This vendor tree crashes GNU make while expanding the
					# aggregate downloader variable.  Its default downloader
					# set is already covered by the static checks above.
					topeet_host_note 'stage=buildroot-config dynamic_tools=static buildroot_version=2021.11'
					return 0
					;;
			esac
			;;
		*)
			topeet_host_warn "buildroot_tools_mode=${TOPEET_HOST_BUILDROOT_TOOLS} action=use-auto"
			;;
	esac
	local probe_timeout=${TOPEET_HOST_BUILDROOT_PROBE_TIMEOUT:-15}
	[[ $probe_timeout =~ ^[0-9]+$ ]] || probe_timeout=15
	if command -v timeout >/dev/null 2>&1; then
		if ! tools=$(timeout "$probe_timeout" make -s -C "$buildroot_dir" \
			O="$output_dir" printvars VARS=DL_TOOLS_DEPENDENCIES \
			QUOTED_VARS=YES 2>/dev/null); then
			topeet_host_warn 'stage=buildroot-config dynamic_tools=unavailable action=use-static-checks'
			return 0
		fi
	elif ! tools=$(make -s -C "$buildroot_dir" O="$output_dir" printvars \
		VARS=DL_TOOLS_DEPENDENCIES QUOTED_VARS=YES 2>/dev/null); then
		topeet_host_warn 'stage=buildroot-config dynamic_tools=unavailable action=use-static-checks'
		return 0
	fi
	tools=$(printf '%s\n' "$tools" | \
		sed -n "s/^DL_TOOLS_DEPENDENCIES='\\(.*\\)'$/\\1/p" | \
		tr ' ' '\n' | sed '/^$/d' | sort -u)
	for tool in $tools; do
		case "$tool" in
			*[!A-Za-z0-9._+-]*) continue ;;
		esac
		topeet_host_probe_buildroot_tool "$tool"
	done
	return 0
}

topeet_host_collect_security()
{
	topeet_host_probe_command openssl-cli openssl openssl
	topeet_host_probe_command bc bc bc
	topeet_host_probe_command dd dd coreutils
	topeet_host_probe_command shuf shuf coreutils
	topeet_host_probe_command readlink readlink coreutils
	topeet_host_probe_command realpath realpath coreutils
	topeet_host_probe_command losetup losetup util-linux
	[ "$(id -u)" -eq 0 ] || topeet_host_probe_command sudo sudo sudo
	topeet_host_probe_sdk_executable sdk-rk-sign-tool \
		"$RK_SDK_DIR/rkbin/tools/rk_sign_tool"
	topeet_host_probe_command veritysetup veritysetup cryptsetup-bin
	topeet_host_probe_command dmsetup dmsetup dmsetup
}

topeet_host_option_matches()
{
	local options=" $1 " names=$2 name
	for name in $names; do
		[[ $options =~ (^|[[:space:]])${name}(_update)?(:|[[:space:]]|$) ]] && return 0
	done
	return 1
}

topeet_host_full_build_requested()
{
	local option

	for option in $1; do
		option=${option%%:*}
		case "$option" in
			all | all-release | *_update) return 0 ;;
		esac
	done
	return 1
}

topeet_host_collect_stage()
{
	case "$1" in
		base) topeet_host_collect_base ;;
		uboot) topeet_host_collect_loader ;;
		kernel) topeet_host_collect_kernel ;;
		buildroot) topeet_host_collect_buildroot ;;
		rootfs) topeet_host_collect_rootfs ;;
		image) topeet_host_collect_image ;;
		security) topeet_host_collect_security ;;
		*)
			topeet_host_warn "status=invalid-stage stage=$1"
			return 2
			;;
	esac
}

topeet_host_stage_selected()
{
	local stage=$1 options=${2:-${OPTIONS:-all}}

	if topeet_host_full_build_requested "$options"; then
		case "$stage" in
			base | uboot | kernel | buildroot | rootfs | image | security)
				return 0
				;;
		esac
	fi

	case "$stage" in
		base) return 0 ;;
		uboot)
			topeet_host_option_matches "$options" \
				'all all-release loader uboot u-boot firmware updateimg ota-updateimg security-createkeys security-misc'
			;;
		kernel)
			topeet_host_option_matches "$options" \
				'all all-release kernel kernel-5.10 kernel-6.1 kernel-modules modules kernel-config kconfig kernel-make kmake wifibt boot recovery recovery-kernel ramboot rootfs buildroot buildroot_update ubuntu ubuntu20 ubuntu22 ubuntu24 ubuntu20_update ubuntu22_update ubuntu24_update debian debian11 debian12 debian11_update debian12_update linux-headers security-ramboot security-system'
			;;
		buildroot)
			topeet_host_option_matches "$options" \
				'all all-release buildroot buildroot-config bconfig buildroot-make bmake buildroot-sdk bsdk rootfs recovery recovery-kernel ramboot ubuntu ubuntu20 ubuntu22 ubuntu24 ubuntu20_update ubuntu22_update ubuntu24_update debian debian11 debian12 debian11_update debian12_update security-ramboot'
			;;
		rootfs)
			topeet_host_option_matches "$options" \
				'all all-release rootfs ubuntu ubuntu20 ubuntu22 ubuntu24 ubuntu20_update ubuntu22_update ubuntu24_update debian debian11 debian12 debian11_update debian12_update extra-parts linux-headers security-system'
			;;
		image)
			topeet_host_option_matches "$options" \
				'all all-release misc extra-parts firmware updateimg ota-updateimg edit-package-file edit-ota-package-file release'
			;;
		security)
			topeet_host_option_matches "$options" \
				'all all-release security security-createkeys security-misc security-ramboot security-system security-remote-sign'
			;;
		*) return 1 ;;
	esac
}

topeet_host_collect_selected()
{
	local options=${1:-${OPTIONS:-all}} stage

	for stage in base uboot kernel buildroot rootfs image security; do
		topeet_host_stage_selected "$stage" "$options" || continue
		topeet_host_collect_stage "$stage" || return $?
	done
}

topeet_host_scan()
{
	local scope=${1:-selected} options=${2:-}

	TOPEET_HOST_SCOPE=$scope
	TOPEET_HOST_OPTIONS=$options
	topeet_host_init
	topeet_host_detect_os
	topeet_host_reset_requirements
	case "$scope" in
		base | uboot | kernel | buildroot | rootfs | image | security)
			topeet_host_collect_stage "$scope"
			;;
		all)
			topeet_host_collect_base
			topeet_host_collect_loader
			topeet_host_collect_kernel
			topeet_host_collect_buildroot
			topeet_host_collect_rootfs
			topeet_host_collect_image
			topeet_host_collect_security
			;;
		selected) topeet_host_collect_selected "$options" ;;
		*)
			topeet_host_warn "status=invalid-scope scope=$scope"
			return 2
			;;
	esac
}

topeet_host_print_report()
{
	local scope=$1 joined
	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
		[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]; then
		topeet_host_note "stage=preflight scope=$scope deps=ready"
	elif [ "${#TOPEET_HOST_MISSING_IDS[@]}" -gt 0 ]; then
		joined=$(IFS=,; echo "${TOPEET_HOST_MISSING_IDS[*]}")
		topeet_host_warn "stage=preflight scope=$scope deps=missing ids=$joined"
		joined=$(IFS=' '; echo "${TOPEET_HOST_MISSING_PKGS[*]}")
		topeet_host_note "apt_packages=$joined"
	fi
	if [ "${#TOPEET_HOST_MANUAL_IDS[@]}" -gt 0 ]; then
		joined=$(IFS=,; echo "${TOPEET_HOST_MANUAL_IDS[*]}")
		topeet_host_warn "manual_dependency=$joined"
		topeet_host_note 'manual_action=fix-the-listed-host-condition-and-rerun-./build.sh-host-doctor'
	fi
	[ "${#TOPEET_HOST_MISSING_PKGS[@]}" -eq 0 ] || \
		topeet_host_note 'dependency_action=best-effort-during-prepare'
}

topeet_host_demo()
{
	local scope=${1:-all}

	topeet_host_init
	topeet_host_detect_os
	topeet_host_reset_requirements
	# Deliberately synthetic: this exercises the user-visible dependency path
	# on a fully prepared host without probing, installing, or changing it.
	topeet_host_add_missing demo-base "rsync build-essential"
	topeet_host_add_missing demo-kernel "flex bison lz4"
	topeet_host_add_missing demo-rootfs "fakeroot qemu-user-static"
	topeet_host_note "demo=synthetic scope=$scope no-system-changes=true"
	topeet_host_print_report demo

	if [ "${TOPEET_HOST_OS_SUPPORTED:-0}" != 1 ] || \
		[ "$(uname -m)" != x86_64 ] || topeet_host_dependency_install_disabled; then
		topeet_host_note 'demo=synthetic install=disabled action=would-continue-build'
		return 0
	fi
	case "${TOPEET_HOST_OFFLINE:-0}" in
		1 | yes | true)
			topeet_host_note 'demo=synthetic network=offline action=would-continue-build'
			;;
		*)
			topeet_host_note 'demo=synthetic network=auto action=would-check-apt-sources-before-install'
			topeet_host_note 'demo=synthetic install=best-effort on_failure=continue-build'
			;;
	esac
}

topeet_host_set_deferred()
{
	if [ "${TOPEET_HOST_DEPS_DEFERRED:-0}" != 1 ]; then
		topeet_host_warn "${*:+$* }deps=deferred action=continue-build"
		topeet_host_warn 'Host preparation skipped; resolve any build errors or missing dependencies manually.'
	fi
	export TOPEET_HOST_DEPS_DEFERRED=1
}

# Automatic preparation is optional. Other hosts and offline builds use the
# customer's existing tools and let the actual build report missing inputs.
topeet_host_can_prepare()
{
	topeet_host_init
	[ "${TOPEET_HOST_DEPS_DEFERRED:-0}" != 1 ] || return 1
	if topeet_host_dependency_install_disabled; then
		topeet_host_set_deferred "install=disabled mode=${TOPEET_HOST_DEPS_MODE}"
		return 1
	fi
	case "${TOPEET_HOST_OFFLINE:-0}" in
		1 | yes | true)
			topeet_host_set_deferred 'network=offline'
			return 1
			;;
	esac
	topeet_host_detect_os
	if [ "${TOPEET_HOST_OS_SUPPORTED:-0}" != 1 ] || \
		[ "$(uname -m)" != x86_64 ]; then
		topeet_host_set_deferred "host=${TOPEET_HOST_OS_ID}:${TOPEET_HOST_OS_VERSION} automatic_preparation=unsupported"
		return 1
	fi
	if ! command -v apt-get >/dev/null 2>&1; then
		topeet_host_set_deferred 'apt=unavailable'
		return 1
	fi
	return 0
}

topeet_host_network_available()
{
	local uris uri rest origin seen=' ' attempts=0
	local -a probe

	# Probe the configured APT sources, including private mirrors. Listing URIs
	# is read-only and needs neither sudo nor a package-index update. Bound the
	# whole check, including DNS, and never use ICMP as a connectivity gate.
	command -v timeout >/dev/null 2>&1 || return 1
	if command -v curl >/dev/null 2>&1; then
		probe=(curl --silent --head --location --output /dev/null
			--connect-timeout 2 --max-time 3)
	elif command -v wget >/dev/null 2>&1; then
		probe=(wget --quiet --spider --timeout=3 --tries=1)
	else
		return 1
	fi
	uris=$(timeout 3 apt-get -q --print-uris update 2>/dev/null) || return 1
	while read -r uri rest; do
		uri=${uri#\'}
		uri=${uri%\'}
		case "$uri" in
			http://* | https://*) ;;
			*) continue ;;
		esac
		origin=${uri#*://}
		origin=${origin%%/*}
		case "$seen" in *" $origin "*) continue ;; esac
		seen+="$origin "
		if timeout 4 "${probe[@]}" "$uri" >/dev/null 2>&1; then
			return 0
		fi
		attempts=$((attempts + 1))
		[ "$attempts" -lt 2 ] || break
	done <<< "$uris"
	return 1
}

topeet_host_prepare_sudo()
{
	TOPEET_HOST_SUDO=()
	if [ "$(id -u)" -eq 0 ]; then
		return 0
	fi
	if ! command -v sudo >/dev/null 2>&1; then
		topeet_host_warn "sudo=missing action=skip-host-preparation"
		return 1
	fi

	if [ "${TOPEET_HOST_NONINTERACTIVE:-0}" = 1 ] || ! [ -t 0 ]; then
		sudo -n -v >/dev/null 2>&1 || {
			topeet_host_warn "sudo=unavailable action=skip-host-preparation"
			return 1
		}
	else
		sudo -v || {
			topeet_host_warn "sudo=cancelled action=skip-host-preparation"
			return 1
		}
	fi
	TOPEET_HOST_SUDO=(sudo)
}

topeet_host_run_logged()
{
	local log_file=$1 timeout_seconds=$2 status
	local -a command pipeline_status
	shift 2
	command=("$@")

	[[ $timeout_seconds =~ ^[0-9]+$ ]] || timeout_seconds=0
	if [ "$timeout_seconds" -gt 0 ] && command -v timeout >/dev/null 2>&1; then
		command=(timeout --foreground "$timeout_seconds" "${command[@]}")
	fi
	if command -v tee >/dev/null 2>&1; then
		if "${command[@]}" 2>&1 | tee -a "$log_file"; then
			pipeline_status=("${PIPESTATUS[@]}")
		else
			pipeline_status=("${PIPESTATUS[@]}")
		fi
		status=${pipeline_status[0]:-1}
		[ "$status" -eq 0 ] || return "$status"
		return "${pipeline_status[1]:-0}"
	fi
	"${command[@]}" >>"$log_file" 2>&1
}

topeet_host_apt()
{
	local log_file=$1 line
	shift
	local timeout_seconds=${TOPEET_APT_TIMEOUT:-600}
	local acquire_timeout=${TOPEET_APT_ACQUIRE_TIMEOUT:-15}
	local lock_timeout=${TOPEET_APT_LOCK_TIMEOUT:-300}
	local retries=2
	local -a command

	[[ $timeout_seconds =~ ^[0-9]+$ ]] || timeout_seconds=600
	[[ $acquire_timeout =~ ^[0-9]+$ ]] || acquire_timeout=15
	[[ $lock_timeout =~ ^[0-9]+$ ]] || lock_timeout=300
	# A failed refresh must not hold an offline build for the install timeout.
	if [ "${1:-}" = update ]; then
		timeout_seconds=${TOPEET_APT_UPDATE_TIMEOUT:-30}
		[[ $timeout_seconds =~ ^[1-9][0-9]*$ ]] || timeout_seconds=30
		acquire_timeout=3
		retries=0
	fi
	command=(env LC_ALL=C DEBIAN_FRONTEND=noninteractive apt-get -q
		-o APT::Update::Error-Mode=any
		-o Dpkg::Use-Pty=0 -o DPkg::Lock::Timeout="$lock_timeout"
		-o Acquire::Retries="$retries"
		-o Acquire::http::Timeout="$acquire_timeout"
		-o Acquire::https::Timeout="$acquire_timeout" "$@")

	if [ "${#TOPEET_HOST_SUDO[@]}" -gt 0 ]; then
		command=("${TOPEET_HOST_SUDO[@]}" "${command[@]}")
	fi
	topeet_host_run_logged "$log_file" "$timeout_seconds" "${command[@]}" || return $?
	# Older APT (Ubuntu 18.04) may return success after failing to fetch indexes.
	if [ "${1:-}" = update ]; then
		while IFS= read -r line; do
			case "$line" in
				Err:* | E:* | 'W: Failed to fetch'*) return 1 ;;
			esac
		done < "$log_file"
	fi
	return 0
}

topeet_host_release_apt_lock()
{
	local lock_fd=${1:-}
	[ -n "$lock_fd" ] || return 0
	flock -u "$lock_fd" >/dev/null 2>&1 || true
	exec {lock_fd}>&-
}

topeet_host_apt_check_candidates()
{
	local package
	TOPEET_HOST_UNAVAILABLE_PKGS=()
	for package in "${TOPEET_HOST_MISSING_PKGS[@]}"; do
		topeet_host_apt_has_candidate "$package" || \
			TOPEET_HOST_UNAVAILABLE_PKGS+=("$package")
	done
	[ "${#TOPEET_HOST_UNAVAILABLE_PKGS[@]}" -eq 0 ]
}

topeet_host_enable_ubuntu_universe()
{
	local log_file=$1 timeout_seconds=${TOPEET_APT_TIMEOUT:-600}
	local -a command

	[ "${TOPEET_HOST_OS_ID:-}" = ubuntu ] || return 1
	[ "${TOPEET_HOST_OS_SUPPORTED:-0}" = 1 ] || return 1
	case "${TOPEET_HOST_AUTO_ENABLE_UNIVERSE:-1}" in
		0 | no | false) return 1 ;;
	esac
	if ! command -v add-apt-repository >/dev/null 2>&1; then
		topeet_host_note 'apt_repository_helper=missing action=auto-install-software-properties-common'
		topeet_host_apt "$log_file" install -y --no-install-recommends \
			software-properties-common || {
			topeet_host_warn "apt_repository_helper=install-failed action=skip-host-preparation log=$log_file"
			return 1
		}
	fi

	[[ $timeout_seconds =~ ^[0-9]+$ ]] || timeout_seconds=600
	command=(env DEBIAN_FRONTEND=noninteractive add-apt-repository -y universe)
	if [ "${#TOPEET_HOST_SUDO[@]}" -gt 0 ]; then
		command=("${TOPEET_HOST_SUDO[@]}" "${command[@]}")
	fi
	topeet_host_note 'apt_repository=universe action=auto-enable'
	if ! topeet_host_run_logged "$log_file" "$timeout_seconds" \
		"${command[@]}"; then
		topeet_host_warn "apt_repository=universe enable=failed action=skip-host-preparation log=$log_file"
		return 1
	fi
	topeet_host_note 'apt_repository=universe action=refresh-package-index'
	topeet_host_apt "$log_file" update || {
		topeet_host_warn "apt-update=failed-after-enabling-universe action=skip-host-preparation log=$log_file"
		return 1
	}
	export TOPEET_HOST_APT_UPDATED=1
}

topeet_host_install_missing()
{
	local log_file log_output=log-only status lock_file lock_fd= lock_timeout joined

	topeet_host_can_prepare || return 1
	if ! topeet_host_network_available; then
		topeet_host_warn 'network=offline-or-unavailable action=skip-host-preparation'
		return 1
	fi
	topeet_host_prepare_sudo || return 1

	log_file=$(mktemp "${TMPDIR:-/tmp}/topeet-host-apt.XXXXXX") || {
		topeet_host_warn "apt_log=unavailable action=skip-host-preparation"
		return 1
	}
	command -v tee >/dev/null 2>&1 && log_output=live
	topeet_host_note "apt_log=$log_file output=$log_output"
	# Multiple SDK builds may reach this point concurrently.  Serialize the
	# update/install pair so apt/dpkg never compete for their global locks.
	if command -v flock >/dev/null 2>&1; then
		lock_file=${TOPEET_HOST_APT_LOCK:-/tmp/topeet-host-apt-$(id -u).lock}
		lock_timeout=${TOPEET_HOST_APT_LOCK_TIMEOUT:-300}
		[[ $lock_timeout =~ ^[0-9]+$ ]] || lock_timeout=300
		if exec {lock_fd}>"$lock_file"; then
			if ! flock -w "$lock_timeout" -x "$lock_fd"; then
				topeet_host_warn "apt_lock=timeout timeout_seconds=$lock_timeout action=skip-host-preparation"
				topeet_host_release_apt_lock "$lock_fd"
				rm -f "$log_file"
				return 1
			fi
		else
			topeet_host_warn "apt_lock=file-unavailable action=continue-without-lock"
		fi
	fi
	if [ "${TOPEET_HOST_APT_UPDATED:-0}" != 1 ]; then
		topeet_host_note "network=checking action=apt-update"
		if topeet_host_apt "$log_file" update; then
			export TOPEET_HOST_APT_UPDATED=1
		else
			status=$?
			topeet_host_release_apt_lock "$lock_fd"
			topeet_host_warn "apt-update=failed exit_code=$status action=skip-host-preparation log=$log_file"
			return 1
		fi
	fi
	if ! topeet_host_apt_check_candidates; then
		if topeet_host_enable_ubuntu_universe "$log_file" && \
			topeet_host_apt_check_candidates; then
			topeet_host_note 'apt_candidates=recovered repository=universe'
		fi
		if [ "${#TOPEET_HOST_UNAVAILABLE_PKGS[@]}" -gt 0 ]; then
			joined=$(IFS=' '; echo "${TOPEET_HOST_UNAVAILABLE_PKGS[*]}")
			topeet_host_release_apt_lock "$lock_fd"
			topeet_host_warn "apt_candidates=missing packages=$joined action=check-ubuntu-apt-sources-or-version"
			topeet_host_warn "apt_log=$log_file"
			return 1
		fi
	fi

	topeet_host_note "install=begin packages=${TOPEET_HOST_MISSING_PKGS[*]}"
	if topeet_host_apt "$log_file" install -y --no-install-recommends \
		"${TOPEET_HOST_MISSING_PKGS[@]}"; then
		topeet_host_release_apt_lock "$lock_fd"
		rm -f "$log_file"
		return 0
	else
		status=$?
		topeet_host_release_apt_lock "$lock_fd"
		topeet_host_warn "install=failed exit_code=$status action=skip-host-preparation log=$log_file"
		return 1
	fi
}

topeet_host_dependency_install_disabled()
{
	case "${TOPEET_HOST_DEPS_MODE:-auto}" in
		never | off | check) return 0 ;;
	esac
	return 1
}

topeet_host_prepare_rootfs_runtime()
{
	topeet_host_external_rootfs_requested || return 0
	topeet_host_dependency_install_disabled && return 0
	case "$(uname -m)" in
		aarch64 | arm64) return 0 ;;
	esac
	topeet_host_binfmt_aarch64_enabled && return 0
	if ! topeet_host_prepare_sudo; then
		topeet_host_warn 'stage=rootfs sudo=unavailable action=skip-host-preparation'
		return 1
	fi
	# WSL and some minimal containers have the binfmt_misc directory but do
	# not mount the filesystem.  Mount it before asking update-binfmts to enable
	# the handler; a failed mount is reported below with an actionable message.
	if [ ! -e /proc/sys/fs/binfmt_misc/register ] && \
		command -v mount >/dev/null 2>&1; then
		"${TOPEET_HOST_SUDO[@]}" mount -t binfmt_misc binfmt_misc \
			/proc/sys/fs/binfmt_misc >/dev/null 2>&1 || true
	fi
	if ! command -v qemu-aarch64-static >/dev/null 2>&1 || \
		! command -v update-binfmts >/dev/null 2>&1; then
		topeet_host_warn 'stage=rootfs binfmt_aarch64=missing action=skip-host-preparation'
		return 1
	fi

	topeet_host_note 'stage=rootfs binfmt_aarch64=disabled action=enable'
	"${TOPEET_HOST_SUDO[@]}" update-binfmts --enable qemu-aarch64 \
		>/dev/null 2>&1 || true
	if ! topeet_host_binfmt_aarch64_enabled; then
		topeet_host_warn 'stage=rootfs binfmt_aarch64=unavailable action=enable-binfmt-support-or-build-on-a-native-arm64-host'
		return 1
	fi
}

topeet_host_prepare_stage_runtime()
{
	case "$1" in
		rootfs) topeet_host_prepare_rootfs_runtime ;;
	esac
}

topeet_host_prepare_stage()
{
	local stage=$1

	topeet_host_can_prepare || return 0

	topeet_host_reset_requirements
	topeet_host_collect_stage "$stage" || return $?
	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
		[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]; then
		topeet_host_note "stage=$stage deps=ready"
		topeet_host_prepare_stage_runtime "$stage" || \
			topeet_host_set_deferred "stage=$stage runtime=unavailable"
		return 0
	fi
	topeet_host_print_report "$stage"

	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -gt 0 ]; then
		if [ "${TOPEET_HOST_DEPS_DEFERRED:-0}" = 1 ] || \
			topeet_host_dependency_install_disabled; then
			topeet_host_note "stage=$stage deps=deferred action=continue-build"
			topeet_host_set_deferred
			return 0
		fi
		topeet_host_install_missing || {
			topeet_host_set_deferred "stage=$stage deps=unresolved"
			return 0
		}
	fi

	topeet_host_reset_requirements
	topeet_host_collect_stage "$stage" || return $?
	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
		[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]; then
		topeet_host_note "stage=$stage deps=installed"
		topeet_host_prepare_stage_runtime "$stage" || \
			topeet_host_set_deferred "stage=$stage runtime=unavailable"
		return 0
	fi
	topeet_host_print_report "$stage"
	if [ "${TOPEET_HOST_DEPS_DEFERRED:-0}" = 1 ] || \
		topeet_host_dependency_install_disabled; then
		topeet_host_note "stage=$stage deps=deferred action=continue-build"
		topeet_host_set_deferred
		return 0
	fi
	topeet_host_set_deferred "stage=$stage deps=unresolved"
	return 0
}

topeet_host_prepare_collected()
{
	local scope=$1 collector=$2
	shift 2

	topeet_host_can_prepare || return 0

	topeet_host_reset_requirements
	"$collector" "$@" || return $?
	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
		[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]; then
		topeet_host_note "stage=$scope deps=ready"
		return 0
	fi
	topeet_host_print_report "$scope"

	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -gt 0 ]; then
		if [ "${TOPEET_HOST_DEPS_DEFERRED:-0}" = 1 ] || \
			topeet_host_dependency_install_disabled; then
			topeet_host_note "stage=$scope deps=deferred action=continue-build"
			topeet_host_set_deferred
			return 0
		fi
		topeet_host_install_missing || {
			topeet_host_set_deferred "stage=$scope deps=unresolved"
			return 0
		}
	fi

	topeet_host_reset_requirements
	"$collector" "$@" || return $?
	if [ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
		[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]; then
		topeet_host_note "stage=$scope deps=installed"
		return 0
	fi
	topeet_host_print_report "$scope"
	if [ "${TOPEET_HOST_DEPS_DEFERRED:-0}" = 1 ] || \
		topeet_host_dependency_install_disabled; then
		topeet_host_note "stage=$scope deps=deferred action=continue-build"
		topeet_host_set_deferred
		return 0
	fi
	topeet_host_set_deferred "stage=$scope deps=unresolved"
	return 0
}

topeet_host_collect_kernel_with_config()
{
	topeet_host_collect_kernel
	topeet_host_collect_kernel_config "$1"
}

topeet_host_prepare_kernel_config()
{
	local config=$1

	[ -r "$config" ] || return 0
	topeet_host_init
	topeet_host_detect_os
	topeet_host_prepare_collected kernel-config \
		topeet_host_collect_kernel_with_config "$config"
}

topeet_host_collect_buildroot_with_config()
{
	local config=$1 buildroot_dir=$2 output_dir=$3

	topeet_host_collect_buildroot
	topeet_host_collect_buildroot_config "$config" "$buildroot_dir" "$output_dir"
}

topeet_host_prepare_buildroot_config()
{
	local config=$1 buildroot_dir=${2:-} output_dir=${3:-}

	[ -r "$config" ] || return 0
	topeet_host_init
	topeet_host_detect_os
	topeet_host_prepare_collected buildroot-config \
		topeet_host_collect_buildroot_with_config \
		"$config" "$buildroot_dir" "$output_dir"
}

topeet_host_collect_stages()
{
	local stage

	for stage in "$@"; do
		topeet_host_collect_stage "$stage" || return $?
	done
}

topeet_host_prepare_sequence()
{
	local scope=${1:-selected} options=${2:-} stage
	local -a stages=()

	TOPEET_HOST_SCOPE=$scope
	TOPEET_HOST_OPTIONS=$options
	case "$scope" in
		base) stages=(base) ;;
		all) stages=(base uboot kernel buildroot rootfs image security) ;;
		selected)
			stages=(base)
			for stage in uboot kernel buildroot rootfs image security; do
				topeet_host_stage_selected "$stage" "$options" || continue
				stages+=("$stage")
			done
			;;
		*)
			topeet_host_warn "status=invalid-scope scope=$scope"
			return 2
			;;
	esac

	topeet_host_init
	topeet_host_detect_os
	# Bootstrap gcc and the basic tools before probing headers in other stages.
	# Collect the remaining stages together so shared packages are installed
	# once, with one sudo/APT transaction for the selected build targets.
	if [ "${stages[0]}" = base ]; then
		topeet_host_prepare_stage base || return $?
		stages=("${stages[@]:1}")
	fi
	[ "${TOPEET_HOST_DEPS_DEFERRED:-0}" != 1 ] || return 0
	[ "${#stages[@]}" -gt 0 ] || return 0
	topeet_host_prepare_collected "$scope" topeet_host_collect_stages \
		"${stages[@]}" || return $?
	[ "${TOPEET_HOST_DEPS_DEFERRED:-0}" != 1 ] || return 0
	for stage in "${stages[@]}"; do
		topeet_host_prepare_stage_runtime "$stage" || {
			topeet_host_set_deferred "stage=$stage runtime=unavailable"
			return 0
		}
	done
}

topeet_host_preflight()
{
	local action=${1:-prepare} scope=${2:-selected} options=${3:-}

	case "$action" in
		doctor)
			topeet_host_sanitize_build_environment
			topeet_host_scan "${scope:-all}" "$options" || return $?
			topeet_host_print_report "${scope:-all}"
			[ "${#TOPEET_HOST_MISSING_IDS[@]}" -eq 0 ] && \
				[ "${#TOPEET_HOST_MANUAL_IDS[@]}" -eq 0 ]
			return
			;;
		demo)
			topeet_host_demo "${scope:-all}"
			return 0
			;;
		prepare)
			topeet_host_sanitize_build_environment
			;;
		*)
			topeet_host_warn "status=invalid-action action=$action"
			return 2
			;;
	esac

	topeet_host_prepare_sequence "$scope" "$options"
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
	action=${1:-doctor}
	scope=${2:-all}
	options=${3:-}
	case "$action" in
		doctor | prepare | demo) topeet_host_preflight "$action" "$scope" "$options" ;;
		*)
		printf 'Usage: %s [doctor|prepare|demo] [base|selected|all] [options]\n' "$0" >&2
			exit 2
			;;
	esac
fi
