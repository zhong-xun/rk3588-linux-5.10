# Rockchip `update.img` OTA integration notes

RKNetUpdate 现已提供受控的完整 `update.img` OTA 入口。网页“系统 OTA”只接受
一个名为 `update.img` 的 Rockchip RKFW/RKAF 容器；它绝不会被当作某个单独分区
镜像 raw 写入。

This document records the compatibility boundary between RKNetUpdate and the
Rockchip `rktoolkit` local OTA utility. It is based on the Rockchip
`rktoolkit` source behavior and the Topeet iTOP-3588 OTA manual used during
implementation.

## What the official Linux flow does

The manual's Linux flow is:

1. Add persistent `oem` and `userdata` partitions to `parameter.txt` and mount
   them by `PARTLABEL`.
2. Build `rockdev/update.img` with a package-file.
3. Copy the complete package to `/userdata/update.img`.
4. Run `update ota /userdata/update.img`.

The bundled `rktoolkit` implementation does not apply the whole package in the
normal system. It extracts the `recovery` item from `update.img`, writes and
byte-compares that item against the recovery partition, then writes a
`boot-recovery` BCB request at misc offset 16 KiB with
`--update_package=<path>`. The actual package application is delegated to the
recovery environment.

## Container facts

`update.img` starts with the little-endian ASCII magic `RKFW`. Its fixed image
header contains the firmware-header offset and size. The firmware header starts
with `RKAF` and contains at most 16 packed 112-byte item entries. Each item has
a name, a package-relative offset, an image size, and a flash offset.

`internal/rkimage` 在接收完整 `update.img` 前验证：

- `RKFW` and `RKAF` magic values;
- firmware header position and declared range;
- item count in the closed range 1..16;
- non-empty, unique item names;
- every item range stays inside the uploaded file without integer overflow.

The normal `boot` and `uboot` upload paths reject a validated `update.img`.
The multi-part package must never be raw-written to a single partition.

## Why RKNetUpdate does not invoke `update` directly

The bundled C utility has no package signature, hash, manifest, AVB, A/B slot,
retry, rollback, or persistent state machine. It also trusts item count and
item offsets, can write a partially updated recovery partition, and only
performs a post-write byte comparison. Its recovery BCB write has no explicit
readback verification.

RKNetUpdate must preserve its stronger invariants: authenticated upload,
SHA-256 of the exact staged file, strict device identity, BCB compare-and-write
with fsync/readback, ordered BCB prepare/verify phases, and fail-closed recovery.

## Active RKNetUpdate workflow

1. The normal system uploads to `/var/lib/rknetupdate/updateimg-v1`, calculates
   a whole-package SHA-256 plus hashes for every RKAF item, and writes a strict
   manifest.  The package must contain compatible `parameter`, `recovery`, and
   `rootfs` items.
2. It verifies the current RK3588 layout (`p1`…`p6`, `p6=rootfs` grown to the
   end of the disk), validates that the package parameter retains that final
   rootfs layout, then writes a hash-bound private BCB request.
3. RKNetUpdate recovery checks the staged package again, shrinks the unmounted
   p6 filesystem, creates a temporary p7 labelled `userdata`, and copies the
   complete package to `/userdata/update.img`. After the GPT reread it waits for
   udev link processing; if a PARTLABEL link is transiently absent, the helper
   accepts a direct partition node only after matching the CRC-validated GPT
   against sysfs parent, PARTNAME, partition number, start LBA, and capacity.
4. It atomically replaces the private BCB with the stock Rockchip
   `--update_package=/userdata/update.img` request. The helper remains in the
   current recovery boot; S39 verifies the exact canonical BCB plus the durable
   `stock-pending` stage, then releases S40 to start the patched SDK recovery.
   No intermediate warm reboot is required. The SDK recovery invokes `rkupdate`
   in its full-package mode, so every payload selected by the package is
   written, including `parameter`, `misc`, and `recovery`.
   For the standard RK3588 package the temporary recovery adds
   `/dev/block/by-name/bootloader -> /dev/mmcblk0` only while this hash-bound
   handoff is pending, so the SDK loader writer has its expected raw-device
   target.
5. The package `parameter` restores the final p1..p6 GPT layout and thereby
   removes the temporary p7 itself. `rkupdate` already has the package open
   before that GPT write, so it can finish all remaining package items. The
   recovery then clears the BCB and reboots the normal system. A power loss
   after the canonical BCB handoff is restartable: S39 remounts p7, verifies
   the stock request and durable stage, and releases the updater again. There
   is no handoff warm reboot and no post-write cleanup preparation.

The standard single-slot package is therefore applied completely:
`package-file` is container metadata, and `parameter` is accepted only when it
exactly describes the final p1..p6 layout. The managed `rkupdate` path writes
all selected package items directly, including `parameter`, `bootloader`,
`uboot`, `misc`, `boot`, `recovery`, and `rootfs` when present. `backup`
normally appears as a `RESERVED` package-file declaration rather than a
payload item. Packages with an actual `userdata` item, A/B item names, or a
different GPT layout are still outside this p7-based single-slot workflow and
need a separate factory/RAM or A/B updater.

The package `recovery` item is deliberately written as part of that complete
operation.  Therefore, a product that expects to perform another RKNetUpdate
OTA after reboot must build `update.img` with the RKNetUpdate recovery overlay
and helper included. A plain SDK recovery image is valid for the current
upgrade, but it replaces the managed recovery during the OTA; deploy the
managed recovery again before the next LAN OTA.

This is a single-slot recovery workflow, not A/B rollback.  Stable power,
serial logs, a recovery backup, and Loader/Maskrom rescue access remain
mandatory for a production update.

## Manual evidence

- Pages 19-22: Linux OTA requires explicit `userdata` and `oem` partitions and
  `PARTLABEL` mounts; the default image grows rootfs instead.
- Pages 23-25: `package-file` selects the package contents, then
  `./build.sh updateimg` produces `rockdev/update.img`.
- Pages 26-27: the package is copied to `/userdata/update.img` and invoked as
  `update ota /userdata/update.img`; recovery performs the upgrade.
