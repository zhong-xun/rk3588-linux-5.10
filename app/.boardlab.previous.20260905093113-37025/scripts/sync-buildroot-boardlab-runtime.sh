#!/usr/bin/env bash
# Synchronize the canonical BoardLab boot scripts into every active SDK.

set -euo pipefail

readonly PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"
readonly TEMPLATE_ROOT="${BOARDLAB_BUILDROOT_TEMPLATE_ROOT:-$PROJECT_ROOT/packaging/buildroot}"
readonly LINUX_ROOT="${TOPEET_LINUX_ROOT:-/home/topeet/Linux}"

readonly -a RUNTIME_PATHS=(
    package/rockchip/boardlab/boardlab-supervisor
    package/rockchip/boardlab/S49weston-boardlab
    board/rockchip/common/overlays/90-boardlab/etc/init.d/S49weston
)

MODE=dry-run
declare -a SDK_ROOTS=()
pending=0

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

for command_name in find sort cmp install sh; do
    command -v "$command_name" >/dev/null 2>&1 || fail "required command is missing: $command_name"
done

[ -d "$TEMPLATE_ROOT" ] || fail "template root is missing: $TEMPLATE_ROOT"
[ -d "$LINUX_ROOT" ] || fail "Linux SDK root is missing: $LINUX_ROOT"

for relative_path in "${RUNTIME_PATHS[@]}"; do
    source_path="$TEMPLATE_ROOT/$relative_path"
    [ -f "$source_path" ] || fail "canonical runtime script is missing: $source_path"
    sh -n "$source_path" || fail "canonical runtime script has invalid syntax: $source_path"
done

while IFS= read -r -d '' sdk_root; do
    SDK_ROOTS+=("$sdk_root")
done < <(
    find "$LINUX_ROOT" -mindepth 1 -maxdepth 2 -type d \
        \( -name '*-linux-*' -o -name 'topeet-extboot-sdk-*' \) -print0 | sort -z
)

[ "${#SDK_ROOTS[@]}" -gt 0 ] || fail "no active SDK directories were found below $LINUX_ROOT"

printf 'mode: %s\n' "$MODE"
printf 'canonical templates: %s\n' "$TEMPLATE_ROOT"
printf 'active SDKs: %s\n' "${#SDK_ROOTS[@]}"

for sdk_root in "${SDK_ROOTS[@]}"; do
    [ -d "$sdk_root/buildroot" ] || fail "SDK has no Buildroot tree: $sdk_root"
    sdk_pending=0

    for relative_path in "${RUNTIME_PATHS[@]}"; do
        source_path="$TEMPLATE_ROOT/$relative_path"
        target_path="$sdk_root/buildroot/$relative_path"
        [ -f "$target_path" ] || fail "SDK runtime script is missing: $target_path"

        if ! cmp -s "$source_path" "$target_path"; then
            sdk_pending=$((sdk_pending + 1))
            pending=$((pending + 1))
            printf 'update: %s\n' "$target_path"
            if [ "$MODE" = apply ]; then
                install -m 0755 "$source_path" "$target_path"
                cmp -s "$source_path" "$target_path" || fail "verification failed after update: $target_path"
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
