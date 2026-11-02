#!/bin/bash -e

TARGET_DIR="${1:-}"
if [ -z "$TARGET_DIR" ] || [ ! -d "$TARGET_DIR" ]; then
	echo "Usage: $0 <rootfs-dir>" >&2
	exit 1
fi

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
SDK_DIR="${SDK_DIR:-$(realpath "$SCRIPT_DIR/../../../..")}"
TARGET_DIR="$(realpath "$TARGET_DIR")"
RKWIFIBT_DIR="$SDK_DIR/external/rkwifibt"
SDK_SOC="${TOPEET_WIFIBT_SOC:-${ROOTFS_SOC:-${RK_CHIP:-${RK_CHIP_FAMILY:-}}}}"
if [ -z "$SDK_SOC" ]; then
	SDK_SOC="$(basename "$SDK_DIR")"
	case "$SDK_SOC" in
		rk[0-9]*_6.1) SDK_SOC="${SDK_SOC%%_*}" ;;
		rk[0-9]*) ;;
		*) SDK_SOC="" ;;
	esac
fi

as_root()
{
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	else
		sudo "$@"
	fi
}

detect_kernel_release()
{
	local candidate release

	for candidate in \
		"$SDK_DIR/kernel/include/config/kernel.release" \
		"$SDK_DIR"/kernel-*/include/config/kernel.release
	do
		[ -r "$candidate" ] || continue
		read -r release < "$candidate" || true
		[ -n "$release" ] || continue
		printf '%s\n' "$release"
		return 0
	done

	for candidate in \
		"$SDK_DIR/kernel/include/generated/utsrelease.h" \
		"$SDK_DIR"/kernel-*/include/generated/utsrelease.h
	do
		[ -r "$candidate" ] || continue
		release="$(sed -n 's/^#define UTS_RELEASE "\([^"]*\)"/\1/p' "$candidate" | head -n 1)"
		[ -n "$release" ] || continue
		printf '%s\n' "$release"
		return 0
	done

	return 1
}

module_kernel_release()
{
	local module="$1"

	command -v modinfo >/dev/null 2>&1 || return 1
	modinfo "$module" 2>/dev/null | sed -n 's/^vermagic:[[:space:]]*\([^[:space:]]*\).*/\1/p' | head -n 1
}

module_usable_for_kernel()
{
	local module="$1"
	local expected="$2"
	local release

	[ -n "$expected" ] || return 0
	release="$(module_kernel_release "$module" || true)"
	[ -n "$release" ] || return 0
	[ "$release" = "$expected" ]
}

remove_stale_module_targets()
{
	local module="$1"
	local expected="$2"
	local dst release

	[ -n "$expected" ] || return 0
	for dst in "$TARGET_DIR/usr/local/modules/$module" "$TARGET_DIR/lib/modules/$module"; do
		[ -e "$dst" ] || continue
		module_usable_for_kernel "$dst" "$expected" && continue
		release="$(module_kernel_release "$dst" || echo unknown)"
		echo ">>> Removing stale $module from ${dst#"$TARGET_DIR"/}: vermagic $release != kernel $expected"
		as_root rm -f "$dst"
	done
}

find_module_list()
{
	local candidate

	for candidate in \
		"${TOPEET_WIFIBT_MODULE_LIST:-}" \
		"$SDK_DIR/buildroot/board/rockchip/${SDK_SOC:-}/fs-overlay/usr/local/modules/topeet-wifibt-modules.list" \
		"$SDK_DIR/buildroot/board/rockchip/${RK_CHIP_FAMILY:-}/fs-overlay/usr/local/modules/topeet-wifibt-modules.list" \
		"$SDK_DIR/buildroot/board/rockchip/${RK_CHIP:-}/fs-overlay/usr/local/modules/topeet-wifibt-modules.list" \
		"$TARGET_DIR/usr/local/modules/topeet-wifibt-modules.list"
	do
		[ -n "$candidate" ] || continue
		[ -s "$candidate" ] || continue
		printf '%s\n' "$candidate"
		return 0
	done


	return 1
}

find_module()
{
	local module="$1"
	local kernel_release dir found

	kernel_release="$(detect_kernel_release || true)"
	for dir in \
				${TOPEET_MODULE_SEARCH_DIRS:-} \
				"$SDK_DIR/output/kernel-modules" \
				"$SDK_DIR/output/target/lib/modules" \
				"$SDK_DIR/kernel" \
				"$SDK_DIR"/kernel-* \
				"$RKWIFIBT_DIR/drivers" \
				"$SDK_DIR/buildroot/board/rockchip/${SDK_SOC:-}/fs-overlay/usr/local/modules" \
				"$SDK_DIR/buildroot/board/rockchip/${RK_CHIP_FAMILY:-}/fs-overlay/usr/local/modules" \
				"$SDK_DIR/buildroot/board/rockchip/${RK_CHIP:-}/fs-overlay/usr/local/modules"
	do
		[ -d "$dir" ] || continue
		case "$dir" in
			"$SDK_DIR/buildroot/board/rockchip/"*)
				while IFS= read -r found; do
					[ -n "$found" ] || continue
					if module_usable_for_kernel "$found" "$kernel_release"; then
						printf '%s\n' "$found"
						return 0
					fi
					echo ">>> Ignoring $module from ${found#"$SDK_DIR"/}: vermagic $(module_kernel_release "$found" || echo unknown) != kernel $kernel_release" >&2
				done < <(find "$dir" -type f -name "$module" | sort)
				;;
			*)
				while IFS= read -r found; do
					[ -n "$found" ] || continue
					if module_usable_for_kernel "$found" "$kernel_release"; then
						printf '%s\n' "$found"
						return 0
					fi
					echo ">>> Ignoring $module from ${found#"$SDK_DIR"/}: vermagic $(module_kernel_release "$found" || echo unknown) != kernel $kernel_release" >&2
				done < <(find "$dir" -type f -name "$module" ! -path '*/buildroot/*' | sort)
				;;
		esac
	done

	return 1
}

copy_wifibt_modules()
{
	local list_file="$1"
	local module src rel
	local kernel_release
	local updated=0 missing=0 skipped=0

	kernel_release="$(detect_kernel_release || true)"
	as_root mkdir -p "$TARGET_DIR/usr/local/modules" "$TARGET_DIR/lib/modules"
	if [ "$(realpath "$list_file")" != "$TARGET_DIR/usr/local/modules/topeet-wifibt-modules.list" ]; then
		as_root install -m 0644 "$list_file" \
			"$TARGET_DIR/usr/local/modules/topeet-wifibt-modules.list"
	fi

	echo ">>> Syncing Wi-Fi/Bluetooth kernel modules from kernel build outputs"
	while IFS= read -r line || [ -n "$line" ]; do
		module="$(printf '%s' "$line" | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//')"
		[ -n "$module" ] || continue

		case "$module" in
			*/*|*.ko) ;;
			*)
				echo ">>> Skipping invalid module list entry: $module"
				skipped=$((skipped + 1))
				continue
				;;
		esac
		case "$module" in
			*/*)
				echo ">>> Skipping path-like module list entry: $module"
				skipped=$((skipped + 1))
				continue
				;;
		esac

			src="$(find_module "$module" || true)"
			if [ -z "$src" ]; then
				echo ">>> Missing requested $module; no kernel build output found"
				remove_stale_module_targets "$module" "$kernel_release"
				missing=$((missing + 1))
				continue
			fi

		as_root install -m 0644 "$src" "$TARGET_DIR/usr/local/modules/$module"
		as_root install -m 0644 "$src" "$TARGET_DIR/lib/modules/$module"
		rel="$src"
		case "$rel" in
			"$SDK_DIR"/*) rel="${rel#"$SDK_DIR"/}" ;;
		esac
		echo ">>> Updated $module from $rel"
		updated=$((updated + 1))
	done < "$list_file"

	echo ">>> Wi-Fi/Bluetooth module sync complete: updated=$updated missing=$missing skipped=$skipped"
}

copy_dir_contents()
{
	local src="$1"
	local dst="$2"

	[ -d "$src" ] || return 0
	as_root mkdir -p "$dst"
	[ "$(realpath "$src")" != "$(realpath "$dst")" ] || return 0
	as_root cp -a "$src"/. "$dst"/
}

copy_firmware()
{
	local list_file="$1"
	local overlay_root firmware_dir fw_dir

	overlay_root="${list_file%/usr/local/modules/topeet-wifibt-modules.list}"
	firmware_dir="$overlay_root/usr/lib/firmware"
	copy_dir_contents "$firmware_dir" "$TARGET_DIR/lib/firmware"

	copy_dir_contents "$SDK_DIR/kernel/firmware" "$TARGET_DIR/lib/firmware"

	if [ -d "$RKWIFIBT_DIR/firmware/realtek" ]; then
		as_root mkdir -p "$TARGET_DIR/lib/firmware/rtlbt"
		for fw_dir in "$RKWIFIBT_DIR"/firmware/realtek/*; do
			[ -d "$fw_dir" ] || continue
			as_root cp -a "$fw_dir"/. "$TARGET_DIR/lib/firmware"/
			as_root cp -a "$fw_dir"/. "$TARGET_DIR/lib/firmware/rtlbt"/
		done
	fi

	if [ -d "$RKWIFIBT_DIR/firmware/aic/usb" ]; then
		for fw_dir in "$RKWIFIBT_DIR"/firmware/aic/usb/*; do
			[ -d "$fw_dir" ] || continue
			copy_dir_contents "$fw_dir" "$TARGET_DIR/lib/firmware/${fw_dir##*/}"
		done
	fi
}

install_wifibt_scripts()
{
	local tool alias hook_dir tools_arch

	[ -d "$RKWIFIBT_DIR" ] || return 0
	tools_arch="${RK_KERNEL_ARCH:-arm64}"
	[ "$tools_arch" != arm64 ] || tools_arch=aarch64

	as_root mkdir -p "$TARGET_DIR/etc" "$TARGET_DIR/usr/bin"
	[ ! -d "$RKWIFIBT_DIR/conf" ] || \
		as_root cp -a "$RKWIFIBT_DIR"/conf/. "$TARGET_DIR/etc"/
	[ ! -d "$RKWIFIBT_DIR/scripts" ] || \
		as_root install -m 0755 "$RKWIFIBT_DIR"/scripts/* "$TARGET_DIR/usr/bin"/
	as_root rm -f "$TARGET_DIR/usr/bin/wifibt-sleep-hook.sh"

	for alias in bt-tty wifibt-info wifibt-vendor wifibt-id wifibt-bus \
		wifibt-chip wifibt-module; do
		as_root ln -sf wifibt-util.sh "$TARGET_DIR/usr/bin/$alias"
	done

	for tool in \
		"$SDK_DIR/device/rockchip/common/tools/$tools_arch/brcm_patchram_plus1" \
		"$SDK_DIR/device/rockchip/common/tools/$tools_arch/dhd_priv" \
		"$SDK_DIR/device/rockchip/common/tools/$tools_arch/rk_hciattach" \
		"$SDK_DIR/device/rockchip/common/tools/$tools_arch/rtk_hciattach" \
		"$RKWIFIBT_DIR/tools/brcm_tools/brcm_patchram_plus1" \
		"$RKWIFIBT_DIR/tools/brcm_tools/dhd_priv"
	do
		[ -f "$tool" ] || continue
		as_root install -m 0755 "$tool" "$TARGET_DIR/usr/bin/${tool##*/}"
	done

	if [ -d "$TARGET_DIR/etc/systemd/system" ] || [ -d "$TARGET_DIR/lib/systemd/system" ]; then
		as_root mkdir -p "$TARGET_DIR/lib/systemd/system"
		as_root install -m 0644 "$RKWIFIBT_DIR/wifibt-init.service" \
			"$TARGET_DIR/lib/systemd/system/wifibt-init.service"
		as_root rm -f "$TARGET_DIR/etc/systemd/system/sysinit.target.wants/wifibt-init.service" \
			"$TARGET_DIR/etc/systemd/system/multi-user.target.wants/wifibt-init.service"
	fi

	if [ -d "$TARGET_DIR/etc/init.d" ]; then
		as_root install -m 0755 "$RKWIFIBT_DIR/S36wifibt-init.sh" \
			"$TARGET_DIR/etc/init.d/wifibt-init"
	fi

	for hook_dir in /usr/lib/pm-utils/sleep.d /lib/systemd/system-sleep; do
		[ -d "$TARGET_DIR/$hook_dir" ] || continue
		as_root install -m 0755 "$RKWIFIBT_DIR/scripts/wifibt-sleep-hook.sh" \
			"$TARGET_DIR/$hook_dir/03wifibt"
	done

	as_root mkdir -p "$TARGET_DIR/etc/generate_logs.d"
	printf '%s\n' '#!/bin/sh' 'wifibt-info > ${1:-/tmp}/wifibt-info.txt' | \
		as_root tee "$TARGET_DIR/etc/generate_logs.d/80-wifibt.sh" >/dev/null
	as_root chmod 755 "$TARGET_DIR/etc/generate_logs.d/80-wifibt.sh"
}

install_topeet_wifibt_autoload()
{
	as_root mkdir -p "$TARGET_DIR/usr/bin"
	as_root tee "$TARGET_DIR/usr/bin/topeet-wifibt-load.sh" >/dev/null <<'EOF'
#!/bin/sh

MODULE_LIST="${TOPEET_WIFIBT_MODULE_LIST:-/usr/local/modules/topeet-wifibt-modules.list}"
MODULE_DIRS="${TOPEET_WIFIBT_MODULE_DIRS:-/usr/local/modules /lib/modules}"
AIC_DEVICES="${TOPEET_WIFIBT_AIC_DEVICES:-a69c:8800 a69c:8801 a69c:8d80 a69c:8d81 a69c:8d40 a69c:8d41 a69c:88dc a69c:88dd 368b:8d90 368b:8d91 368b:8d99}"
RTL_DEVICES="${TOPEET_WIFIBT_RTL_DEVICES:-0bda:d723 7392:d611 0bda:b733 0bda:f72b}"
USB_POLL_RETRIES="${TOPEET_WIFIBT_USB_POLL_RETRIES:-10}"
USB_POLL_INTERVAL="${TOPEET_WIFIBT_USB_POLL_INTERVAL:-0.1}"

log()
{
	echo "topeet-wifibt-load: $*"
	command -v logger >/dev/null 2>&1 && logger -t topeet-wifibt-load "$*" || true
}

module_in_list()
{
	[ -s "$MODULE_LIST" ] || return 1
	sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//' "$MODULE_LIST" | \
		grep -Fxq "$1"
}

module_path()
{
	local module="$1"
	local dir

	module_in_list "$module" || return 1
	for dir in $MODULE_DIRS; do
		[ -f "$dir/$module" ] || continue
		printf '%s\n' "$dir/$module"
		return 0
	done

	return 1
}

load_module()
{
	local module="$1"
	local path

	path="$(module_path "$module" 2>/dev/null)" || return 0
	if insmod "$path" 2>/tmp/topeet-wifibt-insmod.err; then
		log "loaded $module"
		return 0
	fi

	if grep -qi "File exists" /tmp/topeet-wifibt-insmod.err 2>/dev/null; then
		log "$module already loaded"
	else
		log "skip $module: $(cat /tmp/topeet-wifibt-insmod.err 2>/dev/null)"
	fi
	rm -f /tmp/topeet-wifibt-insmod.err
	return 0
}

unload_module()
{
	local module="${1%.ko}"

	rmmod "$module" 2>/dev/null || true
}

usb_device_present_once()
{
	local id vid pid
	local vendor_file
	local product_file
	local usb_devices
	local cur_vid
	local cur_pid

	if command -v lsusb >/dev/null 2>&1; then
		usb_devices="$(lsusb 2>/dev/null || true)"
		for id in "$@"; do
			vid="${id%:*}"
			pid="${id#*:}"
			[ "$vid" != "$id" ] || continue
			printf '%s\n' "$usb_devices" | grep -qi "ID $vid:$pid" && return 0
		done
	fi

	for vendor_file in /sys/bus/usb/devices/*/idVendor; do
		[ -f "$vendor_file" ] || continue
		product_file="${vendor_file%/idVendor}/idProduct"
		[ -f "$product_file" ] || continue
		cur_vid="$(cat "$vendor_file" 2>/dev/null || true)"
		cur_pid="$(cat "$product_file" 2>/dev/null || true)"
		for id in "$@"; do
			vid="${id%:*}"
			pid="${id#*:}"
			[ "$vid" != "$id" ] || continue
			[ "$cur_vid" = "$vid" ] && [ "$cur_pid" = "$pid" ] && return 0
		done
	done

	return 1
}

usb_device_present_any()
{
	local i

	i=0
	while [ "$i" -lt "$USB_POLL_RETRIES" ]; do
		usb_device_present_once "$@" && return 0
		i=$((i + 1))
		[ "$i" -lt "$USB_POLL_RETRIES" ] && sleep "$USB_POLL_INTERVAL"
	done

	return 1
}

detect_usb_family()
{
	local i

	i=0
	while [ "$i" -lt "$USB_POLL_RETRIES" ]; do
		if usb_device_present_once $AIC_DEVICES; then
			printf '%s\n' aic
			return 0
		fi
		if usb_device_present_once $RTL_DEVICES; then
			printf '%s\n' realtek
			return 0
		fi
		i=$((i + 1))
		[ "$i" -lt "$USB_POLL_RETRIES" ] && sleep "$USB_POLL_INTERVAL"
	done

	return 1
}

wait_hci0()
{
	local i

	command -v hciconfig >/dev/null 2>&1 || return 1
	for i in 1 2 3 4 5 6 7 8 9 10; do
		[ -d /sys/class/bluetooth/hci0 ] || hciconfig hci0 >/dev/null 2>&1 && return 0
		sleep 0.2
	done

	return 1
}

load_aic()
{
	load_module btbcm.ko
	load_module btintel.ko
	load_module btrtl.ko
	load_module aic_btusb.ko
	load_module aic_load_fw.ko
	load_module aic8800_fdrv.ko
}

load_realtek()
{
	load_module rtw_core.ko
	load_module rtw_usb.ko
	load_module rtw_8723x.ko
	load_module rtw_8723d.ko
	load_module rtw_8723du.ko
	load_module 8723du.ko
	load_module RTL8733BU.ko
	load_module rtk_btusb.ko
}

start_modules()
{
	[ -s "$MODULE_LIST" ] || {
		log "module list not found: $MODULE_LIST"
		return 0
	}

	case "$(detect_usb_family || true)" in
		aic)
			log "detected AIC USB device"
			load_aic
			;;
		realtek)
			log "detected Realtek USB device"
			load_realtek
			;;
		*)
			log "no AIC/Realtek USB Wi-Fi/Bluetooth device detected"
			return 0
			;;
	esac

	if [ -x /lib/firmware/blue.sh ]; then
		/lib/firmware/blue.sh || true
	elif [ -f /lib/firmware/blue.sh ]; then
		sh /lib/firmware/blue.sh || true
	fi

	# Under systemd this loader runs before bluetoothd. A legacy power-on
	# skips SMP setup; let BlueZ power on the controller through MGMT.
	if wait_hci0 && [ ! -d /run/systemd/system ]; then
		hciconfig hci0 up 2>/dev/null || true
	fi
}

stop_modules()
{
	unload_module rtk_btusb.ko
	unload_module RTL8733BU.ko
	unload_module 8723du.ko
	unload_module rtw_8723du.ko
	unload_module rtw_8723d.ko
	unload_module rtw_8723x.ko
	unload_module rtw_usb.ko
	unload_module rtw_core.ko
	unload_module aic8800_fdrv.ko
	unload_module aic_load_fw.ko
	unload_module aic_btusb.ko
	unload_module btrtl.ko
	unload_module btintel.ko
	unload_module btbcm.ko
}

case "${1:-start}" in
	start)
		start_modules
		;;
	stop)
		stop_modules
		;;
	restart)
		stop_modules
		start_modules
		;;
	*)
		echo "Usage: $0 {start|stop|restart}" >&2
		exit 2
		;;
esac
EOF
	as_root chmod 755 "$TARGET_DIR/usr/bin/topeet-wifibt-load.sh"

	if [ -d "$TARGET_DIR/etc/systemd/system" ] || [ -d "$TARGET_DIR/lib/systemd/system" ]; then
		as_root mkdir -p "$TARGET_DIR/lib/systemd/system" \
			"$TARGET_DIR/etc/systemd/system/sysinit.target.wants"
		as_root tee "$TARGET_DIR/lib/systemd/system/topeet-wifibt-load.service" >/dev/null <<'EOF'
[Unit]
Description=Load Topeet Wi-Fi/Bluetooth kernel modules
DefaultDependencies=no
After=local-fs.target systemd-udevd.service systemd-udev-settle.service
Before=network-pre.target bluetooth.service bluetooth.target wifibt-init.service
Wants=systemd-udev-settle.service

[Service]
Type=oneshot
ExecStart=/usr/bin/topeet-wifibt-load.sh start
ExecStop=/usr/bin/topeet-wifibt-load.sh stop
RemainAfterExit=yes

[Install]
WantedBy=sysinit.target
EOF
		as_root ln -rsf "$TARGET_DIR/lib/systemd/system/topeet-wifibt-load.service" \
			"$TARGET_DIR/etc/systemd/system/sysinit.target.wants/topeet-wifibt-load.service"
	fi
}

verify_wifibt_install()
{
	local module missing=0 service_link
	local installed_list="$TARGET_DIR/usr/local/modules/topeet-wifibt-modules.list"

	if [ ! -s "$installed_list" ]; then
		echo ">>> WARNING: Wi-Fi/Bluetooth module list was not installed"
		missing=$((missing + 1))
	else
		while IFS= read -r line || [ -n "$line" ]; do
			module="$(printf '%s' "$line" | sed 's/[[:space:]]*#.*$//; s/^[[:space:]]*//; s/[[:space:]]*$//')"
			[ -n "$module" ] || continue
			case "$module" in
				*.ko) ;;
				*) continue ;;
			esac
			if [ ! -f "$TARGET_DIR/usr/local/modules/$module" ] && \
				[ ! -f "$TARGET_DIR/lib/modules/$module" ]; then
				echo ">>> WARNING: requested Wi-Fi/Bluetooth module not installed: $module"
				missing=$((missing + 1))
			fi
		done < "$installed_list"
	fi

	if [ ! -x "$TARGET_DIR/usr/bin/topeet-wifibt-load.sh" ]; then
		echo ">>> WARNING: Wi-Fi/Bluetooth autoload helper was not installed"
		missing=$((missing + 1))
	fi

	if [ -d "$TARGET_DIR/etc/systemd/system" ] || [ -d "$TARGET_DIR/lib/systemd/system" ]; then
		service_link="$TARGET_DIR/etc/systemd/system/sysinit.target.wants/topeet-wifibt-load.service"
		if [ ! -f "$TARGET_DIR/lib/systemd/system/topeet-wifibt-load.service" ] || \
			[ ! -e "$service_link" ]; then
			echo ">>> WARNING: Wi-Fi/Bluetooth systemd autoload service is incomplete"
			missing=$((missing + 1))
		fi
	fi

	if [ -d "$TARGET_DIR/etc/init.d" ] && [ ! -x "$TARGET_DIR/etc/init.d/wifibt-init" ]; then
		echo ">>> WARNING: Wi-Fi/Bluetooth init.d service was not installed"
		missing=$((missing + 1))
	fi

	if [ "$missing" -gt 0 ]; then
		echo ">>> Wi-Fi/Bluetooth install verification completed with $missing warning(s)"
		[ "${TOPEET_WIFIBT_STRICT:-0}" != 1 ] || return 1
	else
		echo ">>> Wi-Fi/Bluetooth install verification passed"
	fi
}

main()
{
	local list_file

	list_file="$(find_module_list || true)"
	if [ -z "$list_file" ]; then
		echo ">>> ERROR: No matching Topeet Wi-Fi/Bluetooth module list found" >&2
		return 1
	fi

	echo ">>> Using Wi-Fi/Bluetooth module list: ${list_file#"$SDK_DIR"/}"
	copy_wifibt_modules "$list_file"
	copy_firmware "$list_file"
	install_wifibt_scripts
	install_topeet_wifibt_autoload
	verify_wifibt_install
}

main "$@"
