/**
 * @file ts_core.h
 * @brief TianShanOS Core - Main Include Header
 *
 * TianShanOS 核心模块主头文件
 * 包含所有核心子模块的头文件
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#ifndef TS_CORE_H
#define TS_CORE_H

/* 核心子模块 */
#include "ts_config.h"
#include "ts_log.h"
#include "ts_event.h"
#include "ts_service.h"

/* PSRAM 内存管理 */
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * PSRAM 优先内存分配宏
 * 用于将非关键数据从 DRAM 迁移到 PSRAM，节省 DRAM 空间
 * ========================================================================== */

/**
 * @brief PSRAM 优先分配（带回退）
 * 
 * 优先从 PSRAM 分配，失败时回退到 DRAM
 * 适用于大缓冲区、缓存、图像数据等非 DMA 数据
 * 
 * @param size 分配大小（字节）
 * @return 指针或 NULL
 */
#define TS_MALLOC_PSRAM(size) \
    ({ void *_p = heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); \
       _p ? _p : malloc(size); })

/**
 * @brief PSRAM 优先 calloc（带回退）
 * 
 * @param n 元素数量
 * @param size 每个元素大小
 * @return 指针或 NULL（内存已清零）
 */
#define TS_CALLOC_PSRAM(n, size) \
    ({ void *_p = heap_caps_calloc((n), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); \
       _p ? _p : calloc((n), (size)); })

/**
 * @brief 强制 PSRAM 分配（无回退）
 * 
 * 仅从 PSRAM 分配，失败返回 NULL
 * 用于必须在 PSRAM 的大数据
 * 
 * @param size 分配大小
 * @return 指针或 NULL
 */
#define TS_MALLOC_PSRAM_ONLY(size) \
    heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define TS_CALLOC_PSRAM_ONLY(n, size) \
    heap_caps_calloc((n), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

/**
 * @brief PSRAM 优先 strdup（带回退）
 * 
 * 复制字符串到 PSRAM，失败时回退到 DRAM
 * 
 * @param s 源字符串
 * @return 复制的字符串或 NULL
 */
#define TS_STRDUP_PSRAM(s) \
    ({ const char *_s = (s); \
       size_t _len = _s ? strlen(_s) + 1 : 0; \
       char *_p = _len ? heap_caps_malloc(_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL; \
       if (_p) memcpy(_p, _s, _len); \
       else if (_len) { _p = strdup(_s); } \
       _p; })

/**
 * @brief PSRAM 优先 realloc（带回退）
 * 
 * 重新分配内存到 PSRAM，失败时回退到 DRAM
 * 注意：如果原内存在 DRAM 且需要扩展，会复制到 PSRAM
 * 
 * @param ptr 原指针（可为 NULL）
 * @param size 新大小
 * @return 新指针或 NULL
 */
#define TS_REALLOC_PSRAM(ptr, size) \
    ({ void *_old = (ptr); size_t _sz = (size); \
       void *_new = heap_caps_realloc(_old, _sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); \
       _new ? _new : realloc(_old, _sz); })

/* ============================================================================
 * 版本信息 - 由 CMakeLists.txt 从 version.txt 自动生成
 * ========================================================================== */

/* 这些宏由 CMake 根据 version.txt 自动定义:
 * - TIANSHAN_OS_VERSION_MAJOR  主版本号
 * - TIANSHAN_OS_VERSION_MINOR  次版本号
 * - TIANSHAN_OS_VERSION_PATCH  修订号
 * - TIANSHAN_OS_VERSION        核心版本 (如 "0.2.0")
 * - TIANSHAN_OS_VERSION_FULL   完整版本 (如 "0.2.0+abc1234")
 */

/* 兼容性宏定义 - 如果 CMake 未定义则使用默认值 */
#ifndef TIANSHAN_OS_VERSION
#define TIANSHAN_OS_VERSION "0.0.0"
#endif

#ifndef TIANSHAN_OS_VERSION_FULL
#define TIANSHAN_OS_VERSION_FULL TIANSHAN_OS_VERSION
#endif

/* 旧宏兼容 */
#define TIANSHAN_OS_VERSION_STRING TIANSHAN_OS_VERSION_FULL

/**
 * @brief 获取 TianShanOS 版本字符串
 *
 * @return 版本字符串
 */
const char *ts_get_version(void);

/**
 * @brief 获取 TianShanOS 编译时间
 *
 * @return 编译时间字符串
 */
const char *ts_get_build_time(void);

/* ============================================================================
 * 核心初始化
 * ========================================================================== */

/**
 * @brief 初始化 TianShanOS 核心系统
 *
 * 按顺序初始化所有核心子系统：
 * 1. 配置管理 (ts_config)
 * 2. 日志系统 (ts_log)
 * 3. 事件总线 (ts_event)
 * 4. 服务管理 (ts_service)
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_init(void);

/**
 * @brief 反初始化 TianShanOS 核心系统
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_deinit(void);

/**
 * @brief 检查核心系统是否已初始化
 *
 * @return true 已初始化
 */
bool ts_core_is_initialized(void);

/**
 * @brief 启动 TianShanOS
 *
 * 启动所有已注册的服务
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_start(void);

/**
 * @brief 停止 TianShanOS
 *
 * @return ESP_OK 成功
 */
esp_err_t ts_core_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CORE_H */
