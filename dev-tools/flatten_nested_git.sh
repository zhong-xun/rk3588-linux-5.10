#!/bin/bash
# 扁平化 repo 多仓残骸：把"真实的"内嵌 .git 改名备份，让外层 git 能跟踪到文件
# 背景：迅为 SDK 用 repo 工具管理，.repo 被删后留下一堆 .git 残骸——
#       多数是指向 .repo 的坏符号链接（git 不理会），少数是真实目录（会被当 submodule）
# 用法：bash dev-tools/flatten_nested_git.sh   （在 SDK 根目录执行）
# 备份：真实内嵌 .git 统一改名为 .git.bak-vendor（需要恢复 vendor 历史时改回去）
set -e
cd "$(dirname "$0")/.."

pkill -f '[g]it commit' 2>/dev/null || true
pkill -f '[g]it add' 2>/dev/null || true
sleep 2
rm -rf .git

find . -maxdepth 8 \( -type d -o -type f \) -name '.git' \
    -not -path './buildroot/dl/*' \
    -not -path './prebuilts/*' \
    -not -path './tools/*' \
    -not -path './rkbin/*' \
    > /tmp/nested_git.txt

while IFS= read -r g; do
    mv "$g" "$g.bak-vendor"
done < /tmp/nested_git.txt

echo "RENAME_COUNT: $(wc -l < /tmp/nested_git.txt)"
cat /tmp/nested_git.txt
