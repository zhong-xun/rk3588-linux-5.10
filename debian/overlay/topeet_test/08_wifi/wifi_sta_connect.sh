#!/bin/sh
# NetworkManager STA launcher for the Ubuntu and Debian test overlays.
# The Buildroot overlay deliberately keeps wifi_sta_connect.sh unchanged.
set -eu

PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

config_file=${WIFI_CONFIG_FILE:-/data/wifi_configure.txt}
os_release=${WIFI_OS_RELEASE:-/etc/os-release}
net_class_root=${WIFI_SYS_CLASS_NET:-/sys/class/net}
ssid=
password=
encrypt=psk
mode=station
config_changed=0

usage() {
    cat <<EOF

##################################################################
#usage:
#   $0 [SSID [PASSWORD [station]]]
#   without arguments, read /data/wifi_configure.txt or prompt
#   version: NetworkManager
##################################################################

EOF
}

die() {
    printf '%s\n' "$*" >&2
    exit 1
}

read_config() {
    [ -r "$config_file" ] || return 0

    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            ssid=*) ssid=${line#ssid=} ;;
            password=*) password=${line#password=} ;;
            encrypt=*) encrypt=${line#encrypt=} ;;
            mode=*) mode=${line#mode=} ;;
        esac
    done < "$config_file"
}

save_config() {
    config_directory=$(dirname "$config_file")

    if ! mkdir -p "$config_directory" 2>/dev/null; then
        printf 'Cannot save Wi-Fi configuration: %s\n' "$config_file" >&2
        return 0
    fi
    if ! (
        umask 077
        printf 'ssid=%s\npassword=%s\nencrypt=%s\nmode=station\n' \
            "$ssid" "$password" "$encrypt" > "$config_file"
    ); then
        printf 'Cannot save Wi-Fi configuration: %s\n' "$config_file" >&2
    fi
}

prompt_ssid() {
    [ -t 0 ] || die 'Wi-Fi SSID is missing; pass it as an argument or write it to /data/wifi_configure.txt.'
    printf 'Input ssid: '
    IFS= read -r ssid || die 'Cannot read Wi-Fi SSID.'
    [ -n "$ssid" ] || die 'wifi ssid not empty!!!'
    config_changed=1
}

prompt_password() {
    [ -t 0 ] || die 'Wi-Fi password is missing; pass it as an argument or write it to /data/wifi_configure.txt.'
    printf 'Input password: '
    stty -echo
    if ! IFS= read -r password; then
        stty echo
        printf '\n'
        die 'Cannot read Wi-Fi password.'
    fi
    stty echo
    printf '\n'
    [ -n "$password" ] || die 'wifi password not empty!!!'
    config_changed=1
}

require_supported_rootfs() {
    [ -r "$os_release" ] || die 'This Wi-Fi script requires Ubuntu or Debian with NetworkManager.'
    os_id=$(sed -n 's/^ID=//p' "$os_release" | sed -n '1p' | tr -d '"')

    case "$os_id" in
        ubuntu|debian) ;;
        *) die 'This Wi-Fi script is only for Ubuntu or Debian overlays.' ;;
    esac
}

valid_interface() {
    case "$1" in
        ''|*[!A-Za-z0-9_.:-]*) return 1 ;;
        *) return 0 ;;
    esac
}

find_wifi_interface() {
    LC_ALL=C "$NMCLI" -t -f DEVICE,TYPE,STATE device status 2>/dev/null |
        while IFS=: read -r device type state; do
            [ "$type" = wifi ] || continue
            [ "$state" = unmanaged ] && continue
            valid_interface "$device" || continue
            printf '%s\n' "$device"
            break
        done
}

case "${1:-}" in
    -h|--help)
        usage
        exit 0
        ;;
esac

usage
require_supported_rootfs
read_config

case "$#" in
    0) ;;
    1)
        ssid=$1
        ;;
    2)
        ssid=$1
        password=$2
        ;;
    3)
        ssid=$1
        password=$2
        mode=$3
        ;;
    *)
        usage >&2
        exit 2
        ;;
esac

case "$mode" in
    ''|station) mode=station ;;
    *) die 'This NetworkManager script supports station mode only.' ;;
esac

[ -n "$ssid" ] || prompt_ssid
case "$encrypt" in
    none|open) password= ;;
    *) [ -n "$password" ] || prompt_password ;;
esac

if [ "$config_changed" -eq 1 ]; then
    save_config
fi

if [ -n "${NMCLI:-}" ]; then
    [ -x "$NMCLI" ] || die "nmcli is unavailable: $NMCLI"
else
    NMCLI=$(command -v nmcli || true)
    [ -n "$NMCLI" ] || die 'nmcli is unavailable. Install and start NetworkManager first.'
fi

"$NMCLI" general status >/dev/null 2>&1 || die 'NetworkManager is not running or this user is not authorized to use it.'

if [ -n "${WIFI_IFACE:-}" ]; then
    interface=$WIFI_IFACE
    valid_interface "$interface" || die "Invalid Wi-Fi interface: $interface"
else
    interface=$(find_wifi_interface)
fi
[ -n "${interface:-}" ] || die 'No NetworkManager-managed Wi-Fi interface was found.'
[ -d "$net_class_root/$interface" ] || die "Wi-Fi interface does not exist: $interface"

printf 'user set:\nssid=%s, mode=%s\n' "$ssid" "$mode"
printf 'starting NetworkManager Wi-Fi on %s...\n' "$interface"
"$NMCLI" radio wifi on
if ! "$NMCLI" -w 15 device wifi rescan ifname "$interface"; then
    printf 'Wi-Fi rescan failed; using NetworkManager cached access points.\n' >&2
fi

if [ -n "$password" ]; then
    # --ask consumes the secret from standard input, so it is not visible in
    # the process argument list.
    printf '%s\n' "$password" | "$NMCLI" --ask -w 30 device wifi connect "$ssid" ifname "$interface"
else
    "$NMCLI" -w 30 device wifi connect "$ssid" ifname "$interface"
fi

printf 'ap connected!!\n'
"$NMCLI" -t -f IP4.ADDRESS device show "$interface" || true
