# TianShanOS 内存使用现状报告

**生成时间**: 2026-01-24  
**版本**: v0.3.0  
**目标芯片**: ESP32-S3

## 执行摘要

经过多轮内存碎片优化，DRAM 碎片率从 **~60% 降至 42.1%**，最大连续块从 **~40KB 提升至 68KB**，满足 DMA 和大缓冲区分配需求。当前内存状态健康。

### 最新优化结果（2026-01-24）

| 指标 | 优化前 | 优化后 | 改善 |
|------|--------|--------|------|
| DRAM 碎片率 | ~60% | 42.1% | ↓18% |
| DRAM 最大块 | ~40KB | 68KB | ↑70% |
| DRAM 分配块数 | ~1000 | 842 | ↓16% |
| PSRAM 碎片率 | ~5% | 2.7% | ↓2.3% |

## 当前内存分配

### 运行时内存状态（memory 命令输出）

```
类型      最大块     碎片率   分配块   空闲块   历史最低
DRAM      68.0 KB   42.1%    842      7       114.7 KB
PSRAM     5.38 MB   2.7%     684      3       5.18 MB

DRAM 总计: 302.0 KB | 已用: 184.4 KB | 空闲: 117.6 KB (61% 已用)
```

### 编译时内存使用（Build Size Analysis）

```
┌─────────────────────┬──────────────┬──────────┬────────────────┬───────────────┓
│ Memory Type         │ Used [bytes] │ Used [%] │ Remain [bytes] │ Total [bytes] │
├─────────────────────┼──────────────┼──────────┼────────────────┼───────────────┤
│ DIRAM (DRAM)        │      161,147 │   47.15  │        180,613 │       341,760 │
│   .text (code)      │      101,723 │   29.76  │                │               │
│   .bss (未初始化)   │       35,488 │   10.38  │                │               │
│   .data (已初始化)  │       23,936 │    7.00  │                │               │
├─────────────────────┼──────────────┼──────────┼────────────────┼───────────────┤
│ IRAM (快速访问)     │       16,384 │  100.0   │              0 │        16,384 │
│   .text (中断代码)  │       15,356 │   93.73  │                │               │
│   .vectors          │        1,028 │    6.27  │                │               │
├─────────────────────┼──────────────┼──────────┼────────────────┼───────────────┤
│ Flash Code (.text)  │    1,350,158 │          │                │               │
│ Flash Data (.rodata)│      537,560 │          │                │               │
└─────────────────────┴──────────────┴──────────┴────────────────┴───────────────┘

总镜像大小: 2,030,049 bytes (~1.94 MB)
```

### 关键指标

- **DRAM 编译时使用**: 161,147 / 341,760 bytes = **47.15%** ✅
- **DRAM 运行时空闲**: 117.6 KB ✅ 健康水平
- **DRAM 最大连续块**: 68 KB ✅ 满足 DMA 需求
- **DRAM 碎片率**: 42.1% ✅ 可接受（目标 <50%）
- **IRAM 使用**: 16,384 / 16,384 bytes = **100%** ⚠️ 已满（正常）
- **PSRAM 碎片率**: 2.7% ✅ 优秀

## 问题诊断与解决

### 问题1：原始报告 DRAM 86% 使用率 ✅ 已解决

**原因**：
1. 构建错误导致旧的二进制文件未更新
2. 多个组件引用不存在的 `ts_https` 相关代码
3. CMakeLists.txt 中包含已删除文件的引用

**解决方案**：
- 移除所有 HTTPS 相关遗留代码（commit 674e19e）
- 清理 CMakeLists.txt 构建配置
- 重新编译生成新的二进制文件

**结果**：DRAM 使用率降至 52.35%，进入健康范围

### 问题2：IRAM 100% 使用率 ⚠️ 需监控

**现状**：
- IRAM 仅 16 KB，存放中断处理和时间敏感代码
- 100% 使用率在 ESP32 项目中是正常现象

**影响**：
- 无法添加更多 IRAM 代码（如高速中断处理器）
- 不影响普通功能开发（Flash 执行即可）

**建议**：
- 除非需要极低延迟中断，无需优化
- 如需扩展 IRAM，需移除现有 IRAM 函数（风险高）

## 运行时内存分析

### 工具使用

新增 CLI 命令 `system --memory-detail` 用于运行时分析：

```bash
# 基础内存信息
system --info

# 详细堆内存分析（新增功能）
system --memory-detail

# 预期输出：
=== Detailed Heap Memory Analysis ===

[DRAM - Internal RAM]
  Capabilities: MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
  Heap Summary:
    Total Size: 301312 bytes
    Free: 162861 bytes (54%)
    Allocated: 138451 bytes (46%)
    Largest Free Block: 98304 bytes
    Minimum Ever Free: 138000 bytes

[PSRAM - External RAM]
  Capabilities: MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
  Heap Summary:
    Total Size: 8388608 bytes (8 MB)
    Free: 5767168 bytes (69%)
    Allocated: 2621440 bytes (31%)
    Largest Free Block: 5242880 bytes (5 MB)

[DMA - DMA Capable RAM]
  Capabilities: MALLOC_CAP_DMA
  Total Size: ~35000 bytes
  Free: ~32000 bytes

💡 Optimization Tips:
  ✅ DRAM usage healthy (52%)
  ✅ PSRAM available (69% free)
  ⚠️ IRAM full - cannot add more interrupt handlers
```

### 内存分配建议

根据当前状态，推荐以下分配策略：

1. **继续使用 DRAM 的场景**：
   - 小于 4 KB 的缓冲区
   - 频繁访问的数据结构
   - DMA 需要的缓冲区

2. **应迁移到 PSRAM 的场景**：
   - WebSocket 缓冲区（≥32 KB）
   - HTTP 请求/响应缓冲区（≥16 KB）
   - 图像/帧缓冲区（≥12 KB）
   - 日志环形缓冲区（如超过 8 KB）

3. **禁止使用 DRAM 的场景**：
   - 大于 32 KB 的静态数组
   - 长期缓存的数据（除非 DMA 必需）

## 优化路线图

### 阶段1：监控与基线建立 ✅ 完成

- [x] 实现 `system --memory-detail` 命令
- [x] 修复构建错误并重新编译
- [x] 确认 DRAM 使用率健康（52.35%）
- [x] 文档化当前内存布局

### 阶段2：预防性优化（可选）

**目标**：进一步降至 45-50%，为未来功能预留空间

**优先级低**：
- [ ] 识别大于 16 KB 的 DRAM 缓冲区
- [ ] 迁移 WebSocket 缓冲区到 PSRAM（如存在）
- [ ] 迁移 HTTP 缓冲区到 PSRAM
- [ ] 优化日志系统缓冲区大小

**预期收益**：
- DRAM 可降至 45% (~150 KB 使用)
- 额外释放 20-30 KB 空间

### 阶段3：长期监控

**工具集**：
1. **构建时检查**：
   ```bash
   # 监控每次构建的内存变化
   idf.py size > memory_report.txt
   diff memory_report_old.txt memory_report.txt
   ```

2. **运行时监控**：
   ```c
   // 在关键操作前后记录内存
   heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
   ```

3. **CI/CD 集成**（推荐）：
   - 构建失败条件：DRAM > 70%
   - 警告条件：DRAM > 60%
   - 自动生成内存变化报告

## 内存优化最佳实践

### 代码编写规范

```c
// ✅ 推荐：大缓冲区使用 PSRAM
char *large_buffer = heap_caps_malloc(32768, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
if (!large_buffer) {
    // Fallback 到 DRAM（仅在 PSRAM 不可用时）
    large_buffer = malloc(32768);
    ESP_LOGW(TAG, "Using DRAM for large buffer (PSRAM unavailable)");
}

// ✅ 推荐：小缓冲区直接使用 malloc (默认 DRAM)
char small_buf[256];  // 栈上分配
char *heap_buf = malloc(1024);  // 堆上分配 (DRAM)

// ❌ 禁止：大型静态数组
static char bad_buffer[65536];  // 占用 64 KB DRAM！

// ✅ 推荐：改为动态分配
static char *good_buffer = NULL;
void init() {
    good_buffer = heap_caps_malloc(65536, MALLOC_CAP_SPIRAM);
}
```

### menuconfig 配置建议

```
Component config → ESP PSRAM →
  [*] Support for external, SPI-connected RAM
  [*] Ignore PSRAM when not found
  Allocate memory to PSRAM (Prefer internal)  # 避免默认使用 PSRAM 影响性能
```

## 参考资料

- ESP-IDF 内存类型说明：https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/memory-types.html
- heap_caps API：`components/heap/include/esp_heap_caps.h`
- TianShanOS 内存优化指南：`docs/MEMORY_OPTIMIZATION.md`

## 附录：内存类型对比表

| 类型   | 大小       | 速度 | 用途                       | 限制                   |
|--------|-----------|------|---------------------------|------------------------|
| IRAM   | 16 KB     | 最快 | 中断处理、Cache 关闭时代码  | 容量小，100% 已用      |
| DRAM   | 341 KB    | 快   | 通用数据、DMA 缓冲区        | 容量有限（当前 52%）   |
| PSRAM  | 8 MB      | 中等 | 大缓冲区、图像数据          | 访问延迟高于 DRAM      |
| Flash  | 16 MB     | 慢   | 代码、只读数据、文件系统     | 执行需通过 Cache       |
| RTC    | 16 KB     | 慢   | Deep Sleep 保持数据         | 不支持动态分配         |

---

**生成者**: GitHub Copilot (Claude Sonnet 4.5)  
**工具链**: ESP-IDF v5.5.2 + xtensa-esp-elf-gcc 14.2.0
