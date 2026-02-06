/**
 * @file ts_ota_rollback.c
 * @brief TianShanOS OTA Rollback and Validation
 */

#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_format.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "ts_ota.h"

static const char *TAG = "ts_ota_rollback";

// NVS namespace for OTA data
#define OTA_NVS_NAMESPACE "ts_ota"
#define OTA_NVS_KEY_LAST_UPDATE "last_update"
#define OTA_NVS_KEY_UPDATE_COUNT "update_cnt"

/**
 * @brief Get partition information
 */
esp_err_t ts_ota_get_running_partition_info(ts_ota_partition_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(info->label, running->label, sizeof(info->label) - 1);
    info->address = running->address;
    info->size = running->size;
    info->is_running = true;
    info->is_bootable = true;

    // Get version info
    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc) {
        strncpy(info->version.version, desc->version, sizeof(info->version.version) - 1);
        strncpy(info->version.project_name, desc->project_name, sizeof(info->version.project_name) - 1);
        strncpy(info->version.compile_time, desc->time, sizeof(info->version.compile_time) - 1);
        strncpy(info->version.compile_date, desc->date, sizeof(info->version.compile_date) - 1);
        strncpy(info->version.idf_version, desc->idf_ver, sizeof(info->version.idf_version) - 1);
        info->version.secure_version = desc->secure_version;
    }

    return ESP_OK;
}

/**
 * @brief Get next update partition information
 */
esp_err_t ts_ota_get_next_partition_info(ts_ota_partition_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) {
        ESP_LOGE(TAG, "Failed to get next update partition");
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(info->label, next->label, sizeof(info->label) - 1);
    info->address = next->address;
    info->size = next->size;
    info->is_running = false;

    // Check if partition has valid app
    esp_app_desc_t desc;
    esp_err_t ret = esp_ota_get_partition_description(next, &desc);
    if (ret == ESP_OK) {
        info->is_bootable = true;
        strncpy(info->version.version, desc.version, sizeof(info->version.version) - 1);
        strncpy(info->version.project_name, desc.project_name, sizeof(info->version.project_name) - 1);
        strncpy(info->version.compile_time, desc.time, sizeof(info->version.compile_time) - 1);
        strncpy(info->version.compile_date, desc.date, sizeof(info->version.compile_date) - 1);
        strncpy(info->version.idf_version, desc.idf_ver, sizeof(info->version.idf_version) - 1);
        info->version.secure_version = desc.secure_version;
    } else {
        info->is_bootable = false;
    }

    return ESP_OK;
}

/**
 * @brief Get boot partition information
 */
esp_err_t ts_ota_get_boot_partition_info(ts_ota_partition_info_t *info)
{
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    const esp_partition_t *boot = esp_ota_get_boot_partition();
    if (!boot) {
        ESP_LOGE(TAG, "Failed to get boot partition");
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(info->label, boot->label, sizeof(info->label) - 1);
    info->address = boot->address;
    info->size = boot->size;

    const esp_partition_t *running = esp_ota_get_running_partition();
    info->is_running = (running && running->address == boot->address);

    // Get app description
    esp_app_desc_t desc;
    esp_err_t ret = esp_ota_get_partition_description(boot, &desc);
    if (ret == ESP_OK) {
        info->is_bootable = true;
        strncpy(info->version.version, desc.version, sizeof(info->version.version) - 1);
        strncpy(info->version.project_name, desc.project_name, sizeof(info->version.project_name) - 1);
        strncpy(info->version.compile_time, desc.time, sizeof(info->version.compile_time) - 1);
        strncpy(info->version.compile_date, desc.date, sizeof(info->version.compile_date) - 1);
    }

    return ESP_OK;
}

/**
 * @brief Save update timestamp to NVS
 */
esp_err_t ts_ota_save_update_time(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save current time
    time_t now;
    time(&now);
    ret = nvs_set_i64(handle, OTA_NVS_KEY_LAST_UPDATE, now);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    // Increment update count
    uint32_t count = 0;
    nvs_get_u32(handle, OTA_NVS_KEY_UPDATE_COUNT, &count);
    count++;
    nvs_set_u32(handle, OTA_NVS_KEY_UPDATE_COUNT, count);

    ret = nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Update #%lu recorded at %ld", count, now);
    return ret;
}

/**
 * @brief Get last update timestamp
 */
esp_err_t ts_ota_get_last_update_time(time_t *timestamp)
{
    if (!timestamp) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        *timestamp = 0;
        return ret;
    }

    int64_t ts;
    ret = nvs_get_i64(handle, OTA_NVS_KEY_LAST_UPDATE, &ts);
    if (ret == ESP_OK) {
        *timestamp = (time_t)ts;
    } else {
        *timestamp = 0;
    }

    nvs_close(handle);
    return ret;
}

/**
 * @brief Get update count
 */
esp_err_t ts_ota_get_update_count(uint32_t *count)
{
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }

    ret = nvs_get_u32(handle, OTA_NVS_KEY_UPDATE_COUNT, count);
    if (ret != ESP_OK) {
        *count = 0;
    }

    nvs_close(handle);
    return ret;
}

/**
 * @brief Diagnostic: Print partition table info
 */
void ts_ota_print_partition_info(void)
{
    ESP_LOGI(TAG, "=== OTA Partition Information ===");

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *boot = esp_ota_get_boot_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

    if (running) {
        ESP_LOGI(TAG, "Running: %s (0x%lx, %lu KB)", 
                 running->label, running->address, running->size / 1024);
    }

    if (boot) {
        ESP_LOGI(TAG, "Boot:    %s (0x%lx, %lu KB)%s", 
                 boot->label, boot->address, boot->size / 1024,
                 (running && boot->address == running->address) ? " [same]" : "");
    }

    if (next) {
        ESP_LOGI(TAG, "Next:    %s (0x%lx, %lu KB)", 
                 next->label, next->address, next->size / 1024);

        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(next, &desc) == ESP_OK) {
            ESP_LOGI(TAG, "  Previous firmware: %s v%s", desc.project_name, desc.version);
        } else {
            ESP_LOGI(TAG, "  (empty or invalid)");
        }
    }

    // OTA state
    if (running) {
        esp_ota_img_states_t ota_state;
        if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
            const char *state_str;
            switch (ota_state) {
                case ESP_OTA_IMG_NEW: state_str = "NEW"; break;
                case ESP_OTA_IMG_PENDING_VERIFY: state_str = "PENDING_VERIFY"; break;
                case ESP_OTA_IMG_VALID: state_str = "VALID"; break;
                case ESP_OTA_IMG_INVALID: state_str = "INVALID"; break;
                case ESP_OTA_IMG_ABORTED: state_str = "ABORTED"; break;
                default: state_str = "UNDEFINED"; break;
            }
            ESP_LOGI(TAG, "OTA State: %s", state_str);
        }
    }

    ESP_LOGI(TAG, "=================================");
}
