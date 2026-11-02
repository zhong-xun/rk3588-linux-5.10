#!/usr/bin/env bash
#
# Materialize the BoardLab source tree in every SDK that currently references
# the central development checkout.  SDK archives must contain independent
# sources so customers can build them without /home/topeet/project/boardlab.
#
# The script is intentionally a dry run by default.  --apply creates a fully
# validated incoming copy beside every target before it moves any existing
# link or source tree out of the way.

set -euo pipefail

readonly DEFAULT_SOURCE_ROOT="/home/topeet/project/boardlab"
readonly DEFAULT_LINUX_ROOT="/home/topeet/Linux"
readonly DEFAULT_ROOTFS_PROJECTS_ROOT="/home/topeet/rootfs_build/ubuntu_debian/projects"

SOURCE_ROOT="${BOARDLAB_SOURCE_ROOT:-${DEFAULT_SOURCE_ROOT}}"
LINUX_ROOT="${TOPEET_LINUX_ROOT:-${DEFAULT_LINUX_ROOT}}"
ROOTFS_PROJECTS_ROOT="${TOPEET_ROOTFS_PROJECTS_ROOT:-${DEFAULT_ROOTFS_PROJECTS_ROOT}}"
declare -a EXCLUDED_ROOT_ENTRIES=(
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

declare -a TARGET_PATHS=()
declare -a TARGET_LABELS=()
declare -a TARGET_ACTIONS=()
declare -a STAGED_PATHS=()
declare -a ACTIVE_STAGES=()

APPLY=0
ROOTFS_TARGET=""
SOURCE_FINGERPRINT=""
STAGE_RESULT=""
PENDING_TARGET=""
PENDING_BACKUP=""
RUN_ID="$(date +%Y%m%d%H%M%S)-$$"

info()
{
    printf '%s\n' "$*"
}

warn()
{
    printf 'warning: %s\n' "$*" >&2
}

fail()
{
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage()
{
    cat <<EOF
Usage: $(basename "$0") [--dry-run|--apply]

Materializes BoardLab in SDK app/boardlab directories that resolve exactly to:
  ${DEFAULT_SOURCE_ROOT}

The rootfs Debian/Ubuntu project mirror is also refreshed:
  ${DEFAULT_ROOTFS_PROJECTS_ROOT}/boardlab

Canonical Buildroot boot scripts under packaging/buildroot are installed as
regular files into every active SDK buildroot tree.

Without --apply this command only validates the source and prints the planned
changes.  --apply stages and validates every copy before replacing any target.

Environment overrides are available for controlled staging trees:
  BOARDLAB_SOURCE_ROOT
  TOPEET_LINUX_ROOT
  TOPEET_ROOTFS_PROJECTS_ROOT
EOF
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

is_boardlab_tree()
{
    local tree="$1"

    [ -d "$tree" ] && [ ! -L "$tree" ] &&
        [ -f "$tree/CMakeLists.txt" ] &&
        [ -f "$tree/VERSION" ] &&
        [ -f "$tree/README.md" ] &&
        [ -f "$tree/src/boardbackend.cpp" ] &&
        [ -f "$tree/scripts/build.sh" ] &&
        [ -f "$tree/web/index.html" ] &&
        [ -d "$tree/assets" ] &&
        [ -d "$tree/third_party" ]
}

validate_staged_tree()
{
    local tree="$1"
    local excluded

    is_boardlab_tree "$tree" || fail "staged copy is not a complete BoardLab source tree: $tree"
    for excluded in "${EXCLUDED_ROOT_ENTRIES[@]}"; do
        if [ -e "$tree/$excluded" ] || [ -L "$tree/$excluded" ]; then
            fail "staged copy unexpectedly contains excluded entry: $tree/$excluded"
        fi
    done
}

# The fingerprint verifies the staged source contents while deliberately
# ignoring local build/cache/tool roots that are not distributed to SDK users.
tree_fingerprint()
{
    local tree="$1"
    local entry
    local -a find_args=(. -mindepth 1 '(')
    local excluded

    for excluded in "${EXCLUDED_ROOT_ENTRIES[@]}"; do
        find_args+=(-path "./$excluded" -o)
    done
    find_args+=(-false ')' -prune -o '(' -type d -o -type f -o -type l ')' -print0)

    (
        cd -- "$tree"
        find "${find_args[@]}" | LC_ALL=C sort -z |
            while IFS= read -r -d '' entry; do
                if [ -L "$entry" ]; then
                    printf 'L\0%s\0' "$entry"
                    readlink -- "$entry"
                    printf '\0'
                elif [ -d "$entry" ]; then
                    printf 'D\0%s\0' "$entry"
                else
                    printf 'F\0%s\0' "$entry"
                    sha256sum <"$entry" | awk '{ print $1 }'
                    printf '\0'
                fi
            done
    ) | sha256sum | awk '{ print $1 }'
}

validate_sdk_target_location()
{
    local target="$1"
    local parent
    local sdk_root

    [ "$(basename -- "$target")" = 'boardlab' ] || fail "unsafe SDK target name: $target"
    parent="$(dirname -- "$target")"
    [ "$(basename -- "$parent")" = 'app' ] || fail "SDK target is not directly below app/: $target"
    sdk_root="$(canonical_existing_dir "$(dirname -- "$parent")")"
    case "$sdk_root" in
        "$LINUX_ROOT"/*) ;;
        *) fail "SDK target escapes ${LINUX_ROOT}: $target" ;;
    esac
    [ -f "$sdk_root/buildroot/package/rockchip/boardlab/boardlab.mk" ] || {
        fail "SDK target has no BoardLab Buildroot package: $sdk_root"
    }
}

validate_rootfs_target_location()
{
    local target="$1"
    local parent

    [ "$(basename -- "$target")" = 'boardlab' ] || fail "unsafe rootfs target name: $target"
    parent="$(canonical_existing_dir "$(dirname -- "$target")")"
    [ "$parent" = "$ROOTFS_PROJECTS_ROOT" ] || fail "rootfs target escapes project directory: $target"
}

add_target()
{
    local path="$1"
    local label="$2"
    local existing

    for existing in "${TARGET_PATHS[@]}"; do
        [ "$existing" != "$path" ] || return 0
    done
    TARGET_PATHS+=("$path")
    TARGET_LABELS+=("$label")
}

collect_sdk_targets()
{
    local sdk_root
    local target

    # Standard product SDKs live two levels below LINUX_ROOT, while the unified
    # extboot SDK lives directly below it. Enumerating both supported layouts
    # also finds a customer SDK whose app/ directory has not been materialized
    # yet, while excluding archives and rollback trees.
    while IFS= read -r -d '' sdk_root; do
        [ -f "$sdk_root/buildroot/package/rockchip/boardlab/boardlab.mk" ] || continue
        target="$sdk_root/app/boardlab"
        add_target "$target" 'SDK'
    done < <(
        find "$LINUX_ROOT" -mindepth 1 -maxdepth 2 -type d \
            \( -name '*-linux-*' -o -name 'topeet-extboot-sdk-*' \) -print0 | sort -z
    )

}

classify_target()
{
    local index="$1"
    local target="${TARGET_PATHS[$index]}"
    local label="${TARGET_LABELS[$index]}"
    local resolved

    if [ "$label" = 'SDK' ]; then
        validate_sdk_target_location "$target"
    else
        validate_rootfs_target_location "$target"
    fi

    if [ -L "$target" ]; then
        resolved="$(readlink -f -- "$target" 2>/dev/null || true)"
        if [ "$resolved" = "$SOURCE_ROOT" ]; then
            TARGET_ACTIONS[$index]='replace source symlink with an independent copy'
            return 0
        fi
        if [ "$label" = 'SDK' ]; then
            TARGET_ACTIONS[$index]='skip: link points somewhere other than the BoardLab source'
            return 0
        fi
        fail "rootfs BoardLab target is an unrelated symlink: $target -> ${resolved:-unresolved}"
    fi

    if [ -e "$target" ]; then
        is_boardlab_tree "$target" || fail "refusing to replace unrecognized existing target: $target"
        TARGET_ACTIONS[$index]='refresh existing independent BoardLab source tree'
        return 0
    fi

    TARGET_ACTIONS[$index]='create independent BoardLab source tree'
}

is_actionable()
{
    case "$1" in
        'replace source symlink with an independent copy'|'refresh existing independent BoardLab source tree'|'create independent BoardLab source tree')
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

work_path_is_safe()
{
    local path="$1"
    local prefix="$2"
    local name

    [ -n "$path" ] || return 1
    name="$(basename -- "$path")"
    case "$name" in
        "${prefix}"*) ;;
        *) return 1 ;;
    esac
    [ "$(dirname -- "$path")" != '/' ] || return 1
}

remove_work_path()
{
    local path="$1"
    local prefix="$2"

    work_path_is_safe "$path" "$prefix" || fail "refusing unsafe temporary-path removal: $path"
    rm -rf -- "$path"
}

cleanup_stages()
{
    local stage

    set +e
    for stage in "${ACTIVE_STAGES[@]}"; do
        if [ -e "$stage" ] || [ -L "$stage" ]; then
            remove_work_path "$stage" '.boardlab.incoming.'
        fi
    done
}

restore_pending_target()
{
    if [ -z "$PENDING_TARGET" ] || [ -z "$PENDING_BACKUP" ]; then
        return
    fi
    if { [ -e "$PENDING_BACKUP" ] || [ -L "$PENDING_BACKUP" ]; } &&
        ! { [ -e "$PENDING_TARGET" ] || [ -L "$PENDING_TARGET" ]; }; then
        warn "restoring previous target after an interrupted promotion: $PENDING_TARGET"
        mv -- "$PENDING_BACKUP" "$PENDING_TARGET" || warn "could not restore $PENDING_TARGET; previous tree remains at $PENDING_BACKUP"
    fi
}

on_exit()
{
    local status="$1"

    restore_pending_target
    cleanup_stages
    return "$status"
}

trap 'status=$?; on_exit "$status"' EXIT

stage_target()
{
    local target="$1"
    local parent
    local stage
    local staged_fingerprint
    local -a tar_excludes=()
    local excluded

    parent="$(dirname -- "$target")"
    if [ ! -d "$parent" ]; then
        [ "$(basename -- "$parent")" = app ] || fail "refusing to create unexpected target parent: $parent"
        mkdir -p -- "$parent"
    fi
    parent="$(canonical_existing_dir "$parent")"
    stage="$(mktemp -d "$parent/.boardlab.incoming.XXXXXXXX")"
    ACTIVE_STAGES+=("$stage")
    info "staging: $target"

    for excluded in "${EXCLUDED_ROOT_ENTRIES[@]}"; do
        tar_excludes+=(--exclude="./$excluded" --exclude="./$excluded/*")
    done
    tar -C "$SOURCE_ROOT" "${tar_excludes[@]}" -cpf - . | tar -C "$stage" -xpf -

    validate_staged_tree "$stage"
    staged_fingerprint="$(tree_fingerprint "$stage")"
    [ "$staged_fingerprint" = "$SOURCE_FINGERPRINT" ] || {
        fail "staged copy does not match the distributable source fingerprint: $stage"
    }

    STAGE_RESULT="$stage"
}

promote_stage()
{
    local target="$1"
    local stage="$2"
    local backup="$(dirname -- "$target")/.boardlab.previous.${RUN_ID}"
    local failed="$(dirname -- "$target")/.boardlab.failed.${RUN_ID}"
    local had_previous=0

    if [ -e "$target" ] || [ -L "$target" ]; then
        [ ! -e "$backup" ] && [ ! -L "$backup" ] || fail "refusing to overwrite backup path: $backup"
        PENDING_TARGET="$target"
        PENDING_BACKUP="$backup"
        mv -- "$target" "$backup"
        had_previous=1
    fi

    if ! mv -- "$stage" "$target"; then
        restore_pending_target
        fail "could not promote staged source tree to: $target"
    fi

    if ! validate_staged_tree "$target"; then
        if [ -e "$failed" ] || [ -L "$failed" ]; then
            fail "new target failed validation and recovery path already exists: $failed"
        fi
        mv -- "$target" "$failed" || fail "new target failed validation and could not be isolated: $target"
        if [ "$had_previous" = '1' ]; then
            mv -- "$backup" "$target" || fail "new target failed validation and previous tree could not be restored: $backup"
        fi
        PENDING_TARGET=""
        PENDING_BACKUP=""
        fail "new target failed validation; preserved failed copy at: $failed"
    fi

    PENDING_TARGET=""
    PENDING_BACKUP=""
    if [ "$had_previous" = '1' ]; then
        remove_work_path "$backup" '.boardlab.previous.'
    fi
    info "materialized: $target"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run)
            APPLY=0
            ;;
        --apply)
            APPLY=1
            ;;
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

require_command find
require_command tar
require_command mktemp
require_command mv
require_command mkdir
require_command rm
require_command readlink
require_command sha256sum
require_command sort

SOURCE_ROOT="$(canonical_existing_dir "$SOURCE_ROOT")"
LINUX_ROOT="$(canonical_existing_dir "$LINUX_ROOT")"
ROOTFS_PROJECTS_ROOT="$(canonical_existing_dir "$ROOTFS_PROJECTS_ROOT")"
ROOTFS_TARGET="$ROOTFS_PROJECTS_ROOT/boardlab"
RUNTIME_SYNC="$SOURCE_ROOT/scripts/sync-buildroot-boardlab-runtime.sh"

is_boardlab_tree "$SOURCE_ROOT" || fail "source is not a complete BoardLab source tree: $SOURCE_ROOT"
[ "$SOURCE_ROOT" != "$ROOTFS_TARGET" ] || fail 'source root and rootfs target must be different directories'
[ -x "$RUNTIME_SYNC" ] || fail "Buildroot runtime sync helper is missing or not executable: $RUNTIME_SYNC"

add_target "$ROOTFS_TARGET" 'rootfs project'
collect_sdk_targets

for index in "${!TARGET_PATHS[@]}"; do
    classify_target "$index"
done

info "mode: $([ "$APPLY" = '1' ] && printf 'apply' || printf 'dry-run')"
info "source: $SOURCE_ROOT"
for index in "${!TARGET_PATHS[@]}"; do
    info "${TARGET_LABELS[$index]}: ${TARGET_ACTIONS[$index]}"
    info "  ${TARGET_PATHS[$index]}"
done

if [ "$APPLY" != '1' ]; then
    BOARDLAB_BUILDROOT_TEMPLATE_ROOT="$SOURCE_ROOT/packaging/buildroot" \
        TOPEET_LINUX_ROOT="$LINUX_ROOT" \
        "$RUNTIME_SYNC" --dry-run
    info 'dry-run complete: no files or links were changed; rerun with --apply to materialize the copies.'
    exit 0
fi

SOURCE_FINGERPRINT="$(tree_fingerprint "$SOURCE_ROOT")"
info "source fingerprint: $SOURCE_FINGERPRINT"

for index in "${!TARGET_PATHS[@]}"; do
    if ! is_actionable "${TARGET_ACTIONS[$index]}"; then
        STAGED_PATHS[$index]=''
        continue
    fi
    stage_target "${TARGET_PATHS[$index]}"
    STAGED_PATHS[$index]="$STAGE_RESULT"
done

[ "$(tree_fingerprint "$SOURCE_ROOT")" = "$SOURCE_FINGERPRINT" ] || {
    fail 'source changed while copies were being staged; no target was replaced'
}

for index in "${!TARGET_PATHS[@]}"; do
    [ -n "${STAGED_PATHS[$index]:-}" ] || continue
    promote_stage "${TARGET_PATHS[$index]}" "${STAGED_PATHS[$index]}"
done

BOARDLAB_BUILDROOT_TEMPLATE_ROOT="$SOURCE_ROOT/packaging/buildroot" \
    TOPEET_LINUX_ROOT="$LINUX_ROOT" \
    "$RUNTIME_SYNC" --apply

info 'sync complete: application mirrors and Buildroot runtime scripts are independent hard copies.'
