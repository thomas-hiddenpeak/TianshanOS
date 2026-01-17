/**
 * @file ts_nat.h
 * @brief NAT/NAPT Gateway for TianShanOS
 * 
 * 提供 WiFi STA 到 ETH 的 NAT 网关功能，让以太网设备通过 WiFi 访问外网
 */

#ifndef TS_NAT_H
#define TS_NAT_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief NAT 网关状态
 */
typedef enum {
    TS_NAT_STATE_DISABLED = 0,  /**< NAT 已禁用 */
    TS_NAT_STATE_ENABLED,       /**< NAT 已启用 */
    TS_NAT_STATE_ERROR,         /**< NAT 错误 */
} ts_nat_state_t;

/**
 * @brief NAT 网关配置
 */
typedef struct {
    bool enabled;               /**< 是否启用 NAT */
    bool auto_start;            /**< WiFi 连接后自动启动 NAT */
} ts_nat_config_t;

/**
 * @brief NAT 网关状态信息
 */
typedef struct {
    ts_nat_state_t state;       /**< 当前状态 */
    bool wifi_connected;        /**< WiFi STA 是否已连接 */
    bool eth_up;                /**< ETH 接口是否 UP */
    uint32_t packets_forwarded; /**< 已转发的数据包数量（估计值） */
} ts_nat_status_t;

/**
 * @brief 初始化 NAT 模块
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_nat_init(void);

/**
 * @brief 启用 NAT 网关
 * 
 * 启用从 ETH 接口到 WiFi STA 接口的 NAPT
 * 需要 WiFi STA 已连接并获得 IP
 * 
 * @return ESP_OK 成功
 * @return ESP_ERR_INVALID_STATE WiFi 未连接
 */
esp_err_t ts_nat_enable(void);

/**
 * @brief 禁用 NAT 网关
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_nat_disable(void);

/**
 * @brief 获取 NAT 状态
 * 
 * @param[out] status 状态信息
 * @return ESP_OK 成功
 */
esp_err_t ts_nat_get_status(ts_nat_status_t *status);

/**
 * @brief 检查 NAT 是否已启用
 * 
 * @return true NAT 已启用
 * @return false NAT 未启用
 */
bool ts_nat_is_enabled(void);

/**
 * @brief 保存 NAT 配置到 NVS
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_nat_save_config(void);

/**
 * @brief 从 NVS 加载 NAT 配置
 * 
 * @return ESP_OK 成功
 */
esp_err_t ts_nat_load_config(void);

#ifdef __cplusplus
}
#endif

#endif /* TS_NAT_H */
