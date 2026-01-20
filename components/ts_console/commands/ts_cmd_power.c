/**
 * @file ts_cmd_power.c
 * @brief Power Monitor CLI Command
 * 
 * 移植自 robOS power_monitor 控制台命令
 * 
 * 命令格式：
 *   power status              - 显示完整电源系统状态
 *   power voltage             - 读取供电电压 (GPIO18 ADC)
 *   power chip                - 读取电源芯片数据 (GPIO47 UART)
 *   power start               - 启动后台监控任务
 *   power stop                - 停止后台监控任务
 *   power threshold <min> <max> - 设置电压阈值
 *   power interval <ms>       - 设置采样间隔
 *   power stats               - 显示详细统计
 *   power reset               - 重置统计数据
 *   power debug enable|disable - 启用/禁用调试模式
 *   power test                - 测试 ADC 读取
 *   power help                - 显示帮助
 */

#include "ts_cmd_all.h"
#include "ts_console.h"
#include "ts_api.h"
#include "ts_console.h"
#include "esp_console.h"
#include "ts_console.h"
#include "argtable3/argtable3.h"
#include "ts_console.h"
#include "freertos/FreeRTOS.h"
#include "ts_console.h"
#include "freertos/task.h"
#include "ts_console.h"
#include <stdio.h>
#include "ts_console.h"
#include <string.h>
#include "ts_console.h"
#include <stdlib.h>
#include "ts_console.h"

static const char *TAG = "cmd_power";

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 从 cJSON 获取 double 值
 */
static double get_json_double(const cJSON *obj, const char *key, double def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valuedouble : def;
}

/**
 * @brief 从 cJSON 获取 int 值
 */
static int get_json_int(const cJSON *obj, const char *key, int def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(item) ? item->valueint : def;
}

/**
 * @brief 从 cJSON 获取 bool 值
 */
static bool get_json_bool(const cJSON *obj, const char *key, bool def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : def;
}

/**
 * @brief 从 cJSON 获取 string 值
 */
static const char *get_json_string(const cJSON *obj, const char *key, const char *def)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : def;
}

/*===========================================================================*/
/*                          Command Handlers                                  */
/*===========================================================================*/

static int cmd_power_status(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.status", NULL, &result);
    if (ret != ESP_OK || result.code != TS_API_OK || !result.data) {
        ts_console_printf("Failed to get power status: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
    
    cJSON *data = result.data;
    
    ts_console_printf("\n");
    ts_console_printf("╔══════════════════════════════════════════════════════════════╗\n");
    ts_console_printf("║                    Power Monitor Status                       ║\n");
    ts_console_printf("╚══════════════════════════════════════════════════════════════╝\n");
    ts_console_printf("\n");
    
    ts_console_printf("Running: %s\n", get_json_bool(data, "monitoring_active", false) ? "Yes" : "No");
    ts_console_printf("\n");
    
    /* 电压监控数据 */
    const cJSON *voltage = cJSON_GetObjectItem(data, "voltage");
    if (voltage) {
        ts_console_printf("┌─ Voltage Monitoring ─────────────────────────────────────────┐\n");
        ts_console_printf("│  Supply Voltage: %.2f V                                      \n", get_json_double(voltage, "supply_v", 0));
        ts_console_printf("│  ADC Raw Value:  %d                                          \n", get_json_int(voltage, "adc_raw", 0));
        ts_console_printf("│  Timestamp:      %d ms                                       \n", get_json_int(voltage, "timestamp_ms", 0));
        ts_console_printf("└──────────────────────────────────────────────────────────────┘\n");
        ts_console_printf("\n");
    }
    
    /* 电源芯片数据 */
    const cJSON *chip = cJSON_GetObjectItem(data, "power_chip");
    if (chip && get_json_bool(chip, "valid", false)) {
        ts_console_printf("┌─ Power Chip Data ────────────────────────────────────────────┐\n");
        ts_console_printf("│  Voltage:    %.2f V                                          \n", get_json_double(chip, "voltage_v", 0));
        ts_console_printf("│  Current:    %.3f A                                          \n", get_json_double(chip, "current_a", 0));
        ts_console_printf("│  Power:      %.2f W                                          \n", get_json_double(chip, "power_w", 0));
        ts_console_printf("│  CRC:        %s                                              \n", get_json_bool(chip, "crc_valid", false) ? "OK" : "FAIL");
        ts_console_printf("└──────────────────────────────────────────────────────────────┘\n");
        ts_console_printf("\n");
    } else {
        ts_console_printf("┌─ Power Chip Data ────────────────────────────────────────────┐\n");
        ts_console_printf("│  No data received from power chip                            \n");
        ts_console_printf("│  (Check GPIO47 UART connection)                              \n");
        ts_console_printf("└──────────────────────────────────────────────────────────────┘\n");
        ts_console_printf("\n");
    }
    
    /* 统计信息 */
    const cJSON *stats = cJSON_GetObjectItem(data, "stats");
    if (stats) {
        double uptime_ms = get_json_double(stats, "uptime_ms", 0);
        ts_console_printf("┌─ Statistics ─────────────────────────────────────────────────┐\n");
        ts_console_printf("│  Uptime:              %.0f ms (%.1f hours)                   \n", 
               uptime_ms, uptime_ms / 3600000.0);
        ts_console_printf("│  Voltage Samples:     %d                                     \n", get_json_int(stats, "samples", 0));
        ts_console_printf("│  Average Voltage:     %.2f V                                 \n", get_json_double(stats, "avg_voltage_v", 0));
        ts_console_printf("│  Average Current:     %.3f A                                 \n", get_json_double(stats, "avg_current_a", 0));
        ts_console_printf("│  Average Power:       %.2f W                                 \n", get_json_double(stats, "avg_power_w", 0));
        ts_console_printf("└──────────────────────────────────────────────────────────────┘\n");
    }
    
    ts_api_result_free(&result);
    return 0;
}

static int cmd_power_voltage(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "now", true);
    
    esp_err_t ret = ts_api_call("power.voltage", params, &result);
    cJSON_Delete(params);
    
    if (ret != ESP_OK || result.code != TS_API_OK || !result.data) {
        ts_console_printf("Failed to get voltage data: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
    
    cJSON *data = result.data;
    ts_console_printf("Supply Voltage: %.2f V\n", get_json_double(data, "voltage_v", 0));
    ts_console_printf("ADC Raw:        %d\n", get_json_int(data, "adc_raw", 0));
    ts_console_printf("ADC Voltage:    %d mV\n", get_json_int(data, "voltage_mv", 0));
    ts_console_printf("Timestamp:      %d ms\n", get_json_int(data, "timestamp_ms", 0));
    
    ts_api_result_free(&result);
    return 0;
}

static int cmd_power_chip(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.chip", NULL, &result);
    if (ret != ESP_OK || result.code != TS_API_OK || !result.data) {
        ts_console_printf("Failed to get power chip data: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
    
    cJSON *data = result.data;
    
    if (get_json_int(data, "timestamp_ms", 0) == 0) {
        ts_console_printf("No power chip data available\n");
        ts_console_printf("(Check GPIO47 UART connection)\n");
        ts_api_result_free(&result);
        return 1;
    }
    
    ts_console_printf("Power Chip Data:\n");
    ts_console_printf("  Voltage:   %.2f V\n", get_json_double(data, "voltage_v", 0));
    ts_console_printf("  Current:   %.3f A\n", get_json_double(data, "current_a", 0));
    ts_console_printf("  Power:     %.2f W\n", get_json_double(data, "power_w", 0));
    ts_console_printf("  Valid:     %s\n", get_json_bool(data, "valid", false) ? "Yes" : "No");
    ts_console_printf("  CRC:       %s\n", get_json_bool(data, "crc_valid", false) ? "OK" : "FAIL");
    
    const cJSON *raw = cJSON_GetObjectItem(data, "raw_data");
    if (cJSON_IsArray(raw) && cJSON_GetArraySize(raw) >= 4) {
        ts_console_printf("  Raw Data:  0x%02X 0x%02X 0x%02X 0x%02X\n",
               cJSON_GetArrayItem(raw, 0)->valueint,
               cJSON_GetArrayItem(raw, 1)->valueint,
               cJSON_GetArrayItem(raw, 2)->valueint,
               cJSON_GetArrayItem(raw, 3)->valueint);
    }
    ts_console_printf("  Timestamp: %d ms\n", get_json_int(data, "timestamp_ms", 0));
    
    ts_api_result_free(&result);
    return 0;
}

static int cmd_power_start(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.monitor.start", NULL, &result);
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Power monitor started\n");
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to start power monitor: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_stop(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.monitor.stop", NULL, &result);
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Power monitor stopped\n");
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to stop power monitor: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_threshold(float min_v, float max_v)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "min_v", min_v);
    cJSON_AddNumberToObject(params, "max_v", max_v);
    
    esp_err_t ret = ts_api_call("power.threshold.set", params, &result);
    cJSON_Delete(params);
    
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Voltage thresholds set: %.2f V - %.2f V\n", min_v, max_v);
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to set thresholds: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_interval(uint32_t interval_ms)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddNumberToObject(params, "interval_ms", (int)interval_ms);
    
    esp_err_t ret = ts_api_call("power.interval.set", params, &result);
    cJSON_Delete(params);
    
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Sample interval set to %lu ms\n", (unsigned long)interval_ms);
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to set interval: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_stats(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.stats", NULL, &result);
    if (ret != ESP_OK || result.code != TS_API_OK || !result.data) {
        ts_console_printf("Failed to get statistics: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
    
    cJSON *data = result.data;
    double uptime_ms = get_json_double(data, "uptime_ms", 0);
    int power_chip_packets = get_json_int(data, "power_chip_packets", 0);
    int crc_errors = get_json_int(data, "crc_errors", 0);
    
    ts_console_printf("Power Monitor Statistics:\n");
    ts_console_printf("=========================\n");
    ts_console_printf("Uptime:              %.0f ms (%.1f hours)\n", uptime_ms, uptime_ms / 3600000.0);
    ts_console_printf("Voltage Samples:     %d\n", get_json_int(data, "voltage_samples", 0));
    ts_console_printf("Power Chip Packets:  %d\n", power_chip_packets);
    ts_console_printf("CRC Errors:          %d (%.1f%%)\n", crc_errors,
           power_chip_packets > 0 ? (crc_errors * 100.0f / power_chip_packets) : 0.0f);
    ts_console_printf("Timeout Errors:      %d\n", get_json_int(data, "timeout_errors", 0));
    ts_console_printf("Threshold Violations: %d\n", get_json_int(data, "threshold_violations", 0));
    ts_console_printf("Average Voltage:     %.2f V\n", get_json_double(data, "avg_voltage_v", 0));
    ts_console_printf("Average Current:     %.3f A\n", get_json_double(data, "avg_current_a", 0));
    ts_console_printf("Average Power:       %.2f W\n", get_json_double(data, "avg_power_w", 0));
    
    ts_api_result_free(&result);
    return 0;
}

static int cmd_power_reset(void)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("power.stats.reset", NULL, &result);
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Statistics reset\n");
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to reset statistics: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_debug(bool enable)
{
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    cJSON *params = cJSON_CreateObject();
    cJSON_AddBoolToObject(params, "enable", enable);
    
    esp_err_t ret = ts_api_call("power.debug", params, &result);
    cJSON_Delete(params);
    
    if (ret == ESP_OK && result.code == TS_API_OK) {
        ts_console_printf("Protocol debug %s\n", enable ? "enabled" : "disabled");
        ts_api_result_free(&result);
        return 0;
    } else {
        ts_console_printf("Failed to set debug mode: %s\n", 
               result.message ? result.message : "Unknown error");
        ts_api_result_free(&result);
        return 1;
    }
}

static int cmd_power_test(void)
{
    ts_console_printf("Testing ADC reading...\n");
    ts_console_printf("======================\n");
    
    for (int i = 0; i < 10; i++) {
        ts_api_result_t result;
        ts_api_result_init(&result);
        
        cJSON *params = cJSON_CreateObject();
        cJSON_AddBoolToObject(params, "now", true);
        
        esp_err_t ret = ts_api_call("power.voltage", params, &result);
        cJSON_Delete(params);
        
        if (ret == ESP_OK && result.code == TS_API_OK && result.data) {
            ts_console_printf("Reading %d: raw=%d, mv=%d, actual=%.2f V\n",
                   i + 1, 
                   get_json_int(result.data, "adc_raw", 0),
                   get_json_int(result.data, "voltage_mv", 0),
                   get_json_double(result.data, "voltage_v", 0));
        } else {
            ts_console_printf("Reading %d: FAILED\n", i + 1);
        }
        ts_api_result_free(&result);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return 0;
}

static void cmd_power_help(void)
{
    ts_console_printf("\n");
    ts_console_printf("==================== Power Monitor Commands ====================\n");
    ts_console_printf("\n");
    ts_console_printf("Basic Commands:\n");
    ts_console_printf("  power status                - Show full power system status\n");
    ts_console_printf("  power voltage               - Read supply voltage (GPIO18 ADC)\n");
    ts_console_printf("  power chip                  - Read power chip data (GPIO47 UART)\n");
    ts_console_printf("\n");
    ts_console_printf("Monitoring Control:\n");
    ts_console_printf("  power start                 - Start background monitoring task\n");
    ts_console_printf("  power stop                  - Stop background monitoring task\n");
    ts_console_printf("  power threshold <min> <max> - Set voltage thresholds (V)\n");
    ts_console_printf("  power interval <ms>         - Set sampling interval (100-60000 ms)\n");
    ts_console_printf("\n");
    ts_console_printf("Debug Tools:\n");
    ts_console_printf("  power debug enable|disable  - Enable/disable protocol debug\n");
    ts_console_printf("  power test                  - Test ADC reading\n");
    ts_console_printf("  power stats                 - Show detailed statistics\n");
    ts_console_printf("  power reset                 - Reset statistics\n");
    ts_console_printf("  power help                  - Show this help\n");
    ts_console_printf("\n");
    ts_console_printf("Hardware Configuration:\n");
    ts_console_printf("  GPIO18: Supply voltage monitor (ADC2_CH7, divider 11.4:1)\n");
    ts_console_printf("  GPIO47: Power chip UART RX (9600 8N1, [0xFF][V][I][CRC])\n");
    ts_console_printf("\n");
    ts_console_printf("Examples:\n");
    ts_console_printf("  power voltage               - Read current supply voltage\n");
    ts_console_printf("  power threshold 10 28       - Set thresholds to 10V-28V\n");
    ts_console_printf("  power interval 2000         - Set 2 second sample interval\n");
    ts_console_printf("  power debug enable          - Enable protocol debugging\n");
    ts_console_printf("\n");
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_power_main(int argc, char **argv)
{
    if (argc < 2) {
        /* 无参数时显示状态 */
        return cmd_power_status();
    }
    
    const char *subcmd = argv[1];
    
    if (strcmp(subcmd, "status") == 0) {
        return cmd_power_status();
    }
    else if (strcmp(subcmd, "voltage") == 0) {
        return cmd_power_voltage();
    }
    else if (strcmp(subcmd, "chip") == 0 || strcmp(subcmd, "read") == 0) {
        return cmd_power_chip();
    }
    else if (strcmp(subcmd, "start") == 0) {
        return cmd_power_start();
    }
    else if (strcmp(subcmd, "stop") == 0) {
        return cmd_power_stop();
    }
    else if (strcmp(subcmd, "threshold") == 0 || strcmp(subcmd, "thresholds") == 0) {
        if (argc >= 4) {
            float min_v = atof(argv[2]);
            float max_v = atof(argv[3]);
            return cmd_power_threshold(min_v, max_v);
        } else {
            /* 显示当前阈值 - 从 status API 获取 */
            ts_api_result_t result;
            ts_api_result_init(&result);
            if (ts_api_call("power.status", NULL, &result) == ESP_OK && result.data) {
                const cJSON *voltage = cJSON_GetObjectItem(result.data, "voltage");
                if (voltage) {
                    /* 阈值信息从 status 中获取（如果有的话） */
                }
            }
            ts_api_result_free(&result);
            ts_console_printf("Usage: power threshold <min_voltage> <max_voltage>\n");
            return 1;
        }
    }
    else if (strcmp(subcmd, "interval") == 0) {
        if (argc >= 3) {
            uint32_t interval = atoi(argv[2]);
            return cmd_power_interval(interval);
        } else {
            ts_console_printf("Usage: power interval <milliseconds>\n");
            return 1;
        }
    }
    else if (strcmp(subcmd, "stats") == 0) {
        return cmd_power_stats();
    }
    else if (strcmp(subcmd, "reset") == 0) {
        return cmd_power_reset();
    }
    else if (strcmp(subcmd, "debug") == 0) {
        if (argc >= 3) {
            bool enable = (strcmp(argv[2], "enable") == 0 || strcmp(argv[2], "on") == 0);
            return cmd_power_debug(enable);
        } else {
            ts_console_printf("Usage: power debug enable|disable\n");
            return 1;
        }
    }
    else if (strcmp(subcmd, "test") == 0) {
        return cmd_power_test();
    }
    else if (strcmp(subcmd, "help") == 0) {
        cmd_power_help();
        return 0;
    }
    else {
        ts_console_printf("Unknown command: %s\n", subcmd);
        ts_console_printf("Use 'power help' for usage information\n");
        return 1;
    }
}

/*===========================================================================*/
/*                          Command Registration                              */
/*===========================================================================*/

esp_err_t ts_cmd_power_register(void)
{
    const esp_console_cmd_t cmd = {
        .command = "power",
        .help = "Power monitor: power status|voltage|chip|start|stop|threshold|stats|help",
        .hint = NULL,
        .func = &cmd_power_main,
    };
    
    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        ts_console_printf("Failed to register power command: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/* 服务注册接口 */
esp_err_t ts_power_monitor_register_commands(void)
{
    return ts_cmd_power_register();
}
