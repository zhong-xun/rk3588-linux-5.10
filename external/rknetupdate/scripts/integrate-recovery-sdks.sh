#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TEMPLATE_DIR="$PROJECT_DIR/packaging/recovery"
NOTICE_TEMPLATE_DIR="$TEMPLATE_DIR/sdk/recovery-notice"
LINUX_ROOT="/home/topeet/Linux"
MODE=check
NOTICE_ONLY=false
declare -a REQUESTED_SDKS=()

usage()
{
	cat <<'EOF'
Usage: integrate-recovery-sdks.sh [--check|--apply] [OPTIONS]

Integrate the managed RKNetUpdate recovery into standard single-slot Rockchip
SDKs. The script deliberately does not discover *_extboot_* trees.

Options:
  --check               Verify integration without changing files (default)
  --apply               Apply/update the managed files idempotently
  --notice-only         Only manage external/recovery completion notices
  --linux-root PATH     Rockchip SDK collection (default: /home/topeet/Linux)
  --sdk PATH            Process one SDK root; may be supplied more than once
  -h, --help            Show this help
EOF
}

while (($#)); do
	case "$1" in
		--check) MODE=check; shift ;;
		--apply) MODE=apply; shift ;;
		--notice-only) NOTICE_ONLY=true; shift ;;
		--linux-root)
			(($# >= 2)) || { echo "missing value for --linux-root" >&2; exit 2; }
			LINUX_ROOT="$2"; shift 2 ;;
		--sdk)
			(($# >= 2)) || { echo "missing value for --sdk" >&2; exit 2; }
			REQUESTED_SDKS+=("$2"); shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

for file in \
	"$TEMPLATE_DIR/common/etc/init.d/S39rknetupdate-rootfs" \
	"$TEMPLATE_DIR/common/etc/init.d/S40recovery" \
	"$TEMPLATE_DIR/common/etc/rknetupdate-recovery" \
	"$TEMPLATE_DIR/sdk/post-rknetupdate-recovery.sh" \
	"$NOTICE_TEMPLATE_DIR/recovery_notice.c" \
	"$NOTICE_TEMPLATE_DIR/recovery_notice.h"; do
	[ -r "$file" ] || { echo "missing integration template: $file" >&2; exit 1; }
done
sh -n "$TEMPLATE_DIR/common/etc/init.d/S39rknetupdate-rootfs"
sh -n "$TEMPLATE_DIR/common/etc/init.d/S40recovery"
bash -n "$TEMPLATE_DIR/sdk/post-rknetupdate-recovery.sh"
bash -n "$PROJECT_DIR/scripts/install-sdk-rknetupdate.sh"

declare -a SDKS=()
if ((${#REQUESTED_SDKS[@]})); then
	SDKS=("${REQUESTED_SDKS[@]}")
else
	while IFS= read -r sdk; do
		SDKS+=("$sdk")
	done < <(find "$LINUX_ROOT" -mindepth 2 -maxdepth 2 -type d \
		\( -name 'rk*-linux-5.10' -o -name 'rk*-linux-6.1' \) | sort)
fi

((${#SDKS[@]})) || { echo "no standard Rockchip SDKs found" >&2; exit 1; }

if [ "$NOTICE_ONLY" = false ]; then
	declare -a COMPONENT_ARGS=("--$MODE")
	for sdk in "${SDKS[@]}"; do
		COMPONENT_ARGS+=(--sdk "$sdk")
	done
	"$PROJECT_DIR/scripts/install-sdk-rknetupdate.sh" "${COMPONENT_ARGS[@]}"
fi

same_file()
{
	[ -f "$2" ] && cmp -s "$1" "$2"
}

managed_overlay()
{
	[ -r "$1/etc/rknetupdate-recovery" ] &&
		grep -qx 'rknetupdate-recovery-v1' "$1/etc/rknetupdate-recovery"
}

install_managed_file()
{
	local src="$1" dst="$2" mode="$3" overlay="$4"
	if same_file "$src" "$dst"; then
		return 0
	fi
	if [ -e "$dst" ] && ! managed_overlay "$overlay"; then
		echo "refusing to replace unmanaged file: $dst" >&2
		return 1
	fi
	if [ "$MODE" = apply ]; then
		install -D -m "$mode" "$src" "$dst"
	else
		echo "missing or stale managed file: $dst" >&2
		return 1
	fi
}

managed_recovery_notice()
{
	local file="$1" kind="$2"
	grep -Fqx ' * RKNETUPDATE-MANAGED-RECOVERY-NOTICE-V1' "$file" || return 1
	case "$kind" in
		c)
			grep -Fqx '#include "recovery_notice.h"' "$file" &&
			grep -Fq 'recovery_notice_wait_for_removal' "$file"
			;;
		h)
			grep -Fqx '#ifndef RECOVERY_NOTICE_H' "$file" &&
			grep -Fq 'recovery_notice_wait_for_removal' "$file"
			;;
		*) return 1 ;;
	esac
}

install_recovery_notice_file()
{
	local src="$1" dst="$2" mode="$3" kind="$4"
	if same_file "$src" "$dst"; then
		return 0
	fi
	if [ -e "$dst" ] && ! managed_recovery_notice "$dst" "$kind"; then
		# The first managed rollout used an unmarked v1 file. It is safe to
		# replace only when it still has the recognizable recovery-notice API;
		# any other local implementation remains owned by the SDK maintainer.
		case "$kind" in
			c)
				grep -Fqx '#include "recovery_notice.h"' "$dst" 2>/dev/null &&
					grep -Fq 'recovery_notice_wait_for_removal' "$dst" 2>/dev/null
				;;
			h)
				grep -Fqx '#ifndef RECOVERY_NOTICE_H' "$dst" 2>/dev/null &&
					grep -Fq 'recovery_notice_wait_for_removal' "$dst" 2>/dev/null
				;;
			*) return 1 ;;
		esac || {
			echo "refusing to replace unmanaged recovery notice: $dst" >&2
			return 1
		}
	fi
	if [ "$MODE" = apply ]; then
		install -D -m "$mode" "$src" "$dst"
	else
		echo "missing or stale managed recovery notice: $dst" >&2
		return 1
	fi
}

ensure_recovery_notice_makefile()
{
	local file="$1" count tmp
	count="$(grep -Ec '^[[:space:]]*recovery_notice\.o[[:space:]]*\\?$' "$file" || true)"
	case "$count" in
		1) return 0 ;;
		0) ;;
		*) echo "duplicate recovery_notice.o entry: $file" >&2; return 1 ;;
	esac
	if [ "$MODE" != apply ]; then
		echo "missing recovery_notice.o entry: $file" >&2
		return 1
	fi
	tmp="$(mktemp "${file}.rknetupdate.XXXXXX")"
	if ! awk '
		BEGIN { inserted = 0 }
		/^OBJ = recovery\.o[[:space:]]*\\$/ && !inserted {
			print
			print "\trecovery_notice.o \\"
			inserted = 1
			next
		}
		{ print }
		END { if (!inserted) exit 1 }
	' "$file" > "$tmp"; then
		rm -f "$tmp"
		echo "unable to locate recovery object list: $file" >&2
		return 1
	fi
	chmod --reference="$file" "$tmp"
	mv "$tmp" "$file"
}

ensure_recovery_notice_calls()
{
	local file="$1" include_count call_count
	include_count="$(grep -Fc '#include "recovery_notice.h"' "$file" || true)"
	call_count="$(grep -Fc 'recovery_notice_wait_for_removal(' "$file" || true)"
	if [ "$include_count" -ne 1 ] || [ "$call_count" -ne 2 ] ||
		! grep -Fq 'recovery_notice_wait_for_removal("SD card", sd_device)' "$file" ||
		! grep -Fq 'recovery_notice_wait_for_removal("USB drive", udisk_device)' "$file"; then
		echo "recovery notice call sites are missing or ambiguous: $file" >&2
		echo "preserving SDK-specific recovery.c; integrate its two verified media paths before retrying" >&2
		return 1
	fi
}

ensure_config()
{
	local file="$1" option="$2" count
	count="$(grep -c "^${option}=y$" "$file" || true)"
	case "$count" in
		1) return 0 ;;
		0)
			if [ "$MODE" = apply ]; then
				printf '\n%s=y\n' "$option" >> "$file"
				return 0
			fi
			echo "missing ${option}=y in $file" >&2
			return 1 ;;
		*) echo "duplicate ${option}=y in $file" >&2; return 1 ;;
	esac
}

ensure_gptfdisk_cxx()
{
	local file="$1" default_count static_count legacy_count tmp
	default_count="$(grep -Fc 'GPTFDISK_CXX = $(TARGET_CXX)' "$file" || true)"
	static_count="$(grep -Fc 'GPTFDISK_CXX += -static' "$file" || true)"
	legacy_count="$(grep -Fc 'GPTFDISK_CXX += $(TARGET_CXX) -static' "$file" || true)"

	if [ "$default_count" -eq 1 ] && [ "$static_count" -eq 1 ] && \
		[ "$legacy_count" -eq 0 ]; then
		return 0
	fi

	if [ "$MODE" != apply ]; then
		echo "missing RKNetUpdate gptfdisk CXX compatibility fix: $file" >&2
		return 1
	fi
	if [ "$default_count" -ne 0 ] || [ "$static_count" -ne 0 ] || \
		[ "$legacy_count" -ne 1 ]; then
		echo "refusing unknown gptfdisk CXX layout: $file" >&2
		return 1
	fi

	# Several vendor Buildroot snapshots leave GPTFDISK_CXX empty for shared
	# builds. Enabling SGDISK then tries to execute the first -D compiler flag.
	# Keep the compiler unconditional and add only -static in static builds.
	tmp="$(mktemp "${file}.rknetupdate.XXXXXX")"
	if ! awk '
		BEGIN { inserted = 0; replaced = 0 }
		/^ifneq \(\$\(BR2_STATIC_LIBS\)\$\(BR2_PACKAGE_GPTFDISK_STATIC\),\)$/ && !inserted {
			print "GPTFDISK_CXX = $(TARGET_CXX)"
			print ""
			inserted = 1
		}
		/^GPTFDISK_CXX \+= \$\(TARGET_CXX\) -static$/ {
			print "GPTFDISK_CXX += -static"
			replaced = 1
			next
		}
		{ print }
		END { if (!inserted || !replaced) exit 1 }
	' "$file" > "$tmp"; then
		rm -f "$tmp"
		echo "failed to patch gptfdisk CXX compatibility: $file" >&2
		return 1
	fi
	chmod --reference="$file" "$tmp"
	mv "$tmp" "$file"
}

sdk_recovery_defconfig()
{
	local sdk="$1" product chip
	product="$(basename "$(dirname "$sdk")")"
	case "$product" in
		rk3562) chip=rk3562 ;;
		rk3568|rk3568_mini) chip=rk3568 ;;
		rk3576) chip=rk3576 ;;
		rk3588|rk3588_mini|rk3588s) chip=rk3588 ;;
		*) echo "unsupported SDK product directory: $product" >&2; return 1 ;;
	esac
	printf '%s/buildroot/configs/rockchip_%s_recovery_defconfig\n' "$sdk" "$chip"
}

apply_sdk()
{
	local sdk="$1" common post_overlay overlay defconfig gptfdisk_mk hook_script hook_link layout recovery_dir
	sdk="$(realpath "$sdk")"
	recovery_dir="$sdk/external/recovery"
	for required in "$recovery_dir/Makefile" "$recovery_dir/recovery.c"; do
		[ -f "$required" ] || {
			echo "not a Recovery SDK ($required is missing): $sdk" >&2
			return 1
		}
	done
	ensure_recovery_notice_makefile "$recovery_dir/Makefile" || return 1
	ensure_recovery_notice_calls "$recovery_dir/recovery.c" || return 1
	install_recovery_notice_file "$NOTICE_TEMPLATE_DIR/recovery_notice.c" \
		"$recovery_dir/recovery_notice.c" 0644 c || return 1
	install_recovery_notice_file "$NOTICE_TEMPLATE_DIR/recovery_notice.h" \
		"$recovery_dir/recovery_notice.h" 0644 h || return 1

	if [ "$NOTICE_ONLY" = true ]; then
		return 0
	fi

	common="$sdk/device/rockchip/common"
	post_overlay="$common/scripts/post-overlay.sh"
	for required in "$sdk/build.sh" "$sdk/buildroot" \
		"$post_overlay" "$common/post-hooks"; do
		[ -e "$required" ] || {
			echo "not a standard Recovery SDK ($required is missing): $sdk" >&2
			return 1
		}
	done
	# Rockchip 6.1 installs every child of overlays/recovery as an independent
	# overlay. Rockchip 5.10 installs overlays/recovery itself.
	if grep -q 'install_overlays.*\$POST_OS' "$post_overlay"; then
		layout=nested
		overlay="$common/overlays/recovery/rknetupdate-rootfs"
	elif grep -q 'install_overlay.*\$POST_OS' "$post_overlay"; then
		layout=flat
		overlay="$common/overlays/recovery"
	else
		echo "unknown Recovery overlay framework: $post_overlay" >&2
		return 1
	fi

	defconfig="$(sdk_recovery_defconfig "$sdk")"
	[ -r "$defconfig" ] || { echo "missing Recovery defconfig: $defconfig" >&2; return 1; }
	ensure_config "$defconfig" BR2_PACKAGE_GPTFDISK || return 1
	ensure_config "$defconfig" BR2_PACKAGE_GPTFDISK_SGDISK || return 1
	gptfdisk_mk="$sdk/buildroot/package/gptfdisk/gptfdisk.mk"
	[ -r "$gptfdisk_mk" ] || { echo "missing gptfdisk package: $gptfdisk_mk" >&2; return 1; }
	ensure_gptfdisk_cxx "$gptfdisk_mk" || return 1

	install_managed_file "$TEMPLATE_DIR/common/etc/init.d/S39rknetupdate-rootfs" \
		"$overlay/etc/init.d/S39rknetupdate-rootfs" 0755 "$overlay" || return 1
	install_managed_file "$TEMPLATE_DIR/common/etc/init.d/S40recovery" \
		"$overlay/etc/init.d/S40recovery" 0755 "$overlay" || return 1
	install_managed_file "$TEMPLATE_DIR/common/etc/rknetupdate-recovery" \
		"$overlay/etc/rknetupdate-recovery" 0644 "$overlay" || return 1

	hook_script="$common/scripts/post-rknetupdate-recovery.sh"
	if ! same_file "$TEMPLATE_DIR/sdk/post-rknetupdate-recovery.sh" "$hook_script"; then
		if [ -e "$hook_script" ] && \
			! grep -q '^# RKNETUPDATE-MANAGED-RECOVERY-HOOK-V1$' "$hook_script"; then
			echo "refusing to replace unmanaged helper hook: $hook_script" >&2
			return 1
		fi
		if [ "$MODE" = apply ]; then
			install -m 0755 "$TEMPLATE_DIR/sdk/post-rknetupdate-recovery.sh" "$hook_script"
		else
			echo "missing helper hook: $hook_script" >&2
			return 1
		fi
	fi

	hook_link="$common/post-hooks/92-rknetupdate-recovery.sh"
	if [ -L "$hook_link" ] && [ "$(readlink "$hook_link")" = '../scripts/post-rknetupdate-recovery.sh' ]; then
		:
	elif [ -e "$hook_link" ] || [ -L "$hook_link" ]; then
		echo "refusing unexpected post-hook entry: $hook_link" >&2
		return 1
	elif [ "$MODE" = apply ]; then
		ln -s ../scripts/post-rknetupdate-recovery.sh "$hook_link"
	else
		echo "missing post-hook link: $hook_link" >&2
		return 1
	fi

	printf 'OK\t%s\t%s\t%s\n' "$layout" "$(basename "$defconfig")" "$sdk"
}

failures=0
for sdk in "${SDKS[@]}"; do
	if ! apply_sdk "$sdk"; then
		failures=$((failures + 1))
	fi
done

if ((failures)); then
	echo "$failures SDK integration(s) failed" >&2
	exit 1
fi

echo "RKNetUpdate Recovery $MODE completed for ${#SDKS[@]} standard SDK(s)."
