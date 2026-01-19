/**
 * @file ts_api_power.c
 * @brief Power Monitoring API Handlers
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_power_monitor.h"
#include "ts_power_policy.h"
#include "ts_log.h"
#include <string.h>

#define TAG "api_power"

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief power.status - Get power monitoring status
 * 
 * Returns: voltage, current, power, thresholds, protection status
 */
static esp_err_t api_power_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    cJSON *data = cJSON_CreateObject();
    
    /* Voltage data */
    ts_power_voltage_data_t voltage_data;
    if (ts_power_monitor_get_voltage_data(&voltage_data) == ESP_OK) {
        cJSON *voltage = cJSON_AddObjectToObject(data, "voltage");
        cJSON_AddNumberToObject(voltage, "supply_v", voltage_data.supply_voltage);
        cJSON_AddNumberToObject(voltage, "adc_raw", voltage_data.raw_adc);
        cJSON_AddNumberToObject(voltage, "timestamp_ms", voltage_data.timestamp);
    }
    
    /* Power chip data */
    ts_power_chip_data_t chip_data;
    if (ts_power_monitor_get_power_chip_data(&chip_data) == ESP_OK) {
        cJSON *chip = cJSON_AddObjectToObject(data, "power_chip");
        cJSON_AddBoolToObject(chip, "valid", chip_data.valid);
        cJSON_AddNumberToObject(chip, "voltage_v", chip_data.voltage);
        cJSON_AddNumberToObject(chip, "current_a", chip_data.current);
        cJSON_AddNumberToObject(chip, "power_w", chip_data.power);
        cJSON_AddBoolToObject(chip, "crc_valid", chip_data.crc_valid);
    }
    
    /* Protection status */
    ts_power_policy_status_t prot_status;
    if (ts_power_policy_get_status(&prot_status) == ESP_OK) {
        cJSON *protection = cJSON_AddObjectToObject(data, "protection");
        cJSON_AddBoolToObject(protection, "running", prot_status.running);
        cJSON_AddStringToObject(protection, "state", 
            ts_power_policy_get_state_name(prot_status.state));
        cJSON_AddNumberToObject(protection, "countdown_sec", prot_status.countdown_remaining_sec);
        cJSON_AddNumberToObject(protection, "current_voltage_v", prot_status.current_voltage);
    }
    
    /* Monitor stats */
    ts_power_monitor_stats_t stats;
    if (ts_power_monitor_get_stats(&stats) == ESP_OK) {
        cJSON *stat = cJSON_AddObjectToObject(data, "stats");
        cJSON_AddNumberToObject(stat, "samples", stats.voltage_samples);
        cJSON_AddNumberToObject(stat, "avg_voltage_v", stats.avg_voltage);
        cJSON_AddNumberToObject(stat, "avg_current_a", stats.avg_current);
        cJSON_AddNumberToObject(stat, "avg_power_w", stats.avg_power);
        cJSON_AddNumberToObject(stat, "uptime_ms", stats.uptime_ms);
    }
    
    cJSON_AddBoolToObject(data, "monitoring_active", ts_power_monitor_is_running());
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief power.voltage - Read current voltage
 * 
 * Params: { "now": true } for immediate read
 */
static esp_err_t api_power_voltage(const cJSON *params, ts_api_result_t *result)
{
    ts_power_voltage_data_t data;
    esp_err_t ret;
    
    const cJSON *now_item = cJSON_GetObjectItem(params, "now");
    bool immediate = cJSON_IsTrue(now_item);
    
    if (immediate) {
        ret = ts_power_monitor_read_voltage_now(&data);
    } else {
        ret = ts_power_monitor_get_voltage_data(&data);
    }
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE, "Failed to read voltage");
        return ret;
    }
    
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "voltage_v", data.supply_voltage);
    cJSON_AddNumberToObject(json, "adc_raw", data.raw_adc);
    cJSON_AddNumberToObject(json, "voltage_mv", data.voltage_mv);
    cJSON_AddNumberToObject(json, "timestamp_ms", data.timestamp);
    
    ts_api_result_ok(result, json);
    return ESP_OK;
}

/**
 * @brief power.protection.set - Configure voltage protection
 * 
 * Params: { "enable": true, "low_threshold": 12.6, "recovery_threshold": 18.0, "shutdown_delay": 60 }
 */
static esp_err_t api_power_protection_set(const cJSON *params, ts_api_result_t *result)
{
    ts_power_policy_config_t config;
    ts_power_policy_get_default_config(&config);
    
    /* Parse config params */
    const cJSON *low_thresh = cJSON_GetObjectItem(params, "low_threshold");
    const cJSON *recovery_thresh = cJSON_GetObjectItem(params, "recovery_threshold");
    const cJSON *shutdown_delay = cJSON_GetObjectItem(params, "shutdown_delay");
    const cJSON *recovery_hold = cJSON_GetObjectItem(params, "recovery_hold");
    const cJSON *enable = cJSON_GetObjectItem(params, "enable");
    
    if (cJSON_IsNumber(low_thresh)) {
        config.low_voltage_threshold = (float)low_thresh->valuedouble;
    }
    if (cJSON_IsNumber(recovery_thresh)) {
        config.recovery_voltage_threshold = (float)recovery_thresh->valuedouble;
    }
    if (cJSON_IsNumber(shutdown_delay)) {
        config.shutdown_delay_sec = (uint32_t)shutdown_delay->valueint;
    }
    if (cJSON_IsNumber(recovery_hold)) {
        config.recovery_hold_sec = (uint32_t)recovery_hold->valueint;
    }
    
    /* Update thresholds */
    esp_err_t ret = ts_power_policy_set_thresholds(
        config.low_voltage_threshold, 
        config.recovery_voltage_threshold);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set thresholds");
        return ret;
    }
    
    if (cJSON_IsNumber(shutdown_delay)) {
        ret = ts_power_policy_set_shutdown_delay(config.shutdown_delay_sec);
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to set shutdown delay");
            return ret;
        }
    }
    
    /* Enable/disable if specified */
    if (cJSON_IsBool(enable)) {
        if (cJSON_IsTrue(enable)) {
            ret = ts_power_policy_start();
        } else {
            ret = ts_power_policy_stop();
        }
        if (ret != ESP_OK) {
            ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to enable/disable protection");
            return ret;
        }
    }
    
    /* Return current status */
    cJSON *data = cJSON_CreateObject();
    float low, recovery;
    ts_power_policy_get_thresholds(&low, &recovery);
    cJSON_AddNumberToObject(data, "low_threshold_v", low);
    cJSON_AddNumberToObject(data, "recovery_threshold_v", recovery);
    cJSON_AddBoolToObject(data, "running", ts_power_policy_is_running());
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief power.protection.status - Get protection status
 */
static esp_err_t api_power_protection_status(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    ts_power_policy_status_t status;
    esp_err_t ret = ts_power_policy_get_status(&status);
    
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to get protection status");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "initialized", status.initialized);
    cJSON_AddBoolToObject(data, "running", status.running);
    cJSON_AddStringToObject(data, "state", ts_power_policy_get_state_name(status.state));
    cJSON_AddNumberToObject(data, "current_voltage_v", status.current_voltage);
    cJSON_AddNumberToObject(data, "countdown_remaining_sec", status.countdown_remaining_sec);
    cJSON_AddNumberToObject(data, "recovery_timer_sec", status.recovery_timer_sec);
    cJSON_AddNumberToObject(data, "protection_count", status.protection_count);
    cJSON_AddNumberToObject(data, "uptime_ms", status.uptime_ms);
    
    /* Device status */
    cJSON *device = cJSON_AddObjectToObject(data, "devices");
    cJSON_AddBoolToObject(device, "agx_powered", status.device_status.agx_powered);
    cJSON_AddBoolToObject(device, "lpmu_powered", status.device_status.lpmu_powered);
    cJSON_AddBoolToObject(device, "agx_connected", status.device_status.agx_connected);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief power.monitor.start - Start power monitoring
 */
static esp_err_t api_power_monitor_start(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    esp_err_t ret = ts_power_monitor_start();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to start monitoring");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "running", true);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief power.monitor.stop - Stop power monitoring
 */
static esp_err_t api_power_monitor_stop(const cJSON *params, ts_api_result_t *result)
{
    (void)params;
    esp_err_t ret = ts_power_monitor_stop();
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to stop monitoring");
        return ret;
    }
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "running", false);
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t s_power_endpoints[] = {
    {
        .name = "power.status",
        .description = "Get power monitoring status",
        .category = TS_API_CAT_POWER,
        .handler = api_power_status,
        .requires_auth = false,
    },
    {
        .name = "power.voltage",
        .description = "Read current voltage",
        .category = TS_API_CAT_POWER,
        .handler = api_power_voltage,
        .requires_auth = false,
    },
    {
        .name = "power.protection.set",
        .description = "Configure voltage protection",
        .category = TS_API_CAT_POWER,
        .handler = api_power_protection_set,
        .requires_auth = true,
    },
    {
        .name = "power.protection.status",
        .description = "Get voltage protection status",
        .category = TS_API_CAT_POWER,
        .handler = api_power_protection_status,
        .requires_auth = false,
    },
    {
        .name = "power.monitor.start",
        .description = "Start power monitoring",
        .category = TS_API_CAT_POWER,
        .handler = api_power_monitor_start,
        .requires_auth = true,
    },
    {
        .name = "power.monitor.stop",
        .description = "Stop power monitoring",
        .category = TS_API_CAT_POWER,
        .handler = api_power_monitor_stop,
        .requires_auth = true,
    },
};

esp_err_t ts_api_power_register(void)
{
    TS_LOGI(TAG, "Registering power APIs");
    return ts_api_register_multiple(s_power_endpoints, 
                                    sizeof(s_power_endpoints) / sizeof(s_power_endpoints[0]));
}
