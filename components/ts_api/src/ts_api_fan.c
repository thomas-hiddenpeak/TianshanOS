/**
 * @file ts_api_fan.c
 * @brief Fan Control API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_fan.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_fan"

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *mode_to_string(ts_fan_mode_t mode)
{
    switch (mode) {
        case TS_FAN_MODE_OFF:    return "off";
        case TS_FAN_MODE_MANUAL: return "manual";
        case TS_FAN_MODE_AUTO:   return "auto";
        case TS_FAN_MODE_CURVE:  return "curve";
        default:                 return "unknown";
    }
}

static ts_fan_mode_t string_to_mode(const char *str)
{
    if (!str) return TS_FAN_MODE_MANUAL;
    if (strcmp(str, "off") == 0)    return TS_FAN_MODE_OFF;
    if (strcmp(str, "manual") == 0) return TS_FAN_MODE_MANUAL;
    if (strcmp(str, "auto") == 0)   return TS_FAN_MODE_AUTO;
    if (strcmp(str, "curve") == 0)  return TS_FAN_MODE_CURVE;
    return TS_FAN_MODE_MANUAL;
}

static cJSON *status_to_json(ts_fan_id_t fan_id, const ts_fan_status_t *status)
{
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "id", fan_id);
    cJSON_AddStringToObject(obj, "mode", mode_to_string(status->mode));
    cJSON_AddNumberToObject(obj, "duty", status->duty_percent);
    cJSON_AddNumberToObject(obj, "target_duty", status->target_duty);
    cJSON_AddNumberToObject(obj, "rpm", status->rpm);
    cJSON_AddNumberToObject(obj, "temperature", status->temp / 10.0);
    cJSON_AddBoolToObject(obj, "enabled", status->enabled);
    cJSON_AddBoolToObject(obj, "running", status->is_running);
    cJSON_AddBoolToObject(obj, "fault", status->fault);
    return obj;
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief fan.status - Get fan status
 * 
 * Params: { "id": 0 } or {} for all fans
 * Returns: single fan status or array of all fans
 */
static esp_err_t api_fan_status(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *id_item = cJSON_GetObjectItem(params, "id");
    
    if (cJSON_IsNumber(id_item)) {
        /* Single fan */
        int fan_id = id_item->valueint;
        if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
            return ESP_ERR_INVALID_ARG;
        }
        
        ts_fan_status_t status;
        esp_err_t ret = ts_fan_get_status(fan_id, &status);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to get fan status");
            return ret;
        }
        
        ts_api_result_ok(result, status_to_json(fan_id, &status));
    } else {
        /* All fans */
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < TS_FAN_MAX; i++) {
            ts_fan_status_t status;
            if (ts_fan_get_status(i, &status) == ESP_OK) {
                cJSON_AddItemToArray(arr, status_to_json(i, &status));
            }
        }
        
        cJSON *data = cJSON_CreateObject();
        cJSON_AddItemToObject(data, "fans", arr);
        ts_api_result_ok(result, data);
    }
    
    return ESP_OK;
}

/**
 * @brief fan.set - Set fan speed (manual mode)
 * 
 * Params: { "id": 0, "duty": 50 }
 */
static esp_err_t api_fan_set(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *id_item = cJSON_GetObjectItem(params, "id");
    const cJSON *duty_item = cJSON_GetObjectItem(params, "duty");
    
    if (!cJSON_IsNumber(id_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: id");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(duty_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: duty");
        return ESP_ERR_INVALID_ARG;
    }
    
    int fan_id = id_item->valueint;
    int duty = duty_item->valueint;
    
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
        return ESP_ERR_INVALID_ARG;
    }
    if (duty < 0 || duty > 100) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Duty must be 0-100");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_fan_set_duty(fan_id, duty);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to set fan duty");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", fan_id);
    cJSON_AddNumberToObject(data, "duty", duty);
    cJSON_AddStringToObject(data, "mode", "manual");
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief fan.mode - Set fan mode
 * 
 * Params: { "id": 0, "mode": "auto" }
 */
static esp_err_t api_fan_mode(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *id_item = cJSON_GetObjectItem(params, "id");
    const cJSON *mode_item = cJSON_GetObjectItem(params, "mode");
    
    if (!cJSON_IsNumber(id_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: id");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsString(mode_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: mode");
        return ESP_ERR_INVALID_ARG;
    }
    
    int fan_id = id_item->valueint;
    ts_fan_mode_t mode = string_to_mode(mode_item->valuestring);
    
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_fan_set_mode(fan_id, mode);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to set fan mode");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", fan_id);
    cJSON_AddStringToObject(data, "mode", mode_to_string(mode));
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief fan.enable - Enable or disable a fan
 * 
 * Params: { "id": 0, "enable": true }
 */
static esp_err_t api_fan_enable(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *id_item = cJSON_GetObjectItem(params, "id");
    const cJSON *enable_item = cJSON_GetObjectItem(params, "enable");
    
    if (!cJSON_IsNumber(id_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: id");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsBool(enable_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: enable");
        return ESP_ERR_INVALID_ARG;
    }
    
    int fan_id = id_item->valueint;
    bool enable = cJSON_IsTrue(enable_item);
    
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_fan_enable(fan_id, enable);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to enable/disable fan");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", fan_id);
    cJSON_AddBoolToObject(data, "enabled", enable);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief fan.curve - Set temperature curve
 * 
 * Params: { "id": 0, "curve": [{"temp": 30, "duty": 30}, {"temp": 50, "duty": 70}, {"temp": 70, "duty": 100}] }
 */
static esp_err_t api_fan_curve(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *id_item = cJSON_GetObjectItem(params, "id");
    const cJSON *curve_item = cJSON_GetObjectItem(params, "curve");
    
    if (!cJSON_IsNumber(id_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: id");
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsArray(curve_item)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing required parameter: curve (array)");
        return ESP_ERR_INVALID_ARG;
    }
    
    int fan_id = id_item->valueint;
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid fan ID");
        return ESP_ERR_INVALID_ARG;
    }
    
    int count = cJSON_GetArraySize(curve_item);
    if (count > TS_FAN_MAX_CURVE_POINTS) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Too many curve points");
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_fan_curve_point_t curve[TS_FAN_MAX_CURVE_POINTS];
    for (int i = 0; i < count; i++) {
        cJSON *point = cJSON_GetArrayItem(curve_item, i);
        cJSON *temp = cJSON_GetObjectItem(point, "temp");
        cJSON *duty = cJSON_GetObjectItem(point, "duty");
        
        if (!cJSON_IsNumber(temp) || !cJSON_IsNumber(duty)) {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid curve point format");
            return ESP_ERR_INVALID_ARG;
        }
        
        curve[i].temp = (int16_t)(temp->valuedouble * 10);  /* Convert to 0.1°C */
        curve[i].duty = (uint8_t)duty->valueint;
    }
    
    esp_err_t ret = ts_fan_set_curve(fan_id, curve, count);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to set fan curve");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "id", fan_id);
    cJSON_AddNumberToObject(data, "points", count);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_fan_endpoints[] = {
    {
        .name = "fan.status",
        .description = "Get fan status",
        .category = TS_API_CAT_FAN,
        .handler = api_fan_status,
        .requires_auth = false,
    },
    {
        .name = "fan.set",
        .description = "Set fan speed (manual mode)",
        .category = TS_API_CAT_FAN,
        .handler = api_fan_set,
        .requires_auth = false,
    },
    {
        .name = "fan.mode",
        .description = "Set fan operating mode",
        .category = TS_API_CAT_FAN,
        .handler = api_fan_mode,
        .requires_auth = false,
    },
    {
        .name = "fan.enable",
        .description = "Enable or disable a fan",
        .category = TS_API_CAT_FAN,
        .handler = api_fan_enable,
        .requires_auth = false,
    },
    {
        .name = "fan.curve",
        .description = "Set temperature curve for fan",
        .category = TS_API_CAT_FAN,
        .handler = api_fan_curve,
        .requires_auth = false,
    },
};

esp_err_t ts_api_fan_register(void)
{
    TS_LOGI(TAG, "Registering fan APIs");
    return ts_api_register_multiple(s_fan_endpoints, 
                                    sizeof(s_fan_endpoints) / sizeof(s_fan_endpoints[0]));
}
