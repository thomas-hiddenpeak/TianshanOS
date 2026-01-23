# TianShanOS 版本管理说明

## 版本号格式

遵循 [语义化版本](https://semver.org/lang/zh-CN/) 规范：`MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]`

- **MAJOR**: 重大变更（破坏性 API 改动）
- **MINOR**: 新功能（向后兼容）
- **PATCH**: Bug 修复（向后兼容）

## 自动递增

### 使用脚本

```bash
# 手动指定递增类型
./scripts/bump_version.sh patch  # 0.2.0 → 0.2.1
./scripts/bump_version.sh minor  # 0.2.0 → 0.3.0
./scripts/bump_version.sh major  # 0.2.0 → 1.0.0

# 自动判断（根据最近 commit message）
./scripts/bump_version.sh auto
```

### 自动判断规则

脚本会分析最近一次 commit message 的前缀：

| Commit 前缀 | 递增类型 | 示例 |
|------------|----------|------|
| `BREAKING CHANGE` | major | 1.0.0 |
| `feat:` | minor | 0.3.0 |
| `fix:` | patch | 0.2.1 |
| 其他 | patch (默认) | 0.2.1 |

### 集成到开发流程

**推荐工作流 (feat/fix commit):**

```bash
# 1. 开发功能并提交
git add .
git commit -m "feat(led): 新增彩虹特效"

# 2. 自动递增版本号（会自动 git add version.txt）
./scripts/bump_version.sh auto

# 3. 修正上一次 commit（将版本号变更合并到同一个 commit）
git commit --amend --no-edit

# 4. 推送到远程
git push
```

**方式 2: 独立版本 commit**

```bash
# 1. 开发功能并提交
git commit -m "feat: 新增 WebSocket 实时推送"

# 2. 递增版本号并独立提交
./scripts/bump_version.sh auto
git commit -m "chore: bump version to 0.3.0"
```

## 版本显示

### 固件侧

- **编译时定义**: `CMakeLists.txt` 自动读取 `version.txt` 并生成：
  - `TIANSHAN_VERSION_FULL` (包含 git hash)
  - `TIANSHAN_VERSION_MAJOR/MINOR/PATCH`

- **运行时 API**: 
  ```c
  ts_api_call("ota.version", NULL, &result);
  // result.data → {"version": "0.2.0+edd873fd.01230753"}
  ```

### WebUI 侧

- **Footer 显示**: 页面加载时调用 `ota.version` API 动态更新
- **OTA 页面**: 显示当前/目标版本

## Git 集成

### Pre-commit Hook (可选)

如需每次 commit 自动递增版本号，可创建 `.git/hooks/pre-commit`:

```bash
#!/bin/bash
# 自动递增版本号（根据 commit message）
./scripts/bump_version.sh auto
```

**注意**: 使用 `--amend` 避免产生额外的版本 commit。

### CI/CD 集成 (可选)

```yaml
# .github/workflows/release.yml
- name: Bump version
  run: |
    ./scripts/bump_version.sh auto
    git config user.name "github-actions"
    git config user.email "actions@github.com"
    git commit -am "chore: auto bump version"
    git push
```

## 版本历史追踪

查看版本变更历史：

```bash
# 查看 version.txt 的所有变更
git log --follow --oneline version.txt

# 查看特定版本的 commit
git log --grep="bump version to 0.2.0"
```

## FAQ

**Q: 如何回退版本号？**  
A: 直接编辑 `version.txt` 或使用脚本降低版本号（不推荐）。

**Q: Build metadata (git hash) 如何生成？**  
A: `CMakeLists.txt` 在编译时自动追加当前 git commit hash。

**Q: WebUI 版本号不更新怎么办？**  
A: 检查 `ota.version` API 是否返回正确数据，清除浏览器缓存后重新加载。
