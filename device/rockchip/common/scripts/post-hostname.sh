#!/bin/bash -e

source "${RK_POST_HELPER:-$(dirname "$(realpath "$0")")/post-helper}"

[ -z "$RK_ROOTFS_HOSTNAME_ORIGINAL" ] || exit 0

HOSTNAME="${RK_ROOTFS_HOSTNAME:-topeet}"

message "Setting hostname: $HOSTNAME"

mkdir -p "$TARGET_DIR/etc"
echo "$HOSTNAME" > "$TARGET_DIR/etc/hostname"

touch "$TARGET_DIR/etc/hosts"
sed -i '/^127.0.1.1/d' "$TARGET_DIR/etc/hosts"
echo "127.0.1.1	$HOSTNAME" >> "$TARGET_DIR/etc/hosts"
