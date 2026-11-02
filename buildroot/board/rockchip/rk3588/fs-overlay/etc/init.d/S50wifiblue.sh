#!/bin/sh

AIC_DEVICES="${TOPEET_WIFIBT_AIC_DEVICES:-a69c:8800 a69c:8801 a69c:8d80 a69c:8d81 a69c:8d40 a69c:8d41 a69c:88dc a69c:88dd 368b:8d90 368b:8d91 368b:8d99}"
RTL_DEVICES="${TOPEET_WIFIBT_RTL_DEVICES:-0bda:d723 7392:d611 0bda:b733 0bda:f72b}"
USB_POLL_RETRIES="${TOPEET_WIFIBT_USB_POLL_RETRIES:-10}"
USB_POLL_INTERVAL="${TOPEET_WIFIBT_USB_POLL_INTERVAL:-0.1}"

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

load_module()
{
	local module="$1"

	[ -f "/usr/local/modules/$module" ] || return 0
	insmod "/usr/local/modules/$module" 2>/tmp/S50wifiblue.err && return 0
	grep -qi "File exists" /tmp/S50wifiblue.err 2>/dev/null || \
		echo "S50wifiblue: failed to load $module: $(cat /tmp/S50wifiblue.err 2>/dev/null)"
	rm -f /tmp/S50wifiblue.err
}

unload_module()
{
	rmmod "${1%.ko}" 2>/dev/null || true
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

start_bluetooth_userland()
{
	wait_hci0 || return 0

	if command -v dbus-launch >/dev/null 2>&1; then
		eval "$(dbus-launch --sh-syntax 2>/dev/null)" || true
	fi

	if [ -x /usr/libexec/bluetooth/obexd ]; then
		/usr/libexec/bluetooth/obexd -r /opt/ -a -d >/dev/null 2>&1 &
	fi

	hciconfig hci0 up >/dev/null 2>&1 || true
	hciconfig hci0 piscan >/dev/null 2>&1 || true
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
	load_module btbcm.ko
	load_module btintel.ko
	load_module btrtl.ko
	load_module rtw_core.ko
	load_module rtw_usb.ko
	load_module rtw_8723x.ko
	load_module rtw_8723d.ko
	load_module rtw_8723du.ko
	load_module 8723du.ko
	load_module RTL8733BU.ko
	load_module rtk_btusb.ko
}

case "$1" in
	start|"")
		mkdir -p /data/cfg
		[ -f /etc/wpa_supplicant.conf ] && cp /etc/wpa_supplicant.conf /data/cfg/
		bt_device=0
		case "$(detect_usb_family || true)" in
			aic)
				load_aic
				bt_device=1
				;;
			realtek)
				load_realtek
				bt_device=1
				;;
		esac
		if [ -x /lib/firmware/blue.sh ]; then
			/lib/firmware/blue.sh >/dev/null 2>&1 || true
		fi
		[ "$bt_device" = 1 ] && start_bluetooth_userland
		echo nameserver 114.114.114.114 > /etc/resolv.conf
		;;
	stop|status)
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
		;;
	*)
		echo "Usage: start" >&2
		exit 3
		;;
esac
