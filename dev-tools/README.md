# dev-tools — SDK 工具脚本

> 自己加的开发辅助脚本（不是迅为官方的），随 git 仓库一起跟踪。

| 脚本 | 用途 | 用法 |
|---|---|---|
| `gen_read_settings.py` | 生成 VS Code 降噪阅读配置（`.vscode/settings.json` + `kernel/.vscode/settings.json`） | SDK 根目录执行 `python3 dev-tools/gen_read_settings.py`，改完 SDK 结构后重跑 |
| `pre-commit` | git 钩子：阻止提交超过 50MB 的大文件 | 已安装到 `.git/hooks/`；换仓库时 `cp dev-tools/pre-commit .git/hooks/ && chmod +x .git/hooks/pre-commit` |

## 阅读配置的过滤口径

1. 内核 `arch/` 只保留 **arm64**（RK3588 是 aarch64）——想参考 32 位就把脚本里 `KEEP_ARCH` 改成 `{"arm64", "arm"}`
2. `arch/arm64` 的 `boot/dts`、`configs` 只保留 **rockchip**（脚本里 `KEEP_DTS_PREFIX` 可调）
3. 全 SDK 隐藏编译产物 + 大块二进制目录（与 `.gitignore` 同口径）

## git 仓库口径

- **跟踪**：内核 / U-Boot / device 配置 / buildroot 配方 / external 源码 / 脚本（可读可改的源码）
- **忽略**：编译产物（out/rockdev/*.o/*.img…）、工具链（prebuilts/）、烧写工具（tools/）、私有固件（rkbin/）、根文件系统基础包（*.tar.xz）、厂商 PDF（docs/cn、docs/en）、external 纯二进制 SDK
- 想重新跟踪某个被忽略的目录：删 `.gitignore` 里对应行，再 `git add`
