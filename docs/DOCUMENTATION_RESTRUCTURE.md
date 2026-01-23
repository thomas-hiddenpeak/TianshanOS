# TianShanOS 文档重构方案

本文档说明文档整理计划，优化文档结构，提升可读性和可维护性。

---

## 📋 当前文档清单（20 个文档，~400KB）

### 核心文档（保留）
1. ✅ **README.md** - 项目概览
2. ✅ **QUICK_START.md** (3.7K) - 快速入门
3. ✅ **COMMANDS.md** (57K) - CLI 命令参考
4. ✅ **API_REFERENCE.md** (13K) - API 接口参考
5. ✅ **SECURITY_IMPLEMENTATION.md** (11K) - 安全功能实现
6. ✅ **LED_ARCHITECTURE.md** (15K) - LED 系统架构
7. ✅ **TEST_PLAN.md** (15K) - 测试计划
8. ✅ **TROUBLESHOOTING.md** (51K) - 故障排除
9. ✅ **DEVELOPMENT_PROGRESS.md** (49K) - 开发进度

### 待合并文档
- **架构设计类**：
  - `ARCHITECTURE_DESIGN.md` (43K)
  - `API_DESIGN.md` (7.8K)
  - `CONFIG_SYSTEM_DESIGN.md` (29K)
  - `REFACTORING_ANALYSIS.md` (27K)

- **配置指南类**：
  - `BOARD_CONFIGURATION.md` (7K)
  - `GPIO_MAPPING.md` (6.9K)
  - `COMMAND_SPECIFICATION.md` (14K)

- **迁移记录类**：
  - `PHASE15_WEBSOCKET_CPU.md` (18K)
  - `WEBSOCKET_MIGRATION.md` (7.6K)
  - `VERSION_MANAGEMENT.md` (3.2K)
  - `WEBUI_CLEANUP_PROPOSAL.md` (7.5K)
  - `PROJECT_DESCRIPTION.md` (22K)

---

## 🎯 整理方案

### 方案 A：合并为核心文档（推荐）

#### 1. 创建 `ARCHITECTURE.md`（合并架构文档）

**内容来源**：
- `ARCHITECTURE_DESIGN.md` - 主体架构
- `API_DESIGN.md` - API 设计章节
- `CONFIG_SYSTEM_DESIGN.md` - 配置系统章节
- `REFACTORING_ANALYSIS.md` - 历史重构记录（简化）

**章节结构**：
```
# TianShanOS 系统架构

## 1. 总体架构
## 2. 核心组件
## 3. API 设计
## 4. 配置系统
## 5. 服务管理
## 6. 事件总线
## 7. 扩展性设计
## 附录: 历史重构记录
```

#### 2. 创建 `CONFIGURATION.md`（合并配置文档）

**内容来源**：
- `BOARD_CONFIGURATION.md` - 板级配置
- `GPIO_MAPPING.md` - 引脚映射
- `COMMAND_SPECIFICATION.md` - 命令规范（部分）

**章节结构**：
```
# TianShanOS 配置指南

## 1. 配置层次
## 2. 板级配置 (board.json)
## 3. GPIO 引脚映射 (pins.json)
## 4. 服务配置 (services.json)
## 5. 设备配置 (devices.json)
## 6. 运行时配置 (NVS)
## 7. 配置优先级
```

#### 3. 创建 `archive/` 目录（归档旧文档）

**归档文件**：
- `PHASE15_WEBSOCKET_CPU.md` → `archive/2026-01-phase15-websocket.md`
- `WEBSOCKET_MIGRATION.md` → `archive/2026-01-websocket-migration.md`
- `WEBUI_CLEANUP_PROPOSAL.md` → `archive/2026-01-webui-cleanup.md`
- `PROJECT_DESCRIPTION.md` → `archive/project-description-v1.md`
- `VERSION_MANAGEMENT.md` → 合并到 `DEVELOPMENT_PROGRESS.md`
- `REFACTORING_ANALYSIS.md` → `archive/refactoring-history.md`（保留精华部分到 ARCHITECTURE.md）

#### 4. 优化 `COMMANDS.md`

**操作**：
- 合并 `COMMAND_SPECIFICATION.md` 中的命令设计原则
- 优化章节结构，按功能模块分类
- 添加命令快速索引表

---

### 方案 B：最小化调整（备选）

保留所有现有文档，仅添加：
- `DOCUMENTATION_INDEX.md` - 文档导航索引
- `archive/` 目录 - 移动临时性文档

---

## 📊 对比分析

| 方案 | 文档数量 | 总大小 | 优点 | 缺点 |
|------|---------|--------|------|------|
| **方案 A** | 12 个核心 + 存档 | ~350KB | 结构清晰、易于查找 | 需要重新整理内容 |
| **方案 B** | 20+ 个 | ~400KB | 工作量小 | 文档分散、重复内容多 |

---

## 🚀 实施计划

### Phase 1：创建合并文档（推荐方案 A）

1. **创建 `ARCHITECTURE.md`**
   - 提取各文档核心内容
   - 统一术语和格式
   - 添加架构图和流程图链接

2. **创建 `CONFIGURATION.md`**
   - 整合配置相关内容
   - 添加配置示例
   - 建立配置参数索引

3. **优化 `COMMANDS.md`**
   - 添加快速索引
   - 优化分类结构
   - 补充使用示例

### Phase 2：归档历史文档

1. 创建 `docs/archive/` 目录
2. 移动临时性和历史记录文档
3. 在归档文件头部添加说明和日期

### Phase 3：清理和优化

1. 删除原始文档
2. 更新 README.md 中的文档链接
3. 创建 `DOCUMENTATION_INDEX.md` 文档导航

---

## 📚 最终文档结构（方案 A）

```
docs/
├── README.md                       # 项目概览（根目录）
├── QUICK_START.md                  # 快速入门
├── ARCHITECTURE.md                 # 🆕 系统架构（合并）
├── CONFIGURATION.md                # 🆕 配置指南（合并）
├── COMMANDS.md                     # CLI 命令参考（优化）
├── API_REFERENCE.md                # API 接口参考
├── SECURITY_IMPLEMENTATION.md      # 安全功能实现
├── LED_ARCHITECTURE.md             # LED 系统架构
├── DEVELOPMENT_PROGRESS.md         # 开发进度
├── TEST_PLAN.md                    # 测试计划
├── TROUBLESHOOTING.md              # 故障排除
├── DOCUMENTATION_INDEX.md          # 🆕 文档导航索引
└── archive/                        # 🆕 历史文档归档
    ├── 2026-01-phase15-websocket.md
    ├── 2026-01-websocket-migration.md
    ├── 2026-01-webui-cleanup.md
    ├── project-description-v1.md
    └── refactoring-history.md
```

**文档数量**：12 个核心文档（从 20 个减少到 12 个）
**总大小**：约 350KB（减少 50KB，12.5%）

---

## ✅ 预期收益

1. **可维护性提升**
   - 文档集中，避免重复
   - 结构清晰，易于更新

2. **用户体验改善**
   - 快速找到所需信息
   - 减少文档切换次数

3. **开发效率提高**
   - 新成员上手更快
   - 文档维护成本降低

---

## 🔄 执行状态

- [ ] Phase 1: 创建 ARCHITECTURE.md
- [ ] Phase 1: 创建 CONFIGURATION.md
- [ ] Phase 1: 优化 COMMANDS.md
- [ ] Phase 2: 归档历史文档
- [ ] Phase 3: 清理和优化
- [ ] Phase 3: 更新 README 链接
- [ ] Phase 3: 创建文档导航索引

---

## 📝 备注

- 所有合并操作保留原文档的 Git 历史
- 归档文档添加明确的弃用说明
- 核心概念和术语保持一致性
- 重要的技术决策和设计理念保留
