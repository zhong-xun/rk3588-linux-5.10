#!/usr/bin/env sh
# shellcheck disable=SC2016,SC2249,SC2250
set -eu

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")" && pwd)
PREFIX=${PREFIX:-/opt/boardlab}
SERVICE_NAME=${SERVICE_NAME:-boardlab.service}
WIFI_HELPER_PATH=/usr/lib/boardlab/boardlab-wifi
WIFI_SUDOERS_PATH=/etc/sudoers.d/boardlab-wifi
WIFI_HELPER_READY_PATH=/etc/boardlab/wifi-helper.enabled
NPU_HELPER_PATH=/usr/lib/boardlab/boardlab-npu-load
NPU_SUDOERS_PATH=/etc/sudoers.d/boardlab-npu
NPU_HELPER_READY_PATH=/etc/boardlab/npu-load-helper.enabled
WEB_TOKEN_FILE=${BOARDLAB_WEB_TOKEN_FILE:-/etc/boardlab/web.token}
WEB_TOKEN_VALUE=${BOARDLAB_WEB_TOKEN:-}
DEFAULT_WEB_TOKEN=${BOARDLAB_DEFAULT_WEB_TOKEN:-topeet}
INSTALL_DEPS=${INSTALL_DEPS:-1}
INSTALL_SERVICE=${INSTALL_SERVICE:-1}
FORCE_SERVICE_INSTALL=${FORCE_SERVICE_INSTALL:-0}
ENABLE_SERVICE=${ENABLE_SERVICE:-0}
DISABLE_SERVICE=${DISABLE_SERVICE:-1}
START_SERVICE=${START_SERVICE:-0}
RUN_HARDWARE_VALIDATE=${RUN_HARDWARE_VALIDATE:-0}
CHECK_ONLY=0
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build"}
OS_RELEASE_FILE=${OS_RELEASE_FILE:-/etc/os-release}

first_regular_home_user() {
    for home_path in /home/*; do
        [ -d "$home_path" ] || continue
        home_user=$(basename "$home_path")
        if id "$home_user" >/dev/null 2>&1 && [ "$(id -u "$home_user")" -ge 1000 ]; then
            printf '%s\n' "$home_user"
            return 0
        fi
    done
    return 1
}

default_service_user() {
    if [ -n "${BOARDLAB_USER:-}" ]; then
        printf '%s\n' "$BOARDLAB_USER"
    elif [ -n "${RK_SHELL_USER:-}" ]; then
        printf '%s\n' "$RK_SHELL_USER"
    elif [ -n "${SUDO_USER:-}" ] && id "$SUDO_USER" >/dev/null 2>&1; then
        printf '%s\n' "$SUDO_USER"
    elif [ "$(id -u)" -ne 0 ] && id "$(id -un)" >/dev/null 2>&1; then
        id -un
    elif command -v pgrep >/dev/null 2>&1; then
        session_pid=$(pgrep -x xfce4-session 2>/dev/null | head -n 1 || true)
        session_user=
        if [ -n "$session_pid" ]; then
            session_user=$(stat -c %U "/proc/$session_pid" 2>/dev/null || true)
        fi
        if [ -n "$session_user" ] && [ "$session_user" != root ] && id "$session_user" >/dev/null 2>&1; then
            printf '%s\n' "$session_user"
        elif id topeet >/dev/null 2>&1; then
            printf '%s\n' topeet
        elif first_regular_home_user; then
            :
        else
            id -un
        fi
    elif id topeet >/dev/null 2>&1; then
        printf '%s\n' topeet
    elif first_regular_home_user; then
        :
    else
        id -un
    fi
}

SERVICE_USER=${SERVICE_USER:-$(default_service_user)}

user_home_dir() {
    user_name=$1
    if [ "$user_name" = "root" ]; then
        printf '%s\n' /root
        return
    fi
    if command -v getent >/dev/null 2>&1; then
        home_dir=$(getent passwd "$user_name" | cut -d: -f6)
        if [ -n "$home_dir" ]; then
            printf '%s\n' "$home_dir"
            return
        fi
    fi
    printf '/home/%s\n' "$user_name"
}

user_desktop_dir() {
    user_name=$1
    user_home=$(user_home_dir "$user_name")
    if command -v xdg-user-dir >/dev/null 2>&1; then
        if [ "$(id -u)" -eq 0 ] && [ "$user_name" != root ] && command -v runuser >/dev/null 2>&1; then
            xdg_desktop=$(runuser -u "$user_name" -- env HOME="$user_home" xdg-user-dir DESKTOP 2>/dev/null || true)
        else
            xdg_desktop=$(HOME="$user_home" xdg-user-dir DESKTOP 2>/dev/null || true)
        fi
        case "$xdg_desktop" in
            /*)
                printf '%s\n' "$xdg_desktop"
                return
                ;;
        esac
    fi
    user_dirs_file="$user_home/.config/user-dirs.dirs"
    if [ -r "$user_dirs_file" ]; then
        xdg_desktop=$(sed -n 's/^XDG_DESKTOP_DIR=//p' "$user_dirs_file" | tail -n 1 | tr -d '"')
        if [ -n "$xdg_desktop" ]; then
            case "$xdg_desktop" in
                '$HOME'/*)
                    printf '%s/%s\n' "$user_home" "${xdg_desktop#'$HOME'/}"
                    return
                    ;;
                "$user_home"/*)
                    printf '%s\n' "$xdg_desktop"
                    return
                    ;;
                /*)
                    printf '%s\n' "$xdg_desktop"
                    return
                    ;;
            esac
        fi
    fi
    if [ -d "$user_home/Desktop" ]; then
        printf '%s\n' "$user_home/Desktop"
        return
    fi
    if [ -d "$user_home/桌面" ]; then
        printf '%s\n' "$user_home/桌面"
        return
    fi
    printf '%s\n' "$user_home/Desktop"
}

APT_REQUIRED_PACKAGES="
build-essential
cmake
ninja-build
pkg-config
qtbase5-dev
qtdeclarative5-dev
qtquickcontrols2-5-dev
qml-module-qtquick2
qml-module-qtquick-window2
qml-module-qtquick-controls2
qml-module-qtquick-layouts
qml-module-qtmultimedia
libqt5multimedia5-plugins
gstreamer1.0-tools
gstreamer1.0-plugins-base
gstreamer1.0-plugins-good
libgstreamer1.0-dev
libgstreamer-plugins-base1.0-dev
libxcb-xinerama0
libxkbcommon-x11-0
libqt5svg5
policykit-1
sudo
desktop-file-utils
gtk-update-icon-cache
libglib2.0-bin
xdg-user-dirs
fonts-noto-cjk
fonts-wqy-microhei
network-manager
bluez
modemmanager
net-tools
iproute2
iputils-ping
procps
rfkill
util-linux
isc-dhcp-client
alsa-utils
"

APT_OPTIONAL_PACKAGES="
libopencv-core-dev
libopencv-imgproc-dev
libopencv-imgcodecs-dev
libopencv-videoio-dev
can-utils
libinput-tools
evtest
gpiod
ethtool
libdrm-tests
libxcb-cursor0
v4l-utils
pulseaudio-utils
"

log() {
    printf '\n== %s ==\n' "$*"
}

info() {
    printf '%s\n' "$*"
}

package_installed() {
    dpkg-query -W -f='${Status}' "$1" 2>/dev/null | grep -q 'install ok installed'
}

package_available() {
    apt-cache show "$1" >/dev/null 2>&1
}

systemd_available() {
    command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]
}

udev_available() {
    command -v udevadm >/dev/null 2>&1 && [ -S /run/udev/control ]
}

x11_display_available() {
    [ -S /tmp/.X11-unix/X0 ] || [ -e /tmp/.X11-unix/X0 ]
}

modetest_present() {
    command -v modetest >/dev/null 2>&1 && return 0
    [ -e /usr/bin/modetest ] && return 0
    [ -e /usr/local/bin/modetest ] && return 0
    dpkg-query -S /usr/bin/modetest >/dev/null 2>&1 && return 0
    return 1
}

modetest_path() {
    if command -v modetest >/dev/null 2>&1; then
        command -v modetest
    elif [ -e /usr/bin/modetest ]; then
        printf '%s\n' /usr/bin/modetest
    elif [ -e /usr/local/bin/modetest ]; then
        printf '%s\n' /usr/local/bin/modetest
    else
        dpkg-query -S /usr/bin/modetest 2>/dev/null | head -n 1 || printf '%s\n' /usr/bin/modetest
    fi
}

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        if command -v sudo >/dev/null 2>&1; then
            exec sudo -E "$0" "$@"
        fi
        printf 'This installer needs root. Re-run as root or install sudo.\n' >&2
        exit 1
    fi
}

install_web_token() {
    log "Web access token"
    token_dir=$(dirname -- "$WEB_TOKEN_FILE")
    service_group=$(id -gn "$SERVICE_USER")
    if [ ! -d "$token_dir" ]; then
        install -d -m 0750 -o root -g "$service_group" "$token_dir"
    elif [ "$token_dir" = "/etc/boardlab" ]; then
        chown root:"$service_group" "$token_dir"
        chmod 0750 "$token_dir"
    fi

    token_source=default
    if [ -n "$WEB_TOKEN_VALUE" ]; then
        web_token=$WEB_TOKEN_VALUE
        token_source=environment
    elif [ -s "$WEB_TOKEN_FILE" ]; then
        web_token=$(sed -n '1p' "$WEB_TOKEN_FILE" | tr -d '\r\n')
        token_source=existing
    else
        web_token=$DEFAULT_WEB_TOKEN
    fi

    single_line_token=$(printf '%s' "$web_token" | tr -d '\r\n')
    if [ "$single_line_token" != "$web_token" ]; then
        info "error: Web token must be a single line"
        exit 1
    fi
    token_length=${#web_token}
    if [ "$token_length" -lt 1 ] || [ "$token_length" -gt 256 ]; then
        info "error: Web token must contain 1 to 256 characters"
        exit 1
    fi
    case "$web_token" in
        *[!A-Za-z0-9._~-]*)
            info "error: Web token may contain only letters, numbers, dot, underscore, tilde and hyphen"
            exit 1
            ;;
    esac

    token_tmp=$(mktemp)
    printf '%s\n' "$web_token" > "$token_tmp"
    install -m 0640 -o root -g "$service_group" "$token_tmp" "$WEB_TOKEN_FILE"
    rm -f "$token_tmp"
    info "Web token ready: $WEB_TOKEN_FILE ($token_source, value hidden)"
    info "read token: sudo cat $WEB_TOKEN_FILE"
}

detect_target() {
    log "Target"
    arch=$(dpkg --print-architecture 2>/dev/null || uname -m)
    os_name=unknown
    os_version=unknown
    if [ -r "$OS_RELEASE_FILE" ]; then
        os_name=$(grep '^ID=' "$OS_RELEASE_FILE" | cut -d= -f2- | tr -d '"' || true)
        os_version=$(grep '^VERSION_ID=' "$OS_RELEASE_FILE" | cut -d= -f2- | tr -d '"' || true)
    else
        info "error: cannot read OS release file: $OS_RELEASE_FILE"
        exit 1
    fi

    if ! id "$SERVICE_USER" >/dev/null 2>&1; then
        info "error: desktop/service user does not exist: $SERVICE_USER"
        exit 1
    fi

    info "arch: $arch"
    info "os: ${os_name:-unknown} ${os_version:-unknown}"
    info "install prefix: $PREFIX"
    info "service user: $SERVICE_USER"
    info "build dir: $BUILD_DIR"

    case "$BUILD_DIR" in
        "$ROOT_DIR"/*) ;;
        *) info "warning: BUILD_DIR is outside the current source tree; unset BUILD_DIR to use $ROOT_DIR/build." ;;
    esac

    case "$arch" in
        arm64|aarch64) ;;
        *) info "warning: RK3588 target is normally arm64/aarch64; continuing." ;;
    esac
    case "$os_name:$os_version" in
        buildroot:*)
            info "error: Buildroot uses the BoardLab Buildroot package and SysV/Weston startup; do not run autorun.sh."
            info "Build with: cd ~/rk3588s-project/rk3588s-linux-6.1/buildroot && make O=output/topeet_rk3588s/rockchip_rk3588 boardlab"
            exit 64
            ;;
        ubuntu:20.04|ubuntu:22.04|ubuntu:24.04|debian:11|debian:12)
            info "supported OS target: $os_name $os_version"
            ;;
        *)
            info "warning: supported targets are Ubuntu 20.04/22.04/24.04 and Debian 11/12; continuing."
            ;;
    esac
}

install_optional_package() {
    package_name=$1
    if package_installed "$package_name"; then
        info "optional package already installed: $package_name"
        return 0
    fi
    if [ "$package_name" = "libdrm-tests" ] && modetest_present; then
        info "optional package skipped: libdrm-tests; modetest already exists at $(modetest_path)"
        return 0
    fi
    if package_available "$package_name"; then
        if apt-get install -y "$package_name" >/dev/null 2>&1; then
            info "optional package installed: $package_name"
            return 0
        fi
        info "optional package skipped: $package_name; install did not complete"
        dpkg --configure -a || true
        return 1
    fi
    info "optional package unavailable, skipped: $package_name"
    return 1
}

install_optional_command_candidates() {
    command_name=$1
    shift
    if command -v "$command_name" >/dev/null 2>&1; then
        info "optional command already exists: $command_name"
        return
    fi
    for package_name in "$@"; do
        if install_optional_package "$package_name"; then
            if command -v "$command_name" >/dev/null 2>&1; then
                return
            fi
        fi
    done
    info "optional command unavailable after candidate packages, skipped: $command_name"
}

install_dependencies() {
    if [ "$INSTALL_DEPS" != "1" ]; then
        info "dependency install skipped: INSTALL_DEPS=$INSTALL_DEPS"
        return
    fi
    if ! command -v apt-get >/dev/null 2>&1; then
        info "apt-get not found; dependency install skipped."
        return
    fi

    log "Install dependencies"
    export DEBIAN_FRONTEND=${DEBIAN_FRONTEND:-noninteractive}
    required_packages=$APT_REQUIRED_PACKAGES
    if [ "$os_name:$os_version" = "ubuntu:24.04" ]; then
        # Noble moved hwclock, used by the RTC diagnostic, out of util-linux.
        required_packages="$required_packages util-linux-extra"
    fi
    missing_required=
    unavailable_required=
    for package_name in $required_packages; do
        if package_installed "$package_name"; then
            info "required package already installed: $package_name"
        else
            missing_required="${missing_required}${missing_required:+ }$package_name"
        fi
    done

    if [ -n "$missing_required" ]; then
        dpkg --configure -a || true
        if apt-get update; then
            apt_updated=1
        else
            apt_updated=0
            info "apt-get update failed; checking cached metadata before stopping."
        fi
        for package_name in $missing_required; do
            if ! package_available "$package_name"; then
                unavailable_required="${unavailable_required}${unavailable_required:+ }$package_name"
            fi
        done
        if [ -n "$unavailable_required" ]; then
            info "required packages unavailable in current apt metadata: $unavailable_required"
            if [ "$apt_updated" = "0" ]; then
                info "apt metadata may be stale because apt-get update failed."
            fi
            exit 1
        fi
        # shellcheck disable=SC2086
        apt-get install -y $missing_required
    else
        info "all required packages are already installed; apt-get update skipped"
    fi

    for package_name in $APT_OPTIONAL_PACKAGES; do
        install_optional_package "$package_name" || true
    done
    install_optional_command_candidates glmark2-es2 glmark2-es2 glmark2-es2-x11
    install_optional_command_candidates eglinfo mesa-utils-extra mesa-utils mesa-utils-bin
}

ensure_groups() {
    log "User groups"
    for group_name in input video render dialout audio netdev gpio boardlab-wifi boardlab-npu; do
        if ! getent group "$group_name" >/dev/null 2>&1; then
            groupadd -r "$group_name" 2>/dev/null || true
        fi
        if id "$SERVICE_USER" >/dev/null 2>&1; then
            usermod -aG "$group_name" "$SERVICE_USER" 2>/dev/null || true
        fi
    done
    info "ensured useful groups for $SERVICE_USER: input video render dialout audio netdev gpio boardlab-wifi boardlab-npu"
    info "group changes require logout/login or reboot to affect existing sessions."
}

debugfs_is_mounted() {
    if command -v mountpoint >/dev/null 2>&1; then
        mountpoint -q /sys/kernel/debug
        return
    fi
    grep -qs '[[:space:]]/sys/kernel/debug[[:space:]]' /proc/mounts
}

ensure_npu_debugfs() {
    log "RKNPU telemetry"
    if [ "${BOARDLAB_MOUNT_DEBUGFS:-1}" != "1" ]; then
        info "RKNPU debugfs mount skipped: BOARDLAB_MOUNT_DEBUGFS=${BOARDLAB_MOUNT_DEBUGFS}"
        return
    fi

    if ! debugfs_is_mounted; then
        if ! mkdir -p /sys/kernel/debug 2>/dev/null; then
            info "warning: could not create /sys/kernel/debug; RKNPU utilization charts may be unavailable"
            return
        fi
        if mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null; then
            info "mounted debugfs for RKNPU utilization telemetry"
        else
            info "warning: could not mount debugfs; RKNPU utilization charts may be unavailable"
            return
        fi
    else
        info "debugfs is already mounted for RKNPU utilization telemetry"
    fi

    if [ -r /sys/kernel/debug/rknpu/load ]; then
        info "RKNPU load node is available (the active SoC determines one, two, or three core readings)"
    else
        info "warning: /sys/kernel/debug/rknpu/load is unavailable; the YOLOv8 demo can run but utilization charts may be unavailable"
    fi
}

repair_service_build_ownership() {
    # A root-initiated scripts/build.sh can leave generated CMake subtrees
    # owned by root even when the build directory itself is writable by the
    # desktop user.  autorun intentionally builds as that user, so repair
    # only a project-local build tree before handing it to runuser.
    [ -d "$BUILD_DIR" ] || return 0
    case "$BUILD_DIR" in
        "$ROOT_DIR"/*)
            ;;
        *)
            return 0
            ;;
    esac

    if find "$BUILD_DIR" -xdev ! -user "$SERVICE_USER" -print -quit 2>/dev/null | grep -q .; then
        service_group=$(id -gn "$SERVICE_USER")
        info "repairing root-owned build artifacts for $SERVICE_USER: $BUILD_DIR"
        chown -R "$SERVICE_USER:$service_group" "$BUILD_DIR"
    fi
}

build_and_stage() {
    log "Build and install application"
    if [ "$(id -u)" -eq 0 ] && id "$SERVICE_USER" >/dev/null 2>&1 && command -v runuser >/dev/null 2>&1 && [ "$SERVICE_USER" != "root" ]; then
        build_parent=${BUILD_DIR%/*}
        [ "$build_parent" = "$BUILD_DIR" ] && build_parent=$ROOT_DIR
        repair_service_build_ownership
        if runuser -u "$SERVICE_USER" -- test -w "$ROOT_DIR" &&
            { [ -d "$BUILD_DIR" ] && runuser -u "$SERVICE_USER" -- test -w "$BUILD_DIR" || [ ! -e "$BUILD_DIR" ] && runuser -u "$SERVICE_USER" -- test -w "$build_parent"; }; then
            info "building as $SERVICE_USER to avoid root-owned build files"
            runuser -u "$SERVICE_USER" -- env \
                BUILD_DIR="$BUILD_DIR" \
                BOARDLAB_REQUIRE_GSTREAMER_APPSINK=ON \
                "$ROOT_DIR/scripts/build.sh"
            BUILD_DIR="$BUILD_DIR" PREFIX="$PREFIX" SKIP_BUILD=1 "$ROOT_DIR/scripts/deploy.sh"
            return
        fi
        info "source tree or build dir is not writable by $SERVICE_USER; building as root"
    fi
    BUILD_DIR="$BUILD_DIR" PREFIX="$PREFIX" \
        BOARDLAB_REQUIRE_GSTREAMER_APPSINK=ON \
        "$ROOT_DIR/scripts/deploy.sh"
}


install_system_files() {
    log "Install system files"
    systemd_dir=
    udev_dir=
    desktop_dir=
    icon_dir=
    polkit_action_dir=
    polkit_rules_dir=
    polkit_pkla_dir=
    if [ "$INSTALL_SERVICE" = "1" ]; then
        if install -d /etc/systemd/system 2>/dev/null; then
            systemd_dir=/etc/systemd/system
        else
            info "systemd unit install skipped: /etc/systemd/system is not writable"
        fi
    else
        info "systemd unit install skipped: INSTALL_SERVICE=$INSTALL_SERVICE; existing service files are left unchanged"
    fi
    if install -d /etc/udev/rules.d 2>/dev/null; then
        udev_dir=/etc/udev/rules.d
    else
        info "udev rules install skipped: /etc/udev/rules.d is not writable"
    fi
    if install -d /usr/share/applications 2>/dev/null; then
        desktop_dir=/usr/share/applications
    else
        info "desktop file install skipped: /usr/share/applications is not writable"
    fi
    if install -d /usr/share/icons/hicolor/scalable/apps 2>/dev/null; then
        icon_dir=/usr/share/icons/hicolor/scalable/apps
    else
        info "application icon install skipped: icon directory is not writable"
    fi
    if install -d /usr/share/polkit-1/actions 2>/dev/null; then
        polkit_action_dir=/usr/share/polkit-1/actions
    else
        info "polkit action install skipped: /usr/share/polkit-1/actions is not writable"
    fi
    if install -d /etc/polkit-1/rules.d 2>/dev/null; then
        polkit_rules_dir=/etc/polkit-1/rules.d
    else
        info "polkit rule install skipped: /etc/polkit-1/rules.d is not writable"
    fi
    if install -d /etc/polkit-1/localauthority/50-local.d 2>/dev/null; then
        polkit_pkla_dir=/etc/polkit-1/localauthority/50-local.d
    else
        info "polkit local authority install skipped: /etc/polkit-1/localauthority/50-local.d is not writable"
    fi

    if [ -n "$systemd_dir" ] && [ -f "$PREFIX/lib/systemd/system/$SERVICE_NAME" ]; then
        if [ -e "$systemd_dir/$SERVICE_NAME" ] && [ "$FORCE_SERVICE_INSTALL" != "1" ]; then
            info "systemd unit exists, left unchanged: $systemd_dir/$SERVICE_NAME"
            info "use --force-service-install to overwrite the packaged $SERVICE_NAME unit"
        else
            tmp_service=$(mktemp)
            sed \
                -e "s|^User=.*|User=$SERVICE_USER|" \
                -e "s|^WorkingDirectory=.*|WorkingDirectory=$PREFIX|" \
                -e "s|^Environment=BOARDLAB_WEB_TOKEN_FILE=.*|Environment=BOARDLAB_WEB_TOKEN_FILE=$WEB_TOKEN_FILE|" \
                -e "s|/opt/boardlab|$PREFIX|g" \
                "$PREFIX/lib/systemd/system/$SERVICE_NAME" > "$tmp_service"
            install -m 0644 "$tmp_service" "$systemd_dir/$SERVICE_NAME"
            rm -f "$tmp_service"
            info "installed $systemd_dir/$SERVICE_NAME"
        fi
    elif [ "$INSTALL_SERVICE" = "1" ]; then
        info "service template unavailable or systemd directory skipped: $PREFIX/lib/systemd/system/$SERVICE_NAME"
    fi

    # Older releases installed a fixed pwmchip helper at this app-owned path.
    # The kernel pwm-beeper driver now owns every supported buzzer channel.
    rm -f /usr/lib/boardlab/boardlab-pwm-init

    rm -f "$WIFI_HELPER_READY_PATH"
    wifi_helper_source="$PREFIX/lib/boardlab/boardlab-wifi"
    wifi_sudoers_source="$PREFIX/share/boardlab/boardlab-wifi.sudoers"
    if [ -f "$wifi_helper_source" ]; then
        if install -d -o root -g root -m 0755 /usr/lib/boardlab && \
            install -o root -g root -m 0755 "$wifi_helper_source" "$WIFI_HELPER_PATH"; then
            info "installed restricted Wi-Fi helper: $WIFI_HELPER_PATH"
            if [ -f "$wifi_sudoers_source" ] && command -v visudo >/dev/null 2>&1; then
                if visudo -cf "$wifi_sudoers_source" >/dev/null 2>&1 && \
                    install -d -o root -g root -m 0750 /etc/sudoers.d && \
                    install -o root -g root -m 0440 "$wifi_sudoers_source" "$WIFI_SUDOERS_PATH"; then
                    info "installed restricted Wi-Fi sudo policy: $WIFI_SUDOERS_PATH"
                    wifi_helper_marker=$(mktemp)
                    printf '%s\n' 'BoardLab Wi-Fi helper is ready.' > "$wifi_helper_marker"
                    if install -o root -g "$(id -gn "$SERVICE_USER")" -m 0640 \
                        "$wifi_helper_marker" "$WIFI_HELPER_READY_PATH"; then
                        info "enabled restricted Wi-Fi helper: $WIFI_HELPER_READY_PATH"
                    else
                        info "warning: restricted Wi-Fi helper readiness marker was not installed; NetworkManager policy fallback will be used"
                    fi
                    rm -f "$wifi_helper_marker"
                else
                    info "warning: restricted Wi-Fi sudo policy was not installed; rerun after fixing sudo/visudo"
                fi
            else
                info "warning: restricted Wi-Fi sudo policy unavailable; install sudo and rerun BoardLab autorun"
            fi
        else
            info "warning: restricted Wi-Fi helper install failed: $WIFI_HELPER_PATH"
        fi
    else
        info "warning: restricted Wi-Fi helper unavailable in staged install: $wifi_helper_source"
    fi

    rm -f "$NPU_HELPER_READY_PATH"
    npu_helper_source="$PREFIX/lib/boardlab/boardlab-npu-load"
    npu_sudoers_source="$PREFIX/share/boardlab/boardlab-npu.sudoers"
    if [ -f "$npu_helper_source" ]; then
        if sh -n "$npu_helper_source" && \
            install -d -o root -g root -m 0755 /usr/lib/boardlab && \
            install -o root -g root -m 0755 "$npu_helper_source" "$NPU_HELPER_PATH"; then
            info "installed restricted NPU load helper: $NPU_HELPER_PATH"
            if [ -f "$npu_sudoers_source" ] && command -v visudo >/dev/null 2>&1; then
                if visudo -cf "$npu_sudoers_source" >/dev/null 2>&1 && \
                    install -d -o root -g root -m 0750 /etc/sudoers.d && \
                    install -o root -g root -m 0440 "$npu_sudoers_source" "$NPU_SUDOERS_PATH"; then
                    info "installed restricted NPU sudo policy: $NPU_SUDOERS_PATH"
                    npu_helper_marker=$(mktemp)
                    printf '%s\n' 'BoardLab NPU load helper is ready.' > "$npu_helper_marker"
                    if install -o root -g boardlab-npu -m 0640 \
                        "$npu_helper_marker" "$NPU_HELPER_READY_PATH"; then
                        info "enabled restricted NPU load helper: $NPU_HELPER_READY_PATH"
                    else
                        info "warning: restricted NPU helper readiness marker was not installed"
                    fi
                    rm -f "$npu_helper_marker"
                else
                    info "warning: restricted NPU sudo policy was not installed; rerun after fixing sudo/visudo"
                fi
            else
                info "warning: restricted NPU sudo policy unavailable; install sudo and rerun BoardLab autorun"
            fi
        else
            info "warning: restricted NPU helper install failed: $NPU_HELPER_PATH"
        fi
    else
        info "warning: restricted NPU helper unavailable in staged install: $npu_helper_source"
    fi

    if [ -n "$udev_dir" ] && [ -f "$PREFIX/lib/udev/rules.d/99-boardlab.rules" ]; then
        install -m 0644 "$PREFIX/lib/udev/rules.d/99-boardlab.rules" "$udev_dir/99-boardlab.rules"
        info "installed $udev_dir/99-boardlab.rules"
    fi

    if [ -n "$polkit_action_dir" ] && [ -f "$PREFIX/share/polkit-1/actions/com.boardlab.root.policy" ]; then
        install -m 0644 "$PREFIX/share/polkit-1/actions/com.boardlab.root.policy" "$polkit_action_dir/com.boardlab.root.policy"
        info "installed $polkit_action_dir/com.boardlab.root.policy"
    fi
    for polkit_rule in 60-boardlab-root.rules 61-boardlab-networkmanager.rules; do
        if [ -n "$polkit_rules_dir" ] && [ -f "$PREFIX/share/polkit-1/rules.d/$polkit_rule" ]; then
            install -m 0644 "$PREFIX/share/polkit-1/rules.d/$polkit_rule" "$polkit_rules_dir/$polkit_rule"
            info "installed $polkit_rules_dir/$polkit_rule"
        fi
    done
    polkit_policy_installed=0
    for polkit_pkla in 60-boardlab-root.pkla 61-boardlab-networkmanager.pkla; do
        if [ -n "$polkit_pkla_dir" ] && [ -f "$PREFIX/share/polkit-1/localauthority/50-local.d/$polkit_pkla" ]; then
            install -m 0644 "$PREFIX/share/polkit-1/localauthority/50-local.d/$polkit_pkla" "$polkit_pkla_dir/$polkit_pkla"
            info "installed $polkit_pkla_dir/$polkit_pkla"
            polkit_policy_installed=1
        fi
    done
    if [ "$polkit_policy_installed" = "1" ] && systemd_available && systemctl is-active --quiet polkit.service; then
        if systemctl try-restart polkit.service; then
            info "restarted polkit.service to load BoardLab permission policies"
        else
            info "warning: polkit.service restart failed; reopen BoardLab or restart polkit.service before using privileged hardware controls"
        fi
    fi

    if [ -n "$icon_dir" ] && [ -f "$PREFIX/share/icons/hicolor/scalable/apps/boardlab.svg" ]; then
        install -m 0644 "$PREFIX/share/icons/hicolor/scalable/apps/boardlab.svg" "$icon_dir/boardlab.svg"
        info "installed $icon_dir/boardlab.svg"
    fi

    if [ -n "$desktop_dir" ] && [ -f "$PREFIX/share/applications/boardlab.desktop" ]; then
        sed \
            -e "s|^Exec=.*|Exec=$PREFIX/bin/boardlab-root --framed|" \
            -e "s|^TryExec=.*|TryExec=$PREFIX/bin/boardlab-root|" \
            -e "s|^Path=.*|Path=$PREFIX|" \
            "$PREFIX/share/applications/boardlab.desktop" > "$desktop_dir/boardlab.desktop"
        chmod 0644 "$desktop_dir/boardlab.desktop"
        info "installed $desktop_dir/boardlab.desktop"
        install_user_desktop_launcher "$desktop_dir/boardlab.desktop"
    fi
}

install_user_desktop_launcher() {
    launcher_source=$1
    if [ ! -f "$launcher_source" ]; then
        info "desktop launcher skipped: source is missing: $launcher_source"
        return
    fi
    if ! id "$SERVICE_USER" >/dev/null 2>&1; then
        info "desktop launcher skipped: user not found: $SERVICE_USER"
        return
    fi

    user_desktop_dir=$(user_desktop_dir "$SERVICE_USER")
    user_desktop_file="$user_desktop_dir/boardlab.desktop"
    if ! install -d -m 0755 "$user_desktop_dir" 2>/dev/null; then
        info "desktop launcher skipped: cannot create $user_desktop_dir"
        return
    fi
    launcher_tmp=$(mktemp)
    sed \
        -e "s|^Exec=.*|Exec=$PREFIX/bin/boardlab-root --framed|" \
        -e "s|^TryExec=.*|TryExec=$PREFIX/bin/boardlab-root|" \
        -e "s|^Path=.*|Path=$PREFIX|" \
        "$launcher_source" > "$launcher_tmp"
    install -m 0755 "$launcher_tmp" "$user_desktop_file"
    rm -f "$launcher_tmp"
    chown "$SERVICE_USER:$SERVICE_USER" "$user_desktop_dir" "$user_desktop_file" 2>/dev/null || true
    if command -v gio >/dev/null 2>&1; then
        user_home=$(user_home_dir "$SERVICE_USER")
        runuser -u "$SERVICE_USER" -- env HOME="$user_home" gio set "$user_desktop_file" metadata::trusted true 2>/dev/null || \
            gio set "$user_desktop_file" metadata::trusted true 2>/dev/null || true
    fi
    info "installed desktop launcher: $user_desktop_file"
}

refresh_system() {
    log "Refresh system"
    if command -v update-desktop-database >/dev/null 2>&1; then
        update-desktop-database /usr/share/applications >/dev/null 2>&1 || \
            info "desktop database refresh failed; the next login can rebuild it"
    fi
    if command -v gtk-update-icon-cache >/dev/null 2>&1; then
        gtk-update-icon-cache -f -t /usr/share/icons/hicolor >/dev/null 2>&1 || \
            info "icon cache refresh failed; SVG icon remains directly available"
    fi
    if udev_available; then
        udevadm control --reload-rules || true
        udevadm trigger || true
    else
        info "udev refresh skipped: udev is not running or udevadm is unavailable"
    fi

    if systemd_available && { [ "$INSTALL_SERVICE" = "1" ] || [ "$ENABLE_SERVICE" = "1" ] || [ "$DISABLE_SERVICE" = "1" ] || [ "$START_SERVICE" = "1" ]; }; then
        systemctl daemon-reload || info "systemd daemon-reload failed, continuing"
        if [ "$ENABLE_SERVICE" = "1" ]; then
            systemctl enable "$SERVICE_NAME" || info "service enable failed, continuing: $SERVICE_NAME"
        elif [ "$DISABLE_SERVICE" = "1" ]; then
            systemctl disable --now "$SERVICE_NAME" >/dev/null 2>&1 || true
            systemctl reset-failed "$SERVICE_NAME" >/dev/null 2>&1 || true
            info "service stopped and boot autostart disabled"
        else
            info "service enable skipped: ENABLE_SERVICE=$ENABLE_SERVICE"
            info "service boot autostart state preserved"
        fi
        if [ "$START_SERVICE" = "1" ]; then
            if x11_display_available; then
                if systemctl is-active --quiet "$SERVICE_NAME"; then
                    systemctl restart "$SERVICE_NAME" || info "service restart failed, continuing: $SERVICE_NAME"
                else
                    systemctl start "$SERVICE_NAME" || info "service start failed, continuing: $SERVICE_NAME"
                fi
            else
                info "service start skipped: X11 display :0 is not ready"
            fi
        else
            info "service start skipped: START_SERVICE=$START_SERVICE"
        fi
    elif systemd_available; then
        info "systemd service management skipped: INSTALL_SERVICE=$INSTALL_SERVICE ENABLE_SERVICE=$ENABLE_SERVICE DISABLE_SERVICE=$DISABLE_SERVICE START_SERVICE=$START_SERVICE"
    else
        info "systemd refresh skipped: systemd is not PID 1 or systemctl is unavailable"
    fi
}

run_checks() {
    if [ "$RUN_HARDWARE_VALIDATE" = "1" ] && [ -x "$PREFIX/bin/boardlab-hardware-validate.sh" ]; then
        log "Hardware validation"
        "$PREFIX/bin/boardlab-hardware-validate.sh" || true
    fi
}

check_line() {
    check_name=$1
    check_state=$2
    check_detail=${3:-}
    printf '%-28s %-8s %s\n' "$check_name" "$check_state" "$check_detail"
}

run_read_only_checks() {
    log "BoardLab read-only install check"
    detect_target

    for command_name in cmake c++ qmake dpkg-query apt-get xdg-user-dir gio pkexec xrandr nmcli ip ping dhclient; do
        if command -v "$command_name" >/dev/null 2>&1; then
            check_line "command:$command_name" OK "$(command -v "$command_name")"
        else
            check_line "command:$command_name" MISS "autorun installs the matching package"
        fi
    done
    for package_name in $APT_REQUIRED_PACKAGES; do
        if package_installed "$package_name"; then
            check_line "package:$package_name" OK
        else
            check_line "package:$package_name" MISS "will be installed"
        fi
    done
    for source_path in CMakeLists.txt src/main.cpp qml/Main.qml web/web.qrc web/app.js web/styles.css packaging/boardlab.desktop packaging/boardlab-wifi packaging/boardlab-wifi.sudoers packaging/boardlab-npu-load packaging/boardlab-npu.sudoers assets/icons/boardlab.svg; do
        if [ -e "$ROOT_DIR/$source_path" ]; then
            check_line "source:$source_path" OK
        else
            check_line "source:$source_path" MISS
        fi
    done
    for installed_path in "$PREFIX/bin/boardlab" "$PREFIX/bin/boardlab-desktop" "$PREFIX/bin/boardlab-root" "$PREFIX/bin/boardlab-root-run" "$PREFIX/bin/boardlab-verify-web-assets" "$PREFIX/bin/boardlab-restart-web" "$PREFIX/share/boardlab/web-assets.sha256" "$PREFIX/lib/boardlab/boardlab-wifi" "$PREFIX/lib/boardlab/boardlab-npu-load" "$WIFI_HELPER_PATH" "$WIFI_SUDOERS_PATH" "$NPU_HELPER_PATH" "$NPU_SUDOERS_PATH" "$NPU_HELPER_READY_PATH" /usr/share/applications/boardlab.desktop /usr/share/icons/hicolor/scalable/apps/boardlab.svg /usr/share/polkit-1/actions/com.boardlab.root.policy /etc/polkit-1/rules.d/60-boardlab-root.rules; do
        if [ -e "$installed_path" ]; then
            check_line installed OK "$installed_path"
        else
            check_line installed INFO "not installed: $installed_path"
        fi
    done
    for secure_item in \
        "npu-helper:$NPU_HELPER_PATH:root:root:755" \
        "npu-sudoers:$NPU_SUDOERS_PATH:root:root:440" \
        "npu-ready:$NPU_HELPER_READY_PATH:root:boardlab-npu:640"; do
        secure_name=${secure_item%%:*}
        secure_rest=${secure_item#*:}
        secure_path=${secure_rest%%:*}
        secure_expected=${secure_rest#*:}
        if [ -e "$secure_path" ] && command -v stat >/dev/null 2>&1; then
            secure_actual=$(stat -c '%U:%G:%a' "$secure_path" 2>/dev/null || true)
            if [ "$secure_actual" = "$secure_expected" ]; then
                check_line "$secure_name" OK "$secure_actual"
            else
                check_line "$secure_name" WARN "expected $secure_expected, got ${secure_actual:-unavailable}"
            fi
        fi
    done
}

print_summary() {
    log "Done"
    info "application: $PREFIX/bin/boardlab"
    info "runner: $PREFIX/bin/boardlab-run"
    info "desktop launcher: $PREFIX/bin/boardlab-root (PolicyKit root session)"
    info "root launcher: $PREFIX/bin/boardlab-root"
    info "service template: $PREFIX/lib/systemd/system/$SERVICE_NAME"
    info "systemd service: installed for optional X11 kiosk use, disabled and stopped by default"
    info "udev rules: /etc/udev/rules.d/99-boardlab.rules"
    info "buzzer access: exact pwm-beeper input event through the input group; PWM sysfs remains kernel-owned"
    info "Wi-Fi helper: $WIFI_HELPER_PATH (sudo grants only BoardLab's whitelisted NetworkManager operations)"
    info "NPU helper: $NPU_HELPER_PATH (sudo grants only no-argument RKNPU load reads)"
    info "desktop file: /usr/share/applications/boardlab.desktop"
    info "desktop launcher: $(user_desktop_dir "$SERVICE_USER")/boardlab.desktop"
    info "manual root run: $PREFIX/bin/boardlab-root --framed"
    info "manual user run: $PREFIX/bin/boardlab-desktop --framed"
    info "XFCE preview: QT_QPA_PLATFORM=xcb $PREFIX/bin/boardlab --framed"
    info "desktop behavior: starting BoardLab opens Qt and its Web bridge; choose '扫码控制' in Qt to scan the QR code"
    info "LAN Web bridge: $PREFIX/bin/boardlab-run opens --web --web-bind 0.0.0.0 --web-port 17999 while Qt is running"
    info "LAN Web authentication: Bearer Token required for all /api/* endpoints"
    info "Web token file: $WEB_TOKEN_FILE (read with: sudo cat $WEB_TOKEN_FILE)"
    info "Web QRC verifier: $PREFIX/bin/boardlab-verify-web-assets"
    info "check a running manual Web bridge: $PREFIX/bin/boardlab-verify-web-assets --url http://127.0.0.1:17999"
    info "after an upgrade, restart a running BoardLab session to load the new Web assets"
    info "service status: systemctl status $SERVICE_NAME --no-pager"
    info "optional X11 kiosk autostart: rerun ./autorun.sh --enable-service after display :0 is ready"
    info "service logs: journalctl -u $SERVICE_NAME -n 80 --no-pager"
}

usage() {
    cat <<EOF
Usage: ./autorun.sh [--check] [--no-apt] [--disable-service] [--prefix PATH] [--user USER] [--hardware-validate]

Environment:
  PREFIX=/opt/boardlab        install prefix
  BOARDLAB_USER=<user>           systemd service user
  RK_SHELL_USER=<user>           legacy alias for BOARDLAB_USER
  BOARDLAB_WEB_TOKEN=<token>     set or replace the Web token (1-256 chars)
  BOARDLAB_DEFAULT_WEB_TOKEN=topeet  first-install default when no token file exists
  BOARDLAB_WEB_TOKEN_FILE=<path> token file path (default: /etc/boardlab/web.token)
  INSTALL_DEPS=1                  install apt dependencies
  INSTALL_SERVICE=1               install the BoardLab systemd service by default
  FORCE_SERVICE_INSTALL=0         do not overwrite an existing service unit by default
  ENABLE_SERVICE=0                do not enable boardlab.service at boot by default
  DISABLE_SERVICE=1               stop and disable any existing service by default
  START_SERVICE=0                 do not start the kiosk service after install
  RUN_HARDWARE_VALIDATE=0         run extra hardware validation gate
Examples:
  ./autorun.sh
  ./autorun.sh --check
  ./autorun.sh --no-apt
  ./autorun.sh                    # installs desktop launchers; service stays disabled and stopped
  ./autorun.sh --enable-service   # explicitly enable and start the optional kiosk service
  ./autorun.sh --start            # start only this time, without enabling at boot
  ./autorun.sh --disable-service  # stop and disable the optional kiosk service
  BOARDLAB_USER=topeet ./autorun.sh --prefix /opt/boardlab
EOF
}

for arg in "$@"; do
    case "$arg" in
        -h|--help)
            usage
            exit 0
            ;;
        --check)
            CHECK_ONLY=1
            ;;
    esac
done

if [ "$CHECK_ONLY" != "1" ]; then
    require_root "$@"
fi

while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-apt)
            INSTALL_DEPS=0
            ;;
        --check)
            CHECK_ONLY=1
            ;;
        --no-enable)
            ENABLE_SERVICE=0
            DISABLE_SERVICE=1
            ;;
        --disable-service|--disable)
            ENABLE_SERVICE=0
            DISABLE_SERVICE=1
            START_SERVICE=0
            ;;
        --keep-service-enable|--keep-enable)
            ENABLE_SERVICE=0
            DISABLE_SERVICE=0
            ;;
        --enable-service|--enable)
            INSTALL_SERVICE=1
            ENABLE_SERVICE=1
            DISABLE_SERVICE=0
            START_SERVICE=1
            ;;
        --install-service)
            INSTALL_SERVICE=1
            ;;
        --no-install-service)
            INSTALL_SERVICE=0
            ENABLE_SERVICE=0
            DISABLE_SERVICE=0
            START_SERVICE=0
            ;;
        --force-service-install)
            INSTALL_SERVICE=1
            FORCE_SERVICE_INSTALL=1
            ;;
        --start)
            INSTALL_SERVICE=1
            START_SERVICE=1
            ;;
        --prefix)
            shift
            PREFIX=${1:?missing value for --prefix}
            ;;
        --user)
            shift
            SERVICE_USER=${1:?missing value for --user}
            ;;
        --hardware-validate)
            RUN_HARDWARE_VALIDATE=1
            ;;
        *)
            printf 'Unknown option: %s\n' "$1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [ "$CHECK_ONLY" = "1" ]; then
    run_read_only_checks
    exit 0
fi

detect_target
install_dependencies
ensure_groups
ensure_npu_debugfs
build_and_stage
install_web_token
install_system_files
refresh_system
run_checks
print_summary
