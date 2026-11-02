PREBUILT KERNEL MODULES - TEMPORARY BRINGUP ONLY

This directory contains only the prebuilt .ko modules listed in
topeet-wifibt-modules.list for initial Wi-Fi/Bluetooth validation. Buildroot
refreshes listed modules from this SDK's kernel build outputs when available.

Do not treat these modules as a long-term production solution until the target
kernel version, module vermagic, license status, and source availability have
been verified.

Recommended follow-up:
- Prefer rebuilding the modules from kernel or external driver source.
- On the target rootfs, check each module with: modinfo <module>.ko
- Confirm the vermagic matches the running kernel before loading.
- Replace prebuilt modules with source-built modules when the driver source is
  available in kernel/ or external/.
