/**
 * @file ts_api_temp.c
 * @brief Temperature API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_temp_source.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_temp"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief temp.sources - Get all temperature sources info
 */
static esp_err_t api_temp_sources(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    ts_temp_status_t status;
    esp_err_t ret = ts_temp_get_status(&status);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get temp status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "initialized", status.initialized);
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(status.active_source));
    cJSON_AddNumberToObject(data, "current_temp_01c", status.current_temp);
    cJSON_AddNumberToObject(data, "current_temp_c", status.current_temp / 10.0);
    cJSON_AddBoolToObject(data, "manual_mode", status.manual_mode);
    cJSON_AddNumberToObject(data, "provider_count", status.provider_count);
    
    /* Provider list */
    cJSON *providers = cJSON_AddArrayToObject(data, "providers");
    for (uint32_t i = 0; i < status.provider_count; i++) {
        ts_temp_provider_info_t *p = &status.providers[i];
        cJSON *provider = cJSON_CreateObject();
        cJSON_AddStringToObject(provider, "name", p->name ? p->name : "unknown");
        cJSON_AddStringToObject(provider, "type", ts_temp_source_type_to_str(p->type));
        cJSON_AddBoolToObject(provider, "active", p->active);
        cJSON_AddNumberToObject(provider, "last_value_01c", p->last_value);
        cJSON_AddNumberToObject(provider, "last_value_c", p->last_value / 10.0);
        cJSON_AddNumberToObject(provider, "last_update_ms", p->last_update_ms);
        cJSON_AddNumberToObject(provider, "update_count", p->update_count);
        cJSON_AddItemToArray(providers, provider);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.read - Read current effective temperature
 * 
 * Params: { "source": "agx_auto" } for specific source
 */
static esp_err_t api_temp_read(const cJSON *params, ts_api_result_t *result)
{
    ts_temp_data_t data;
    
    const cJSON *source_item = cJSON_GetObjectItem(params, "source");
    
    if (cJSON_IsString(source_item) && source_item->valuestring) {
        /* Read from specific source */
        ts_temp_source_type_t source_type = TS_TEMP_SOURCE_DEFAULT;
        const char *src_str = source_item->valuestring;
        
        if (strcmp(src_str, "default") == 0) {
            source_type = TS_TEMP_SOURCE_DEFAULT;
        } else if (strcmp(src_str, "sensor_local") == 0 || strcmp(src_str, "local") == 0) {
            source_type = TS_TEMP_SOURCE_SENSOR_LOCAL;
        } else if (strcmp(src_str, "agx_auto") == 0 || strcmp(src_str, "agx") == 0) {
            source_type = TS_TEMP_SOURCE_AGX_AUTO;
        } else if (strcmp(src_str, "manual") == 0) {
            source_type = TS_TEMP_SOURCE_MANUAL;
        } else {
            ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid source type");
            return ESP_ERR_INVALID_ARG;
        }
        
        esp_err_t ret = ts_temp_get_by_source(source_type, &data);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Source not found or no data");
            return ret;
        }
    } else {
        /* Read effective temperature */
        int16_t temp = ts_temp_get_effective(&data);
        (void)temp;  // data already filled
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "temperature_01c", data.value);
    cJSON_AddNumberToObject(json, "temperature_c", data.value / 10.0);
    cJSON_AddStringToObject(json, "source", ts_temp_source_type_to_str(data.source));
    cJSON_AddNumberToObject(json, "timestamp_ms", data.timestamp_ms);
    cJSON_AddBoolToObject(json, "valid", data.valid);
    
    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief temp.manual - Set/get manual temperature mode
 * 
 * Params: { "enable": true, "temperature_c": 45.0 }
 */
static esp_err_t api_temp_manual(const cJSON *params, ts_api_result_t *result)
{
    const cJSON *enable_item = cJSON_GetObjectItem(params, "enable");
    const cJSON *temp_item = cJSON_GetObjectItem(params, "temperature_c");
    const cJSON *temp_01c_item = cJSON_GetObjectItem(params, "temperature_01c");
    
    esp_err_t ret = ESP_OK;
    
    /* Set manual temperature if provided */
    if (cJSON_IsNumber(temp_item)) {
        int16_t temp_01c = (int16_t)(temp_item->valuedouble * 10.0);
        ret = ts_temp_set_manual(temp_01c);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual temperature");
            return ret;
        }
    } else if (cJSON_IsNumber(temp_01c_item)) {
        ret = ts_temp_set_manual((int16_t)temp_01c_item->valueint);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual temperature");
            return ret;
        }
    }
    
    /* Enable/disable manual mode if specified */
    if (cJSON_IsBool(enable_item)) {
        ret = ts_temp_set_manual_mode(cJSON_IsTrue(enable_item));
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set manual mode");
            return ret;
        }
    }
    
    /* Return current status */
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "manual_mode", ts_temp_is_manual_mode());
    
    ts_temp_data_t temp_data;
    ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "current_temp_01c", temp_data.value);
    cJSON_AddNumberToObject(data, "current_temp_c", temp_data.value / 10.0);
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(temp_data.source));
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief temp.status - Get temperature system status summary
 */
static esp_err_t api_temp_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *data = cJSON_CreateObject();
    
    /* Basic status */
    cJSON_AddBoolToObject(data, "initialized", ts_temp_source_is_initialized());
    cJSON_AddBoolToObject(data, "manual_mode", ts_temp_is_manual_mode());
    cJSON_AddStringToObject(data, "active_source", ts_temp_source_type_to_str(ts_temp_get_active_source()));
    
    /* Current effective temperature */
    ts_temp_data_t temp_data;
    int16_t temp = ts_temp_get_effective(&temp_data);
    cJSON_AddNumberToObject(data, "temperature_01c", temp);
    cJSON_AddNumberToObject(data, "temperature_c", temp / 10.0);
    cJSON_AddBoolToObject(data, "valid", temp_data.valid);
    cJSON_AddNumberToObject(data, "timestamp_ms", temp_data.timestamp_ms);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_temp_endpoints[] = {
    {
        .name = "temp.sources",
        .description = "Get all temperature sources info",
        .category = TS_API_CAT_DEVICE,  // Use DEVICE category
        .handler = api_temp_sources,
        .requires_auth = false,
    },
    {
        .name = "temp.read",
        .description = "Read current temperature",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_read,
        .requires_auth = false,
    },
    {
        .name = "temp.manual",
        .description = "Set/get manual temperature mode",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_manual,
        .requires_auth = true,
    },
    {
        .name = "temp.status",
        .description = "Get temperature system status",
        .category = TS_API_CAT_DEVICE,
        .handler = api_temp_status,
        .requires_auth = false,
    },
};

esp_err_t ts_api_temp_register(void)
{
    TS_LOGI(TAG, "Registering temperature APIs");
    return ts_api_register_multiple(s_temp_endpoints, 
                                    sizeof(s_temp_endpoints) / sizeof(s_temp_endpoints[0]));
}
