/**
 * @file ts_config_meta.c
 * @brief TianShanOS Configuration Meta Management Implementation
 *
 * 元配置管理实现
 * 存储在 NVS "ts_meta" 命名空间
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#include <string.h>
#include "ts_config_meta.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "ts_config_meta";

/* ============================================================================
 * NVS 键名定义
 * ========================================================================== */

#define NVS_KEY_GLOBAL_SEQ      "global_seq"
#define NVS_KEY_SYNC_SEQ        "sync_seq"
#define NVS_KEY_PENDING_SYNC    "pending_sync"
#define NVS_KEY_SCHEMA_VER_FMT  "schema_v%d"     /* schema_v0, schema_v1, ... */

/* ============================================================================
 * 私有变量
 * ========================================================================== */

static struct {
    bool            initialized;
    nvs_handle_t    nvs_handle;
    SemaphoreHandle_t mutex;
    
    /* 缓存的元数据（避免频繁 NVS 读取） */
    uint32_t        global_seq;
    uint32_t        sync_seq;
    uint8_t         pending_sync;
    uint16_t        schema_versions[TS_CONFIG_MODULE_MAX];
} s_meta = {0};

/* ============================================================================
 * 初始化
 * ========================================================================== */

esp_err_t ts_config_meta_init(void)
{
    if (s_meta.initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing configuration meta management...");
    
    /* 创建互斥锁 */
    s_meta.mutex = xSemaphoreCreateMutex();
    if (s_meta.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* 打开 NVS 命名空间 */
    esp_err_t ret = nvs_open(TS_CONFIG_META_NAMESPACE, NVS_READWRITE, &s_meta.nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s': %s", 
                 TS_CONFIG_META_NAMESPACE, esp_err_to_name(ret));
        vSemaphoreDelete(s_meta.mutex);
        s_meta.mutex = NULL;
        return ret;
    }
    
    /* 加载缓存数据 */
    
    /* global_seq */
    ret = nvs_get_u32(s_meta.nvs_handle, NVS_KEY_GLOBAL_SEQ, &s_meta.global_seq);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_meta.global_seq = 0;
        ESP_LOGI(TAG, "global_seq not found, initialized to 0");
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read global_seq: %s", esp_err_to_name(ret));
        s_meta.global_seq = 0;
    }
    
    /* sync_seq */
    ret = nvs_get_u32(s_meta.nvs_handle, NVS_KEY_SYNC_SEQ, &s_meta.sync_seq);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_meta.sync_seq = 0;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read sync_seq: %s", esp_err_to_name(ret));
        s_meta.sync_seq = 0;
    }
    
    /* pending_sync */
    ret = nvs_get_u8(s_meta.nvs_handle, NVS_KEY_PENDING_SYNC, &s_meta.pending_sync);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        s_meta.pending_sync = 0;
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read pending_sync: %s", esp_err_to_name(ret));
        s_meta.pending_sync = 0;
    }
    
    /* schema versions */
    for (int i = 0; i < TS_CONFIG_MODULE_MAX; i++) {
        char key[16];
        snprintf(key, sizeof(key), NVS_KEY_SCHEMA_VER_FMT, i);
        ret = nvs_get_u16(s_meta.nvs_handle, key, &s_meta.schema_versions[i]);
        if (ret != ESP_OK) {
            s_meta.schema_versions[i] = 0;
        }
    }
    
    s_meta.initialized = true;
    
    ESP_LOGI(TAG, "Meta initialized: global_seq=%lu, sync_seq=%lu, pending=0x%02x",
             (unsigned long)s_meta.global_seq, 
             (unsigned long)s_meta.sync_seq,
             s_meta.pending_sync);
    
    return ESP_OK;
}

esp_err_t ts_config_meta_deinit(void)
{
    if (!s_meta.initialized) {
        return ESP_OK;
    }
    
    nvs_close(s_meta.nvs_handle);
    
    if (s_meta.mutex) {
        vSemaphoreDelete(s_meta.mutex);
        s_meta.mutex = NULL;
    }
    
    s_meta.initialized = false;
    ESP_LOGI(TAG, "Meta deinitialized");
    return ESP_OK;
}

/* ============================================================================
 * 序列号管理
 * ========================================================================== */

uint32_t ts_config_meta_get_global_seq(void)
{
    return s_meta.global_seq;
}

uint32_t ts_config_meta_increment_global_seq(void)
{
    if (!s_meta.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return 0;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.global_seq++;
    
    esp_err_t ret = nvs_set_u32(s_meta.nvs_handle, NVS_KEY_GLOBAL_SEQ, s_meta.global_seq);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save global_seq: %s", esp_err_to_name(ret));
    }
    
    uint32_t seq = s_meta.global_seq;
    xSemaphoreGive(s_meta.mutex);
    
    ESP_LOGD(TAG, "global_seq incremented to %lu", (unsigned long)seq);
    return seq;
}

uint32_t ts_config_meta_get_sync_seq(void)
{
    return s_meta.sync_seq;
}

esp_err_t ts_config_meta_set_sync_seq(uint32_t seq)
{
    if (!s_meta.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.sync_seq = seq;
    
    esp_err_t ret = nvs_set_u32(s_meta.nvs_handle, NVS_KEY_SYNC_SEQ, s_meta.sync_seq);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    xSemaphoreGive(s_meta.mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save sync_seq: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

/* ============================================================================
 * 待同步标记管理
 * ========================================================================== */

uint8_t ts_config_meta_get_pending_sync(void)
{
    return s_meta.pending_sync;
}

esp_err_t ts_config_meta_set_pending_sync(ts_config_module_t module)
{
    if (!s_meta.initialized || module >= TS_CONFIG_MODULE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.pending_sync |= (1 << module);
    
    esp_err_t ret = nvs_set_u8(s_meta.nvs_handle, NVS_KEY_PENDING_SYNC, s_meta.pending_sync);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    xSemaphoreGive(s_meta.mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pending_sync: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Set pending_sync for module %d, mask=0x%02x", module, s_meta.pending_sync);
    }
    
    return ret;
}

esp_err_t ts_config_meta_clear_pending_sync(ts_config_module_t module)
{
    if (!s_meta.initialized || module >= TS_CONFIG_MODULE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.pending_sync &= ~(1 << module);
    
    esp_err_t ret = nvs_set_u8(s_meta.nvs_handle, NVS_KEY_PENDING_SYNC, s_meta.pending_sync);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    xSemaphoreGive(s_meta.mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pending_sync: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Cleared pending_sync for module %d, mask=0x%02x", module, s_meta.pending_sync);
    }
    
    return ret;
}

esp_err_t ts_config_meta_clear_all_pending_sync(void)
{
    if (!s_meta.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.pending_sync = 0;
    
    esp_err_t ret = nvs_set_u8(s_meta.nvs_handle, NVS_KEY_PENDING_SYNC, s_meta.pending_sync);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    xSemaphoreGive(s_meta.mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save pending_sync: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Cleared all pending_sync");
    }
    
    return ret;
}

bool ts_config_meta_is_pending_sync(ts_config_module_t module)
{
    if (module >= TS_CONFIG_MODULE_MAX) {
        return false;
    }
    return (s_meta.pending_sync & (1 << module)) != 0;
}

/* ============================================================================
 * Schema 版本管理
 * ========================================================================== */

uint16_t ts_config_meta_get_schema_version(ts_config_module_t module)
{
    if (module >= TS_CONFIG_MODULE_MAX) {
        return 0;
    }
    return s_meta.schema_versions[module];
}

esp_err_t ts_config_meta_set_schema_version(ts_config_module_t module, uint16_t version)
{
    if (!s_meta.initialized || module >= TS_CONFIG_MODULE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    xSemaphoreTake(s_meta.mutex, portMAX_DELAY);
    
    s_meta.schema_versions[module] = version;
    
    char key[16];
    snprintf(key, sizeof(key), NVS_KEY_SCHEMA_VER_FMT, module);
    
    esp_err_t ret = nvs_set_u16(s_meta.nvs_handle, key, version);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_meta.nvs_handle);
    }
    
    xSemaphoreGive(s_meta.mutex);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save schema_version for module %d: %s", 
                 module, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Set schema_version for module %d to %d", module, version);
    }
    
    return ret;
}

/* ============================================================================
 * 调试
 * ========================================================================== */

void ts_config_meta_dump(void)
{
    ESP_LOGI(TAG, "=== Configuration Meta ===");
    ESP_LOGI(TAG, "  global_seq:   %lu", (unsigned long)s_meta.global_seq);
    ESP_LOGI(TAG, "  sync_seq:     %lu", (unsigned long)s_meta.sync_seq);
    ESP_LOGI(TAG, "  pending_sync: 0x%02x", s_meta.pending_sync);
    
    ESP_LOGI(TAG, "  Schema versions:");
    const char *module_names[] = {"NET", "DHCP", "WIFI", "LED", "FAN", "DEVICE", "SYSTEM"};
    for (int i = 0; i < TS_CONFIG_MODULE_MAX; i++) {
        if (s_meta.schema_versions[i] > 0) {
            ESP_LOGI(TAG, "    %s: v%d", module_names[i], s_meta.schema_versions[i]);
        }
    }
    
    if (s_meta.pending_sync != 0) {
        ESP_LOGI(TAG, "  Pending sync modules:");
        for (int i = 0; i < TS_CONFIG_MODULE_MAX; i++) {
            if (s_meta.pending_sync & (1 << i)) {
                ESP_LOGI(TAG, "    - %s", module_names[i]);
            }
        }
    }
    ESP_LOGI(TAG, "==========================");
}
