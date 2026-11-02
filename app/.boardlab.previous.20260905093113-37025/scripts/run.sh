#!/usr/bin/env sh
set -eu

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd)
APP=${BOARDLAB_APP:-${APP:-"$ROOT_DIR/build/boardlab"}}
OS_RELEASE_FILE=${BOARDLAB_OS_RELEASE_FILE:-${OS_RELEASE_FILE:-/etc/os-release}}

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

runtime_os_id() {
    if [ ! -r "$OS_RELEASE_FILE" ]; then
        return 0
    fi
    sed -n 's/^ID=//p' "$OS_RELEASE_FILE" | head -n 1 | tr -d '"\047' | tr '[:upper:]' '[:lower:]'
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
        RUNTIME_OS_ID=$(runtime_os_id)
        if [ "$RUNTIME_OS_ID" = "buildroot" ]; then
            PLATFORM=wayland
        elif [ "$RUNTIME_OS_ID" = "ubuntu" ] || [ "$RUNTIME_OS_ID" = "debian" ]; then
            PLATFORM=xcb
        elif [ -n "${DISPLAY:-}" ] && { ! root_without_xauthority || ! has_drm_display; }; then
            PLATFORM=xcb
        elif [ -n "${WAYLAND_DISPLAY:-}" ]; then
            PLATFORM=wayland
        elif has_drm_display; then
            PLATFORM=eglfs
        elif [ -e /dev/fb0 ]; then
            PLATFORM=linuxfb
        else
            PLATFORM=eglfs
        fi
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

# The selected backend is authoritative.  This is especially important on
# Ubuntu/Debian desktops that inherit a stale QT_QPA_PLATFORM/Wayland value,
# and on Buildroot shells that inherit a stale DISPLAY value.
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
fi

if [ "$MODE" = "kiosk" ]; then
    # shellcheck disable=SC2086
    exec "$APP" --kiosk $WEB_ARGS "$@"
fi

# shellcheck disable=SC2086
exec "$APP" --framed $WEB_ARGS "$@"
