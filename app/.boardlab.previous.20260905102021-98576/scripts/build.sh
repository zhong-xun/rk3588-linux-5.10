#!/usr/bin/env sh
set -eu

ROOT_DIR=$(unset CDPATH; cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"

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
BUILD_TYPE=${BUILD_TYPE:-Release}
REQUIRE_GSTREAMER_APPSINK=${BOARDLAB_REQUIRE_GSTREAMER_APPSINK:-OFF}
ENABLE_NPU_YOLO=${BOARDLAB_ENABLE_NPU_YOLO:-ON}
REQUIRE_NPU_YOLO=${BOARDLAB_REQUIRE_NPU_YOLO:-OFF}
RKNN_PROVIDER=${BOARDLAB_RKNN_PROVIDER:-AUTO}
CMAKE_COMMAND=${BOARDLAB_CMAKE_COMMAND:-cmake}
TOOLCHAIN_FILE=${BOARDLAB_CMAKE_TOOLCHAIN_FILE:-${CMAKE_TOOLCHAIN_FILE:-}}
SYSROOT=${BOARDLAB_SYSROOT:-${CMAKE_SYSROOT:-}}
CMAKE_PREFIX=${BOARDLAB_CMAKE_PREFIX_PATH:-${CMAKE_PREFIX_PATH:-}}
QRC_FILE="$ROOT_DIR/qml/qml.qrc"
SOURCE_FINGERPRINT_FILE="$BUILD_DIR/.boardlab-source-inputs.sha256"
BUILD_FINGERPRINT_FILE="$BUILD_DIR/.boardlab-build-inputs.sha256"

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
    resolved_build_dir=$(normalize_path "$BUILD_DIR")
    if [ "$resolved_build_dir" != "$BUILD_DIR" ]; then
        printf 'Refusing automatic cleanup because BUILD_DIR changed resolution: %s -> %s\n' \
            "$BUILD_DIR" "$resolved_build_dir" >&2
        exit 2
    fi
    case "$resolved_build_dir" in
        "$ROOT_DIR"/*)
            ;;
        *)
            printf 'Refusing automatic stale-cache cleanup outside the source tree: %s\n' "$resolved_build_dir" >&2
            printf 'Remove it manually or choose a BUILD_DIR below %s.\n' "$ROOT_DIR" >&2
            exit 2
            ;;
    esac
    [ "$resolved_build_dir" != "$ROOT_DIR" ] || {
        printf 'Refusing automatic cleanup of the source directory: %s\n' "$resolved_build_dir" >&2
        exit 2
    }
    printf 'Removing stale CMake build directory: %s\n' "$reason"
    rm -rf -- "$resolved_build_dir"
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

build_input_fingerprint() {
    {
        printf '%s\0' \
            "cmake-command=$CMAKE_COMMAND" \
            "cmake-version=$CMAKE_VERSION" \
            "build-type=$BUILD_TYPE" \
            "require-gstreamer-appsink=$REQUIRE_GSTREAMER_APPSINK" \
            "enable-npu-yolo=$ENABLE_NPU_YOLO" \
            "require-npu-yolo=$REQUIRE_NPU_YOLO" \
            "rknn-provider=$RKNN_PROVIDER" \
            "toolchain-file=$TOOLCHAIN_FILE" \
            "sysroot=$SYSROOT" \
            "cmake-prefix-path=$CMAKE_PREFIX" \
            "cc=${CC:-}" \
            "cxx=${CXX:-}" \
            "cflags=${CFLAGS:-}" \
            "cxxflags=${CXXFLAGS:-}" \
            "ldflags=${LDFLAGS:-}" \
            "pkg-config-path=${PKG_CONFIG_PATH:-}" \
            "pkg-config-sysroot=${PKG_CONFIG_SYSROOT_DIR:-}" \
            "pkg-config-libdir=${PKG_CONFIG_LIBDIR:-}"
        if [ -n "$TOOLCHAIN_FILE" ] && [ -f "$TOOLCHAIN_FILE" ]; then
            sha256sum "$TOOLCHAIN_FILE"
        fi
        for configure_argument do
            printf 'configure-argument=%s\0' "$configure_argument"
        done
    } | sha256sum | awk '{print $1}'
}

clean_changed_build_inputs() {
    [ -f "$BUILD_DIR/CMakeCache.txt" ] || return 0

    if [ ! -f "$BUILD_FINGERPRINT_FILE" ]; then
        remove_stale_build_dir "build input fingerprint is missing; rebuilding to avoid reusing another SDK toolchain or ABI"
        return
    fi

    stored_fingerprint=$(sed -n '1{s/[[:space:]].*$//;p;}' "$BUILD_FINGERPRINT_FILE")
    if [ "$stored_fingerprint" != "$BUILD_FINGERPRINT" ]; then
        remove_stale_build_dir "toolchain or build configuration changed"
    fi
}

write_source_fingerprint() {
    fingerprint_tmp="$SOURCE_FINGERPRINT_FILE.tmp.$$"
    (umask 022 && printf '%s\n' "$SOURCE_FINGERPRINT" > "$fingerprint_tmp")
    mv -f "$fingerprint_tmp" "$SOURCE_FINGERPRINT_FILE"
}

write_build_fingerprint() {
    fingerprint_tmp="$BUILD_FINGERPRINT_FILE.tmp.$$"
    (umask 022 && printf '%s\n' "$BUILD_FINGERPRINT" > "$fingerprint_tmp")
    mv -f "$fingerprint_tmp" "$BUILD_FINGERPRINT_FILE"
}

cmake_version_components() {
    CMAKE_VERSION=$("$CMAKE_COMMAND" --version 2>/dev/null |
        sed -n '1{s/^[^0-9]*\([0-9][0-9.]*\).*$/\1/p;}')
    [ -n "$CMAKE_VERSION" ] || {
        printf 'Could not determine CMake version from: %s\n' "$CMAKE_COMMAND" >&2
        exit 2
    }
    CMAKE_VERSION_MAJOR=${CMAKE_VERSION%%.*}
    version_remainder=${CMAKE_VERSION#*.}
    CMAKE_VERSION_MINOR=${version_remainder%%.*}
    case "$CMAKE_VERSION_MAJOR:$CMAKE_VERSION_MINOR" in
        *[!0-9:]*|:*)
            printf 'Unsupported CMake version string: %s\n' "$CMAKE_VERSION" >&2
            exit 2
            ;;
    esac
}

validate_build_dir
command -v "$CMAKE_COMMAND" >/dev/null 2>&1 || {
    printf 'CMake command is not available: %s\n' "$CMAKE_COMMAND" >&2
    exit 2
}
cmake_version_components
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

set -- \
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" \
    "-DBOARDLAB_REQUIRE_GSTREAMER_APPSINK=$REQUIRE_GSTREAMER_APPSINK" \
    "-DBOARDLAB_ENABLE_NPU_YOLO=$ENABLE_NPU_YOLO" \
    "-DBOARDLAB_REQUIRE_NPU_YOLO=$REQUIRE_NPU_YOLO" \
    "-DBOARDLAB_RKNN_PROVIDER=$RKNN_PROVIDER" \
    "$@"
[ -z "$CMAKE_PREFIX" ] || set -- "-DCMAKE_PREFIX_PATH=$CMAKE_PREFIX" "$@"
[ -z "$SYSROOT" ] || set -- "-DCMAKE_SYSROOT=$SYSROOT" "$@"
if [ -n "$TOOLCHAIN_FILE" ]; then
    [ -f "$TOOLCHAIN_FILE" ] || {
        printf 'CMake toolchain file does not exist: %s\n' "$TOOLCHAIN_FILE" >&2
        exit 2
    }
    set -- "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE" "$@"
fi

BUILD_FINGERPRINT=$(build_input_fingerprint "$@")
[ -n "$BUILD_FINGERPRINT" ] || {
    printf 'Could not calculate the BoardLab build input fingerprint.\n' >&2
    exit 2
}
clean_changed_build_inputs

if [ "$CMAKE_VERSION_MAJOR" -gt 3 ] ||
    { [ "$CMAKE_VERSION_MAJOR" -eq 3 ] && [ "$CMAKE_VERSION_MINOR" -ge 13 ]; }; then
    "$CMAKE_COMMAND" -S "$ROOT_DIR" -B "$BUILD_DIR" "$@"
else
    mkdir -p "$BUILD_DIR"
    (cd "$BUILD_DIR" && "$CMAKE_COMMAND" "$ROOT_DIR" "$@")
fi

BUILD_JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
if [ "$CMAKE_VERSION_MAJOR" -gt 3 ] ||
    { [ "$CMAKE_VERSION_MAJOR" -eq 3 ] && [ "$CMAKE_VERSION_MINOR" -ge 12 ]; }; then
    "$CMAKE_COMMAND" --build "$BUILD_DIR" --parallel "$BUILD_JOBS"
else
    "$CMAKE_COMMAND" --build "$BUILD_DIR" -- "-j$BUILD_JOBS"
fi
write_source_fingerprint
write_build_fingerprint

printf 'Built %s\n' "$BUILD_DIR/boardlab"
