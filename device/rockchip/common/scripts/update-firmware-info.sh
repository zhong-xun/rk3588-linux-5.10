#!/bin/bash

set -e

usage()
{
	cat <<'EOF'
Usage: update-firmware-info.sh <parameter.txt>

Generate the numeric FIRMWARE_VER and the Topeet-specific readable image name
stored in parameter.txt for afptool/update.img.

Automatic mappings:
  buildroot  -> FIRMWARE_VER=1.0,   TOPEET_FIRMWARE=Buildroot
  ubuntu20   -> FIRMWARE_VER=20.04, TOPEET_FIRMWARE=Ubuntu20.04
  ubuntu22   -> FIRMWARE_VER=22.04, TOPEET_FIRMWARE=Ubuntu22.04
  ubuntu24   -> FIRMWARE_VER=24.04, TOPEET_FIRMWARE=Ubuntu24.04
  debian11   -> FIRMWARE_VER=11.0,  TOPEET_FIRMWARE=Debian11
  debian12   -> FIRMWARE_VER=12.0,  TOPEET_FIRMWARE=Debian12

The rootfs comes from RK_ROOTFS_SYSTEM. Optional build-time overrides:
  TOPEET_FIRMWARE_FLAVOR
  TOPEET_FIRMWARE_VERSION (numeric only, for example 22.04)
  TOPEET_FIRMWARE_NAME    (letters/numbers/text, for example Ubuntu22.04-HDMI)

RK_FIRMWARE_FLAVOR, RK_FIRMWARE_VERSION and RK_FIRMWARE_NAME are accepted as
compatibility aliases. afptool stores the version as numeric components, so it
cannot store text such as "ubuntu22" in FIRMWARE_VER. TOPEET_FIRMWARE is an
additional text field consumed by Topeet-RK-Flash. A flashing tool may show
22.04 as 22.4 because the numeric field does not retain decimal zero padding.
EOF
}

die()
{
	echo "ERROR: $*" >&2
	exit 1
}

trim_parameter_value()
{
	awk -F: -v key="$2" '
		$1 == key {
			sub(/^[^:]*:[[:space:]]*/, "")
			sub(/[[:space:]\r]+$/, "")
			print
			exit
		}
	' "$1"
}

validate_version()
{
	awk -v version="$1" 'BEGIN {
		count = split(version, component, ".")
		if (count < 2 || count > 4)
			exit 1
		for (i = 1; i <= count; i++) {
			if (component[i] !~ /^[0-9]+$/ ||
			    component[i] < 0 || component[i] > 255)
				exit 1
		}
	}'
}

case "${1:-}" in
	-h|--help)
		usage
		exit 0
		;;
esac

PARAMETER=${1:-}
[ -n "$PARAMETER" ] || {
	usage >&2
	exit 2
}
[ -r "$PARAMETER" ] || die "parameter file is not readable: $PARAMETER"

CURRENT_VERSION=$(trim_parameter_value "$PARAMETER" FIRMWARE_VER)
CURRENT_NAME=$(trim_parameter_value "$PARAMETER" TOPEET_FIRMWARE)
[ -n "$CURRENT_VERSION" ] || die "FIRMWARE_VER is missing from $PARAMETER"

FLAVOR=${TOPEET_FIRMWARE_FLAVOR:-${RK_FIRMWARE_FLAVOR:-${RK_ROOTFS_SYSTEM:-}}}
FLAVOR=${FLAVOR//\"/}
FLAVOR=$(printf '%s' "$FLAVOR" | tr '[:upper:]' '[:lower:]')

DEFAULT_VERSION=
DEFAULT_NAME=
case "$FLAVOR" in
	buildroot)
		DEFAULT_VERSION=1.0
		DEFAULT_NAME=Buildroot
		;;
	ubuntu20|ubuntu20.04|focal)
		DEFAULT_VERSION=20.04
		DEFAULT_NAME=Ubuntu20.04
		;;
	ubuntu22|ubuntu22.04|jammy)
		DEFAULT_VERSION=22.04
		DEFAULT_NAME=Ubuntu22.04
		;;
	ubuntu24|ubuntu24.04|noble)
		DEFAULT_VERSION=24.04
		DEFAULT_NAME=Ubuntu24.04
		;;
	debian11|bullseye)
		DEFAULT_VERSION=11.0
		DEFAULT_NAME=Debian11
		;;
	debian12|bookworm)
		DEFAULT_VERSION=12.0
		DEFAULT_NAME=Debian12
		;;
	"")
		;;
	*)
		echo "WARNING: no firmware metadata mapping for rootfs '$FLAVOR'; use TOPEET_FIRMWARE_NAME to label it" >&2
		;;
esac

VERSION=${TOPEET_FIRMWARE_VERSION:-${RK_FIRMWARE_VERSION:-${DEFAULT_VERSION:-$CURRENT_VERSION}}}
FIRMWARE_NAME=${TOPEET_FIRMWARE_NAME:-${RK_FIRMWARE_NAME:-${DEFAULT_NAME:-$CURRENT_NAME}}}

validate_version "$VERSION" || die \
	"invalid firmware version '$VERSION': afptool requires 2-4 numeric components in the range 0..255 (for example 22.04); put distribution text in TOPEET_FIRMWARE_NAME"

if [ -n "$FIRMWARE_NAME" ]; then
	case "$FIRMWARE_NAME" in
		*$'\n'*|*$'\r'*)
			die "firmware name must be a single printable line"
			;;
	esac
	if LC_ALL=C printf '%s' "$FIRMWARE_NAME" | grep -q '[[:cntrl:]]'; then
		die "firmware name must be a single printable line: $FIRMWARE_NAME"
	fi
	NAME_BYTES=$(LC_ALL=C printf '%s' "$FIRMWARE_NAME" | wc -c)
	[ "$NAME_BYTES" -le 96 ] || die \
		"firmware name is too long ($NAME_BYTES bytes, maximum 96): $FIRMWARE_NAME"
fi

TMP_PARAMETER=$(mktemp "${PARAMETER}.tmp.XXXXXX")
trap 'rm -f "$TMP_PARAMETER"' EXIT

TOPEET_PARAMETER_NAME="$FIRMWARE_NAME" awk -v version="$VERSION" '
	BEGIN {
		firmware_name = ENVIRON["TOPEET_PARAMETER_NAME"]
		found_version = 0
		found_name = 0
	}
	/^FIRMWARE_VER[[:space:]]*:/ {
		print "FIRMWARE_VER: " version
		if (firmware_name != "" && !found_name) {
			print "TOPEET_FIRMWARE: " firmware_name
			found_name = 1
		}
		found_version = 1
		next
	}
	/^TOPEET_FIRMWARE[[:space:]]*:/ {
		if (firmware_name != "" && !found_name) {
			print "TOPEET_FIRMWARE: " firmware_name
			found_name = 1
		}
		next
	}
	{ print }
	END {
		if (!found_version)
			exit 1
	}
' "$PARAMETER" > "$TMP_PARAMETER" || die "failed to update firmware metadata in $PARAMETER"

chmod --reference="$PARAMETER" "$TMP_PARAMETER" 2>/dev/null || true
mv -f -- "$TMP_PARAMETER" "$PARAMETER"
trap - EXIT

echo "Firmware metadata: ${FLAVOR:-custom/default} -> FIRMWARE_VER=$VERSION, TOPEET_FIRMWARE=${FIRMWARE_NAME:-not-set}"
