#!/bin/bash

# TianShanOS 版本号自动递增脚本
# 用法:
#   ./scripts/bump_version.sh patch  # 0.2.0 → 0.2.1
#   ./scripts/bump_version.sh minor  # 0.2.0 → 0.3.0
#   ./scripts/bump_version.sh major  # 0.2.0 → 1.0.0
#   ./scripts/bump_version.sh auto   # 根据最近 commit message 自动判断

set -e

VERSION_FILE="version.txt"
BUMP_TYPE="${1:-auto}"

# 检查文件是否存在
if [[ ! -f "$VERSION_FILE" ]]; then
    echo "❌ Error: $VERSION_FILE not found!"
    exit 1
fi

# 读取当前版本
CURRENT_VERSION=$(cat "$VERSION_FILE" | tr -d '[:space:]')
echo "📦 当前版本: $CURRENT_VERSION"

# 解析版本号 (支持 MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD])
if [[ ! $CURRENT_VERSION =~ ^([0-9]+)\.([0-9]+)\.([0-9]+) ]]; then
    echo "❌ Error: Invalid version format in $VERSION_FILE (expected: MAJOR.MINOR.PATCH)"
    exit 1
fi

MAJOR="${BASH_REMATCH[1]}"
MINOR="${BASH_REMATCH[2]}"
PATCH="${BASH_REMATCH[3]}"

# 自动判断递增类型（从最近的 commit message）
if [[ "$BUMP_TYPE" == "auto" ]]; then
    # 获取最近一次 commit message
    LAST_COMMIT=$(git log -1 --pretty=%B 2>/dev/null || echo "")
    
    if [[ $LAST_COMMIT =~ BREAKING\ CHANGE ]]; then
        BUMP_TYPE="major"
        echo "🔥 检测到 BREAKING CHANGE → major"
    elif [[ $LAST_COMMIT =~ ^feat ]]; then
        BUMP_TYPE="minor"
        echo "✨ 检测到 feat → minor"
    elif [[ $LAST_COMMIT =~ ^fix ]]; then
        BUMP_TYPE="patch"
        echo "🐛 检测到 fix → patch"
    else
        BUMP_TYPE="patch"
        echo "📝 默认 → patch"
    fi
fi

# 计算新版本号
case "$BUMP_TYPE" in
    major)
        NEW_VERSION="$((MAJOR + 1)).0.0"
        ;;
    minor)
        NEW_VERSION="${MAJOR}.$((MINOR + 1)).0"
        ;;
    patch)
        NEW_VERSION="${MAJOR}.${MINOR}.$((PATCH + 1))"
        ;;
    *)
        echo "❌ Error: Invalid bump type '$BUMP_TYPE' (use: major, minor, patch, auto)"
        exit 1
        ;;
esac

echo "🚀 新版本: $NEW_VERSION"

# 写入新版本
echo "$NEW_VERSION" > "$VERSION_FILE"

# 如果在 git 仓库中，自动添加到暂存区
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git add "$VERSION_FILE"
    echo "✅ 已添加到 git 暂存区"
else
    echo "⚠️  不在 git 仓库中，跳过 git add"
fi

echo "✅ 版本号已更新: $CURRENT_VERSION → $NEW_VERSION"
