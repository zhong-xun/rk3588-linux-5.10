#!/bin/bash -e

# RKNETUPDATE-MANAGED-FILESYSTEM-HOOK-V1

STANDALONE="${RKNETUPDATE_STANDALONE:-}"
if [ -z "$STANDALONE" ]; then
  source "${RK_POST_HELPER:-$(dirname "$(realpath "$0")")/post-helper}"
else
  TARGET_DIR="$(realpath "${1:?target rootfs is required}")"
  POST_OS="${2:-unknown}"
  POST_INIT_SYSTEMD=
  POST_INIT_SYSV=
  POST_INIT_BUSYBOX=
  [ -d "$TARGET_DIR/etc/systemd/system" ] && POST_INIT_SYSTEMD=1
  if [ -e "$TARGET_DIR/etc/init.d/rcS" ]; then
    [ -d "$TARGET_DIR/etc/rcS.d" ] && POST_INIT_SYSV=1
    grep -wq BusyBox "$TARGET_DIR/etc/inittab" 2>/dev/null && POST_INIT_BUSYBOX=1
  fi
  error() { echo "RKNetUpdate: $*" >&2; }
  message() { echo "RKNetUpdate: $*"; }
fi

# Recovery and ramboot images have their own initramfs helper and must not
# receive a normal-system web service.
case "$POST_OS" in
  recovery|ramboot) exit 0 ;;
esac

if [ -n "${RK_SDK_DIR:-}" ]; then
  SDK_DIR="$RK_SDK_DIR"
else
  SDK_DIR="$(cd "$(dirname "$(realpath "$0")")/../../../.." && pwd)"
fi
PROJECT_DIR="${RKNETUPDATE_PROJECT_DIR:-$SDK_DIR/external/rknetupdate}"
BIN="${RKNETUPDATE_BIN:-$PROJECT_DIR/output/bin/rknetupdate-linux-arm64}"
CONTROL_BIN="${RKNETUPDATE_CONTROL_BIN:-$PROJECT_DIR/output/bin/rknetupdate-control-linux-arm64}"
SDK_CONFIG_SRC="$SDK_DIR/device/rockchip/common/scripts/rknetupdate-filesystem.conf"
LEGACY_CONFIG_SRC="$PROJECT_DIR/packaging/filesystem/rknetupdate.conf"
if [ -n "${RKNETUPDATE_CONFIG:-}" ]; then
  CONFIG_SRC="$RKNETUPDATE_CONFIG"
elif [ -r "$SDK_CONFIG_SRC" ]; then
  CONFIG_SRC="$SDK_CONFIG_SRC"
else
  CONFIG_SRC="$LEGACY_CONFIG_SRC"
fi
SERVICE_SRC="${RKNETUPDATE_SERVICE:-$PROJECT_DIR/packaging/filesystem/systemd/rknetupdate.service}"
SYSV_SRC="${RKNETUPDATE_SYSV:-$PROJECT_DIR/packaging/filesystem/sysv/S90rknetupdate}"

if [ ! -x "$BIN" ] || [ ! -x "$CONTROL_BIN" ]; then
  if [ ! -r "$PROJECT_DIR/Makefile" ]; then
    error "Missing SDK-local RKNetUpdate source: $PROJECT_DIR"
    error "Install the SDK-local component before building the rootfs"
    exit 1
  fi
  message "Building SDK-local RKNetUpdate filesystem helpers..."
  make -C "$PROJECT_DIR" arm64 control-arm64
fi

for required in "$BIN" "$CONTROL_BIN" "$CONFIG_SRC" "$SERVICE_SRC" "$SYSV_SRC"; do
  if [ ! -r "$required" ]; then
    error "Missing RKNetUpdate filesystem asset: $required"
    error "Build the SDK-local ARM64 binaries with: make -C $PROJECT_DIR arm64 control-arm64"
    exit 1
  fi
done

same_file()
{
  [ -f "$2" ] && cmp -s "$1" "$2"
}

managed_file()
{
  [ -f "$1" ] && {
    grep -q '^# RKNETUPDATE-MANAGED-FILESYSTEM-V1$' "$1" ||
      grep -q '^# RKNETUPDATE-MANAGED-FILESYSTEM-CONFIG-V1$' "$1" ||
      grep -q '^ExecStart=/usr/sbin/rknetupdate ' "$1" ||
      grep -q '^DAEMON=/usr/sbin/rknetupdate$' "$1"
  }
}

managed_binary()
{
  [ -x "$1" ] || return 1
  command -v readelf >/dev/null 2>&1 || return 1
  LC_ALL=C readelf -h "$1" 2>/dev/null | grep -q 'Machine:.*AArch64' || return 1
  strings "$1" 2>/dev/null | grep -q 'rknetupdate'
}

install_managed()
{
  src="$1"
  dst="$2"
  mode="$3"
  if same_file "$src" "$dst"; then
    return 0
  fi
  if [ -e "$dst" ] && ! managed_file "$dst"; then
    error "Refusing to replace unmanaged RKNetUpdate file: $dst"
    exit 1
  fi
  install -D -m "$mode" "$src" "$dst"
}

install_binary()
{
  src="$1"
  dst="$2"
  if same_file "$src" "$dst"; then
    return 0
  fi
  if [ -e "$dst" ] && ! managed_binary "$dst"; then
    error "Refusing to replace unmanaged executable: $dst"
    exit 1
  fi
  install -D -m 0755 "$src" "$dst"
}

message "Installing RKNetUpdate normal-system files..."
install_binary "$BIN" "$TARGET_DIR/usr/sbin/rknetupdate"
install_binary "$CONTROL_BIN" "$TARGET_DIR/usr/sbin/rknetupdate-control"

# Keep a vendor/customer configuration intact. A target that still contains
# the exact legacy dry-run template is safe to migrate to the SDK-selected
# managed profile; this is what upgrades existing Buildroot target trees.
if [ ! -e "$TARGET_DIR/etc/rknetupdate.conf" ]; then
  install_managed "$CONFIG_SRC" "$TARGET_DIR/etc/rknetupdate.conf" 0600
elif same_file "$CONFIG_SRC" "$TARGET_DIR/etc/rknetupdate.conf"; then
  :
elif [ -r "$LEGACY_CONFIG_SRC" ] && \
     same_file "$LEGACY_CONFIG_SRC" "$TARGET_DIR/etc/rknetupdate.conf"; then
  message "Upgrading legacy RKNetUpdate dry-run config to the SDK profile"
  install -D -m 0600 "$CONFIG_SRC" "$TARGET_DIR/etc/rknetupdate.conf"
else
  message "Keeping existing customer RKNetUpdate config"
fi

if [ "${POST_INIT_SYSTEMD:-}" ]; then
  install_managed "$SERVICE_SRC" "$TARGET_DIR/etc/systemd/system/rknetupdate.service" 0644
  mkdir -p "$TARGET_DIR/etc/systemd/system/multi-user.target.wants"
  ln -sfn ../rknetupdate.service \
    "$TARGET_DIR/etc/systemd/system/multi-user.target.wants/rknetupdate.service"
fi

if [ "${POST_INIT_SYSV:-}" ] || [ "${POST_INIT_BUSYBOX:-}" ]; then
  install_managed "$SYSV_SRC" "$TARGET_DIR/etc/init.d/S90rknetupdate" 0755
fi

mkdir -p "$TARGET_DIR/var/lib/rknetupdate/uploads" \
  "$TARGET_DIR/var/lib/rknetupdate/rootfs-v2" \
  "$TARGET_DIR/var/lib/rknetupdate/bundle-v1" \
  "$TARGET_DIR/var/lib/rknetupdate/updateimg-v1"
chmod 700 "$TARGET_DIR/var/lib/rknetupdate" \
  "$TARGET_DIR/var/lib/rknetupdate/uploads" \
  "$TARGET_DIR/var/lib/rknetupdate/rootfs-v2" \
  "$TARGET_DIR/var/lib/rknetupdate/bundle-v1" \
  "$TARGET_DIR/var/lib/rknetupdate/updateimg-v1"
