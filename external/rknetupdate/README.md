# RKNetUpdate

RKNetUpdate 是面向瑞芯微 Linux 开发板的局域网镜像升级服务，兼容 arm64 Buildroot、Ubuntu 和 Debian。除单独 `uboot.img` / `boot.img` 写入和 `rootfs.img` 离线分区升级外，网页第四项“系统 OTA”上传完整 Rockchip `update.img`，由定制 recovery 离线完成系统升级。标准 RK3588 单槽包中的 `bootloader`、`uboot`、`misc`、`boot`、`recovery` 和 `rootfs` 都会被写入；`parameter` 仅在与当前 GPT 完全一致时通过最终 GPT 恢复生效。

`update.img` 是包含多个分区条目的 Rockchip OTA 容器，不是 `boot.img`、`uboot.img` 或单独的 `rootfs.img`。服务严格识别 `RKFW`/`RKAF` 容器并拒绝把它作为单分区镜像刷写；完整 OTA 流程见 [docs/rktoolkit-ota-integration.md](docs/rktoolkit-ota-integration.md)，兼容 API 的 bundle 说明见 [docs/offline-bundle-v1.md](docs/offline-bundle-v1.md)。

完整 OTA 会写入包内 `recovery`。若升级后仍需继续使用网络 OTA，打包 `update.img` 时必须把 RKNetUpdate recovery overlay/helper 一同打入 `recovery.img`；普通 SDK recovery 可完成当前升级，但会替换该受控 recovery。

> 写错 U-Boot、Boot、Misc 或 Rootfs 分区可能导致设备无法启动。正式上板前必须准备串口、Maskrom/Loader 救砖工具、原始镜像和稳定电源。系统 OTA 已接入完整 `update.img` recovery 工作流，但它不是 A/B 回滚，也没有签名信任链。

## 安全模型

- HTTP API 仍使用 Bearer token；工厂局域网默认值为 `topeet`，Web 页面会自动使用它。可用 `token=`、`RKNETUPDATE_TOKEN` 或 `--token` 覆盖为设备专用值。
- 默认 `dry_run=true` 且 `allow_flash=false`，不会写分区。
- U-Boot/Boot 直接真实写入必须同时设置 `dry_run=false`、`allow_flash=true` 和 `expected_parent_device=/dev/<eMMC整盘>`。rootfs、bundle 与完整 `update.img` OTA 使用各自独立的 recovery 写入开关，不会放开直接写入。
- 单镜像只接受与目标一致的精确文件名；bundle 仅接受 `rootfs.img` 与可选 `boot.img`、`uboot.img`，并且 rootfs 必须存在。
- 镜像以流式方式写入权限为 `0600` 的随机临时文件，同时计算 SHA-256；不把整个镜像放入内存。
- 一个上传请求就是一个升级请求：服务完成 SHA-256、格式、容量和目标分区校验后立即开始 U-Boot/Boot 写入，或自动执行 rootfs、bundle、完整 `update.img` 的 BCB 准备、BCB 复核与干净重启。页面和公开 API 不再要求输入确认短语或一次性确认 token。
- 默认分区映射是 `/dev/disk/by-partlabel/uboot` 与 `/dev/disk/by-partlabel/boot`。服务只允许这类标签链接，并验证最终目标是内核标记的分区，不允许整盘。
- 服务从 sysfs 反查并公开最终分区路径和父整盘，例如标签映射到 `/dev/mmcblk0p4`、父盘为 `/dev/mmcblk0`。真实模式启动时，`expected_parent_device` 必须是绝对、非符号链接、非分区的整盘块设备；上传前和写入前都会再次精确匹配父盘。
- 显式设备路径不能是符号链接。普通文件默认拒绝，仅 `test_mode=true` 时可作为测试分区。
- 不解析或猜测 parameter 中的 raw offset，不执行由用户输入拼接的 shell 命令。
- 同一时间只允许一个上传/刷写任务；失败的 rootfs/bundle/`update.img` 暂存会保留以便 recovery/串口排障，其他已完成的单镜像暂存会清理。
- 写入前检查镜像不大于目标分区；写入后执行 `fsync`，再按镜像实际长度回读并比对 SHA-256。

## SDK 分区证据

本工程对四份 6.1 SDK 的 U-Boot/Boot 分区证据做了只读审计。Rootfs recovery 测试链路另外只在当前 RK3588 SDK 中增加 recovery rootfs overlay，并修改 `post-recovery.sh` 注入 helper；没有修改 parameter、U-Boot、DTS、内核或其他 SoC SDK：

| SoC | parameter 证据 | 结论 |
| --- | --- | --- |
| RK3562 | `/home/topeet/Linux/rk3562/rk3562-linux-6.1/device/rockchip/.chips/rk3562/parameter-buildroot-fit.txt` | GPT 中存在 `uboot` 与 `boot` 标签 |
| RK3568 | `/home/topeet/Linux/rk3568/rk3568-linux-6.1/device/rockchip/.chips/rk3566_rk3568/parameter-buildroot-fit.txt` | GPT 中存在 `uboot` 与 `boot` 标签 |
| RK3576 | `/home/topeet/Linux/rk3576/rk3576-linux-6.1/device/rockchip/.chips/rk3576/parameter.txt` | GPT 中存在 `uboot` 与 `boot` 标签 |
| RK3588 | `/home/topeet/Linux/rk3588/rk3588-linux-6.1/device/rockchip/.chips/rk3588/parameter.txt` | GPT 中存在 `uboot` 与 `boot` 标签 |

四份非 A/B 默认布局的核心条目一致：`uboot` 大小为 `0x2000` 个 512 字节扇区，即 4 MiB；`boot` 大小为 `0x20000` 个扇区，即 64 MiB。该信息只用于核对预期，不用于定位写入。运行时以 Linux 实际分区设备和容量为准。

SDK 打包链证据位于各 SDK 的 `device/rockchip/common/scripts/mk-firmware.sh`、`mk-updateimg.sh` 和 `rkflash.sh`：它们把 `parameter.txt`、`package-file` 与镜像放入 firmware/rockdev 打包流程。RK3588 当前源码中 `/home/topeet/Linux/rk3588/rk3588-linux-6.1/kernel-6.1/boot.img` 是 FIT/DTB magic `0xd00dfeed`，因此本服务对 FIT 容器采用宽容检查，不会把“不是 Android boot magic”误判为非法。

## Recovery smoke 闭环

`rknetupdate-control` 用于正常系统，生产路径不可通过 CLI 改写：misc 固定为 `/dev/disk/by-partlabel/misc`，父整盘固定为直接块设备 `/dev/mmcblk0`。程序同时核对内核 `PARTNAME=misc`、sysfs 父子关系和分区容量。它使用 SDK 的 1088-byte Rockchip BCB 结构，位置为 misc 偏移 16 KiB：`command[32] + status[32] + recovery[768] + needupdate[4] + systemFlag[252]`，不使用 AOSP stage 布局。

准备和重启是两个独立命令。准备命令只在 BCB 的 command/status/recovery/needupdate 共 836 字节全部为零时写入固定请求；任何残留字段都会导致拒绝且不写入。写入执行 `fsync` 并逐字节回读；它不会重启：

```sh
rknetupdate-control prepare-smoke \
  --confirm 'PREPARE RKNETUPDATE RECOVERY SMOKE'
```

固定请求为 `command=boot-recovery`，recovery 字段精确为 `recovery\n--rknetupdate-smoke-v1\n`，systemFlag 完整保留。确认新 recovery 镜像已经安装、串口和救砖链路就绪后，才执行第二条命令；它会再次精确验证 BCB，并要求不同的确认短语：

```sh
rknetupdate-control reboot-smoke \
  --confirm 'REBOOT INTO RKNETUPDATE RECOVERY SMOKE'
```

正常系统不会直接调用内核 reboot syscall：确认通过后先 `sync`，systemd 系统优先执行 `/usr/bin/systemctl --no-block reboot` 并兼容 `/bin/systemctl`，非 systemd Buildroot 固定执行 `/sbin/reboot`，均不使用 force 参数，使 PID 1 按正常关机流程卸载已挂载文件系统。`--no-block` 使命令在重启事务入队后返回，避免服务关闭时把仍在等待的 reboot 子进程终止并误报失败。systemd 单元在保持 `ProtectSystem=strict` 的同时仅额外放行 `/run/systemd`，供 `systemctl` 更新 `reboot-param` 后提交 clean reboot；否则 Ubuntu 20.04 会以 `Failed to unlink reboot parameter file: Read-only file system` 拒绝请求。测试通过注入的假操作验证命令选择和调用顺序，不会执行真实重启。

Recovery 的 S39 按 `RKNETUPDATE_DEVICE_WAIT_SECONDS` 等待固定 misc PARTLABEL（默认 120 秒），再只读比较 BCB 前 836 字节。精确请求会调用 `rknetupdate-recovery --smoke`；可识别但版本错误或畸形的 smoke 请求也会进入 helper 的失败路径。Smoke 模式不挂载或访问 userdata，不读取 manifest，不运行 e2fsck，不打开 rootfs 写入；它验证 recovery marker、misc/rootfs 的固定标签和父盘、rootfs 未在任何位置挂载，以及精确 BCB 请求，然后仅清空 command/status/recovery/needupdate，保留 systemFlag，执行 `fsync` 和精确回读。状态同时输出到 stdout 和 RAM 文件 `/run/rknetupdate-smoke.status.json`。

helper 在成功后会自行 `sync` 并调用内核重启；S39 保留 BusyBox 重启作为旧 helper 的后备路径。任何可识别的 smoke 失败都会提前设置 stock-recovery 抑制标记，S40 不会启动 `/usr/bin/recovery`，设备停留在 recovery/rescue 供串口检查。RAM 状态在断电或重启后不会保留，因此首次上板必须同步采集串口日志。

## Rockchip 原生 TF 升级交接

Recovery 中的 `S39rknetupdate-rootfs` 必须把 Rockchip 原生 TF/SD 升级优先级放在
RKNetUpdate 私有请求之前。它识别 `/proc/cmdline` 中的 `sdfwupdate`，或精确匹配
`misc + 16 KiB` 处 1088-byte BCB 的前 836 bytes：

```text
command  = boot-recovery
status   = empty
recovery = recovery
           --rk_fwupdate
needupdate = empty
```

命中后 S39 立即退出，不创建 `rknetupdate-stock-recovery-suppressed`，不等待或挂载
userdata，也不启动 RKNetUpdate helper；后续 `S40recovery` 继续启动原生
`/usr/bin/recovery`，由 SDK recovery 自己挂载 TF 用户分区、读取配置并调用
Rockchip `rkupdate`。BCB fallback 只接受完整规范请求，不接受在私有字段或
`systemFlag` 中偶然出现的 `--rk_fwupdate` 字符串。

SDDiskTool_v2 制作的升级卡必须在用户分区根目录放置：

```text
sdupdate.img
sd_boot_config.config    # fw_update=1
```

当前本地 RK3568/RK3576/RK3588 SDK recovery 明确支持 `vfat`/FAT32 挂载，且 TF
升级逻辑查找上述两个文件。RKNetUpdate 不重写这套原生 `rkupdate` 流程；
`scripts/test-recovery-sd-handoff.sh` 只验证启动交接，不替代真实板卡升级验收。

## Rootfs recovery 测试链路

当前主路径是 schema 2 临时 p7 迁移。正常系统固定从以下目录读取升级包：

```text
/var/lib/rknetupdate/rootfs-v2/rootfs.img
/var/lib/rknetupdate/rootfs-v2/rootfs.manifest.json
```

manifest 使用严格 JSON，拒绝未知字段。镜像 SHA-256、ext4 UUID、block size 和 block count 都必须与最终文件完全一致：

```json
{
  "schema": 2,
  "image": "rootfs.img",
  "size": 536870912,
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "filesystem": "ext4",
  "filesystem_uuid": "01234567-89ab-cdef-0123-456789abcdef",
  "block_size": 4096,
  "block_count": 131072,
  "target_partlabel": "rootfs",
  "expected_parent": "/dev/mmcblk0"
}
```

正常系统先执行 prepare。确认短语必须包含完整镜像哈希，程序会验证镜像、p6 GPT 身份和 ext4 几何，再把规范化 schema 2 请求写入 misc BCB：

```sh
rknetupdate-control prepare-rootfs \
  --allow-write \
  --confirm "PREPARE_ROOTFS_V2:<完整sha256>"
```

确认 recovery 已安装且救砖链路可用后，执行第二次独立授权并正常关机进入 recovery：

```sh
rknetupdate-control reboot-rootfs \
  --allow-write \
  --confirm "REBOOT_ROOTFS_V2:<完整sha256>"
```

Recovery 中的固定状态机为 `prepared -> shrunk -> p7-ready -> staged -> p6-verified -> gpt-restored -> expanded`。程序离线缩小 p6，在磁盘尾部临时创建 `PARTLABEL=userdata` 的 p7，把升级包和状态复制到 p7，然后 raw 写入并完整读回 p6。验证成功后删除 p7，恢复 p6 原始 start LBA、type GUID、unique GUID 和名称，将 p6 延伸到 GPT last usable LBA，最后扩展 ext4。每个不可逆步骤前后都会重新解析 primary/backup GPT 和 CRC；BCB 阶段只允许幂等或向前更新，全部完成后才清除请求并保留 `systemFlag`。

`rknetupdate-recovery` 只允许在带有 `/etc/rknetupdate-recovery` 标记的 initramfs 中运行，生产设备固定为 `/dev/mmcblk0`、p6 `rootfs`、misc PARTLABEL `misc`。成功且 BCB 已清除后 helper 会输出阶段与每 5% 的写入进度，随后 `sync` 并调用内核重启返回正常系统；S39 只保留兼容后备重启。任何校验、写入、读回、GPT 或扩容失败都保留 BCB、抑制 stock recovery，并停留在 recovery/rescue，不自动重启。

旧 schema 1 `/userdata/{rootfs.img,rootfs.manifest.json,rootfs.apply}` 写入路径仅作为兼容代码保留，不是本次默认 RK3588 rootfs 升级方案，也不提供临时 p7 的断电重入保证。

2026-07-14 已在 `192.168.1.118` 的 RK3588/eMMC 实板完成一次 schema 2 全流程测试。测试镜像 raw SHA-256 为 `ff63025375666a5fb0dabee3201b9b94e0fa1b9e178cb96c4636037f68eff669`，目标 ext4 UUID 为 `d5bed891-f45e-42e6-9f84-60152589ffec`。设备约三分钟后自动返回 Ubuntu；验收确认 p7 已删除、p6 恢复到 `31016861184` bytes、原 PARTUUID `614e0000-0000-4b53-8000-1d28000054a9` 保持、ext4 已扩展到 `7572475` 个 4096-byte blocks、BCB 前 836 bytes 清零且 `systemFlag` 保持。测试 marker 为 `/etc/rknetupdate-rootfs-test`。

这仍是单槽、原地修改 GPT 的测试方案，不是断电原子更新。掉电发生在 ext4 收缩或 GPT 写入窗口时仍可能需要 USB Loader/Maskrom 救援；生产方案应采用 A/B rootfs、启动确认和自动回滚。首次用于其他板卡时仍必须核对实际 GPT、镜像容量、供电稳定性和 recovery FIT 容量。

HTTP rootfs 工作流把镜像固定暂存到上述目录，原子生成 schema 2 manifest 并执行 ext4 超块/几何与 SHA-256 校验。完整 `e2fsck -f -n` 只在带有 e2fsprogs 1.47.0 的 recovery 中执行，因此 Ubuntu 22.04 的旧 e2fsck 不会把 Buildroot 的 `orphan_file` feature 误判为损坏。一次已认证上传会由服务自动写入并回读 BCB、再次验证 BCB 后请求干净重启；正常系统不会直接打开 rootfs 分区写入。本地与实板验证命令如下：

```sh
make test vet control control-arm64 recovery recovery-arm64
make test-rknetupdate-handoff-validation
sudo -n ./scripts/test-recovery-loop.sh output/bin/rknetupdate-recovery-linux-amd64
sudo -n ./scripts/test-rootfs-migration-loop.sh
sudo -n ./scripts/test-bundle-migration-loop.sh
sudo -n ./scripts/test-updateimg-migration-loop.sh
```

loop 测试只创建临时 GPT 的 `rootfs` 与 `misc` 两个分区，验证 rootfs 写入/回读、smoke 不写 rootfs、BCB 字段保留及“目标已挂载必须拒绝”的失败路径，并在退出时卸载、分离 loop、删除临时目录。

`test-rootfs-migration-loop.sh` 验证 schema 2 的离线整盘尾部迁移：缩小 p6、创建临时 p7 暂存升级包、写入并读回 p6、删除 p7、恢复 p6 到 GPT 末尾并扩展 ext4。测试会在各持久阶段重启，破坏 p6 前 8 MiB 模拟 raw 写入中断，并验证从 p7 重试；同时覆盖错误哈希、p6 已挂载、暂存空间不足、p6 GPT 身份不符的 fail-closed 拒绝，以及 p6 起始 LBA/type GUID/unique GUID 保持和 BCB `systemFlag` 保留。

`test-updateimg-migration-loop.sh` 生成最小合法 RKFW/RKAF 包，验证完整包从 p6 复制到临时 p7、canonical hardlink 和 stock BCB handoff。它还精确模拟 GPT 已创建 p7、但 BCB 仍停在 `updateimg-shrunk` 的断点，确认下一次 recovery 可幂等恢复到 `stock-handoff`。

Rockchip 5.10/6.1 SDK 将 S39/S40 启动脚本和版本标记保存在各自的 Recovery overlay 中，并由统一的 post-rootfs hook 明确安装这些文件和 arm64 静态 helper；这样不会受部分 5.10 SDK 错误的 overlay allow-list 参数影响。5.10 使用扁平 `device/rockchip/common/overlays/recovery/`，6.1 使用其中的 `rknetupdate-rootfs/` 子目录。批量集成器会识别两种框架，给对应 SoC 的 Recovery defconfig 增加 `sgdisk`，并修复 vendor Buildroot `gptfdisk` 的空 `CXX` 变量问题。集成时源码会复制到每个 SDK 的 `external/rknetupdate/`，Buildroot hook 缺少产物时会在那里自动编译，不依赖 SDK 外部的绝对路径：

```sh
./scripts/integrate-recovery-sdks.sh --apply
./scripts/integrate-recovery-sdks.sh --check

cd /home/topeet/Linux/rk3588/rk3588-linux-6.1
./build.sh recovery
```

缺少 SDK-local 源码、构建失败或文件不是 AArch64 ELF 时，recovery 构建会直接失败，不会生成一个缺少升级程序但看似可用的镜像。若希望提前生成 SDK 内的三个 ARM64 产物，可执行 `make -C external/rknetupdate all-arm64`。
当前支持矩阵、各 SDK overlay 路径和限制见 [docs/recovery-sdk-integration-matrix.md](docs/recovery-sdk-integration-matrix.md)。A/B、extboot、SPI-NOR 分区布局不满足 `misc=p2/rootfs=p6/rootfs:grow`，批量集成不代表这些布局可以执行临时 p7 升级。

## 多镜像离线升级（bundle-v1）

`bundle-v1` 不是 `update.img`，也不再出现在网页第四项“系统 OTA”。它仅保留 `POST /api/v1/bundles` 的兼容 API：至少上传 `rootfs.img`，可同时提交 `boot.img`、`uboot.img`。正常系统对每项做文件名、容量、SHA-256 和 ext4 几何检查，原子生成 `/var/lib/rknetupdate/bundle-v1/bundle.manifest.json`；同一次已认证上传中服务会自动写入/复核 BCB，并请求重启 recovery。

recovery 会临时缩小 p6、创建 p7 `userdata`，把所有成员和清单复制到 p7 后按 `rootfs → boot → uboot` 写入、`fsync` 并逐项 SHA-256 回读。成功才删除 p7、恢复 GPT、扩展 rootfs、清空 BCB。`uboot` 始终最后写，降低前面任一项失败后的救砖风险。

Buildroot 新 ext4 镜像若含 `orphan_file`，Ubuntu 22.04 的 `e2fsck 1.46.5` 会报 `FEATURE_C12`。bundle 暂存不会把这个旧工具的“不支持 feature”误判为文件系统损坏；最终 `e2fsck -f -n` 在 SDK recovery 的 e2fsprogs 1.47.0 中执行，失败即 fail-closed。完整流程、限制和 API 示例见 [docs/offline-bundle-v1.md](docs/offline-bundle-v1.md)。

## 构建

要求 Go 1.22 或更新版本：

```sh
make fmt
make test
make vet
make build control recovery arm64 control-arm64 recovery-arm64
```

或运行：

```sh
./scripts/build.sh
```

产物位于 `output/bin/`。`rknetupdate-linux-arm64` 与固定路径的 `rknetupdate-control-linux-arm64` 可部署到常见 Buildroot、Ubuntu、Debian 根文件系统；`rknetupdate-recovery-linux-arm64` 只用于 recovery initramfs。

## 集成全部 Rockchip SDK 文件系统

如果 SDK 集合位于 `/home/topeet/Linux`，可将 SDK-local 源码、用户态程序和
启动服务批量接入所有标准 Ubuntu、Debian、Buildroot，以及
`*_extboot_6.1` 的 Ubuntu rootfs 入口：

```sh
./scripts/integrate-filesystem-sdks.sh --apply
./scripts/integrate-filesystem-sdks.sh --check
```

脚本将源码放入每个 SDK 的 `external/rknetupdate/`；SDK 原生
`post-hooks` 会在 Buildroot/Ubuntu/Debian 构建时按需编译并安装静态 ARM64
二进制、配置和现有 init 服务；已有客户配置会保留。普通 Buildroot 公共配置会启用
`sgdisk`，而 `e2fsck`/`resize2fs` 已由 Rockchip 的 `fs/e2fs.config` 提供。
13 套标准单槽 eMMC SDK 默认使用真实烧写配置并启用 rootfs/update.img
Recovery 升级；7 套 `*_extboot_6.1` 因不具备对应分区契约，继续使用安全
演练配置。两类配置的 token 都默认为 `topeet`，详情见
[`docs/filesystem-sdk-integration-matrix.md`](docs/filesystem-sdk-integration-matrix.md)。

## 安装与演练

```sh
install -m 0755 output/bin/rknetupdate-linux-arm64 /usr/sbin/rknetupdate
install -m 0755 output/bin/rknetupdate-control-linux-arm64 /usr/sbin/rknetupdate-control
install -d -m 0700 /var/lib/rknetupdate/uploads /var/lib/rknetupdate/rootfs-v2 /var/lib/rknetupdate/bundle-v1
install -m 0600 configs/rknetupdate.conf.example /etc/rknetupdate.conf
```

出厂 Token 为 `topeet`。每次打开或刷新网页都必须手动输入，前端不会把 Token 写入源码、浏览器存储或 URL。客户部署时可直接修改 `/etc/rknetupdate.conf`：

```ini
token=客户自定义值
```

也可以通过环境变量临时覆盖：

```sh
export RKNETUPDATE_TOKEN="$(head -c 32 /dev/urandom | base64)"
/usr/sbin/rknetupdate --config /etc/rknetupdate.conf
```

首次必须保持：

```ini
allow_flash=false
dry_run=true
test_mode=false
```

浏览器访问 `http://开发板IP:17888`，输入当前配置的 Token 并验证通过后，再选择目标和对应镜像并点击“上传并开始…”。演练会完成文件名、大小、格式、分区解析和 SHA-256，但不会打开分区写入。

上板前必须先识别实际 eMMC。不要假设 eMMC 一定是 `mmcblk0`，有 SD 卡或不同启动介质时也可能是 `mmcblk1`：

```sh
lsblk -o NAME,PATH,TYPE,SIZE,FSTYPE,LABEL,PARTLABEL,MOUNTPOINTS
readlink -f /dev/disk/by-partlabel/uboot
readlink -f /dev/disk/by-partlabel/boot
```

确认两个最终分区属于同一块 eMMC 后，再填写其父整盘路径。页面会同时显示“配置映射、实际分区、父磁盘、预期父盘”。Buildroot 若没有 `/dev/disk/by-partlabel/`，可把 `uboot_device`、`boot_device` 显式设置为上面核实过的直接分区路径，例如 `/dev/mmcblk0p3`、`/dev/mmcblk0p5`；这些显式路径不能是符号链接。

## 启用真实写入

确认串口和救砖链路可用后，修改配置：

```ini
expected_parent_device=/dev/mmcblk0
rootfs_enabled=true
rootfs_allow_write=true
rootfs_max_upload_mib=8192
bundle_enabled=true
bundle_allow_write=true
bundle_max_upload_mib=8448
```

上例中的 `/dev/mmcblk0` 只是示意，必须替换为当前板卡只读检查得到的 eMMC 整盘。`bundle_allow_write=true` 只放开离线 bundle recovery 工作流；U-Boot/Boot 的直接写入仍保持原有 `dry_run` 和 `allow_flash` 配置。点击上传后服务会自动核对实际分区、父磁盘、分区容量、镜像大小和 SHA-256 并启动升级。任务返回 `reboot_requested` 只表示 BCB 已准备和重启已请求；仍需等待 recovery 完成后验证系统启动链。

## systemd

```sh
install -m 0644 packaging/filesystem/systemd/rknetupdate.service /etc/systemd/system/rknetupdate.service
systemctl daemon-reload
systemctl enable --now rknetupdate
journalctl -u rknetupdate -f
```

## Buildroot BusyBox init

```sh
install -m 0755 packaging/filesystem/sysv/S90rknetupdate /etc/init.d/S90rknetupdate
/etc/init.d/S90rknetupdate start
```

Rockchip 默认 Buildroot 配置保持 `BR2_INIT_BUSYBOX=y` 和动态 eudev，脚本
只安装到现有 `/etc/init.d/S90rknetupdate`，不会启用 systemd 或替换 udev。
如果目标 Buildroot 未启用 `start-stop-daemon` applet，脚本会回退为直接后台
启动并记录 PID。

## HTTP API

除健康检查外，所有 API 都要求 `Authorization: Bearer <token>`；未配置覆盖值时使用 `Authorization: Bearer topeet`。

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/api/v1/health` | 进程健康检查 |
| `GET` | `/api/v1/device` | 设备、运行模式、分区能力 |
| `GET` | `/api/v1/status` | 当前或最近任务状态 |
| `POST` | `/api/v1/uploads?target=boot` | multipart `image` 流式上传；校验通过后立即启动升级 |
| `POST` | `/api/v1/uploads?target=updateimg` | 上传唯一文件 `update.img`；校验完整 RKFW/RKAF 包后自动进入系统 OTA |
| `POST` | `/api/v1/bundles` | 重复 multipart `image`：至少 rootfs，可含 boot/uboot；校验后自动进入离线升级 |
| `POST` | `/api/v1/reboot` | 传入已完成的 `job_id`；仅允许 U-Boot/Boot 真实写入且回读哈希一致后安全重启 |

`/api/v1/device` 的每个 target 返回 `device`（配置映射）、`resolved_device`（实际分区）和 `parent_device`（父整盘）；任务对象也返回相同字段。真实模式下三者会在上传前及写入前重新解析并验证。完整系统 OTA 需要 `updateimg_enabled=true` 和 `updateimg_allow_write=true`，通过 `target=updateimg` 上传；rootfs/bundle/`update.img` 的 BCB 准备、复核和 recovery 重启由上传请求自动完成。

U-Boot/Boot 直接写入不会自动重启。网页只在任务状态为 `succeeded`（或上次 clean reboot 未执行的可重试状态 `system_reboot_failed`）且 `sha256` 与 `readback_sha256` 一致时显示重启确认；选择“立即重启设备”后，服务再次按任务 ID 复核这些条件，先返回 `202 Accepted`，再执行 `sync` 和系统对应的 clean reboot 命令。重启命令失败不会抹掉已经验证的烧写结果，可以直接重试而不必再次写分区。

## 镜像格式检查边界

格式检查用于发现明显选错文件，不替代签名校验。服务识别 Android boot magic、FIT/DTB magic、`KRNL` 标记，并在 FIT 头部搜索内核或 U-Boot 线索。由于瑞芯微 SDK 的 `uboot.img` 和 `boot.img` 都可能使用 FIT 容器，无法确定时会记录“低/中置信度”并允许继续；只有内容明确指向另一个目标时才拒绝。

## 回滚与上板验证清单

1. 保存当前设备的 `uboot`、`boot` 分区镜像和完整 `update.img`。
2. 准备对应 SoC 的 Loader/Maskrom USB 烧录工具及可启动恢复镜像。
3. 串口记录升级前后 U-Boot banner、启动介质、slot、kernel command line。
4. 用 `lsblk` 和 `readlink -f` 确认 `/dev/disk/by-partlabel/uboot` 和 `boot` 指向预期 eMMC 分区，而不是 SD 卡或其他介质；确认页面父磁盘与 `expected_parent_device` 完全一致。
5. 先在同硬件备用板执行 dry-run，再写 `boot`，最后才评估 `uboot`。
6. 写入后先核对页面回读 SHA-256，再安排受控重启。
7. 验证网络、电源异常时的恢复流程。第一阶段不是断电安全事务更新；写入中断可能留下不可启动分区。

## 后续阶段

- `bundle-v1`：`rootfs.img`、`boot.img`、`uboot.img` 的离线 p7 暂存、BCB recovery、断点续跑与 loop 验证已接入；产品化仍应迁移到 A/B 并增加启动确认、持久状态回传和自动回滚。
- `update.img`：已作为网页“系统 OTA”的完整包入口；当前单槽流程要求参数布局与现有 p6 rootfs:grow 布局一致。recovery helper 用临时 p7 暂存并校验完整包，随后在同一次 recovery 启动中把 canonical BCB 交给 SDK `rkupdate`，不再通过中间 warm reboot 二次进入 recovery；`parameter` 恢复 p1..p6 GPT 后自动移除 p7，清 BCB 并重启正常系统。
- recovery 执行 `partprobe` 后会等待 udev 事件；若 `/dev/disk/by-partlabel/*` 正处于重建空窗，迁移 helper 只允许从 CRC 校验通过的同一 eMMC GPT 找回对应直接分区，并再次核对 sysfs 父盘、PARTNAME、分区号、起始 LBA 和容量。任一身份不一致仍然 fail-closed。
- 产品化还应增加 TLS、签名清单、防回滚版本、审计持久化、维护窗口和硬件 watchdog 协作。
