# TianshanOS Frontend Modification

> 精简版：配色规范 + 致命错误 + 编译方法 + 修改历史

---

## 一、修改原则

1. **绝对不改变任何元素的位置和布局**
2. **绝对不删除任何功能按钮或函数**
3. **每次修改代码前必须说「喵喵喵～」**
4. **每次修改后必须更新本文档**

---

## 二、UI 配色规范

| 类型 | 背景色 | 文字色 | 边框色 | 用途 |
|-----|--------|--------|--------|------|
| `btn-service-style` | `#f0f8ff` | `#007bff` | `#d0e8ff` | OTA、登出、组件管理 |
| `btn-danger` | `#ffebee` | `#c62828` | `#ef9a9a` | 删除、已关闭 |
| `btn-success` | `#e8f5e9` | `#2e7d32` | `#a5d6a7` | 运行中、添加 |
| `btn-warning` | `#fff8e1` | `#f57c00` | `#ffd54f` | 测试、检测中 |
| 灰色小按钮 | - | `#666` | - | 服务、USB、同步等 |
| 背景色 | `#f5f6fa` | - | - | 曲线点、预览图 |

---

## 三、致命错误经历

### 错误 1: 删除功能函数
修改 emoji 时意外删除 `onclick` 或 JS 函数，导致按钮失效。
**教训**：只动 HTML 文本，不删逻辑代码。

### 错误 2: RemixIcon 未定义
使用未在 `remixicon.css` 定义的图标类，显示为方框。
**教训**：先定义 `.ri-xxx:before { content: "\xxxx"; }` 再使用。

### 错误 3: www.bin 未更新
WebUI 修改后直接 `idf.py build`，www.bin 不会重新打包。
**教训**：必须 `rm build/www.bin` 后再编译。

### 错误 4: RemixIcon Unicode 错误
猜测 Unicode 值导致图标显示错误。
**教训**：从 `/Users/massif/RemixIcon/fonts/remixicon.css` 查找正确值。

### 错误 5: CSS 类名始终存在导致样式残留
```javascript
// 错误：manual 类始终存在，切换模式后边框残留
<button class="fan-mode-tab manual ${mode === 'manual' ? 'active' : ''}">

// 正确：类名只在激活时添加
<button class="fan-mode-tab ${mode === 'manual' ? 'active manual' : ''}">
```
**教训**：CSS 类名与 JS 动态类名必须配合设计。

---

## 四、编译方法

```bash
# 加载 ESP-IDF 环境
source ~/esp/v5.5.2/esp-idf/export.sh

# 编译（WebUI 修改后必须删除旧 www.bin）
rm -f build/www.bin && idf.py build

# 烧录（必须在项目目录下执行）
cd ~/TianshanOS
idf.py -p /dev/cu.usbmodem113301 flash
```

---

## 五、修改历史

| 版本 | 日期 | 关键修改 |
|-----|------|---------|
| v1-v2 | 2026-01-31 | RemixIcon 基础、布局恢复 |
| v3-v4 | 2026-01-31 | emoji 清理、图标系统化 |
| v5-v6 | 2026-01-31 | 页面清理、细节优化 |
| v7-v8 | 2026-01-31 | 登录界面、用户图标统一 |
| v9-v10 | 2026-01-31~02-05 | 设备面板、风扇控制、内存页面优化 |
| v11 | 2026-02-06 | 网络页面优化 |
| v12 | 2026-02-06 | 文件页面优化（emoji→RemixIcon、样式修复） |
| v13 | 2026-02-06 | 电源保护开关（toggle→图标） |
| v14 | 2026-02-06 | 文件页面存储按钮优化 |
| v15 | 2026-02-06 | 终端页面日志按钮优化 |
| v16 | 2026-02-06 | Toast 消息配色统一 |
| v17 | 2026-02-06 | 设备面板状态图标、OTA 页面按钮 |
| v18 | 2026-02-06 | 风扇卡片白色背景、模式按钮边框修复 |
| v19 | 2026-02-06 | 风扇模式emoji删除、曲线按钮蓝色样式、LED设备图标删除 |
| v20 | 2026-02-06 | LED卡片白色背景、删除分割线、开关改为toggle图标、保存改为蓝色remixicon |
| v21 | 2026-02-07 | LED卡片整体白色背景统一、效果按钮(solid/breathing/sparkle)改为蓝色btn-service-style、风扇曲线模式删除"当前xx%"小字 |
| v22 | 2026-02-07 | 效果按钮hover样式与btn-service-style一致、LED开关改为灯泡图标(ri-lightbulb-fill/line) |
