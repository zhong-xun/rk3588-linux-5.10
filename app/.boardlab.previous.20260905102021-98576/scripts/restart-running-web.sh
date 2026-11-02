#!/usr/bin/env sh
# Restart a stale *desktop* BoardLab instance after an in-place upgrade.
#
# BoardLab embeds its Web assets in the executable through Qt's QRC system.
# Replacing /opt/boardlab/bin/boardlab while a desktop process is running does
# not replace those in-memory assets.  This helper is deliberately narrow: it
# only touches a BoardLab process serving the requested Web port, preserves
# its graphical session variables, and verifies the replacement afterwards.
set -eu

PREFIX=${PREFIX:-/opt/boardlab}
PORT=${BOARDLAB_WEB_PORT:-17999}
# This is deliberately only an explicit --user filter.  The installer may be
# configured for the topeet service user while a customer launched the desktop
# bridge manually as root, so inheriting BOARDLAB_USER here would miss the
# live process that actually owns the requested Web port.
SERVICE_USER=
LOG_FILE=${BOARDLAB_RESTART_LOG:-/tmp/boardlab-desktop-restart.log}
FORCE_RESTART=0

info() {
    printf '%s\n' "$*"
}

fail() {
    printf 'BoardLab desktop restart failed: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [--prefix PATH] [--port PORT] [--user USER] [--force]

Restarts a stale graphical BoardLab process serving PORT only when its live
QRC Web assets differ from the installed manifest. --force also replaces a
running process after a backend-only upgrade. It does not start a new desktop
application when no BoardLab Web bridge is running. --user is an optional
explicit owner filter; without it, the live process owner is preserved.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --prefix)
            [ "$#" -ge 2 ] || fail "missing value for --prefix"
            PREFIX=$2
            shift 2
            ;;
        --port)
            [ "$#" -ge 2 ] || fail "missing value for --port"
            PORT=$2
            shift 2
            ;;
        --user)
            [ "$#" -ge 2 ] || fail "missing value for --user"
            SERVICE_USER=$2
            shift 2
            ;;
        --force)
            FORCE_RESTART=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

case "$PORT" in
    ''|*[!0-9]*) fail "Web port must be numeric: $PORT" ;;
esac
[ "$PORT" -ge 1 ] && [ "$PORT" -le 65535 ] || fail "Web port is out of range: $PORT"

VERIFIER="$PREFIX/bin/boardlab-verify-web-assets"
# Match the installed desktop entry.  This also upgrades an already-running
# legacy user-session bridge into the authorized root desktop process.
LAUNCHER="$PREFIX/bin/boardlab-root"
URL="http://127.0.0.1:${PORT}"

[ -x "$VERIFIER" ] || fail "installed Web verifier is missing: $VERIFIER"
[ -x "$LAUNCHER" ] || fail "desktop launcher is missing: $LAUNCHER"
command -v curl >/dev/null 2>&1 || fail "curl is required to check the local Web bridge"

# Nothing is listening: this is a normal first-install situation.  Do not
# unexpectedly open a GUI merely because an installer was run.
if ! curl -fsS --max-time 1 "$URL/healthz" >/dev/null 2>&1; then
    info "no local BoardLab Web bridge is running on 127.0.0.1:${PORT}; desktop restart not needed"
    exit 0
fi

if [ "$FORCE_RESTART" != "1" ] && "$VERIFIER" --url "$URL" >/dev/null 2>&1; then
    info "running BoardLab Web bridge already serves the installed QRC assets"
    exit 0
fi

[ "$(id -u)" -eq 0 ] || fail "root is required to restart another user's graphical BoardLab process"
command -v setsid >/dev/null 2>&1 || fail "setsid is required to detach the replacement desktop process"

read_proc_env() {
    proc_pid=$1
    env_name=$2
    tr '\000' '\n' <"/proc/${proc_pid}/environ" 2>/dev/null |
        sed -n "s/^${env_name}=//p" | tail -n 1
}

proc_owner() {
    stat -c %U "/proc/$1" 2>/dev/null || true
}

find_stale_desktop_pid() {
    for proc_dir in /proc/[0-9]*; do
        [ -r "$proc_dir/cmdline" ] || continue
        proc_pid=${proc_dir#/proc/}
        proc_name=$(cat "$proc_dir/comm" 2>/dev/null || true)
        case "$proc_name" in
            boardlab|rk-shell-qt) ;;
            *) continue ;;
        esac
        proc_args=$(tr '\000' ' ' <"$proc_dir/cmdline" 2>/dev/null || true)
        case "$proc_args" in
            *"--web-port ${PORT}"*) ;;
            *) continue ;;
        esac
        proc_user=$(proc_owner "$proc_pid")
        [ -n "$proc_user" ] || continue
        if [ -n "$SERVICE_USER" ] && [ "$proc_user" != "$SERVICE_USER" ]; then
            continue
        fi
        printf '%s\n' "$proc_pid"
        return 0
    done
    return 1
}

APP_PID=$(find_stale_desktop_pid || true)
[ -n "$APP_PID" ] || fail "${URL} has stale QRC assets, but no matching BoardLab desktop process could be identified"
APP_USER=$(proc_owner "$APP_PID")
[ -n "$APP_USER" ] || fail "cannot determine the owner of BoardLab PID $APP_PID"

DISPLAY_VALUE=$(read_proc_env "$APP_PID" DISPLAY)
XAUTHORITY_VALUE=$(read_proc_env "$APP_PID" XAUTHORITY)
XDG_RUNTIME_VALUE=$(read_proc_env "$APP_PID" XDG_RUNTIME_DIR)
DBUS_VALUE=$(read_proc_env "$APP_PID" DBUS_SESSION_BUS_ADDRESS)

if [ -z "$DISPLAY_VALUE" ]; then
    fail "BoardLab PID $APP_PID has no X11 DISPLAY; refusing to relaunch the Ubuntu/Debian desktop process"
fi

APP_MODE=--kiosk
APP_ARGS=$(tr '\000' ' ' <"/proc/${APP_PID}/cmdline" 2>/dev/null || true)
case "$APP_ARGS" in
    *' --framed '*|--framed\ *) APP_MODE=--framed ;;
esac

if command -v getent >/dev/null 2>&1; then
    APP_HOME=$(getent passwd "$APP_USER" | awk -F: 'NR == 1 { print $6 }')
else
    APP_HOME=/home/$APP_USER
fi
[ -n "$APP_HOME" ] || APP_HOME=/home/$APP_USER

info "restarting stale BoardLab desktop instance (PID $APP_PID, user $APP_USER, mode $APP_MODE)"
kill -TERM "$APP_PID" 2>/dev/null || fail "cannot stop BoardLab PID $APP_PID"
attempt=0
while kill -0 "$APP_PID" 2>/dev/null && [ "$attempt" -lt 50 ]; do
    sleep 0.1
    attempt=$((attempt + 1))
done
if kill -0 "$APP_PID" 2>/dev/null; then
    info "BoardLab PID $APP_PID did not exit after 5 seconds; forcing shutdown"
    kill -KILL "$APP_PID" 2>/dev/null || fail "cannot force-stop BoardLab PID $APP_PID"
fi

# The process is started with the old process's session variables, rather
# than guessing :0 or the display manager user.  This works on both Xvnc and
# normal XFCE/Xorg sessions.  Ubuntu/Debian product images are X11-only, so a
# stale WAYLAND_DISPLAY is deliberately not propagated.  Do not invoke runuser for a root-owned instance:
# closing that nested root login can terminate the newly detached process.
: >"$LOG_FILE"
chmod 0644 "$LOG_FILE" 2>/dev/null || true
# The installer is often run from an SSH terminal.  A plain background child
# remains in that terminal's session and receives SIGHUP as soon as the
# installer exits, leaving a successful-looking upgrade with no Qt/Web
# process.  A separate session keeps the replacement alive while retaining
# the original user's graphical-session environment.
if [ "$APP_USER" = "root" ]; then
    env \
        HOME="$APP_HOME" USER="$APP_USER" LOGNAME="$APP_USER" \
        DISPLAY="$DISPLAY_VALUE" WAYLAND_DISPLAY= \
        XAUTHORITY="$XAUTHORITY_VALUE" XDG_RUNTIME_DIR="$XDG_RUNTIME_VALUE" \
        DBUS_SESSION_BUS_ADDRESS="$DBUS_VALUE" \
        setsid "$LAUNCHER" "$APP_MODE" >"$LOG_FILE" 2>&1 &
else
    command -v runuser >/dev/null 2>&1 || fail "runuser is required to preserve the desktop user session"
    runuser -u "$APP_USER" -- env \
        HOME="$APP_HOME" USER="$APP_USER" LOGNAME="$APP_USER" \
        DISPLAY="$DISPLAY_VALUE" WAYLAND_DISPLAY= \
        XAUTHORITY="$XAUTHORITY_VALUE" XDG_RUNTIME_DIR="$XDG_RUNTIME_VALUE" \
        DBUS_SESSION_BUS_ADDRESS="$DBUS_VALUE" \
        setsid "$LAUNCHER" "$APP_MODE" >"$LOG_FILE" 2>&1 &
fi

attempt=0
while [ "$attempt" -lt 100 ]; do
    if curl -fsS --max-time 1 "$URL/healthz" >/dev/null 2>&1; then
        break
    fi
    sleep 0.1
    attempt=$((attempt + 1))
done
curl -fsS --max-time 1 "$URL/healthz" >/dev/null 2>&1 || {
    sed -n '1,120p' "$LOG_FILE" >&2 || true
    fail "replacement BoardLab desktop process did not become healthy; log: $LOG_FILE"
}

"$VERIFIER" --url "$URL"
info "BoardLab desktop restart completed; the running Web UI now matches the installed QRC assets"
