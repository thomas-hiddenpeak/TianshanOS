/**
 * @file ts_nat.c
 * @brief NAT/NAPT Gateway Implementation
 * 
 * 使用 ESP-IDF LWIP NAPT 功能实现 WiFi STA -> ETH 的 NAT 网关
 */

#include "ts_nat.h"
#include "ts_wifi.h"
#include "ts_eth.h"
#include "ts_net_manager.h"
#include "ts_log.h"
#include "esp_netif.h"
#include "lwip/lwip_napt.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

#define TAG "ts_nat"

/* NVS 存储键 */
#define NVS_NAMESPACE "ts_nat"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_AUTO_START "auto_start"

/* 模块状态 */
static struct {
    bool initialized;
    ts_nat_state_t state;
    ts_nat_config_t config;
} s_nat = {
    .initialized = false,
    .state = TS_NAT_STATE_DISABLED,
    .config = {
        .enabled = false,
        .auto_start = true,  /* 默认自动启动 */
    },
};

/* ============================================================================
 * 内部函数
 * ========================================================================== */

static esp_netif_t *get_wifi_sta_netif(void)
{
    return ts_wifi_get_netif(TS_WIFI_IF_STA);
}

static esp_netif_t *get_eth_netif(void)
{
    return ts_eth_get_netif();
}

/* ============================================================================
 * 公共 API
 * ========================================================================== */

esp_err_t ts_nat_init(void)
{
    if (s_nat.initialized) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "Initializing NAT module");
    
    /* 从 NVS 加载配置 */
    ts_nat_load_config();
    
    s_nat.initialized = true;
    TS_LOGI(TAG, "NAT module initialized (auto_start=%s)", 
            s_nat.config.auto_start ? "yes" : "no");
    
    return ESP_OK;
}

esp_err_t ts_nat_enable(void)
{
    if (!s_nat.initialized) {
        ts_nat_init();
    }
    
    /* 检查 WiFi STA 是否已连接 */
    if (!ts_wifi_is_connected()) {
        TS_LOGW(TAG, "Cannot enable NAT: WiFi STA not connected");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 获取 WiFi STA 的 netif（出口接口） */
    esp_netif_t *wifi_netif = get_wifi_sta_netif();
    if (!wifi_netif) {
        TS_LOGE(TAG, "Cannot get WiFi STA netif");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 获取 ETH 的 netif（入口接口，DHCP 服务器所在接口） */
    esp_netif_t *eth_netif = ts_eth_get_netif();
    if (!eth_netif) {
        TS_LOGE(TAG, "Cannot get ETH netif");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 设置 WiFi STA 为默认网络接口（路由出口） */
    esp_netif_set_default_netif(wifi_netif);
    TS_LOGI(TAG, "Set WiFi STA as default netif for routing");
    
    /* 在 ETH 接口上启用 NAPT（提供 DHCP 的接口）
     * 这是 ESP-IDF NAPT 的正确用法：在「内网接口」上启用 NAPT
     * 参考 esp-idf/examples/wifi/softap_sta
     */
    esp_err_t ret = esp_netif_napt_enable(eth_netif);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to enable NAPT on ETH: %s", esp_err_to_name(ret));
        s_nat.state = TS_NAT_STATE_ERROR;
        return ret;
    }
    
    s_nat.state = TS_NAT_STATE_ENABLED;
    s_nat.config.enabled = true;
    
    /* 获取 WiFi IP 用于显示 */
    esp_netif_ip_info_t wifi_ip, eth_ip;
    esp_netif_get_ip_info(wifi_netif, &wifi_ip);
    esp_netif_get_ip_info(eth_netif, &eth_ip);
    
    TS_LOGI(TAG, "NAT enabled: ETH (" IPSTR ") -> WiFi STA (" IPSTR ")", 
            IP2STR(&eth_ip.ip), IP2STR(&wifi_ip.ip));
    
    return ESP_OK;
}

esp_err_t ts_nat_disable(void)
{
    if (s_nat.state != TS_NAT_STATE_ENABLED) {
        return ESP_OK;
    }
    
    /* 在 ETH 接口上禁用 NAPT */
    esp_netif_t *eth_netif = ts_eth_get_netif();
    if (eth_netif) {
        esp_err_t ret = esp_netif_napt_disable(eth_netif);
        if (ret != ESP_OK) {
            TS_LOGW(TAG, "Failed to disable NAPT on ETH: %s", esp_err_to_name(ret));
        }
    }
    
    s_nat.state = TS_NAT_STATE_DISABLED;
    s_nat.config.enabled = false;
    
    TS_LOGI(TAG, "NAT disabled");
    return ESP_OK;
}

esp_err_t ts_nat_get_status(ts_nat_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->state = s_nat.state;
    status->wifi_connected = ts_wifi_is_connected();
    
    /* 检查 ETH 状态 */
    ts_net_manager_status_t net_status;
    if (ts_net_manager_get_status(&net_status) == ESP_OK) {
        status->eth_up = net_status.eth.link_up;
    } else {
        status->eth_up = false;
    }
    
    /* 估计转发包数量（NAPT 没有直接统计） */
    status->packets_forwarded = 0;
    
    return ESP_OK;
}

bool ts_nat_is_enabled(void)
{
    return s_nat.state == TS_NAT_STATE_ENABLED;
}

esp_err_t ts_nat_save_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    nvs_set_u8(handle, NVS_KEY_ENABLED, s_nat.config.enabled ? 1 : 0);
    nvs_set_u8(handle, NVS_KEY_AUTO_START, s_nat.config.auto_start ? 1 : 0);
    
    ret = nvs_commit(handle);
    nvs_close(handle);
    
    TS_LOGI(TAG, "NAT config saved");
    return ret;
}

esp_err_t ts_nat_load_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        TS_LOGI(TAG, "No saved NAT config, using defaults");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }
    
    uint8_t val;
    if (nvs_get_u8(handle, NVS_KEY_ENABLED, &val) == ESP_OK) {
        s_nat.config.enabled = (val != 0);
    }
    if (nvs_get_u8(handle, NVS_KEY_AUTO_START, &val) == ESP_OK) {
        s_nat.config.auto_start = (val != 0);
    }
    
    nvs_close(handle);
    
    TS_LOGI(TAG, "NAT config loaded: enabled=%s, auto_start=%s",
            s_nat.config.enabled ? "yes" : "no",
            s_nat.config.auto_start ? "yes" : "no");
    
    return ESP_OK;
}
