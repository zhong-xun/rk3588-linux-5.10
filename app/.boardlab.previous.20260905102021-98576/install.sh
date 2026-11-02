#!/usr/bin/env bash
# Synchronize BoardLab into every discovered customer Buildroot SDK and the shared
# Ubuntu/Debian projects directory.

set -euo pipefail

readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
source "$PROJECT_ROOT/scripts/lib/boardlab-sdk-discovery.sh"
SOURCE_ROOT="${BOARDLAB_SOURCE_ROOT:-$PROJECT_ROOT}"
LINUX_ROOT="${TOPEET_LINUX_ROOT:-/home/topeet/Linux}"
ROOTFS_PROJECTS_ROOT="${TOPEET_ROOTFS_PROJECTS_ROOT:-/home/topeet/rootfs_build/ubuntu_debian/projects}"
MODE=dry-run

readonly -a EXCLUDED_SOURCE_ENTRIES=(
    .git
    .gitignore
    .agents
    .claude
    .codex
    .hallmark
    .playwright-cli
    build
    build-host-ui
    output
    tmp
)
readonly -a EXCLUDED_SOURCE_PREFIXES=(
    build-
    build_
    cmake-build-
)

readonly -a BUILDROOT_RUNTIME_PATHS=(
    package/rockchip/boardlab/00-boardlab-main.conf
    package/rockchip/boardlab/99-boardlab-wifi.conf
    package/rockchip/boardlab/99-boardlab.rules
    package/rockchip/boardlab/boardlab.env
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

declare -a DISCOVERED_SDKS=()

usage()
{
    cat <<EOF
Usage: $(basename "$0") [--apply|--dry-run]

Synchronize BoardLab into every SDK with a Buildroot BoardLab package below:
  $LINUX_ROOT

and into the Ubuntu/Debian project mirror:
  $ROOTFS_PROJECTS_ROOT/boardlab

The default mode is --dry-run. Use --apply to synchronize validated targets.

Environment overrides for controlled staging trees:
  BOARDLAB_SOURCE_ROOT
  TOPEET_LINUX_ROOT
  TOPEET_ROOTFS_PROJECTS_ROOT
EOF
}

fail()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || fail "required command is missing: $1"
}

canonical_existing_dir()
{
    local path="$1"

    [ -d "$path" ] || fail "required directory is missing: $path"
    (cd -- "$path" && pwd -P)
}

root_entry_is_excluded()
{
    local name="$1"
    local excluded
    local prefix

    for excluded in "${EXCLUDED_SOURCE_ENTRIES[@]}"; do
        [ "$name" != "$excluded" ] || return 0
    done
    for prefix in "${EXCLUDED_SOURCE_PREFIXES[@]}"; do
        case "$name" in
            "$prefix"*) return 0 ;;
        esac
    done
    return 1
}

collect_and_validate_sdks()
{
    local sdk_root
    local relative

    while IFS= read -r -d '' sdk_root; do
        [ -f "$sdk_root/buildroot/package/rockchip/boardlab/boardlab.mk" ] || continue
        relative="${sdk_root#"$LINUX_ROOT"/}"
        DISCOVERED_SDKS+=("$relative")
    done < <(boardlab_discover_sdk_roots "$LINUX_ROOT")

    [ "${#DISCOVERED_SDKS[@]}" -gt 0 ] || {
        fail "no SDK containing buildroot/package/rockchip/boardlab/boardlab.mk was found below $LINUX_ROOT"
    }
}

validate_buildroot_integration()
{
    local relative
    local buildroot
    local package_dir
    local sdk_root
    local resolved_buildroot
    local resolved_package_dir
    local canonical_runtime_path
    local runtime_path

    for relative in "${DISCOVERED_SDKS[@]}"; do
        sdk_root="$(canonical_existing_dir "$LINUX_ROOT/$relative")"
        buildroot="$sdk_root/buildroot"
        package_dir="$buildroot/package/rockchip/boardlab"

        resolved_buildroot="$(canonical_existing_dir "$buildroot")"
        case "$resolved_buildroot" in
            "$sdk_root"/*) ;;
            *) fail "SDK Buildroot directory escapes its SDK root: $buildroot -> $resolved_buildroot" ;;
        esac
        resolved_package_dir="$(canonical_existing_dir "$package_dir")"
        case "$resolved_package_dir" in
            "$resolved_buildroot"/*) ;;
            *) fail "BoardLab package directory escapes its Buildroot tree: $resolved_package_dir" ;;
        esac

        [ -f "$package_dir/Config.in" ] || fail "BoardLab Config.in is missing: $package_dir/Config.in"
        [ -f "$package_dir/boardlab.mk" ] || fail "BoardLab package is missing: $package_dir/boardlab.mk"
        rg -Fq 'source "package/rockchip/boardlab/Config.in"' "$buildroot/package/rockchip/Config.in" || {
            fail "Buildroot menu does not include BoardLab: $buildroot/package/rockchip/Config.in"
        }
        rg -Fq 'BOARDLAB_SITE = $(TOPDIR)/../app/boardlab' "$package_dir/boardlab.mk" || {
            fail "BoardLab package does not use the SDK-local source: $package_dir/boardlab.mk"
        }

        for runtime_path in "${BUILDROOT_RUNTIME_PATHS[@]}"; do
            canonical_runtime_path="$SOURCE_ROOT/packaging/buildroot/$runtime_path"
            [ -f "$canonical_runtime_path" ] || {
                fail "canonical Buildroot runtime file is missing: $canonical_runtime_path"
            }
        done
    done
}

validate_materialized_tree()
{
    local target="$1"
    local entry
    local name
    local link
    local parent
    local resolved

    [ -d "$target" ] && [ ! -L "$target" ] || fail "target is not an independent directory: $target"
    parent="$(canonical_existing_dir "$(dirname -- "$target")")"
    case "$target" in
        "$ROOTFS_PROJECTS_ROOT/boardlab")
            [ "$parent" = "$ROOTFS_PROJECTS_ROOT" ] || fail "rootfs target parent escapes its project root: $parent"
            ;;
        "$LINUX_ROOT"/*/app/boardlab|"$LINUX_ROOT"/*/*/app/boardlab)
            case "$parent" in
                "$LINUX_ROOT"/*) ;;
                *) fail "SDK target parent escapes the Linux root: $parent" ;;
            esac
            ;;
        *) fail "unexpected materialized target location: $target" ;;
    esac
    [ -f "$target/CMakeLists.txt" ] || fail "target is incomplete: $target"
    [ -f "$target/VERSION" ] || fail "target has no VERSION: $target"
    [ -x "$target/install.sh" ] || fail "target has no executable install.sh: $target"

    while IFS= read -r -d '' entry; do
        name="$(basename -- "$entry")"
        root_entry_is_excluded "$name" && {
            fail "excluded development entry was copied: $entry"
        }
    done < <(find "$target" -mindepth 1 -maxdepth 1 -print0)

    while IFS= read -r -d '' link; do
        resolved="$(readlink -f -- "$link" 2>/dev/null || true)"
        case "$resolved" in
            "$SOURCE_ROOT"|"$SOURCE_ROOT"/*)
                fail "target contains a link back to the development source: $link"
                ;;
        esac
    done < <(find "$target" -type l -print0)
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --apply) MODE=apply ;;
        --dry-run) MODE=dry-run ;;
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

for command_name in find readlink rg sort; do
    require_command "$command_name"
done

SOURCE_ROOT="$(canonical_existing_dir "$SOURCE_ROOT")"
LINUX_ROOT="$(canonical_existing_dir "$LINUX_ROOT")"
ROOTFS_PROJECTS_ROOT="$(canonical_existing_dir "$ROOTFS_PROJECTS_ROOT")"

SYNC_SCRIPT="$SOURCE_ROOT/scripts/sync-sdk-boardlab.sh"
RUNTIME_SYNC="$SOURCE_ROOT/scripts/sync-buildroot-boardlab-runtime.sh"
[ -x "$SYNC_SCRIPT" ] || fail "source sync helper is missing or not executable: $SYNC_SCRIPT"
[ -x "$RUNTIME_SYNC" ] || fail "runtime sync helper is missing or not executable: $RUNTIME_SYNC"

collect_and_validate_sdks
validate_buildroot_integration

printf 'BoardLab source: %s\n' "$SOURCE_ROOT"
printf 'mode: %s\n' "$MODE"
printf 'validated Buildroot SDKs: %d\n' "${#DISCOVERED_SDKS[@]}"

BOARDLAB_SOURCE_ROOT="$SOURCE_ROOT" \
    TOPEET_LINUX_ROOT="$LINUX_ROOT" \
    TOPEET_ROOTFS_PROJECTS_ROOT="$ROOTFS_PROJECTS_ROOT" \
    "$SYNC_SCRIPT" "--$MODE"

if [ "$MODE" = dry-run ]; then
    printf 'dry-run complete: no targets were changed\n'
    exit 0
fi

validate_materialized_tree "$ROOTFS_PROJECTS_ROOT/boardlab"
for relative in "${DISCOVERED_SDKS[@]}"; do
    validate_materialized_tree "$LINUX_ROOT/$relative/app/boardlab"
done

BOARDLAB_BUILDROOT_TEMPLATE_ROOT="$SOURCE_ROOT/packaging/buildroot" \
    TOPEET_LINUX_ROOT="$LINUX_ROOT" \
    "$RUNTIME_SYNC" --check

printf 'sync verified: %d Buildroot SDKs and 1 Ubuntu/Debian project mirror\n' \
    "${#DISCOVERED_SDKS[@]}"
