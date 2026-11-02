#!/usr/bin/env sh
set -eu

OS_RELEASE_FILE=${BOARDLAB_OS_RELEASE_FILE:-${OS_RELEASE_FILE:-/etc/os-release}}

say() {
    printf '%s\n' "$*"
}

check_cmd() {
    if command -v "$1" >/dev/null 2>&1; then
        say "OK   command: $1"
    else
        say "MISS command: $1"
    fi
}

check_path() {
    if [ -e "$1" ]; then
        say "OK   path: $1"
    else
        say "MISS path: $1"
    fi
}

say "== System =="
uname -a || true
printf 'arch: '
dpkg --print-architecture 2>/dev/null || uname -m

say
say "== Qt / build tools =="
check_cmd cmake
check_cmd qmake
check_cmd qmllint
check_cmd make
check_cmd file

say
say "== Runtime tools =="
check_cmd curl
check_cmd wget
check_cmd nmcli
check_cmd ping
check_cmd ifconfig
check_cmd route
check_cmd pkill
check_cmd arecord
check_cmd aplay
check_cmd evtest
check_cmd gpioinfo
check_cmd mmcli
check_cmd ip
check_cmd ethtool
check_cmd eglinfo
check_cmd modetest

say
say "== Optional tools =="
check_cmd candump
check_cmd cansend
say "INFO CAN TX/RX in the independent CAN demo uses Linux SocketCAN directly; can-utils is optional."

say
say "== Graphics / input =="
check_path /dev/dri
check_path /dev/input
os_id=$(sed -n 's/^ID=//p' "$OS_RELEASE_FILE" 2>/dev/null | head -n 1 | tr -d '"\047' | tr '[:upper:]' '[:lower:]')
case "$os_id" in
    buildroot)
        if [ -n "${WAYLAND_DISPLAY:-}" ]; then
            say "OK   Buildroot Wayland display: WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
        else
            say "MISS Buildroot requires WAYLAND_DISPLAY for Weston"
        fi
        if [ "${QT_QPA_PLATFORM:-wayland}" = "wayland" ]; then
            say "OK   Buildroot Qt platform: wayland"
        else
            say "MISS Buildroot QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-unset}; expected wayland"
        fi
        ;;
    ubuntu|debian)
        if [ -n "${DISPLAY:-}" ]; then
            say "OK   Ubuntu/Debian X11 display: DISPLAY=$DISPLAY"
        else
            say "MISS Ubuntu/Debian requires an active X11 DISPLAY"
        fi
        if [ "${QT_QPA_PLATFORM:-xcb}" = "xcb" ]; then
            say "OK   Ubuntu/Debian Qt platform: xcb"
        else
            say "MISS Ubuntu/Debian QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-unset}; expected xcb"
        fi
        ;;
    *)
        say "INFO unknown OS graphics profile: ${os_id:-unavailable}"
        [ -z "${DISPLAY:-}" ] || say "INFO DISPLAY=$DISPLAY"
        [ -z "${WAYLAND_DISPLAY:-}" ] || say "INFO WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
        ;;
esac

say
say "== Device nodes =="
for pattern in /sys/class/backlight /sys/class/thermal /sys/class/net /dev/ttyS* /dev/ttyUSB* /dev/cdc-wdm*; do
    # Intentionally allow unmatched globs to print as missing.
    found=0
    for item in $pattern; do
        if [ -e "$item" ]; then
            say "OK   $item"
            found=1
        fi
    done
    if [ "$found" = "0" ]; then
        say "MISS $pattern"
    fi
done

say
say "== Groups =="
id || true
say
say "Expected useful groups: input video render dialout"
