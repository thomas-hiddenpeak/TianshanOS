/**
 * @file ts_api_ota.c
 * @brief TianShanOS OTA Core API Implementation
 *
 * Provides unified API endpoints for OTA operations.
 * Used by both CLI and WebUI.
 */

#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "ts_api.h"
#include "ts_ota.h"

static const char *TAG = "ts_api_ota";

// ============================================================================
//                           API Handlers
// ============================================================================

/**
 * @brief Get OTA status
 * API: ota.status
 */
static esp_err_t api_ota_status(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_status_t status;

    esp_err_t ret = ts_ota_get_status(&status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取状态失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }
    
    // State
    const char *state_str;
    switch (status.state) {
        case TS_OTA_STATE_IDLE: state_str = "idle"; break;
        case TS_OTA_STATE_CHECKING: state_str = "checking"; break;
        case TS_OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case TS_OTA_STATE_VERIFYING: state_str = "verifying"; break;
        case TS_OTA_STATE_WRITING: state_str = "writing"; break;
        case TS_OTA_STATE_PENDING_REBOOT: state_str = "pending_reboot"; break;
        case TS_OTA_STATE_ERROR: state_str = "error"; break;
        default: state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(json, "state", state_str);

    // Running partition info
    cJSON *running = cJSON_AddObjectToObject(json, "running");
    cJSON_AddStringToObject(running, "label", status.running.label);
    cJSON_AddNumberToObject(running, "address", status.running.address);
    cJSON_AddNumberToObject(running, "size", status.running.size);
    cJSON_AddStringToObject(running, "version", status.running.version.version);
    cJSON_AddStringToObject(running, "project", status.running.version.project_name);
    cJSON_AddStringToObject(running, "compile_date", status.running.version.compile_date);
    cJSON_AddStringToObject(running, "compile_time", status.running.version.compile_time);
    cJSON_AddStringToObject(running, "idf_version", status.running.version.idf_version);

    // Next partition info
    cJSON *next = cJSON_AddObjectToObject(json, "next");
    cJSON_AddStringToObject(next, "label", status.next.label);
    cJSON_AddNumberToObject(next, "address", status.next.address);
    cJSON_AddNumberToObject(next, "size", status.next.size);
    cJSON_AddBoolToObject(next, "bootable", status.next.is_bootable);
    if (status.next.is_bootable) {
        cJSON_AddStringToObject(next, "version", status.next.version.version);
        cJSON_AddStringToObject(next, "project", status.next.version.project_name);
    }

    // Rollback info
    cJSON_AddBoolToObject(json, "pending_verify", status.pending_verify);
    if (status.pending_verify) {
        cJSON_AddNumberToObject(json, "rollback_timeout", status.rollback_timeout);
    }

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get OTA progress
 * API: ota.progress
 */
static esp_err_t api_ota_progress(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_progress_t progress;

    esp_err_t ret = ts_ota_get_progress(&progress);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取进度失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    // State
    const char *state_str;
    switch (progress.state) {
        case TS_OTA_STATE_IDLE: state_str = "idle"; break;
        case TS_OTA_STATE_CHECKING: state_str = "checking"; break;
        case TS_OTA_STATE_DOWNLOADING: state_str = "downloading"; break;
        case TS_OTA_STATE_VERIFYING: state_str = "verifying"; break;
        case TS_OTA_STATE_WRITING: state_str = "writing"; break;
        case TS_OTA_STATE_PENDING_REBOOT: state_str = "pending_reboot"; break;
        case TS_OTA_STATE_ERROR: state_str = "error"; break;
        default: state_str = "unknown"; break;
    }
    cJSON_AddStringToObject(json, "state", state_str);
    cJSON_AddNumberToObject(json, "percent", progress.progress_percent);
    cJSON_AddNumberToObject(json, "total_size", progress.total_size);
    cJSON_AddNumberToObject(json, "received_size", progress.received_size);
    cJSON_AddStringToObject(json, "message", progress.status_msg ? progress.status_msg : "");

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Get firmware version
 * API: ota.version
 */
static esp_err_t api_ota_version(const cJSON *params, ts_api_result_t *result)
{
    ts_ota_status_t status;
    
    esp_err_t ret = ts_ota_get_status(&status);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "获取版本失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    if (!json) {
        ts_api_result_error(result, TS_API_ERR_NO_MEM, "内存不足");
        return ESP_ERR_NO_MEM;
    }

    const ts_ota_version_info_t *version = &status.running.version;
    cJSON_AddStringToObject(json, "version", version->version);
    cJSON_AddStringToObject(json, "project", version->project_name);
    cJSON_AddStringToObject(json, "compile_date", version->compile_date);
    cJSON_AddStringToObject(json, "compile_time", version->compile_time);
    cJSON_AddStringToObject(json, "idf_version", version->idf_version);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Start OTA from URL
 * API: ota.start_url
 * Params: url (required), auto_reboot, allow_downgrade, skip_verify
 */
static esp_err_t api_ota_start_url(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *url_item = cJSON_GetObjectItem(params, "url");
    if (!url_item || !cJSON_IsString(url_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 url 参数");
        return ESP_ERR_INVALID_ARG;
    }

    ts_ota_config_t config = {
        .source = TS_OTA_SOURCE_HTTPS,
        .url = url_item->valuestring,
        .auto_reboot = true,
        .allow_downgrade = false,
        .skip_cert_verify = false,
    };

    // Optional parameters
    cJSON *auto_reboot = cJSON_GetObjectItem(params, "auto_reboot");
    if (auto_reboot && cJSON_IsBool(auto_reboot)) {
        config.auto_reboot = cJSON_IsTrue(auto_reboot);
    }

    cJSON *allow_downgrade = cJSON_GetObjectItem(params, "allow_downgrade");
    if (allow_downgrade && cJSON_IsBool(allow_downgrade)) {
        config.allow_downgrade = cJSON_IsTrue(allow_downgrade);
    }

    cJSON *skip_verify = cJSON_GetObjectItem(params, "skip_verify");
    if (skip_verify && cJSON_IsBool(skip_verify)) {
        config.skip_cert_verify = cJSON_IsTrue(skip_verify);
    }

    esp_err_t ret = ts_ota_start(&config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "started", true);
    cJSON_AddStringToObject(json, "url", config.url);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Start OTA from SD card file
 * API: ota.start_file
 * Params: path (required), auto_reboot, allow_downgrade
 */
static esp_err_t api_ota_start_file(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *path_item = cJSON_GetObjectItem(params, "path");
    if (!path_item || !cJSON_IsString(path_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 path 参数");
        return ESP_ERR_INVALID_ARG;
    }

    ts_ota_config_t config = {
        .source = TS_OTA_SOURCE_SDCARD,
        .url = path_item->valuestring,  // url 字段用于存储文件路径
        .auto_reboot = true,
        .allow_downgrade = false,
    };

    // Optional parameters
    cJSON *auto_reboot = cJSON_GetObjectItem(params, "auto_reboot");
    if (auto_reboot && cJSON_IsBool(auto_reboot)) {
        config.auto_reboot = cJSON_IsTrue(auto_reboot);
    }

    cJSON *allow_downgrade = cJSON_GetObjectItem(params, "allow_downgrade");
    if (allow_downgrade && cJSON_IsBool(allow_downgrade)) {
        config.allow_downgrade = cJSON_IsTrue(allow_downgrade);
    }

    esp_err_t ret = ts_ota_start(&config);
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "文件不存在");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动 OTA 失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "started", true);
    cJSON_AddStringToObject(json, "path", config.url);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Abort OTA
 * API: ota.abort
 */
static esp_err_t api_ota_abort(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_abort();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "中止失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "aborted", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Mark firmware as valid (cancel rollback)
 * API: ota.validate
 */
static esp_err_t api_ota_validate(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_mark_valid();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "验证失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "validated", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Rollback to previous firmware
 * API: ota.rollback
 */
static esp_err_t api_ota_rollback(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_rollback();
    if (ret == ESP_ERR_NOT_FOUND) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "无可用回滚分区");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "回滚失败");
        return ret;
    }

    // Should not reach here (device should reboot)
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "rolling_back", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Begin firmware upload
 * API: ota.upload_begin
 * Params: size (required)
 */
static esp_err_t api_ota_upload_begin(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少参数");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *size_item = cJSON_GetObjectItem(params, "size");
    if (!size_item || !cJSON_IsNumber(size_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "缺少 size 参数");
        return ESP_ERR_INVALID_ARG;
    }

    size_t size = (size_t)size_item->valuedouble;

    esp_err_t ret = ts_ota_upload_begin(size);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "OTA 正在进行中");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "启动上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ready", true);
    cJSON_AddNumberToObject(json, "expected_size", size);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief End firmware upload
 * API: ota.upload_end
 */
static esp_err_t api_ota_upload_end(const cJSON *params, ts_api_result_t *result)
{
    bool auto_reboot = true;

    if (params) {
        cJSON *reboot_item = cJSON_GetObjectItem(params, "auto_reboot");
        if (reboot_item && cJSON_IsBool(reboot_item)) {
            auto_reboot = cJSON_IsTrue(reboot_item);
        }
    }

    esp_err_t ret = ts_ota_upload_end(auto_reboot);
    if (ret == ESP_ERR_INVALID_STATE) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "无活动上传");
        return ret;
    } else if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "完成上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "completed", true);
    cJSON_AddBoolToObject(json, "reboot_pending", auto_reboot);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief Abort firmware upload
 * API: ota.upload_abort
 */
static esp_err_t api_ota_upload_abort(const cJSON *params, ts_api_result_t *result)
{
    esp_err_t ret = ts_ota_upload_abort();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "中止上传失败");
        return ret;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "aborted", true);

    ts_api_result_ok(result, json);
    return ESP_OK;
}

// ============================================================================
//                           API Registration
// ============================================================================

esp_err_t ts_api_ota_register(void)
{
    ESP_LOGI(TAG, "Registering OTA APIs");

    ts_api_endpoint_t endpoints[] = {
        {
            .name = "ota.status",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_status,
            .requires_auth = false,
            .description = "获取 OTA 状态",
        },
        {
            .name = "ota.progress",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_progress,
            .requires_auth = false,
            .description = "获取 OTA 进度",
        },
        {
            .name = "ota.version",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_version,
            .requires_auth = false,
            .description = "获取固件版本",
        },
        {
            .name = "ota.start_url",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_url,
            .requires_auth = true,
            .description = "从 URL 启动 OTA",
        },
        {
            .name = "ota.start_file",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_start_file,
            .requires_auth = true,
            .description = "从 SD 卡启动 OTA",
        },
        {
            .name = "ota.abort",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_abort,
            .requires_auth = true,
            .description = "中止 OTA",
        },
        {
            .name = "ota.validate",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_validate,
            .requires_auth = true,
            .description = "标记固件有效",
        },
        {
            .name = "ota.rollback",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_rollback,
            .requires_auth = true,
            .description = "回滚到上一版本",
        },
        {
            .name = "ota.upload_begin",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_begin,
            .requires_auth = true,
            .description = "开始固件上传",
        },
        {
            .name = "ota.upload_end",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_end,
            .requires_auth = true,
            .description = "结束固件上传",
        },
        {
            .name = "ota.upload_abort",
            .category = TS_API_CAT_SYSTEM,
            .handler = api_ota_upload_abort,
            .requires_auth = true,
            .description = "中止固件上传",
        },
    };

    size_t num_endpoints = sizeof(endpoints) / sizeof(endpoints[0]);
    
    for (size_t i = 0; i < num_endpoints; i++) {
        esp_err_t ret = ts_api_register(&endpoints[i]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s: %s", endpoints[i].name, esp_err_to_name(ret));
            return ret;
        }
    }

    ESP_LOGI(TAG, "Registered %zu OTA API endpoints", num_endpoints);
    return ESP_OK;
}
