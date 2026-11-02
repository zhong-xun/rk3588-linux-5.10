#!/usr/bin/env sh
set -eu

SCRIPT=$0
if command -v readlink >/dev/null 2>&1; then
    RESOLVED=$(readlink -f -- "$0" 2>/dev/null || printf '%s\n' "$0")
    [ -n "$RESOLVED" ] && SCRIPT=$RESOLVED
fi
ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$SCRIPT")/.." && pwd)
if [ -n "${BOARDLAB_APP:-}" ]; then
    APP=$BOARDLAB_APP
elif [ -n "${APP:-}" ]; then
    APP=$APP
elif [ -x "$ROOT_DIR/bin/boardlab" ]; then
    APP=$ROOT_DIR/bin/boardlab
else
    APP=$ROOT_DIR/build/boardlab
fi

if [ -f "$ROOT_DIR/lib/boardlab/librknnrt.so" ]; then
    export LD_LIBRARY_PATH="$ROOT_DIR/lib/boardlab${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

case "$APP" in
    "$ROOT_DIR"/build-host-ui/boardlab|"$ROOT_DIR"/build-host-ui/boardlab/*|"$ROOT_DIR"/build-host-ui.retired/boardlab|"$ROOT_DIR"/build-host-ui.retired/boardlab/*|"$ROOT_DIR"/build-host-ui.retired.*/boardlab|"$ROOT_DIR"/build-host-ui.retired.*/boardlab/*)
        printf 'Refusing retired application binary: %s\n' "$APP" >&2
        printf 'Use: %s/scripts/run.sh\n' "$ROOT_DIR" >&2
        exit 2
        ;;
esac

if [ ! -x "$APP" ]; then
    printf 'Executable not found: %s\n' "$APP" >&2
    printf 'Build first with: %s/scripts/build.sh\n' "$ROOT_DIR" >&2
    exit 1
fi

case "${1:-}" in
    auto|wayland|x11|xcb|eglfs|linuxfb|offscreen|smoke)
        BACKEND=$1
        shift || true
        ;;
    *)
        BACKEND=auto
        ;;
esac

MODE=${BOARDLAB_MODE:-${RK_SHELL_MODE:-kiosk}}
if [ "${1:-}" = "--kiosk" ]; then
    MODE=kiosk
    shift
elif [ "${1:-}" = "--framed" ]; then
    MODE=framed
    shift
fi

if [ "${1:-}" = "--" ]; then
    shift
fi

has_drm_display() {
    if [ ! -d /dev/dri ]; then
        return 1
    fi
    set -- /dev/dri/card*
    [ -e "$1" ]
}

root_without_xauthority() {
    [ "$(id -u 2>/dev/null || printf '%s' 1)" = "0" ] && [ -z "${XAUTHORITY:-}" ]
}

wayland_socket_available() {
    wayland_name=${WAYLAND_DISPLAY:-wayland-0}
    case "$wayland_name" in
        /*)
            [ -S "$wayland_name" ] || [ -e "$wayland_name" ]
            ;;
        *)
            runtime_dir=${XDG_RUNTIME_DIR:-/run}
            [ -S "$runtime_dir/$wayland_name" ] || [ -e "$runtime_dir/$wayland_name" ] ||
                [ -S "/run/$wayland_name" ] || [ -e "/run/$wayland_name" ]
            ;;
    esac
}

select_automatic_platform() {
    if [ -n "${QT_QPA_PLATFORM:-}" ]; then
        printf '%s\n' "$QT_QPA_PLATFORM"
    elif [ -n "${WAYLAND_DISPLAY:-}" ] && wayland_socket_available; then
        printf '%s\n' wayland
    elif [ -n "${DISPLAY:-}" ] && { ! root_without_xauthority || ! has_drm_display; }; then
        printf '%s\n' xcb
    elif wayland_socket_available; then
        printf '%s\n' wayland
    elif has_drm_display; then
        printf '%s\n' eglfs
    elif [ -e /dev/fb0 ]; then
        printf '%s\n' linuxfb
    else
        # A headless SDK can still expose the Web console and JSON inventory.
        printf '%s\n' offscreen
    fi
}

WEB_ARGS="--web --web-bind 0.0.0.0 --web-port 17999"
for arg in "$@"; do
    if [ "$arg" = "--web" ]; then
        WEB_ARGS=""
        break
    fi
done

case "$BACKEND" in
    auto)
        PLATFORM=$(select_automatic_platform)
        ;;
    wayland)
        PLATFORM=wayland
        ;;
    x11|xcb)
        PLATFORM=xcb
        ;;
    eglfs)
        PLATFORM=eglfs
        ;;
    linuxfb)
        PLATFORM=linuxfb
        ;;
    offscreen|smoke)
        PLATFORM=offscreen
        export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
        export QML_DISABLE_DISK_CACHE=${QML_DISABLE_DISK_CACHE:-1}
        ;;
    *)
        printf 'Usage: %s [auto|wayland|x11|xcb|eglfs|linuxfb|offscreen|smoke] [--kiosk|--framed] [-- app arguments]\n' "$0" >&2
        exit 2
        ;;
esac

# An explicit backend argument is authoritative. Automatic mode preserves an
# explicit QT_QPA_PLATFORM and otherwise follows the live display endpoint,
# independent of the root filesystem's distribution name.
export QT_QPA_PLATFORM=$PLATFORM
export QML_DISABLE_DISK_CACHE=${QML_DISABLE_DISK_CACHE:-0}

if [ "$QT_QPA_PLATFORM" = "eglfs" ]; then
    export QT_QPA_EGLFS_INTEGRATION=${QT_QPA_EGLFS_INTEGRATION:-eglfs_kms}
    export QT_QPA_EGLFS_FORCE888=${QT_QPA_EGLFS_FORCE888:-1}
    MODE=${BOARDLAB_MODE:-${RK_SHELL_MODE:-kiosk}}
elif [ "$QT_QPA_PLATFORM" = "xcb" ]; then
    # Xvnc and some Rockchip X11 images fall back to multi-threaded llvmpipe
    # for Qt Quick OpenGL.  Software Qt Quick with the basic loop is stable
    # and keeps the idle dashboard from saturating the CPU.
    export QT_XCB_GL_INTEGRATION=none
    export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
    export QSG_RENDER_LOOP=${QSG_RENDER_LOOP:-basic}
elif [ "$QT_QPA_PLATFORM" = "offscreen" ]; then
    export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
    export QSG_RENDER_LOOP=${QSG_RENDER_LOOP:-basic}
fi

if [ "$MODE" = "kiosk" ]; then
    # shellcheck disable=SC2086
    exec "$APP" --kiosk $WEB_ARGS "$@"
fi

# shellcheck disable=SC2086
exec "$APP" --framed $WEB_ARGS "$@"
