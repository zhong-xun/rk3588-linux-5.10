#!/usr/bin/env bash
# Synchronize canonical BoardLab package/runtime files into every active SDK.

set -euo pipefail

readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "$PROJECT_ROOT/scripts/lib/boardlab-sdk-discovery.sh"
readonly TEMPLATE_ROOT="${BOARDLAB_BUILDROOT_TEMPLATE_ROOT:-$PROJECT_ROOT/packaging/buildroot}"
readonly LINUX_ROOT="${TOPEET_LINUX_ROOT:-/home/topeet/Linux}"

readonly -a RUNTIME_PATHS=(
    package/rockchip/boardlab/Config.in
    package/rockchip/boardlab/boardlab.mk
    package/rockchip/boardlab/boardlab.env
    package/rockchip/boardlab/00-boardlab-main.conf
    package/rockchip/boardlab/99-boardlab-wifi.conf
    package/rockchip/boardlab/99-boardlab.rules
    package/rockchip/boardlab/weston-boardlab.ini
    package/rockchip/boardlab/weston-desktop.ini
    package/rockchip/boardlab/boardlab-npu-load
    package/rockchip/boardlab/boardlab-supervisor
    package/rockchip/boardlab/S43wpa_supplicant
    package/rockchip/boardlab/S46boardlab-net-prep
    package/rockchip/boardlab/S49weston-boardlab
    package/rockchip/boardlab/S60boardlab
    board/rockchip/common/overlays/90-boardlab/etc/init.d/S49weston
)

MODE=dry-run
declare -a SDK_ROOTS=()
pending=0
RUN_ID="$(date +%Y%m%d%H%M%S)-$$"

usage()
{
    cat <<EOF
Usage: $(basename "$0") [--dry-run|--check|--apply]

Synchronize BoardLab Buildroot runtime scripts from:
  $TEMPLATE_ROOT

into all active SDK directories below:
  $LINUX_ROOT

Environment overrides:
  BOARDLAB_BUILDROOT_TEMPLATE_ROOT
  TOPEET_LINUX_ROOT
EOF
}

fail()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

canonical_existing_dir()
{
    local path="$1"

    [ -d "$path" ] || fail "required directory is missing: $path"
    (cd -- "$path" && pwd -P)
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run) MODE=dry-run ;;
        --check) MODE=check ;;
        --apply) MODE=apply ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            usage >&2
            fail "unknown option: $1"
            ;;
    esac
    shift
done

for command_name in find sort cmp install sh stat readlink dirname; do
    command -v "$command_name" >/dev/null 2>&1 || fail "required command is missing: $command_name"
done

[ -d "$TEMPLATE_ROOT" ] || fail "template root is missing: $TEMPLATE_ROOT"
[ -d "$LINUX_ROOT" ] || fail "Linux SDK root is missing: $LINUX_ROOT"
CANONICAL_LINUX_ROOT="$(canonical_existing_dir "$LINUX_ROOT")"

for relative_path in "${RUNTIME_PATHS[@]}"; do
    source_path="$TEMPLATE_ROOT/$relative_path"
    [ -f "$source_path" ] || fail "canonical runtime file is missing: $source_path"
    case "$relative_path" in
        *.mk|*/Config.in|*.ini|*.conf|*.rules)
            ;;
        *)
            sh -n "$source_path" || fail "canonical runtime script has invalid syntax: $source_path"
            ;;
    esac
done

while IFS= read -r -d '' sdk_root; do
    SDK_ROOTS+=("$sdk_root")
done < <(boardlab_discover_sdk_roots "$CANONICAL_LINUX_ROOT")

[ "${#SDK_ROOTS[@]}" -gt 0 ] || fail "no active SDK directories were found below $LINUX_ROOT"

printf 'mode: %s\n' "$MODE"
printf 'canonical templates: %s\n' "$TEMPLATE_ROOT"
printf 'active SDKs: %s\n' "${#SDK_ROOTS[@]}"

for sdk_root in "${SDK_ROOTS[@]}"; do
    sdk_root="$(canonical_existing_dir "$sdk_root")"
    case "$sdk_root" in
        "$CANONICAL_LINUX_ROOT"/*) ;;
        *) fail "SDK root escapes Linux root: $sdk_root" ;;
    esac
    resolved_buildroot="$(canonical_existing_dir "$sdk_root/buildroot")"
    case "$resolved_buildroot" in
        "$sdk_root"/*) ;;
        *) fail "SDK Buildroot directory escapes its SDK root: $sdk_root/buildroot -> $resolved_buildroot" ;;
    esac
    sdk_pending=0

    for relative_path in "${RUNTIME_PATHS[@]}"; do
        source_path="$TEMPLATE_ROOT/$relative_path"
        target_path="$sdk_root/buildroot/$relative_path"
        resolved_target_parent="$(readlink -m -- "$(dirname -- "$target_path")")"
        resolved_target_path="$(readlink -m -- "$target_path")"
        case "$resolved_target_parent" in
            "$resolved_buildroot"|"$resolved_buildroot"/*) ;;
            *) fail "runtime target parent escapes its Buildroot tree: $target_path -> $resolved_target_parent" ;;
        esac
        case "$resolved_target_path" in
            "$resolved_buildroot"/*) ;;
            *) fail "runtime target escapes its Buildroot tree: $target_path -> $resolved_target_path" ;;
        esac
        case "$relative_path" in
            *.mk|*/Config.in|*.env|*.ini|*.conf|*.rules) install_mode=0644 ;;
            *) install_mode=0755 ;;
        esac
        target_mode=missing
        if [ -f "$target_path" ]; then
            target_mode=$(stat -c '%a' "$target_path" 2>/dev/null || printf unknown)
        fi

        if ! cmp -s "$source_path" "$target_path" ||
           [ "$target_mode" != "${install_mode#0}" ]; then
            sdk_pending=$((sdk_pending + 1))
            pending=$((pending + 1))
            printf 'update: %s\n' "$target_path"
            if [ "$MODE" = apply ]; then
                if [ -e "$target_path" ] || [ -L "$target_path" ]; then
                    backup_path="$sdk_root/.boardlab-runtime.previous.$RUN_ID/$relative_path"
                    mkdir -p -- "$(dirname -- "$backup_path")"
                    cp -a -- "$target_path" "$backup_path"
                fi
                install -D -m "$install_mode" "$source_path" "$target_path"
                cmp -s "$source_path" "$target_path" || fail "verification failed after update: $target_path"
                [ "$(stat -c '%a' "$target_path")" = "${install_mode#0}" ] ||
                    fail "mode verification failed after update: $target_path"
            fi
        fi
    done

    if [ "$sdk_pending" -eq 0 ]; then
        printf 'match: %s\n' "$sdk_root"
    fi
done

case "$MODE" in
    check)
        [ "$pending" -eq 0 ] || fail "$pending runtime script copies differ from the canonical templates"
        ;;
    apply)
        printf 'sync complete: %s runtime script copies updated\n' "$pending"
        ;;
    dry-run)
        printf 'dry-run complete: %s runtime script copies would be updated\n' "$pending"
        ;;
esac
