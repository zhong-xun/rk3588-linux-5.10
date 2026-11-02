#!/usr/bin/env bash
# Synchronize BoardLab into the 14 customer Buildroot SDKs and the shared
# Ubuntu/Debian projects directory.

set -euo pipefail

readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
readonly EXPECTED_SDK_COUNT=14

SOURCE_ROOT="${BOARDLAB_SOURCE_ROOT:-$PROJECT_ROOT}"
LINUX_ROOT="${TOPEET_LINUX_ROOT:-/home/topeet/Linux}"
ROOTFS_PROJECTS_ROOT="${TOPEET_ROOTFS_PROJECTS_ROOT:-/home/topeet/rootfs_build/ubuntu_debian/projects}"
MODE=apply

readonly -a EXPECTED_SDKS=(
    rk3562/rk3562-linux-5.10
    rk3562/rk3562-linux-6.1
    rk3568/rk3568-linux-5.10
    rk3568/rk3568-linux-6.1
    rk3568_mini/rk3568-linux-5.10
    rk3568_mini/rk3568-linux-6.1
    rk3576/rk3576-linux-6.1
    rk3576_lga/rk3576-linux-6.1
    rk3588/rk3588-linux-5.10
    rk3588/rk3588-linux-6.1
    rk3588_mini/rk3588-linux-5.10
    rk3588_mini/rk3588-linux-6.1
    rk3588s/rk3588s-linux-5.10
    rk3588s/rk3588s-linux-6.1
)

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

readonly -a BUILDROOT_RUNTIME_PATHS=(
    package/rockchip/boardlab/boardlab-supervisor
    package/rockchip/boardlab/S49weston-boardlab
    board/rockchip/common/overlays/90-boardlab/etc/init.d/S49weston
)

declare -a DISCOVERED_SDKS=()

usage()
{
    cat <<EOF
Usage: $(basename "$0") [--apply|--dry-run]

Synchronize BoardLab into exactly 14 customer Buildroot SDKs below:
  $LINUX_ROOT

and into the Ubuntu/Debian project mirror:
  $ROOTFS_PROJECTS_ROOT/boardlab

The default mode is --apply. Use --dry-run to validate and preview without
changing any target.

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

is_expected_sdk()
{
    local candidate="$1"
    local expected

    for expected in "${EXPECTED_SDKS[@]}"; do
        [ "$candidate" != "$expected" ] || return 0
    done
    return 1
}

collect_and_validate_sdks()
{
    local sdk_root
    local relative
    local expected

    while IFS= read -r -d '' sdk_root; do
        [ -f "$sdk_root/buildroot/package/rockchip/boardlab/boardlab.mk" ] || continue
        relative="${sdk_root#"$LINUX_ROOT"/}"
        DISCOVERED_SDKS+=("$relative")
    done < <(
        find "$LINUX_ROOT" -mindepth 2 -maxdepth 2 -type d -name '*-linux-*' -print0 | sort -z
    )

    [ "${#EXPECTED_SDKS[@]}" -eq "$EXPECTED_SDK_COUNT" ] || {
        fail "internal SDK list does not contain $EXPECTED_SDK_COUNT entries"
    }
    [ "${#DISCOVERED_SDKS[@]}" -eq "$EXPECTED_SDK_COUNT" ] || {
        fail "expected $EXPECTED_SDK_COUNT BoardLab SDKs, found ${#DISCOVERED_SDKS[@]} below $LINUX_ROOT"
    }

    for expected in "${EXPECTED_SDKS[@]}"; do
        [ -d "$LINUX_ROOT/$expected" ] || fail "expected SDK is missing: $LINUX_ROOT/$expected"
    done
    for relative in "${DISCOVERED_SDKS[@]}"; do
        is_expected_sdk "$relative" || fail "unexpected BoardLab SDK found: $LINUX_ROOT/$relative"
    done
}

validate_buildroot_integration()
{
    local relative
    local buildroot
    local package_dir
    local runtime_path

    for relative in "${EXPECTED_SDKS[@]}"; do
        buildroot="$LINUX_ROOT/$relative/buildroot"
        package_dir="$buildroot/package/rockchip/boardlab"

        [ -f "$package_dir/Config.in" ] || fail "BoardLab Config.in is missing: $package_dir/Config.in"
        [ -f "$package_dir/boardlab.mk" ] || fail "BoardLab package is missing: $package_dir/boardlab.mk"
        rg -Fq 'source "package/rockchip/boardlab/Config.in"' "$buildroot/package/rockchip/Config.in" || {
            fail "Buildroot menu does not include BoardLab: $buildroot/package/rockchip/Config.in"
        }
        rg -Fq 'BOARDLAB_SITE = $(TOPDIR)/../app/boardlab' "$package_dir/boardlab.mk" || {
            fail "BoardLab package does not use the SDK-local source: $package_dir/boardlab.mk"
        }

        for runtime_path in "${BUILDROOT_RUNTIME_PATHS[@]}"; do
            [ -f "$buildroot/$runtime_path" ] || {
                fail "Buildroot runtime file is missing: $buildroot/$runtime_path"
            }
        done
    done
}

validate_materialized_tree()
{
    local target="$1"
    local excluded
    local link
    local resolved

    [ -d "$target" ] && [ ! -L "$target" ] || fail "target is not an independent directory: $target"
    [ -f "$target/CMakeLists.txt" ] || fail "target is incomplete: $target"
    [ -f "$target/VERSION" ] || fail "target has no VERSION: $target"
    [ -x "$target/install.sh" ] || fail "target has no executable install.sh: $target"

    for excluded in "${EXCLUDED_SOURCE_ENTRIES[@]}"; do
        if [ -e "$target/$excluded" ] || [ -L "$target/$excluded" ]; then
            fail "excluded development entry was copied: $target/$excluded"
        fi
    done

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
for relative in "${EXPECTED_SDKS[@]}"; do
    validate_materialized_tree "$LINUX_ROOT/$relative/app/boardlab"
done

BOARDLAB_BUILDROOT_TEMPLATE_ROOT="$SOURCE_ROOT/packaging/buildroot" \
    TOPEET_LINUX_ROOT="$LINUX_ROOT" \
    "$RUNTIME_SYNC" --check

printf 'sync verified: all discovered Buildroot SDKs and 1 Ubuntu/Debian project mirror\n'
