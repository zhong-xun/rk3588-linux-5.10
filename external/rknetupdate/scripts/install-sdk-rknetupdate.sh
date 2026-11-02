#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MODE=check
declare -a REQUESTED_SDKS=()

usage()
{
	cat <<'EOF'
Usage: install-sdk-rknetupdate.sh [--check|--apply] --sdk PATH [OPTIONS]

Install the RKNetUpdate source component into one or more Rockchip SDKs.
The component is intentionally self-contained so SDK post-hooks do not rely
on a sibling checkout or an absolute host path.

Options:
  --check               Verify the SDK-local component without changing files
  --apply               Apply/update the managed SDK-local component
  --sdk PATH            Process one SDK root; may be supplied more than once
  -h, --help            Show this help
EOF
}

while (($#)); do
	case "$1" in
		--check) MODE=check; shift ;;
		--apply) MODE=apply; shift ;;
		--sdk)
			(($# >= 2)) || { echo "missing value for --sdk" >&2; exit 2; }
			REQUESTED_SDKS+=("$2"); shift 2 ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
	esac
done

((${#REQUESTED_SDKS[@]})) || { echo "at least one --sdk is required" >&2; exit 2; }

MARKER="$PROJECT_DIR/packaging/sdk/rknetupdate-sdk-source-v1"
[ -r "$MARKER" ] || { echo "missing SDK component marker: $MARKER" >&2; exit 1; }

declare -a FIXED_FILES=(
	"$PROJECT_DIR/Makefile"
	"$PROJECT_DIR/go.mod"
	"$MARKER"
	"$PROJECT_DIR/packaging/filesystem/rknetupdate.conf"
	"$PROJECT_DIR/packaging/filesystem/rknetupdate.safe.conf"
	"$PROJECT_DIR/packaging/filesystem/rknetupdate.standard-emmc.conf"
	"$PROJECT_DIR/packaging/filesystem/systemd/rknetupdate.service"
	"$PROJECT_DIR/packaging/filesystem/sysv/S90rknetupdate"
)

declare -a OUTPUT_FILES=(
	"$PROJECT_DIR/output/bin/rknetupdate-linux-arm64"
	"$PROJECT_DIR/output/bin/rknetupdate-control-linux-arm64"
	"$PROJECT_DIR/output/bin/rknetupdate-recovery-linux-arm64"
)

for file in "${FIXED_FILES[@]}"; do
	[ -r "$file" ] || { echo "missing SDK component source: $file" >&2; exit 1; }
done

same_file()
{
	[ -f "$2" ] && cmp -s "$1" "$2"
}

managed_component()
{
	local component="$1"
	[ -r "$component/rknetupdate-sdk-source-v1" ] &&
		grep -Fxq 'RKNetUpdate SDK-local source component v1' \
			"$component/rknetupdate-sdk-source-v1"
}

copy_one()
{
	local source="$1" component="$2" relative destination
	relative="${source#"$PROJECT_DIR/"}"
	case "$relative" in
		packaging/sdk/rknetupdate-sdk-source-v1)
			destination="$component/rknetupdate-sdk-source-v1"
			;;
		*)
			destination="$component/$relative"
			;;
	esac

	if [ "$MODE" = check ]; then
		if ! same_file "$source" "$destination"; then
			echo "missing or stale SDK component file: $destination" >&2
			return 1
		fi
		return 0
	fi

	install -D -m 0644 "$source" "$destination"
}

stage_output()
{
	local source="$1" component="$2" destination
	destination="$component/output/bin/$(basename "$source")"
	if [ "$MODE" = check ]; then
		if ! same_file "$source" "$destination"; then
			echo "missing or stale SDK component binary: $destination" >&2
			return 1
		fi
		return 0
	fi
	install -D -m 0755 "$source" "$destination"
}

for sdk in "${REQUESTED_SDKS[@]}"; do
	sdk="$(realpath "$sdk")"
	component="$sdk/external/rknetupdate"
	[ -d "$sdk/external" ] || {
		echo "SDK has no external directory: $sdk" >&2
		exit 1
	}

	if [ -e "$component" ] && ! managed_component "$component"; then
		echo "refusing to replace unmanaged SDK component: $component" >&2
		exit 1
	fi
	if [ "$MODE" = check ] && ! managed_component "$component"; then
		echo "missing SDK-local RKNetUpdate component: $component" >&2
		exit 1
	fi

	if [ "$MODE" = apply ]; then
		mkdir -p "$component"
	fi

	status=0
	for source in "${FIXED_FILES[@]}"; do
		copy_one "$source" "$component" || status=1
	done
	while IFS= read -r -d '' source; do
		copy_one "$source" "$component" || status=1
	done < <(find "$PROJECT_DIR/cmd" "$PROJECT_DIR/internal" -type f \
		! -name '*_test.go' -print0)
	for source in "${OUTPUT_FILES[@]}"; do
		if [ -x "$source" ]; then
			stage_output "$source" "$component" || status=1
		fi
	done

	if ((status)); then
		exit 1
	fi
	printf 'OK\t%s\n' "$sdk"
done

echo "RKNetUpdate SDK-local source $MODE completed for ${#REQUESTED_SDKS[@]} SDK(s)."
