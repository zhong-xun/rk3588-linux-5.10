#!/usr/bin/env sh
set -eu

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build"}
BUILD_TYPE=${BUILD_TYPE:-Release}
REQUIRE_GSTREAMER_APPSINK=${BOARDLAB_REQUIRE_GSTREAMER_APPSINK:-OFF}
QRC_FILE="$ROOT_DIR/qml/qml.qrc"
SOURCE_FINGERPRINT_FILE="$BUILD_DIR/.boardlab-source-inputs.sha256"

validate_build_dir() {
    case "$BUILD_DIR" in
        ""|"/"|"$ROOT_DIR"|"$ROOT_DIR/")
            printf 'Refusing unsafe BUILD_DIR: %s\n' "$BUILD_DIR" >&2
            exit 2
            ;;
        "$ROOT_DIR"/build-host-ui|"$ROOT_DIR"/build-host-ui/*|"$ROOT_DIR"/build-host-ui.retired|"$ROOT_DIR"/build-host-ui.retired.*)
            # This directory contains a retired host preview binary whose
            # embedded Web dashboard predates the independent music, buzzer,
            # and LED tools.  Never let normal project commands revive it.
            printf 'Refusing retired BUILD_DIR: %s\n' "$BUILD_DIR" >&2
            printf 'Use the supported build directory: %s/build\n' "$ROOT_DIR" >&2
            exit 2
            ;;
    esac
}

remove_stale_build_dir() {
    reason=$1
    case "$BUILD_DIR" in
        "$ROOT_DIR"/*)
            ;;
        *)
            printf 'Refusing automatic stale-cache cleanup outside the source tree: %s\n' "$BUILD_DIR" >&2
            printf 'Remove it manually or choose a BUILD_DIR below %s.\n' "$ROOT_DIR" >&2
            exit 2
            ;;
    esac
    printf 'Removing stale CMake build directory: %s\n' "$reason"
    rm -rf -- "$BUILD_DIR"
}

clean_stale_build_dir() {
    if [ -d "$BUILD_DIR" ] && [ ! -f "$BUILD_DIR/CMakeCache.txt" ] &&
        find "$BUILD_DIR" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null | grep -q .; then
        remove_stale_build_dir "$BUILD_DIR has build artifacts but no CMake cache"
        return
    fi

    if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
        return
    fi

    cached_source=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)
    cached_build=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)

    if [ -n "$cached_source" ] && [ "$cached_source" != "$ROOT_DIR" ]; then
        remove_stale_build_dir "$BUILD_DIR was configured for source $cached_source, current source is $ROOT_DIR"
        return
    fi

    if [ -n "$cached_build" ] && [ "$cached_build" != "$BUILD_DIR" ]; then
        remove_stale_build_dir "cache was generated in $cached_build, current build dir is $BUILD_DIR"
        return
    fi

    # CTest and the standalone NPU smoke target were removed from the product
    # build.  A reconfigure does not reliably remove their generated metadata
    # or binaries, so rebuild once from a clean project-local tree when an old
    # cache is encountered.
    if [ -d "$BUILD_DIR/Testing" ] || [ -f "$BUILD_DIR/CTestTestfile.cmake" ] ||
        find "$BUILD_DIR" -path '*/CTestTestfile.cmake' -print -quit 2>/dev/null | grep -q . ||
        [ -e "$BUILD_DIR/npu-yolo-demo-smoke" ] ||
        find "$BUILD_DIR/CMakeFiles" -maxdepth 1 -type d \
            \( -name '*-test.dir' -o -name '*_test.dir' \) -print -quit 2>/dev/null | grep -q .; then
        remove_stale_build_dir "$BUILD_DIR contains retired test build artifacts"
        return
    fi

    if [ -f "$BUILD_DIR/cmake_install.cmake" ] &&
        grep -q 'bin/rk-shell-qt' "$BUILD_DIR/cmake_install.cmake" &&
        ! grep -q 'bin/boardlab' "$BUILD_DIR/cmake_install.cmake"; then
        remove_stale_build_dir "$BUILD_DIR still installs the old rk-shell-qt target"
    fi
}

source_input_fingerprint() {
    {
        printf '%s\0' "$ROOT_DIR/CMakeLists.txt"
        for input_path in \
            "$ROOT_DIR/src" \
            "$ROOT_DIR/qml" \
            "$ROOT_DIR/web" \
            "$ROOT_DIR/assets" \
            "$ROOT_DIR/third_party/rknn"; do
            [ -d "$input_path" ] || continue
            find "$input_path" -type f -print0
        done
    } | LC_ALL=C sort -z | xargs -0 sha256sum | sha256sum | awk '{print $1}'
}

clean_changed_source_inputs() {
    [ -f "$BUILD_DIR/CMakeCache.txt" ] || return 0

    if [ ! -f "$SOURCE_FINGERPRINT_FILE" ]; then
        remove_stale_build_dir "source input fingerprint is missing; rebuilding to avoid a timestamp-preserved source sync using stale objects"
        return
    fi

    stored_fingerprint=$(sed -n '1{s/[[:space:]].*$//;p;}' "$SOURCE_FINGERPRINT_FILE")
    if [ "$stored_fingerprint" != "$SOURCE_FINGERPRINT" ]; then
        remove_stale_build_dir "source input fingerprint changed; rebuilding to avoid a timestamp-preserved source sync using stale objects"
    fi
}

write_source_fingerprint() {
    fingerprint_tmp="$SOURCE_FINGERPRINT_FILE.tmp.$$"
    (umask 022 && printf '%s\n' "$SOURCE_FINGERPRINT" > "$fingerprint_tmp")
    mv -f "$fingerprint_tmp" "$SOURCE_FINGERPRINT_FILE"
}

validate_build_dir
clean_stale_build_dir

command -v sha256sum >/dev/null 2>&1 || {
    printf 'sha256sum is required to validate BoardLab build inputs.\n' >&2
    exit 2
}
SOURCE_FINGERPRINT=$(source_input_fingerprint)
[ -n "$SOURCE_FINGERPRINT" ] || {
    printf 'Could not calculate the BoardLab source input fingerprint.\n' >&2
    exit 2
}
clean_changed_source_inputs

if find "$ROOT_DIR/qml" -type f ! -name 'qml.qrc' -newer "$QRC_FILE" | grep -q .; then
    touch "$QRC_FILE"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DBOARDLAB_REQUIRE_GSTREAMER_APPSINK="$REQUIRE_GSTREAMER_APPSINK"
cmake --build "$BUILD_DIR" --parallel "${JOBS:-$(nproc 2>/dev/null || echo 4)}"
write_source_fingerprint

printf 'Built %s\n' "$BUILD_DIR/boardlab"
