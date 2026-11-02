#!/bin/sh
set -eu

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

SDK_ROOT="$TMPDIR/sdk"
TARGET_DIR="$SDK_ROOT/buildroot/output/test/target"
KERNEL_DIR="$SDK_ROOT/kernel"
MODULES='8723du.ko aic8800_fdrv.ko aic_load_fw.ko rtk_btusb.ko'

make_file() {
	mkdir -p "$(dirname "$1")"
	printf '%s\n' "$2" > "$1"
}

mkdir -p "$TARGET_DIR/usr/local/modules"
make_file "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/rtl8723du/8723du.ko" 8723du.ko
make_file "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/aic8800/aic8800_fdrv/aic8800_fdrv.ko" aic8800_fdrv.ko
make_file "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/aic8800/aic_load_fw/aic_load_fw.ko" aic_load_fw.ko
make_file "$KERNEL_DIR/drivers/bluetooth/rtk_btusb.ko" rtk_btusb.ko
make_file "$KERNEL_DIR/drivers/bluetooth/unlisted.ko" unlisted.ko
printf '%s\n' $MODULES > "$TARGET_DIR/usr/local/modules/topeet-wifibt-modules.list"

SDK_ROOT="$SDK_ROOT" "$SCRIPT_DIR/sync-prebuilt-modules.sh" "$TARGET_DIR"

for module in $MODULES; do
	cmp "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/rtl8723du/$module" "$TARGET_DIR/usr/local/modules/$module" 2>/dev/null || cmp "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/aic8800/aic8800_fdrv/$module" "$TARGET_DIR/usr/local/modules/$module" 2>/dev/null || cmp "$KERNEL_DIR/drivers/net/wireless/rockchip_wlan/aic8800/aic_load_fw/$module" "$TARGET_DIR/usr/local/modules/$module" 2>/dev/null || cmp "$KERNEL_DIR/drivers/bluetooth/$module" "$TARGET_DIR/usr/local/modules/$module"
done
test ! -e "$TARGET_DIR/usr/local/modules/unlisted.ko"
