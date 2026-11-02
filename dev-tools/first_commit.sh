#!/bin/bash
# 首次入库：git init + 配置 + 钩子 + add + commit
# 用法：bash dev-tools/first_commit.sh   （在 SDK 根目录执行）
set -e
cd "$(dirname "$0")/.."

git init
git config user.name zhongxun
git config user.email zhongxun@rk3588
cp dev-tools/pre-commit .git/hooks/pre-commit
chmod +x .git/hooks/pre-commit

git add -A
git commit -q -F - <<'MSG'
init: 迅为 iTOP-RK3588 SDK (rk3588-linux-5.10) 初始入库

- 跟踪：内核 / U-Boot / device 配置 / buildroot 配方 / external 源码 / 脚本
- 忽略：编译产物、工具链(prebuilts)、烧写工具(tools)、私有固件(rkbin)、
  根文件系统基础包(tar.xz)、厂商 PDF(docs/cn|en)
- repo 多仓残骸已改名备份为 *.bak-vendor（见 dev-tools/flatten_nested_git.sh）
MSG

echo "=== COMMIT DONE ==="
git log --oneline | head -2
git count-objects -vH | grep size-pack
echo "TRACKED_FILES: $(git ls-files | wc -l)"
