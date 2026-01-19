/**
 * @file ts_api_config.c
 * @brief Configuration API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_api.h"
#include "ts_config.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_config"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief config.get - Get configuration value
 */
static esp_err_t api_config_get(const cJSON *params, ts_api_result_t *result)
{
    if (params == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *key = cJSON_GetObjectItem(params, "key");
    if (key == NULL || !cJSON_IsString(key)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'key' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", key->valuestring);
    
    /* Try to get as different types */
    int64_t int_val;
    bool bool_val;
    double dbl_val;
    char str_val[256];
    
    if (ts_config_get_int64(key->valuestring, &int_val, 0) == ESP_OK) {
        cJSON_AddNumberToObject(data, "value", (double)int_val);
        cJSON_AddStringToObject(data, "type", "int");
    } else if (ts_config_get_bool(key->valuestring, &bool_val, false) == ESP_OK) {
        cJSON_AddBoolToObject(data, "value", bool_val);
        cJSON_AddStringToObject(data, "type", "bool");
    } else if (ts_config_get_double(key->valuestring, &dbl_val, 0.0) == ESP_OK) {
        cJSON_AddNumberToObject(data, "value", dbl_val);
        cJSON_AddStringToObject(data, "type", "double");
    } else if (ts_config_get_string(key->valuestring, str_val, sizeof(str_val), NULL) == ESP_OK) {
        cJSON_AddStringToObject(data, "value", str_val);
        cJSON_AddStringToObject(data, "type", "string");
    } else {
        cJSON_Delete(data);
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found");
        return ESP_ERR_NOT_FOUND;
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief config.set - Set configuration value
 */
static esp_err_t api_config_set(const cJSON *params, ts_api_result_t *result)
{
    if (params == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *key = cJSON_GetObjectItem(params, "key");
    const cJSON *value = cJSON_GetObjectItem(params, "value");
    
    if (key == NULL || !cJSON_IsString(key)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'key' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (value == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'value' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ESP_OK;
    
    if (cJSON_IsBool(value)) {
        ret = ts_config_set_bool(key->valuestring, cJSON_IsTrue(value));
    } else if (cJSON_IsNumber(value)) {
        /* Check if it's an integer or float */
        double d = value->valuedouble;
        if (d == (int64_t)d) {
            ret = ts_config_set_int64(key->valuestring, (int64_t)d);
        } else {
            ret = ts_config_set_double(key->valuestring, d);
        }
    } else if (cJSON_IsString(value)) {
        ret = ts_config_set_string(key->valuestring, value->valuestring);
    } else {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Unsupported value type");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set config");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", key->valuestring);
    cJSON_AddBoolToObject(data, "success", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief config.delete - Delete configuration value
 */
static esp_err_t api_config_delete(const cJSON *params, ts_api_result_t *result)
{
    if (params == NULL) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *key = cJSON_GetObjectItem(params, "key");
    if (key == NULL || !cJSON_IsString(key)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'key' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = ts_config_delete(key->valuestring);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_NOT_FOUND, "Key not found or delete failed");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "key", key->valuestring);
    cJSON_AddBoolToObject(data, "deleted", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief config.list - List all configuration keys
 */
static esp_err_t api_config_list(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    cJSON *data = cJSON_CreateObject();
    cJSON *items = cJSON_AddArrayToObject(data, "items");
    
    /* Get config statistics */
    size_t total_count = 0, nvs_count = 0, file_count = 0;
    ts_config_get_stats(&total_count, &nvs_count, &file_count);
    cJSON_AddNumberToObject(data, "total_keys", (double)total_count);
    cJSON_AddNumberToObject(data, "nvs_keys", (double)nvs_count);
    cJSON_AddNumberToObject(data, "file_keys", (double)file_count);
    
    /* Note: Full iteration would require iterator API */
    /* For now just return stats */
    (void)items;
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief config.save - Save configuration to persistent storage
 */
static esp_err_t api_config_save(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    
    esp_err_t ret = ts_config_save();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to save config");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "saved", true);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_api_config_register(void)
{
    static const ts_api_endpoint_t config_apis[] = {
        {
            .name = "config.get",
            .description = "Get configuration value",
            .category = TS_API_CAT_CONFIG,
            .handler = api_config_get,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "config.set",
            .description = "Set configuration value",
            .category = TS_API_CAT_CONFIG,
            .handler = api_config_set,
            .requires_auth = true,
            .permission = "config.write"
        },
        {
            .name = "config.delete",
            .description = "Delete configuration value",
            .category = TS_API_CAT_CONFIG,
            .handler = api_config_delete,
            .requires_auth = true,
            .permission = "config.admin"
        },
        {
            .name = "config.list",
            .description = "List configuration keys",
            .category = TS_API_CAT_CONFIG,
            .handler = api_config_list,
            .requires_auth = false,
            .permission = NULL
        },
        {
            .name = "config.save",
            .description = "Save configuration to storage",
            .category = TS_API_CAT_CONFIG,
            .handler = api_config_save,
            .requires_auth = true,
            .permission = "config.write"
        }
    };
    
    esp_err_t ret = ts_api_register_multiple(config_apis, 
                                              sizeof(config_apis) / sizeof(config_apis[0]));
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Config API registered");
    }
    return ret;
}
