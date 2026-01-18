/**
 * @file ts_config_meta.h
 * @brief TianShanOS Configuration Meta Management
 *
 * 元配置管理：
 * - global_seq: 全局配置序列号（每次持久化递增）
 * - sync_seq: 上次同步到 SD卡时的序列号
 * - pending_sync: 待同步模块的位掩码
 * - schema_version: 各模块的 Schema 版本
 *
 * 存储在 NVS 命名空间 "ts_meta"
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_CONFIG_META_H
#define TS_CONFIG_META_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ts_config_module.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 初始化
 * ========================================================================== */

/**
 * @brief 初始化元配置管理
 * 
 * 打开 NVS "ts_meta" 命名空间，加载元配置
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_init(void);

/**
 * @brief 反初始化元配置管理
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_deinit(void);

/* ============================================================================
 * 序列号管理
 * ========================================================================== */

/**
 * @brief 获取全局序列号
 * 
 * @return 当前全局序列号
 */
uint32_t ts_config_meta_get_global_seq(void);

/**
 * @brief 递增并获取全局序列号
 * 
 * 原子操作：递增 global_seq 并保存到 NVS
 * 
 * @return 递增后的序列号
 */
uint32_t ts_config_meta_increment_global_seq(void);

/**
 * @brief 获取同步序列号
 * 
 * @return 上次同步到 SD卡时的序列号
 */
uint32_t ts_config_meta_get_sync_seq(void);

/**
 * @brief 设置同步序列号
 * 
 * @param seq 新的同步序列号
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_set_sync_seq(uint32_t seq);

/* ============================================================================
 * 待同步标记管理
 * ========================================================================== */

/**
 * @brief 获取待同步模块的位掩码
 * 
 * @return 位掩码，bit N 表示模块 N 需要同步
 */
uint8_t ts_config_meta_get_pending_sync(void);

/**
 * @brief 设置模块待同步标记
 * 
 * @param module 模块ID
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_set_pending_sync(ts_config_module_t module);

/**
 * @brief 清除模块待同步标记
 * 
 * @param module 模块ID
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_clear_pending_sync(ts_config_module_t module);

/**
 * @brief 清除所有待同步标记
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_clear_all_pending_sync(void);

/**
 * @brief 检查模块是否需要同步
 * 
 * @param module 模块ID
 * @return true 需要同步
 */
bool ts_config_meta_is_pending_sync(ts_config_module_t module);

/* ============================================================================
 * Schema 版本管理
 * ========================================================================== */

/**
 * @brief 获取模块的已保存 Schema 版本
 * 
 * @param module 模块ID
 * @return Schema 版本号，0 表示未保存
 */
uint16_t ts_config_meta_get_schema_version(ts_config_module_t module);

/**
 * @brief 设置模块的 Schema 版本
 * 
 * @param module 模块ID
 * @param version 版本号
 * @return ESP_OK 成功
 */
esp_err_t ts_config_meta_set_schema_version(ts_config_module_t module, uint16_t version);

/* ============================================================================
 * 调试
 * ========================================================================== */

/**
 * @brief 打印元配置信息
 */
void ts_config_meta_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_CONFIG_META_H */
