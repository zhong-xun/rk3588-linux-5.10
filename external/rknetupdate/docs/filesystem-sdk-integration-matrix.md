# RKNetUpdate filesystem SDK integration

`scripts/integrate-filesystem-sdks.sh --apply` copies the SDK-local
RKNetUpdate source component into every standard Rockchip SDK under
`/home/topeet/Linux` and into the Ubuntu rootfs path of each
`*_extboot_6.1` tree. The normal-system post-rootfs hook builds missing ARM64
outputs from `$SDK_DIR/external/rknetupdate`, so it does not use a sibling
checkout or an absolute host path.

The post-rootfs hook installs:

- `/usr/sbin/rknetupdate`
- `/usr/sbin/rknetupdate-control`
- `/etc/rknetupdate.conf` (fresh/legacy generic profiles are selected by SDK;
  customized configurations are preserved)
- a systemd unit and `multi-user.target` enable link when systemd is present
- `/etc/init.d/S90rknetupdate` for the existing BusyBox/SysV-style init
- private state directories under `/var/lib/rknetupdate`

The binaries are static AArch64 executables, so the image does not need a Go
runtime or a distribution-specific package. Buildroot keeps its existing
BusyBox init and dynamic eudev device manager; the hook only adds one `S90`
script to the existing init directory. It does not enable systemd or replace
udev. Ubuntu/Debian images use their existing systemd when present.

## Buildroot tools

The Rockchip Buildroot common configuration already includes
`fs/e2fs.config`, which provides `e2fsck` and `resize2fs`. The integration adds
`gptfdisk`/`sgdisk` to the same common configuration. Standard profiles that
include `fs/vfat.config` already provide `parted`; it is intentionally not
forced for minimal toolchains whose locale/wchar support is disabled. These
tools are used by the Recovery helper and by diagnostics; they do not change
the GPT layout during normal boot.

## Runtime profiles

The 13 standard SDKs use the verified single-slot eMMC production profile:
`token=topeet`, `expected_parent_device=/dev/mmcblk0`, `allow_flash=true`,
`dry_run=false`, and rootfs/update.img Recovery writes enabled. Their matching
Recovery integration and partition layout are listed in
`recovery-sdk-integration-matrix.md`.

The seven `*_extboot_6.1` SDKs use the safe profile with `allow_flash=false`,
`dry_run=true`, and rootfs/update.img disabled. They have no independent
`misc` partition or compatible p6/p7 Recovery workflow, so enabling the
standard profile there would be unsafe.

An existing vendor/customer config is retained on rebuild. An exact copy of
the old generic dry-run template is recognized and upgraded to the selected
SDK profile. The integration refuses to replace an unrelated service, init
script, executable, or customized configuration.

## Capability matrix

| SDK family | Normal user-space service | Buildroot partition tools | Recovery rootfs/update.img |
| --- | --- | --- | --- |
| `rk*-linux-5.10` | Ubuntu, Debian, Buildroot | e2fsck, resize2fs, sgdisk; parted in `fs/vfat.config` profiles | Enabled with the matching integrated Recovery |
| `rk*-linux-6.1` | Ubuntu, Debian, Buildroot | e2fsck, resize2fs, sgdisk; parted in `fs/vfat.config` profiles | Enabled with the matching integrated Recovery |
| `*_extboot_6.1` | Ubuntu | N/A (no Buildroot tree) | Not enabled: no single-slot Recovery contract in this SDK family |

This matrix describes source integration. It is not a claim that every SoC
has been physically tested on the RK3588 board; each image must be tested on
its matching board and partition table.
