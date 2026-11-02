#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 VS Code "降噪阅读配置"（settings.json）—— i.MX6ULL 时代配置的 RK3588 版
==========================================================================
用法（WSL 里，SDK 根目录执行）:
    python3 dev-tools/gen_read_settings.py

生成两份配置（VS Code 只认"工作区根目录"的 .vscode，所以两种打开方式都照顾到）:
    .vscode/settings.json         —— 打开整个 SDK 根目录时生效（推荐）
    kernel/.vscode/settings.json  —— 只打开 kernel/ 目录时生效

过滤思路（对应笔记《06_RK3588_SDK工程结构与上手指南》）:
    1. 内核 arch 目录只保留 arm64（RK3588 是 aarch64）
       想同时保留 32 位 ARM 参考：把 KEEP_ARCH 改成 {"arm64", "arm"}
    2. arch/arm64 的 dts / configs 只保留 rockchip（字符区间法，和老配置风格一致）
    3. 全 SDK 隐藏编译产物 + 大块二进制目录（与 .gitignore 同口径）
"""
import json
from pathlib import Path

SDK_ROOT = Path(__file__).resolve().parent.parent
KEEP_ARCH = {"arm64"}            # ★ 想参考 32 位就改成 {"arm64", "arm"}
KEEP_DTS_PREFIX = ("r",)         # dts/configs 名字以这些开头的保留（rockchip）

# ---- 文件噪音（编译中间产物），files.exclude 与 search.exclude 都用 ----
FILE_NOISE = {
    "**/*.o": True, "**/*.o.cmd": True, "**/*.ko": True, "**/*.cmd": True,
    "**/*.su": True, "**/*.mod": True, "**/*.mod.c": True,
    "**/*.a": True, "**/*.so": True, "**/*.so.*": True,
    "**/*.elf": True, "**/*.map": True, "**/*.img": True,
    "**/*.dtb": True, "**/*.bin": True,
    "**/.git": True,
}

# ---- 大块二进制 / 编译产物目录（只在 SDK 根工作区需要隐藏） ----
SDK_BLOB_DIRS = [
    "out", "rockdev", "output", "prebuilts", "tools", "rkbin",
    "buildroot/dl", "buildroot/archives", "buildroot/output",
    "docs/cn", "docs/en",
    "device/rockchip/common/tools", "device/rockchip/common/linux-kbuild",
    "device/rockchip/common/fonts", "device/rockchip/common/extra-parts",
    "external/rknn-toolkit2", "external/rknpu2", "external/libmali",
    "external/rkwifibt", "external/chromium",
]
SDK_BLOB_GLOBS = [
    "**/*.tar", "**/*.tar.*", "**/*.gz", "**/*.xz", "**/*.zip", "**/*.7z",
    "**/*.whl", "**/*.deb", "**/*.pdf",
]


def arch_excludes(prefix: str) -> dict:
    """隐藏 kernel/arch 里非 arm64 的架构。prefix: '' 或 'kernel/'"""
    out = {}
    arch_dir = SDK_ROOT / "kernel" / "arch"
    if arch_dir.is_dir():
        for d in sorted(arch_dir.iterdir()):
            if d.is_dir() and d.name not in KEEP_ARCH:
                out[f"{prefix}arch/{d.name}"] = True
    return out


def dts_config_excludes(prefix: str) -> dict:
    """arch/arm64 下 boot/dts 和 configs 只留 rockchip（r 开头）"""
    out = {}
    for sub in ("boot/dts", "configs"):
        out[f"{prefix}arch/arm64/{sub}/[a-q]*"] = True
        out[f"{prefix}arch/arm64/{sub}/[s-z]*"] = True
    return out


def build_kernel_settings() -> dict:
    """kernel/ 作为工作区根时的配置（纯内核降噪）"""
    hidden = {}
    hidden.update(FILE_NOISE)
    hidden.update(arch_excludes(""))
    hidden.update(dts_config_excludes(""))
    return {
        "files.exclude": hidden,
        "search.exclude": {**hidden, "Documentation": True},
    }


def build_sdk_settings() -> dict:
    """SDK 根作为工作区根时的配置（全工程降噪）"""
    hidden = {}
    hidden.update(FILE_NOISE)
    hidden.update(arch_excludes("kernel/"))
    hidden.update(dts_config_excludes("kernel/"))
    for d in SDK_BLOB_DIRS:
        hidden[d] = True
    for g in SDK_BLOB_GLOBS:
        hidden[g] = True
    return {
        "files.exclude": hidden,
        "search.exclude": {**hidden, "kernel/Documentation": True},
    }


def save(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=4, ensure_ascii=False) + "\n",
                    encoding="utf-8")
    n = len(data["files.exclude"])
    print(f"[生成] {path.relative_to(SDK_ROOT)}  ({n} 条过滤规则)")


def main() -> None:
    save(SDK_ROOT / ".vscode" / "settings.json", build_sdk_settings())
    save(SDK_ROOT / "kernel" / ".vscode" / "settings.json", build_kernel_settings())
    print("\n提示：VS Code 重新加载窗口后生效（Ctrl+Shift+P -> Reload Window）")


if __name__ == "__main__":
    main()
