#!/usr/bin/env sh
set -u

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BOARDLAB_BUILD_DIR:-"$ROOT_DIR/build"}
# The public command is boardlab-hardware-validate.sh.  This file remains the
# compatibility implementation for installations that still invoke its former
# RK3588-specific name.
REPORT=${BOARDLAB_VALIDATE_REPORT:-${RK3588_VALIDATE_REPORT:-${RK3568_VALIDATE_REPORT:-"/tmp/boardlab-hardware-validation.txt"}}}
JSON_REPORT=${BOARDLAB_VALIDATE_JSON:-${RK3588_VALIDATE_JSON:-${RK3568_VALIDATE_JSON:-"/tmp/boardlab-hardware-validation.json"}}}
AUDIO_VALIDATE_FILE=${BOARDLAB_AUDIO_VALIDATE_FILE:-${RK3588_AUDIO_VALIDATE_FILE:-${RK3568_AUDIO_VALIDATE_FILE:-"/tmp/boardlab-audio-validate.wav"}}}
DEVICE_TREE_ROOT=${BOARDLAB_DEVICE_TREE_ROOT:-/proc/device-tree}
ETH_PING_TARGET=${BOARDLAB_ETH_PING_TARGET:-${RK3588_ETH_PING_TARGET:-${RK3568_ETH_PING_TARGET:-}}}
UART_DEVICE=${BOARDLAB_UART_DEVICE:-${RK3588_UART_DEVICE:-${RK3568_UART_DEVICE:-}}}
CAN_IFACE=${BOARDLAB_CAN_IFACE:-${RK3588_CAN_IFACE:-${RK3568_CAN_IFACE:-}}}
CAMERA_DEVICE=${BOARDLAB_CAMERA_DEVICE:-${RK3588_CAMERA_DEVICE:-${RK3568_CAMERA_DEVICE:-}}}
AUDIO_SMOKE=${BOARDLAB_AUDIO_SMOKE:-${RK3588_AUDIO_SMOKE:-${RK3568_AUDIO_SMOKE:-0}}}
OS_RELEASE_FILE=${BOARDLAB_OS_RELEASE_FILE:-${OS_RELEASE_FILE:-/etc/os-release}}

read_os_release_value() {
    _os_key=$1
    if [ -r "$OS_RELEASE_FILE" ]; then
        sed -n "s/^${_os_key}=//p" "$OS_RELEASE_FILE" 2>/dev/null \
            | head -n 1 | tr -d '"\047'
    fi
}

os_id=$(read_os_release_value ID | tr '[:upper:]' '[:lower:]')
os_line=$(read_os_release_value PRETTY_NAME)
version_id=$(read_os_release_value VERSION_ID)

if [ "$os_id" = "buildroot" ]; then
    if [ -x "$ROOT_DIR/scripts/run.sh" ]; then
        RUNNER_HINT="XDG_RUNTIME_DIR=/run WAYLAND_DISPLAY=wayland-0 ./scripts/run.sh wayland --kiosk"
    else
        RUNNER_HINT="/etc/init.d/S60boardlab restart after S49weston is ready"
    fi
elif [ -x "$ROOT_DIR/scripts/run.sh" ]; then
    RUNNER_HINT="DISPLAY=:0 ./scripts/run.sh x11 --kiosk"
elif [ -x "$ROOT_DIR/bin/boardlab-root" ]; then
    RUNNER_HINT="$ROOT_DIR/bin/boardlab-root --kiosk from the active X11 desktop"
elif [ -x "$ROOT_DIR/bin/boardlab-run" ]; then
    RUNNER_HINT="DISPLAY=:0 $ROOT_DIR/bin/boardlab-run"
else
    RUNNER_HINT="the installed BoardLab runner"
fi

PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0
MANUAL_COUNT=0
SKIP_COUNT=0

read_device_tree_value() {
    _dt_file=$1
    if [ -r "$_dt_file" ]; then
        tr '\000' ' ' < "$_dt_file" 2>/dev/null | tr '\n' ' ' | sed 's/[[:space:]][[:space:]]*/ /g; s/^ //; s/ $//'
    fi
}

lowercase() {
    printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

contains_token() {
    case "$1" in
        *"$2"*) return 0 ;;
        *) return 1 ;;
    esac
}

detect_profile() {
    DT_MODEL=$(read_device_tree_value "$DEVICE_TREE_ROOT/model")
    DT_COMPATIBLE=$(read_device_tree_value "$DEVICE_TREE_ROOT/compatible")
    profile_request=${BOARDLAB_BOARD_PROFILE:-}
    target_evidence="$DT_MODEL $DT_COMPATIBLE"
    PROFILE_SOURCE=device-tree

    case "$profile_request" in
        topeet-rk3562)
            PROFILE_ID=topeet-rk3562
            TARGET_SOC=RK3562
            TARGET_LABEL=iTOP-RK3562
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3568-mini)
            PROFILE_ID=topeet-rk3568-mini
            TARGET_SOC=RK3568
            TARGET_LABEL="iTOP-RK3568 Mini"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3588)
            PROFILE_ID=topeet-rk3588
            TARGET_SOC=RK3588
            TARGET_LABEL=iTOP-RK3588
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3568)
            PROFILE_ID=topeet-rk3568
            TARGET_SOC=RK3568
            TARGET_LABEL=iTOP-RK3568
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3576)
            PROFILE_ID=topeet-rk3576
            TARGET_SOC=RK3576
            TARGET_LABEL="iTOP-RK3576 Standard"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3576-lga)
            PROFILE_ID=topeet-rk3576-lga
            TARGET_SOC=RK3576
            TARGET_LABEL="iTOP-RK3576 LGA"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3588-mini)
            PROFILE_ID=topeet-rk3588-mini
            TARGET_SOC=RK3588
            TARGET_LABEL="iTOP-RK3588 Mini"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        topeet-rk3588s)
            PROFILE_ID=topeet-rk3588s
            TARGET_SOC=RK3588S
            TARGET_LABEL=iTOP-RK3588S
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        generic-rk3562)
            PROFILE_ID=generic-rk3562
            TARGET_SOC=RK3562
            TARGET_LABEL="RK3562 Board"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        generic-rk3588)
            PROFILE_ID=generic-rk3588
            TARGET_SOC=RK3588
            TARGET_LABEL="RK3588 Board"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        generic-rk3568)
            PROFILE_ID=generic-rk3568
            TARGET_SOC=RK3568
            TARGET_LABEL="RK3568 Board"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        generic-rk3576)
            PROFILE_ID=generic-rk3576
            TARGET_SOC=RK3576
            TARGET_LABEL="RK3576 Board"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        generic-rk3588s)
            PROFILE_ID=generic-rk3588s
            TARGET_SOC=RK3588S
            TARGET_LABEL="RK3588S Board"
            PROFILE_SOURCE=BOARDLAB_BOARD_PROFILE
            ;;
        *)
            target_lower=$(lowercase "$target_evidence")
            compatible_lower=$(lowercase "$DT_COMPATIBLE")
            if [ -z "$(printf '%s' "$target_lower" | tr -d '[:space:]')" ]; then
                target_evidence=${BOARDLAB_TARGET:-${RK_SHELL_QT_TARGET:-${RK3588_TARGET:-}}}
                target_lower=$(lowercase "$target_evidence")
                PROFILE_SOURCE=target-marker
            fi
            if contains_token "$compatible_lower" 'topeet,itop-rk3562'; then
                PROFILE_ID=topeet-rk3562
                TARGET_SOC=RK3562
                TARGET_LABEL=iTOP-RK3562
            elif contains_token "$compatible_lower" 'topeet,itop-rk3568-mini'; then
                PROFILE_ID=topeet-rk3568-mini
                TARGET_SOC=RK3568
                TARGET_LABEL="iTOP-RK3568 Mini"
            elif contains_token "$compatible_lower" 'topeet,itop-rk3568-standard' || contains_token "$compatible_lower" 'topeet,itop-rk3568'; then
                PROFILE_ID=topeet-rk3568
                TARGET_SOC=RK3568
                TARGET_LABEL=iTOP-RK3568
            elif contains_token "$compatible_lower" 'topeet,itop-rk3576-lga'; then
                PROFILE_ID=topeet-rk3576-lga
                TARGET_SOC=RK3576
                TARGET_LABEL="iTOP-RK3576 LGA"
            elif contains_token "$compatible_lower" 'topeet,itop-rk3576-standard' || contains_token "$compatible_lower" 'topeet,itop-rk3576'; then
                PROFILE_ID=topeet-rk3576
                TARGET_SOC=RK3576
                TARGET_LABEL="iTOP-RK3576 Standard"
            elif contains_token "$compatible_lower" 'topeet,itop-rk3588s'; then
                PROFILE_ID=topeet-rk3588s
                TARGET_SOC=RK3588S
                TARGET_LABEL=iTOP-RK3588S
            elif contains_token "$compatible_lower" 'topeet,itop-rk3588-mini'; then
                PROFILE_ID=topeet-rk3588-mini
                TARGET_SOC=RK3588
                TARGET_LABEL="iTOP-RK3588 Mini"
            elif contains_token "$compatible_lower" 'topeet,itop-rk3588-standard' || contains_token "$compatible_lower" 'topeet,rk3588'; then
                PROFILE_ID=topeet-rk3588
                TARGET_SOC=RK3588
                TARGET_LABEL=iTOP-RK3588
            elif contains_token "$target_lower" rk3588s; then
                TARGET_SOC=RK3588S
                if contains_token "$target_lower" topeet || contains_token "$target_lower" itop; then
                    PROFILE_ID=topeet-rk3588s
                    TARGET_LABEL=iTOP-RK3588S
                else
                    PROFILE_ID=generic-rk3588s
                    TARGET_LABEL="RK3588S Board"
                fi
            elif contains_token "$target_lower" rk3588; then
                TARGET_SOC=RK3588
                if contains_token "$target_lower" topeet || contains_token "$target_lower" itop; then
                    PROFILE_ID=topeet-rk3588
                    TARGET_LABEL=iTOP-RK3588
                else
                    PROFILE_ID=generic-rk3588
                    TARGET_LABEL="RK3588 Board"
                fi
            elif contains_token "$target_lower" rk3576_lga || contains_token "$target_lower" rk3576-lga; then
                TARGET_SOC=RK3576
                PROFILE_ID=topeet-rk3576-lga
                TARGET_LABEL="iTOP-RK3576 LGA"
            elif contains_token "$target_lower" rk3576; then
                TARGET_SOC=RK3576
                if contains_token "$target_lower" topeet || contains_token "$target_lower" itop; then
                    PROFILE_ID=topeet-rk3576
                    TARGET_LABEL="iTOP-RK3576 Standard"
                else
                    PROFILE_ID=generic-rk3576
                    TARGET_LABEL="RK3576 Board"
                fi
            elif contains_token "$target_lower" rk3568; then
                TARGET_SOC=RK3568
                if contains_token "$target_lower" topeet || contains_token "$target_lower" itop; then
                    PROFILE_ID=topeet-rk3568
                    TARGET_LABEL=iTOP-RK3568
                else
                    PROFILE_ID=generic-rk3568
                    TARGET_LABEL="RK3568 Board"
                fi
            elif contains_token "$target_lower" rk3562; then
                TARGET_SOC=RK3562
                if contains_token "$target_lower" topeet || contains_token "$target_lower" itop; then
                    PROFILE_ID=topeet-rk3562
                    TARGET_LABEL=iTOP-RK3562
                else
                    PROFILE_ID=generic-rk3562
                    TARGET_LABEL="RK3562 Board"
                fi
            else
                PROFILE_ID=generic-linux
                TARGET_SOC=unknown
                TARGET_LABEL="Generic Linux Board"
                PROFILE_SOURCE=runtime-fallback
            fi
            ;;
    esac

    case "$TARGET_SOC" in
        RK3562) TARGET_BASELINE="RK3562 Buildroot product target" ;;
        RK3588) TARGET_BASELINE="RK3588 Buildroot, Debian 11/12 or Ubuntu 20.04/22.04/24.04" ;;
        RK3568) TARGET_BASELINE="RK3568 Buildroot, Debian 11 or Ubuntu 20.04" ;;
        RK3576) TARGET_BASELINE="RK3576 Buildroot product target" ;;
        RK3588S) TARGET_BASELINE="RK3588S Buildroot product target" ;;
        *) TARGET_BASELINE="BoardLab Linux target" ;;
    esac
}

detect_profile

timestamp() {
    date -Iseconds 2>/dev/null || date
}

json_escape() {
    printf '%s' "$1" | sed 's/\\/\\\\/g; s/"/\\"/g'
}

reset_report() {
    # shellcheck disable=SC2094 # JSON_REPORT is written here; only its path string is expanded.
    {
        printf '# BoardLab Hardware Validation\n'
        printf 'generated: %s\n' "$(timestamp)"
        printf 'profile: %s\n' "$PROFILE_ID"
        printf 'profile source: %s\n' "$PROFILE_SOURCE"
        printf 'target: %s\n' "$TARGET_BASELINE"
        printf 'device tree model: %s\n' "${DT_MODEL:-unavailable}"
        printf 'device tree compatible: %s\n' "${DT_COMPATIBLE:-unavailable}"
        printf 'report: %s\n' "$REPORT"
        printf '\n'
    } > "$REPORT"
}

line() {
    printf '%s\n' "$*" | tee -a "$REPORT"
}

section() {
    line
    line "## $*"
}

record() {
    _record_status=$1
    _record_name=$2
    _record_detail=${3:-}
    case "$_record_status" in
        PASS) PASS_COUNT=$((PASS_COUNT + 1)) ;;
        WARN) WARN_COUNT=$((WARN_COUNT + 1)) ;;
        FAIL) FAIL_COUNT=$((FAIL_COUNT + 1)) ;;
        MANUAL) MANUAL_COUNT=$((MANUAL_COUNT + 1)) ;;
        SKIP) SKIP_COUNT=$((SKIP_COUNT + 1)) ;;
    esac
    if [ -n "$_record_detail" ]; then
        line "[$_record_status] $_record_name - $_record_detail"
    else
        line "[$_record_status] $_record_name"
    fi
}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

check_cmd() {
    if have_cmd "$1"; then
        record PASS "command: $1" "$(command -v "$1")"
    else
        record WARN "command: $1" "missing"
    fi
}

check_path() {
    if [ -e "$1" ]; then
        record PASS "path: $1" "present"
    else
        record WARN "path: $1" "missing"
    fi
}

check_glob() {
    label=$1
    pattern=$2
    found=0
    values=
    for item in $pattern; do
        if [ -e "$item" ]; then
            found=1
            values="${values}${values:+, }$item"
        fi
    done
    if [ "$found" = "1" ]; then
        record PASS "$label" "$values"
    else
        record WARN "$label" "no match: $pattern"
    fi
}

capture() {
    title=$1
    shift
    line
    line "\$ $*"
    "$@" >> "$REPORT" 2>&1
    code=$?
    if [ "$code" = "0" ]; then
        record PASS "$title" "command exited 0"
    else
        record WARN "$title" "command exited $code"
    fi
}

capture_sh() {
    title=$1
    command_text=$2
    line
    line "\$ $command_text"
    sh -c "$command_text" >> "$REPORT" 2>&1
    code=$?
    if [ "$code" = "0" ]; then
        record PASS "$title" "command exited 0"
    else
        record WARN "$title" "command exited $code"
    fi
}

timeout_prefix() {
    seconds=$1
    if have_cmd timeout; then
        printf 'timeout %s ' "$seconds"
    else
        printf ''
    fi
}

record_env_hint() {
    _env_name=$1
    _env_detail=$2
    eval "_env_value=\${$_env_name:-}"
    if [ -n "$_env_value" ]; then
        record PASS "env: $_env_name" "$_env_detail"
    else
        record MANUAL "env: $_env_name" "$_env_detail"
    fi
}

write_json_summary() {
    generated=$(timestamp)
    state=clear
    if [ "$FAIL_COUNT" -gt 0 ]; then
        state=fail
    elif [ "$WARN_COUNT" -gt 0 ] || [ "$MANUAL_COUNT" -gt 0 ]; then
        state=needs-review
    fi
    generated_escaped=$(json_escape "$generated")
    report_escaped=$(json_escape "$REPORT")
    json_report_escaped=$(json_escape "$JSON_REPORT")
    runner_hint_escaped=$(json_escape "$RUNNER_HINT")
    profile_escaped=$(json_escape "$PROFILE_ID")
    profile_source_escaped=$(json_escape "$PROFILE_SOURCE")
    target_escaped=$(json_escape "$TARGET_BASELINE")
    model_escaped=$(json_escape "$DT_MODEL")
    compatible_escaped=$(json_escape "$DT_COMPATIBLE")
    {
        printf '{\n'
        printf '  "generatedAt": "%s",\n' "$generated_escaped"
        printf '  "state": "%s",\n' "$state"
        printf '  "profile": "%s",\n' "$profile_escaped"
        printf '  "profileSource": "%s",\n' "$profile_source_escaped"
        printf '  "soc": "%s",\n' "$TARGET_SOC"
        printf '  "target": "%s",\n' "$target_escaped"
        printf '  "deviceTreeModel": "%s",\n' "$model_escaped"
        printf '  "deviceTreeCompatible": "%s",\n' "$compatible_escaped"
        printf '  "reportPath": "%s",\n' "$report_escaped"
        printf '  "jsonPath": "%s",\n' "$json_report_escaped"
        printf '  "pass": %s,\n' "$PASS_COUNT"
        printf '  "warn": %s,\n' "$WARN_COUNT"
        printf '  "fail": %s,\n' "$FAIL_COUNT"
        printf '  "manual": %s,\n' "$MANUAL_COUNT"
        printf '  "skipped": %s,\n' "$SKIP_COUNT"
        printf '  "runnerHint": "%s"\n' "$runner_hint_escaped"
        printf '}\n'
    } > "$JSON_REPORT"
}

reset_report

section "Board Profile"
record PASS "board profile" "$PROFILE_ID via $PROFILE_SOURCE"
record PASS "target baseline" "$TARGET_BASELINE"
if [ -n "$DT_MODEL$DT_COMPATIBLE" ]; then
    record PASS "device-tree evidence" "${DT_MODEL:-model unavailable}; ${DT_COMPATIBLE:-compatible unavailable}"
else
    record WARN "device-tree evidence" "${DEVICE_TREE_ROOT} has no readable model/compatible values"
fi

section "System Baseline"
capture "uname" uname -a
if have_cmd dpkg; then
    arch=$(dpkg --print-architecture 2>/dev/null || true)
else
    arch=$(uname -m)
fi
case "$arch" in
    arm64|aarch64) record PASS "CPU architecture" "$arch" ;;
    *) record WARN "CPU architecture" "$arch; expected arm64/aarch64 on $TARGET_LABEL target" ;;
esac

if [ -r "$OS_RELEASE_FILE" ]; then
    case "$TARGET_SOC:$os_id:$version_id" in
        RK3562:buildroot:*|RK3568:buildroot:*|RK3576:buildroot:*|RK3588:buildroot:*|RK3588S:buildroot:*|RK3568:ubuntu:20.04|RK3568:debian:11|RK3588:ubuntu:20.04|RK3588:ubuntu:22.04|RK3588:ubuntu:24.04|RK3588:debian:11|RK3588:debian:12|unknown:buildroot:*|unknown:ubuntu:20.04|unknown:ubuntu:22.04|unknown:ubuntu:24.04|unknown:debian:11|unknown:debian:12)
            record PASS "OS version" "$os_line"
            ;;
        *)
            record WARN "OS version" "${os_line:-unknown}; expected $TARGET_BASELINE"
            ;;
    esac
else
    record WARN "OS version" "$OS_RELEASE_FILE not readable"
fi

section "Build And Runtime Commands"
for cmd in cmake qmake qmllint make nmcli ping ifconfig route pkill speaker-test arecord aplay evtest gpioinfo gpioset gpioget mmcli ip ethtool v4l2-ctl amixer pactl stty timeout; do
    check_cmd "$cmd"
done
for cmd in candump cansend; do
    if have_cmd "$cmd"; then
        record PASS "optional command: $cmd" "$(command -v "$cmd")"
    else
        record WARN "optional command: $cmd" "missing; independent CAN demo uses Linux SocketCAN directly"
    fi
done

section "User Permissions"
capture "id" id
groups_output=$(id -nG 2>/dev/null || true)
for group_name in input video render dialout; do
    case " $groups_output " in
        *" $group_name "*) record PASS "group: $group_name" "current user belongs to $group_name" ;;
        *) record WARN "group: $group_name" "current user is not in $group_name" ;;
    esac
done

section "Graphics And Input"
check_path /dev/dri
check_glob "DRM cards" "/dev/dri/card*"
check_glob "DRM render nodes" "/dev/dri/renderD*"
check_path /dev/input
check_glob "input events" "/dev/input/event*"
case "$os_id" in
    buildroot)
        if [ -n "${WAYLAND_DISPLAY:-}" ]; then
            record PASS "Buildroot Wayland session" "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
        else
            record MANUAL "Buildroot Wayland session" "WAYLAND_DISPLAY is missing; validate after S49weston creates wayland-0"
        fi
        if [ -n "${DISPLAY:-}" ]; then
            record WARN "stale X11 environment" "DISPLAY=$DISPLAY is ignored on the Buildroot Weston/Wayland product path"
        fi
        ;;
    ubuntu|debian)
        if [ -n "${DISPLAY:-}" ]; then
            record PASS "Ubuntu/Debian X11 session" "DISPLAY=$DISPLAY"
        else
            record MANUAL "Ubuntu/Debian X11 session" "DISPLAY is missing; run validation from the active X11 desktop session"
        fi
        if [ -n "${WAYLAND_DISPLAY:-}" ]; then
            record WARN "stale Wayland environment" "WAYLAND_DISPLAY=$WAYLAND_DISPLAY is ignored; Ubuntu/Debian BoardLab uses xcb"
        fi
        ;;
    *)
        if [ -n "${DISPLAY:-}" ]; then
            record PASS "X11 session" "DISPLAY=$DISPLAY"
        elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
            record PASS "Wayland session" "WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
        else
            record MANUAL "display session" "no DISPLAY/WAYLAND_DISPLAY for the unrecognized OS profile"
        fi
        ;;
esac
if [ -r /proc/bus/input/devices ]; then
    if grep -Eiq 'touch|goodix|gt9|edt|ft5|ili|hid.*touch' /proc/bus/input/devices; then
        record PASS "touch device hint" "touch-like input device found in /proc/bus/input/devices"
    else
        record MANUAL "touch device hint" "no obvious touch name; confirm with evtest and UI gestures"
    fi
    capture_sh "input device inventory" "sed -n '1,220p' /proc/bus/input/devices"
else
    record WARN "input device inventory" "/proc/bus/input/devices not readable"
fi
record MANUAL "display UI validation" "run $RUNNER_HINT; confirm full-screen rendering, no black screen, no touch offset, direct Home launcher entry, and no phone-style bottom navigation"

section "Backlight And Thermal"
check_glob "backlight devices" "/sys/class/backlight/*"
for brightness in /sys/class/backlight/*/brightness; do
    if [ -r "$brightness" ]; then
        value=$(cat "$brightness" 2>/dev/null || true)
        if [ -w "$brightness" ]; then
            record PASS "backlight brightness" "$brightness=$value; writable"
        else
            record WARN "backlight brightness" "$brightness=$value; not writable by current user"
        fi
    fi
done
check_glob "thermal zones" "/sys/class/thermal/thermal_zone*"

section "Network"
if have_cmd ip; then
    capture "network links" ip -brief link
    capture "network addresses" ip -brief addr
    capture "network routes" ip route
fi
if have_cmd nmcli; then
    capture "NetworkManager devices" nmcli device
    capture "NetworkManager connections" nmcli -f NAME,TYPE,DEVICE connection show
else
    record WARN "NetworkManager devices" "nmcli missing"
fi
check_glob "ethernet sysfs" "/sys/class/net/e*"
check_glob "wireless sysfs" "/sys/class/net/wl*"
check_glob "wwan sysfs" "/sys/class/net/ww*"
for iface in /sys/class/net/e*; do
    if [ -d "$iface" ]; then
        name=$(basename "$iface")
        carrier=$(cat "$iface/carrier" 2>/dev/null || printf 'unknown')
        operstate=$(cat "$iface/operstate" 2>/dev/null || printf 'unknown')
        record PASS "ethernet link: $name" "carrier=$carrier operstate=$operstate"
        if have_cmd ethtool; then
            capture_sh "ethernet ethtool: $name" "ethtool '$name' 2>/dev/null | sed -n '1,80p'"
        fi
    fi
done
if [ -n "$ETH_PING_TARGET" ] && have_cmd ping; then
    for iface in /sys/class/net/e*; do
        if [ -d "$iface" ]; then
            name=$(basename "$iface")
            capture_sh "ethernet ping: $name -> $ETH_PING_TARGET" "ping -I '$name' -c 2 -W 3 '$ETH_PING_TARGET'"
        fi
    done
else
    record_env_hint BOARDLAB_ETH_PING_TARGET "set to an IP/host to run interface-bound Ethernet ping evidence; legacy RK3588_ETH_PING_TARGET/RK3568_ETH_PING_TARGET are also accepted"
fi
record MANUAL "Ethernet validation" "open the Ethernet demo, confirm the same iface/MAC/IPv4 and compare Ping status/log with this report"

section "Serial, CAN, GPIO, Cellular"
check_glob "UART ttyS" "/dev/ttyS*"
check_glob "UART ttyFIQ" "/dev/ttyFIQ*"
check_glob "UART ttyUSB" "/dev/ttyUSB*"
check_glob "UART ttyACM" "/dev/ttyACM*"
check_glob "CAN netdev" "/sys/class/net/can*"
check_glob "virtual CAN netdev" "/sys/class/net/vcan*"
if have_cmd ip; then
    capture_sh "CAN detailed links" "ip -details link show type can 2>/dev/null || true"
fi
if [ -n "$UART_DEVICE" ]; then
    if [ -e "$UART_DEVICE" ]; then
        record PASS "UART selected device" "$UART_DEVICE"
        if have_cmd stty; then
            capture_sh "UART stty: $UART_DEVICE" "stty -F '$UART_DEVICE' -a"
        fi
    else
        record WARN "UART selected device" "$UART_DEVICE missing"
    fi
else
    record_env_hint BOARDLAB_UART_DEVICE "set to /dev/ttyS* or /dev/ttyUSB* to capture permissions/stty evidence; legacy RK3588_UART_DEVICE/RK3568_UART_DEVICE are also accepted"
fi
if [ -n "$CAN_IFACE" ]; then
    if [ -d "/sys/class/net/$CAN_IFACE" ]; then
        record PASS "CAN selected interface" "$CAN_IFACE"
        capture_sh "CAN selected link detail: $CAN_IFACE" "ip -details link show '$CAN_IFACE'"
        capture_sh "CAN counters: $CAN_IFACE" "ip -statistics link show '$CAN_IFACE'"
    else
        record WARN "CAN selected interface" "$CAN_IFACE missing"
    fi
else
    record_env_hint BOARDLAB_CAN_IFACE "set to can0/can1 to capture bitrate/error-counter evidence; legacy RK3588_CAN_IFACE/RK3568_CAN_IFACE are also accepted"
fi
check_path /sys/class/gpio
check_glob "cellular control nodes" "/dev/cdc-wdm*"
if have_cmd mmcli; then
    capture "ModemManager modem list" mmcli -L
fi
record MANUAL "GPIO validation" "confirm safe pin mapping before using the GPIO demo or sysfs writes"
record MANUAL "UART validation" "connect loopback or peer device, select matching baud/data/parity/stop/flow parameters, then use the UART demo TX and RX sample"
record MANUAL "CAN validation" "connect a CAN transceiver/peer, then configure bitrate/dbitrate and verify the CAN demo SocketCAN TX/RX plus loop sender"
record MANUAL "4G validation" "insert SIM/antenna, set APN/interface if needed, then use the 4G demo Init, Check, Ping, Stop"

section "Camera And Watchdog"
check_glob "video devices" "/dev/video*"
check_glob "watchdog devices" "/dev/watchdog*"
if have_cmd v4l2-ctl; then
    capture "V4L2 devices" v4l2-ctl --list-devices
    for video_node in /dev/video*; do
        if [ -e "$video_node" ]; then
            capture_sh "V4L2 caps: $video_node" "v4l2-ctl -d '$video_node' --all 2>/dev/null | sed -n '1,120p'"
        fi
    done
fi
if have_cmd wdctl; then
    capture "watchdog control" wdctl
fi
if [ -n "$CAMERA_DEVICE" ]; then
    if have_cmd v4l2-ctl; then
        capture_sh "camera opt-in: $CAMERA_DEVICE" "$(timeout_prefix 8)v4l2-ctl -d '$CAMERA_DEVICE' --stream-mmap --stream-count=30 --stream-to=/dev/null"
    else
        record WARN "camera opt-in" "v4l2-ctl missing"
    fi
else
    record_env_hint BOARDLAB_CAMERA_DEVICE "set to a detected /dev/video* node to run timed V4L2 capture evidence; legacy RK3588_CAMERA_DEVICE/RK3568_CAMERA_DEVICE are also accepted"
fi
record MANUAL "camera validation" "open the Camera demo, confirm the selectable node list matches V4L2 caps above, start preview, confirm frames update, and stop preview"
record MANUAL "watchdog validation" "only when reset is acceptable, open the Watchdog demo and run the built-in watchdog test with a safe timeout"

section "Audio"
if have_cmd aplay; then
    capture "ALSA playback devices" aplay -l
fi
if have_cmd arecord; then
    capture "ALSA capture devices" arecord -l
fi
if [ "$AUDIO_SMOKE" = "1" ]; then
    if have_cmd speaker-test; then
        capture_sh "audio speaker-test short run" "$(timeout_prefix 4)speaker-test -t sine -f 1000 -l 1"
    fi
    if have_cmd arecord; then
        capture_sh "audio record 2s" "arecord -q -d 2 -f S16_LE -r 48000 -c 2 '$AUDIO_VALIDATE_FILE'"
    fi
    if have_cmd aplay && [ -f "$AUDIO_VALIDATE_FILE" ]; then
        capture_sh "audio playback recorded sample" "$(timeout_prefix 4)aplay -q '$AUDIO_VALIDATE_FILE'"
    fi
    capture_sh "audio stuck process check" "pgrep -af 'aplay|arecord|speaker-test' || true"
else
    record_env_hint BOARDLAB_AUDIO_SMOKE "set to 1 to run short speaker/record/playback evidence; legacy RK3588_AUDIO_SMOKE/RK3568_AUDIO_SMOKE are also accepted"
fi
record MANUAL "speaker validation" "open the Audio demo and confirm speaker-test is audible"
record MANUAL "microphone validation" "record 2s in the Audio demo and confirm playback contains microphone input"

section "Profile-Specific Scope"
case "$PROFILE_ID" in
    topeet-rk3588)
        check_glob "RK3588 board LEDs" "/sys/class/leds/work1 /sys/class/leds/work2"
        record MANUAL "HDMI input validation" "select a discovered HDMIRX capture node in the HDMI input demo and confirm the preview updates"
        ;;
    topeet-rk3568)
        record SKIP "LED control" "RK3568 Linux manual uses LED9 trigger/GPIO122; BoardLab deliberately does not expose that unsafe legacy path"
        record SKIP "HDMI input capture" "RK3568 Linux manual covers HDMI display output, not capture; this profile declares hdmiin=false"
        ;;
    topeet-rk3562|topeet-rk3568-mini|topeet-rk3576|topeet-rk3576-lga|topeet-rk3588-mini|topeet-rk3588s|generic-rk3562|generic-rk3568|generic-rk3576|generic-rk3588s|generic-rk3588)
        record SKIP "board-specific controls" "this profile has no BoardLab hardware-wiring configuration; validate only discovered runtime nodes"
        ;;
    *)
        record SKIP "board-specific controls" "no supported RK3568/RK3588 profile was identified from device-tree evidence"
        ;;
esac

section "BoardLab Artifacts"
if [ -x "$BUILD_DIR/boardlab" ]; then
    record PASS "application binary" "$BUILD_DIR/boardlab"
elif [ -x "$ROOT_DIR/bin/boardlab" ]; then
    record PASS "application binary" "$ROOT_DIR/bin/boardlab"
else
    record FAIL "application binary" "not found in build/ or bin/"
fi
if [ -x "$ROOT_DIR/scripts/run.sh" ]; then
    record PASS "runner script" "$ROOT_DIR/scripts/run.sh"
elif [ -x "$ROOT_DIR/bin/boardlab-run" ]; then
    record PASS "runner script" "$ROOT_DIR/bin/boardlab-run"
else
    record FAIL "runner script" "not found in scripts/ or bin/"
fi
record PASS "hardware validation report" "$REPORT"

section "Summary"
line "PASS=$PASS_COUNT WARN=$WARN_COUNT FAIL=$FAIL_COUNT MANUAL=$MANUAL_COUNT SKIP=$SKIP_COUNT"
line "Report saved to: $REPORT"
write_json_summary
line "JSON saved to: $JSON_REPORT"

if [ "$FAIL_COUNT" -gt 0 ]; then
    exit 1
fi

exit 0
