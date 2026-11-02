# RKNetUpdate 离线多镜像升级包（bundle-v1）

`bundle-v1` 是 RKNetUpdate 保留的兼容多镜像升级 API，不依赖 Rockchip 的
`update.img`。浏览器或 API 分别上传固定文件名的 `rootfs.img`、`boot.img`
和可选的 `uboot.img`；服务为每一项建立 SHA-256 和严格清单，然后只通过
私有 recovery 离线写入。

## 支持范围

| 文件 | 是否可选 | 写入目标 | 顺序 |
| --- | --- | --- | --- |
| `rootfs.img` | 必须 | `PARTLABEL=rootfs`（p6） | 1 |
| `boot.img` | 可选 | `PARTLABEL=boot` | 2 |
| `uboot.img` | 可选 | `PARTLABEL=uboot` | 3，最后 |

不支持作为 bundle 成员的条目：`bootloader`/`MiniLoaderAll.bin`、`parameter`、
`misc`、`recovery`、`userdata`、`backup`。它们不是可按 `PARTLABEL` 安全
写入的普通分区，必须采用单独设计的恢复或工厂烧录流程。

`rootfs.img` 必须是 ext4。`boot.img` 与 `uboot.img` 可为当前 SDK 的 FIT、
Android boot 或其他受现有格式检查允许的原始分区镜像。

## 为什么不直接在运行中写 rootfs

正常系统只把 bundle 暂存到 `/var/lib/rknetupdate/bundle-v1`。一次已认证上传
会在服务端自动写入并回读 Rockchip misc BCB、再次复核 BCB，再执行正常关机
并进入定制 recovery。recovery 的步骤是：

```text
normal Linux
  └─ 暂存文件 + SHA-256 + bundle.manifest.json
      └─ BCB: manifest hash + rootfs GPT/ext4 identity
          └─ recovery
              ├─ 缩小 p6，创建临时 p7/userdata
              ├─ 将全部 bundle 文件复制、fsync 到 p7
              ├─ rootfs 写入并完整 SHA-256 回读
              ├─ boot 写入并完整 SHA-256 回读
              ├─ uboot 写入并完整 SHA-256 回读（最后）
              ├─ 删除 p7，恢复 GPT/p6 容量，扩展 ext4
              └─ 清理 BCB，重启正常系统
```

如果在 rootfs raw 写入中断，下一次进入 recovery 会从 p7 的已验证副本继续。
任何哈希、GPT、挂载状态、容量或 BCB 检查失败都会保留 BCB 并停在 recovery，
不会交给 stock recovery 或自动重启。

这不是 A/B 原子升级。掉电发生在 U-Boot 写入或 GPT 修改等窗口，仍可能需要
串口、Loader/Maskrom 救援。

## 启用前的必要条件

1. recovery 镜像必须已经包含本项目最新的 `rknetupdate-recovery` helper 和
   S39 脚本；更新服务本身不能替换 recovery。
2. 实际布局必须是 RK3588 的 p6=`rootfs` 且 p6 默认延伸到 GPT 最后可用 LBA。
3. `misc`、`rootfs`、`boot`、`uboot` 都必须属于 `/dev/mmcblk0`；先用
   `lsblk` 与 `readlink -f /dev/disk/by-partlabel/...` 核对。
4. 准备稳定供电、串口日志、原始分区备份和 Maskrom/Loader 救砖工具。

生产配置：

```ini
expected_parent_device=/dev/mmcblk0
bundle_enabled=true
bundle_allow_write=true
bundle_max_upload_mib=8448
bundle_stage_dir=/var/lib/rknetupdate/bundle-v1
```

`bundle_stage_dir` 和父盘在生产模式下固定为上述路径；不能重定向到任意
挂载点。只想演练时保持 `bundle_allow_write=false`。

## Web 与 API

网页第四项“系统 OTA”现在只接受完整的 Rockchip `update.img`，不再把
`rootfs.img` 伪装成系统 OTA。多镜像 bundle（`rootfs.img` 加可选 `boot.img`、
`uboot.img`）保留为兼容 API，供已有自动化工具使用；上传成功后应核对每个
成员的文件名、大小、SHA-256、最终分区和父盘。

API 是重复 `image` 字段的流式 multipart：

```sh
curl --fail-with-body \
  -H "Authorization: Bearer $RKNETUPDATE_TOKEN" \
  -F image=@rootfs.img \
  -F image=@boot.img \
  -F image=@uboot.img \
  http://BOARD_IP:17888/api/v1/bundles
```

响应的任务 `sha256` 是整个 `bundle.manifest.json` 的 SHA-256，不是某一
镜像的 hash。上传响应即为升级已启动：真实模式自动完成两阶段 BCB 写入/复核
并请求进入 recovery；演练模式只做检查，不会写 BCB、GPT 或任何分区。

## 新 ext4 feature 的兼容性

Ubuntu 22.04 的 `e2fsck 1.46.5` 不认识 Buildroot 新镜像中的 `orphan_file`
（常显示为 `FEATURE_C12`）。bundle 暂存阶段因此只做 ext4 superblock/几何和
SHA-256 检查；完整的 `e2fsck -f -n` 被放到 recovery 执行。当前 SDK recovery
内的 e2fsprogs 为 1.47.0，支持该 feature。若 recovery 的 e2fsck 仍失败，状态
机会 fail-closed，绝不会写 rootfs。

## 验证

```sh
go test ./...
go test -race ./...
go vet ./...
sudo -n ./scripts/test-bundle-migration-loop.sh
```

loop 测试使用临时 GPT loop 磁盘，验证 p7 暂存、rootfs 写入中断后恢复、
`boot`/`uboot` 回读、GPT/p6 标识恢复和 BCB `systemFlag` 保留；它不接触实际
开发板。
