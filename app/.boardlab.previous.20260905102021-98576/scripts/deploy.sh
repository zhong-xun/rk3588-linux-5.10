#!/usr/bin/env sh
# shellcheck disable=SC2250
set -eu

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd)

normalize_path() (
    path_to_normalize=$1
    case "$path_to_normalize" in
        /*) ;;
        *) path_to_normalize="$ROOT_DIR/$path_to_normalize" ;;
    esac

    if command -v realpath >/dev/null 2>&1; then
        realpath -m -- "$path_to_normalize"
    elif command -v readlink >/dev/null 2>&1; then
        readlink -m -- "$path_to_normalize"
    else
        printf 'Neither realpath nor readlink is available to validate BUILD_DIR.\n' >&2
        exit 2
    fi
)

BUILD_DIR=$(normalize_path "${BUILD_DIR:-$ROOT_DIR/build}")
PREFIX=${PREFIX:-"$ROOT_DIR/stage"}
CMAKE_COMMAND=${BOARDLAB_CMAKE_COMMAND:-cmake}
LOCAL_BIN_DIR=${LOCAL_BIN_DIR-"__boardlab_default__"}
if [ "$LOCAL_BIN_DIR" = "__boardlab_default__" ]; then
    case "$PREFIX" in
        "$ROOT_DIR/stage"|"$ROOT_DIR/stage/")
            SYSTEM_BIN_DIR=
            ;;
        *)
            SYSTEM_BIN_DIR=/usr/local/bin
            ;;
    esac
else
    SYSTEM_BIN_DIR=$LOCAL_BIN_DIR
fi

link_system_launcher() {
    target=$1
    link=$2
    if [ "$target" = "$link" ]; then
        return 0
    fi
    ln -sf "$target" "$link"
}

quarantine_retired_build() {
    # The retired host preview contains the old one-page Web UI. Do not let a
    # successful board installation leave an executable that can silently
    # bring that UI back. Development-only build directories are retained
    # outside the production prefix for diagnosis.
    case "$PREFIX" in
        /opt/boardlab|/opt/boardlab/)
            ;;
        *)
            return 0
            ;;
    esac
    if [ "${BOARDLAB_KEEP_RETIRED_BUILD:-0}" = "1" ]; then
        return 0
    fi

    disable_retired_binary() {
        retired_path=$1
        retired_binary="$retired_path/boardlab"
        [ -e "$retired_binary" ] || return 0

        # Renaming the directory alone is not enough: a user can still run
        # the old preview binary by hand and receive its retired one-page UI.
        # Keep its build files for diagnosis, but make the executable inert.
        if [ -x "$retired_binary" ] && ! chmod a-x "$retired_binary"; then
            printf 'Warning: could not disable retired Web binary: %s\n' "$retired_binary" >&2
        fi
    }

    # A previous deployment may already have moved the legacy build. Recheck
    # every retained copy so this guard also repairs older installations.
    for retired_path in "$ROOT_DIR"/build-host-ui.retired "$ROOT_DIR"/build-host-ui.retired.*; do
        [ -d "$retired_path" ] || continue
        disable_retired_binary "$retired_path"
    done

    legacy_dir="$ROOT_DIR/build-host-ui"
    [ -d "$legacy_dir" ] || return 0
    retired_dir="$ROOT_DIR/build-host-ui.retired"
    if [ -e "$retired_dir" ]; then
        retired_dir="$retired_dir.$(date +%Y%m%d%H%M%S)"
    fi
    if mv "$legacy_dir" "$retired_dir"; then
        disable_retired_binary "$retired_dir"
        printf 'Retired legacy Web build: %s\n' "$retired_dir"
    else
        printf 'Warning: could not retire legacy Web build: %s\n' "$legacy_dir" >&2
    fi
}

validate_cmake_cache() {
    case "$BUILD_DIR" in
        ""|"/"|"$ROOT_DIR"|"$ROOT_DIR/")
            printf 'Refusing unsafe BUILD_DIR: %s\n' "$BUILD_DIR" >&2
            exit 2
            ;;
        "$ROOT_DIR"/build-host-ui|"$ROOT_DIR"/build-host-ui/*|"$ROOT_DIR"/build-host-ui.retired|"$ROOT_DIR"/build-host-ui.retired.*)
            printf 'Refusing retired BUILD_DIR: %s\n' "$BUILD_DIR" >&2
            printf 'Use the supported build directory: %s/build\n' "$ROOT_DIR" >&2
            exit 2
            ;;
    esac

    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        printf 'Missing CMake cache: %s/CMakeCache.txt. Run scripts/build.sh first.\n' "$BUILD_DIR" >&2
        exit 2
    fi

    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)
    cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)

    if [ -n "$cached_source" ] && [ "$cached_source" != "$ROOT_DIR" ]; then
        printf 'Stale CMake cache: %s was configured for source %s, current source is %s\n' "$BUILD_DIR" "$cached_source" "$ROOT_DIR" >&2
        printf 'Run: rm -rf "%s" && "%s/scripts/build.sh"\n' "$BUILD_DIR" "$ROOT_DIR" >&2
        exit 2
    fi

    if [ -n "$cached_build" ] && [ "$cached_build" != "$BUILD_DIR" ]; then
        printf 'Stale CMake cache: cache was generated in %s, current build dir is %s\n' "$cached_build" "$BUILD_DIR" >&2
        printf 'Run: rm -rf "%s" && "%s/scripts/build.sh"\n' "$BUILD_DIR" "$ROOT_DIR" >&2
        exit 2
    fi
}

install_cmake_project() {
    command -v "$CMAKE_COMMAND" >/dev/null 2>&1 || {
        printf 'CMake command is not available: %s\n' "$CMAKE_COMMAND" >&2
        exit 2
    }

    cmake_version=$("$CMAKE_COMMAND" --version 2>/dev/null |
        sed -n '1{s/^[^0-9]*\([0-9][0-9.]*\).*$/\1/p;}')
    cmake_major=${cmake_version%%.*}
    cmake_remainder=${cmake_version#*.}
    cmake_minor=${cmake_remainder%%.*}
    case "$cmake_major:$cmake_minor" in
        *[!0-9:]*|:*)
            printf 'Unsupported CMake version string: %s\n' "$cmake_version" >&2
            exit 2
            ;;
    esac

    if [ "$cmake_major" -gt 3 ] || {
        [ "$cmake_major" -eq 3 ] && [ "$cmake_minor" -ge 15 ];
    }; then
        "$CMAKE_COMMAND" --install "$BUILD_DIR" --prefix "$PREFIX"
    else
        "$CMAKE_COMMAND" "-DCMAKE_INSTALL_PREFIX=$PREFIX" \
            -P "$BUILD_DIR/cmake_install.cmake"
    fi
}

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    BUILD_DIR="$BUILD_DIR" "$ROOT_DIR/scripts/build.sh"
fi

validate_cmake_cache
install_cmake_project
# The production build no longer ships the former standalone NPU smoke
# executable. Remove it during in-place upgrades so old test artifacts cannot
# survive in the installation prefix. Keep the cleanup bounded even when a
# caller supplies an unusual prefix.
if [ -n "$PREFIX" ] && [ "$PREFIX" != "/" ]; then
    rm -f "$PREFIX/bin/npu-yolo-demo-smoke"
fi
quarantine_retired_build

mkdir -p "$PREFIX/share/boardlab" "$PREFIX/lib/boardlab" "$PREFIX/lib/systemd/system" "$PREFIX/lib/udev/rules.d" "$PREFIX/share/applications" "$PREFIX/share/polkit-1/actions" "$PREFIX/share/polkit-1/rules.d" "$PREFIX/share/polkit-1/localauthority/50-local.d"
# Remove the retired fixed-channel helper from an in-place staged upgrade.
rm -f "$PREFIX/lib/boardlab/boardlab-pwm-init"
web_manifest="$PREFIX/share/boardlab/web-assets.sha256"
{
    printf '# BoardLab QRC Web asset SHA-256 manifest. Generated by scripts/deploy.sh.\n'
    (cd "$ROOT_DIR/web" && sha256sum app.js styles.css)
} > "$web_manifest"
chmod 0644 "$web_manifest"
install -m 0755 "$ROOT_DIR/scripts/verify-installed-web-assets.sh" "$PREFIX/bin/boardlab-verify-web-assets"
install -m 0755 "$ROOT_DIR/scripts/restart-running-web.sh" "$PREFIX/bin/boardlab-restart-web"
cp "$ROOT_DIR/README.md" "$PREFIX/share/boardlab/"
cp "$ROOT_DIR/VERSION" "$PREFIX/share/boardlab/"
cp "$ROOT_DIR/packaging/boardlab.service" "$PREFIX/lib/systemd/system/"
cp "$ROOT_DIR/packaging/99-boardlab.rules" "$PREFIX/lib/udev/rules.d/"
install -m 0755 "$ROOT_DIR/packaging/boardlab-wifi" "$PREFIX/lib/boardlab/boardlab-wifi"
install -m 0644 "$ROOT_DIR/packaging/boardlab-wifi.sudoers" "$PREFIX/share/boardlab/boardlab-wifi.sudoers"
install -m 0755 "$ROOT_DIR/packaging/boardlab-npu-load" "$PREFIX/lib/boardlab/boardlab-npu-load"
install -m 0644 "$ROOT_DIR/packaging/boardlab-npu.sudoers" "$PREFIX/share/boardlab/boardlab-npu.sudoers"
cp "$ROOT_DIR/scripts/check-rk3588-env.sh" "$PREFIX/bin/"
install -m 0755 "$ROOT_DIR/scripts/boardlab-hardware-validate.sh" "$PREFIX/bin/boardlab-hardware-validate.sh"
install -m 0755 "$ROOT_DIR/scripts/rk3588-hardware-validate.sh" "$PREFIX/bin/rk3588-hardware-validate.sh"
chmod +x "$PREFIX/bin/check-rk3588-env.sh"

install -m 0755 "$ROOT_DIR/scripts/run.sh" "$PREFIX/bin/boardlab-run"

cat > "$PREFIX/bin/boardlab-desktop" <<'EOF'
#!/usr/bin/env sh
set -eu
SCRIPT=$0
if command -v readlink >/dev/null 2>&1; then
    RESOLVED=$(readlink -f -- "$0" 2>/dev/null || printf '%s\n' "$0")
    [ -n "$RESOLVED" ] && SCRIPT=$RESOLVED
fi
DIR=$(unset CDPATH; cd -- "$(dirname -- "$SCRIPT")" && pwd)

exec "$DIR/boardlab-run" "$@"
EOF
chmod +x "$PREFIX/bin/boardlab-desktop"

cat > "$PREFIX/bin/boardlab-root" <<'EOF'
#!/usr/bin/env sh
set -eu
SCRIPT=$0
if command -v readlink >/dev/null 2>&1; then
    RESOLVED=$(readlink -f -- "$0" 2>/dev/null || printf '%s\n' "$0")
    [ -n "$RESOLVED" ] && SCRIPT=$RESOLVED
fi
DIR=$(unset CDPATH; cd -- "$(dirname -- "$SCRIPT")" && pwd)
if [ "$(id -u)" -eq 0 ]; then
    exec "$DIR/boardlab-root-run" "$@"
fi
if ! command -v pkexec >/dev/null 2>&1; then
    printf 'pkexec not found; install policykit-1 or run %s/boardlab-run as root.\n' "$DIR" >&2
    exit 1
fi
exec pkexec "$DIR/boardlab-root-run" "$@"
EOF
chmod +x "$PREFIX/bin/boardlab-root"

cat > "$PREFIX/bin/boardlab-root-run" <<'EOF'
#!/usr/bin/env sh
set -eu
SCRIPT=$0
if command -v readlink >/dev/null 2>&1; then
    RESOLVED=$(readlink -f -- "$0" 2>/dev/null || printf '%s\n' "$0")
    [ -n "$RESOLVED" ] && SCRIPT=$RESOLVED
fi
DIR=$(unset CDPATH; cd -- "$(dirname -- "$SCRIPT")" && pwd)
# The PolicyKit action explicitly permits GUI launch.  pkexec retains the
# caller's DISPLAY and XAUTHORITY for this action; do not clear either here.
# The root process needs that existing X11 session cookie to draw into the
# customer's desktop or VNC session.
if [ -d /run/user/0 ]; then
    export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/0}
else
    unset XDG_RUNTIME_DIR
fi
export QT_QPA_PLATFORM=xcb
export QT_XCB_GL_INTEGRATION=none
export QT_QUICK_BACKEND=${QT_QUICK_BACKEND:-software}
export QSG_RENDER_LOOP=${QSG_RENDER_LOOP:-basic}
exec "$DIR/boardlab-run" "$@"
EOF
chmod +x "$PREFIX/bin/boardlab-root-run"

while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        Exec=*)
            printf 'Exec=%s/bin/boardlab-root --framed\n' "$PREFIX"
            ;;
        TryExec=*)
            printf 'TryExec=%s/bin/boardlab-root\n' "$PREFIX"
            ;;
        Path=*)
            printf 'Path=%s\n' "$PREFIX"
            ;;
        Icon=*)
            # XFCE on several vendor images does not reliably resolve freshly
            # installed hicolor theme names.  A desktop-file absolute path is
            # supported by the specification and also works before icon-cache
            # refresh or the next login.
            printf 'Icon=%s/share/icons/hicolor/scalable/apps/boardlab.svg\n' "$PREFIX"
            ;;
        *)
            printf '%s\n' "$line"
            ;;
    esac
done < "$ROOT_DIR/packaging/boardlab.desktop" > "$PREFIX/share/applications/boardlab.desktop"
sed \
    -e "s|@BOARDLAB_ROOT_RUN@|$PREFIX/bin/boardlab-root-run|g" \
    "$ROOT_DIR/packaging/com.boardlab.root.policy" > "$PREFIX/share/polkit-1/actions/com.boardlab.root.policy"
cp "$ROOT_DIR/packaging/60-boardlab-root.rules" "$PREFIX/share/polkit-1/rules.d/"
cp "$ROOT_DIR/packaging/61-boardlab-networkmanager.rules" "$PREFIX/share/polkit-1/rules.d/"
cp "$ROOT_DIR/packaging/60-boardlab-root.pkla" "$PREFIX/share/polkit-1/localauthority/50-local.d/"
cp "$ROOT_DIR/packaging/61-boardlab-networkmanager.pkla" "$PREFIX/share/polkit-1/localauthority/50-local.d/"

ln -sf boardlab "$PREFIX/bin/rk-shell-qt"
ln -sf boardlab-run "$PREFIX/bin/rk-shell-qt-run"
ln -sf boardlab-desktop "$PREFIX/bin/rk-shell-qt-desktop"
ln -sf boardlab-root "$PREFIX/bin/rk-shell-qt-root"
ln -sf boardlab.service "$PREFIX/lib/systemd/system/rk-shell-qt.service"
ln -sf 99-boardlab.rules "$PREFIX/lib/udev/rules.d/99-rk-shell-qt.rules"
ln -sf boardlab.desktop "$PREFIX/share/applications/rk-shell-qt.desktop"

if [ -n "$SYSTEM_BIN_DIR" ]; then
    mkdir -p "$SYSTEM_BIN_DIR"
    link_system_launcher "$PREFIX/bin/boardlab" "$SYSTEM_BIN_DIR/boardlab"
    link_system_launcher "$PREFIX/bin/boardlab-run" "$SYSTEM_BIN_DIR/boardlab-run"
    link_system_launcher "$PREFIX/bin/boardlab-desktop" "$SYSTEM_BIN_DIR/boardlab-desktop"
    link_system_launcher "$PREFIX/bin/boardlab-root" "$SYSTEM_BIN_DIR/boardlab-root"
fi

printf 'Deployed to %s\n' "$PREFIX"
printf 'Binary: %s/bin/boardlab\n' "$PREFIX"
printf 'Runner: %s/bin/boardlab-run\n' "$PREFIX"
printf 'Desktop launcher (X11/xcb via PolicyKit root): %s/bin/boardlab-root\n' "$PREFIX"
printf 'Root launcher: %s/bin/boardlab-root\n' "$PREFIX"
printf 'Environment checker: %s/bin/check-rk3588-env.sh\n' "$PREFIX"
printf 'Hardware validator: %s/bin/boardlab-hardware-validate.sh\n' "$PREFIX"
printf 'Legacy RK3588 validator alias: %s/bin/rk3588-hardware-validate.sh\n' "$PREFIX"
printf 'Web QRC verifier: %s/bin/boardlab-verify-web-assets\n' "$PREFIX"
printf 'Web desktop restart helper: %s/bin/boardlab-restart-web\n' "$PREFIX"
printf 'Web QRC manifest: %s/share/boardlab/web-assets.sha256\n' "$PREFIX"
printf 'Service template: %s/lib/systemd/system/boardlab.service\n' "$PREFIX"
printf 'udev rules template: %s/lib/udev/rules.d/99-boardlab.rules\n' "$PREFIX"
printf 'Desktop entry: %s/share/applications/boardlab.desktop\n' "$PREFIX"
printf 'Polkit action: %s/share/polkit-1/actions/com.boardlab.root.policy\n' "$PREFIX"
printf 'Polkit rule: %s/share/polkit-1/rules.d/60-boardlab-root.rules\n' "$PREFIX"
printf 'Polkit local authority: %s/share/polkit-1/localauthority/50-local.d/60-boardlab-root.pkla\n' "$PREFIX"
if [ -n "$SYSTEM_BIN_DIR" ]; then
    printf 'Terminal launchers: %s/boardlab, %s/boardlab-run, %s/boardlab-desktop, %s/boardlab-root\n' "$SYSTEM_BIN_DIR" "$SYSTEM_BIN_DIR" "$SYSTEM_BIN_DIR" "$SYSTEM_BIN_DIR"
fi
