# RKNetUpdate Recovery SDK integration matrix

## What is changed

RKNetUpdate keeps Rockchip's `external/recovery` and `rkupdate` implementation.
The integration only adds the managed layer around it:

1. `S39rknetupdate-rootfs` recognizes the private BCB intents, validates the
   durable stage, runs the rootfs/bundle/update.img migration helper, and keeps
   stock recovery suppressed on every fail-closed path.
2. The same S39 explicitly hands Rockchip SD/TF requests to stock recovery
   before any private request or userdata probe. It recognizes `sdfwupdate` or
   an exact `misc + 16 KiB` BCB request with `recovery\n--rk_fwupdate\n` and
   never creates the private suppression marker for that path.
3. `S40recovery` releases the stock updater only after a verified update.img
   handoff, creates the temporary loader alias, and mirrors updater logs to the
   serial console.
4. `post-rknetupdate-recovery.sh` explicitly installs the two init scripts,
   marker, and shared static AArch64 helper during Recovery rootfs
   post-processing. This avoids a 5.10 vendor overlay allow-list bug. A
   missing overlay or wrong-architecture helper fails the build.
5. The active SoC Recovery Buildroot defconfig enables `GPTFDISK/SGDISK`.
   Rockchip's recovery package already supplies e2fsprogs/resize2fs and the
   vfat fragment supplies parted/partprobe.
6. The installer fixes the vendor Buildroot `gptfdisk` package's empty `CXX`
   variable. Without this compatibility fix, enabling SGDISK in most SDK
   snapshots fails by trying to execute `-D_LARGEFILE_SOURCE` as a command.
7. A GPT reread can temporarily remove eudev `by-partlabel` links even though
   `/dev/mmcblk0p1` through `p7` already exist. The migration helper waits for
   udev, then permits a direct-node fallback only when the CRC-validated GPT
   and sysfs agree on parent disk, PARTNAME, partition number, start LBA, and
   capacity. S39 pins the already validated standard-layout misc p2 identity
   across the same recovery run.
8. Recovery completion for Rockchip SD/TF and USB updates waits for the actual
   source block-device path to disappear before proceeding to the vendor
   reboot. `recovery_notice.c` blinks an available status LED and emits up to
   ten tones only as a best-effort indication. Its only automatic beeper match
   is an input device named exactly `pwm-beeper` or `pwm beeper` that exposes
   both `EV_SND` and `SND_TONE`; it never falls back to `SND_BELL`, another
   EV_SND device, or a `pwmchipN` path. Missing LEDs and beepers are normal on
   customer carrier boards and do not change the update or removal policy.

The source of truth is `packaging/recovery/`. Apply or audit it with:

```sh
./scripts/integrate-recovery-sdks.sh --apply
./scripts/integrate-recovery-sdks.sh --check
```

## Rockchip SD/TF contract

The native recovery path, rather than RKNetUpdate, owns a card made by
SDDiskTool_v2. The card user volume must be FAT32/vfat for the current SDK
baseline and contain both `sdupdate.img` and a root-level
`sd_boot_config.config` whose content includes `fw_update=1`. The request BCB
is the Rockchip 1088-byte structure at `misc + 16 KiB`, with the canonical
`boot-recovery` command and `recovery\n--rk_fwupdate\n` field.

The local recovery sources (`external/recovery/sdboot.c`, `recovery.c`, and
`roots.c`) confirm the cmdline/config/file sequence and explicit `vfat` mount
path. exFAT is not enabled by this integration. SDDiskTool_v2 therefore blocks
large-file exFAT plans unless a product-specific recovery/kernel capability is
explicitly declared and separately verified on hardware.

集成器还会把 RKNetUpdate 的 Go 源码和运行时资源复制到每个 SDK 的
`external/rknetupdate/`。Recovery post-hook 默认使用这个 SDK-local 目录；若
helper 尚未生成，Buildroot 构建期间执行
`make -C external/rknetupdate recovery-arm64`，因此不依赖 SDK 外部的绝对路径。
也可以在 SDK 内预先运行 `make -C external/rknetupdate all-arm64`。

The installer owns the SDK-local `external/rknetupdate/` component, its
versioned overlay files, its uniquely named hook, the post-hook symlink, the
two required Buildroot options, and the exact two-line `gptfdisk` CXX
compatibility fix. It refuses to overwrite an unknown file or patch an unknown
package layout.

## Standard SDKs integrated

| Product | Kernel | SDK root | Overlay form | Recovery defconfig |
|---|---:|---|---|---|
| RK3562 | 5.10 | `/home/topeet/Linux/rk3562/rk3562-linux-5.10` | flat | `rockchip_rk3562_recovery_defconfig` |
| RK3562 | 6.1 | `/home/topeet/Linux/rk3562/rk3562-linux-6.1` | nested | `rockchip_rk3562_recovery_defconfig` |
| RK3568 | 5.10 | `/home/topeet/Linux/rk3568/rk3568-linux-5.10` | flat | `rockchip_rk3568_recovery_defconfig` |
| RK3568 | 6.1 | `/home/topeet/Linux/rk3568/rk3568-linux-6.1` | nested | `rockchip_rk3568_recovery_defconfig` |
| RK3568 mini | 5.10 | `/home/topeet/Linux/rk3568_mini/rk3568-linux-5.10` | flat | `rockchip_rk3568_recovery_defconfig` |
| RK3568 mini | 6.1 | `/home/topeet/Linux/rk3568_mini/rk3568-linux-6.1` | nested | `rockchip_rk3568_recovery_defconfig` |
| RK3576 | 6.1 | `/home/topeet/Linux/rk3576/rk3576-linux-6.1` | nested | `rockchip_rk3576_recovery_defconfig` |
| RK3588 | 5.10 | `/home/topeet/Linux/rk3588/rk3588-linux-5.10` | flat | `rockchip_rk3588_recovery_defconfig` |
| RK3588 | 6.1 | `/home/topeet/Linux/rk3588/rk3588-linux-6.1` | nested | `rockchip_rk3588_recovery_defconfig` |
| RK3588 mini | 5.10 | `/home/topeet/Linux/rk3588_mini/rk3588-linux-5.10` | flat | `rockchip_rk3588_recovery_defconfig` |
| RK3588 mini | 6.1 | `/home/topeet/Linux/rk3588_mini/rk3588-linux-6.1` | nested | `rockchip_rk3588_recovery_defconfig` |
| RK3588S | 5.10 | `/home/topeet/Linux/rk3588s/rk3588s-linux-5.10` | flat | `rockchip_rk3588_recovery_defconfig` |
| RK3588S | 6.1 | `/home/topeet/Linux/rk3588s/rk3588s-linux-6.1` | nested | `rockchip_rk3588_recovery_defconfig` |

`flat` means that RKNetUpdate is stored directly under
`device/rockchip/common/overlays/recovery/`. `nested` means that it lives under
`overlays/recovery/rknetupdate-rootfs/`. The managed post-rootfs hook copies
the files explicitly for both forms, because some 5.10 SDKs incorrectly pass
the Recovery defconfig name as an overlay allow-list.

## Recovery completion notice

The canonical C source lives in
`packaging/recovery/sdk/recovery-notice/`. It is copied idempotently to each
standard SDK's `external/recovery/` by `integrate-recovery-sdks.sh`; `--check`
compares the exact managed source and verifies the two media-specific call
sites without rewriting SDK-specific `recovery.c` flow. The canonical source
selects the numerically smallest matching `/dev/input/eventN` when more than
one valid PWM beeper exists.

Use `--notice-only` when the SDK does not need the full RKNetUpdate recovery
overlay. This mode limits both `--apply` and `--check` to
`external/recovery/`; it never changes Buildroot configuration, hooks, or
rootfs overlays.

Run these host checks before applying to an SDK collection:

```sh
make test-recovery-notice-contract
make test-recovery-notice-integration
make test-recovery-mmc-detection
make test-recovery-sd-config
make test-recovery-flow-contract
make test-rknetupdate-handoff-validation
```

The first creates temporary LED, input-event, and removable-media fixtures. It
proves deterministic selection of the lowest valid event number, rejects both
`SND_BELL` and near-match device names, confirms device removal for one second
to reject transient node loss, and verifies that a board without indicators
still exits only after the simulated medium disappears. The second
uses a temporary 5.10-style SDK layout to cover installer apply/check
idempotence. The third checks every 5.10/6.1 recovery source against a fake
sysfs layout where the MMC host and block-device numbers intentionally differ.
The fourth verifies exact `sdfwupdate` boot-argument and `fw_update` key
matching, bounded values, and malformed configuration rejection across every
source tree. The fifth checks the success/failure handoff, whole-device TF
removal path, and strict compilation of the shared notice, MMC, and TF parser
code. None of these tests writes a physical input device, flashes storage, or
reboots a board.

The RKNetUpdate handoff test covers the two 6.1 Recovery trees that accept the
managed complete-image request. It runs standard SHA-256 vectors, malformed
state JSON cases, exact zero-padded BCB field checks, and verifies that a
recognized managed request fails closed instead of falling back to the
ordinary updater when marker, metadata, or package digest validation fails.

## Supported partition contract

The managed migration currently supports the standard single-slot eMMC layout:

```text
p1 uboot, p2 misc, p3 boot, p4 recovery, p5 backup, p6 rootfs:grow
```

Recovery temporarily shrinks p6 and creates p7 `userdata`, then restores the
final p1..p6 GPT. The following configurations are intentionally outside this
contract:

- A/B layouts: p6/p7 are `system_a/system_b`; p7 must never be repurposed.
- extboot layouts: no misc partition and rootfs is normally p3.
- SPI-NOR layouts: no compatible misc/rootfs p6 contract.
- non-ext4 rootfs or a rootfs that is not the grow-to-end partition.

The seven `*_extboot_6.1` trees under `/home/topeet/Linux` have no standalone
Buildroot Recovery framework and are not discovered by the installer.

## Verification boundary

`--check` verifies all managed hashes, hook links, shell syntax, the AArch64
helper, and required Buildroot options. A completed `./build.sh recovery` proves
that the SDK produced a Recovery image with its own kernel and tools. Hardware
validation still has to be performed on one board for each product family;
building another SoC's image is not permission to flash it onto an RK3588.

Host regression includes `test-recovery-sd-handoff.sh` for native TF release,
`test-sdk-recovery-handoff.sh` for the same-boot stock updater gate, and
`test-updateimg-migration-loop.sh` for the `updateimg-shrunk + p7 present`
resume state.
