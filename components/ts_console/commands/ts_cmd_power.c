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
#include "ts_power_monitor.h"
#include "ts_api.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "cmd_power";

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 确保电源监控已初始化
 * @return true 如果已初始化或初始化成功
 */
static bool ensure_power_monitor_initialized(void)
{
    if (ts_power_monitor_is_running()) {
        return true;
    }
    
    /* 尝试初始化 */
    esp_err_t ret = ts_power_monitor_init(NULL);
    if (ret == ESP_OK) {
        return true;
    }
    
    /* 如果已经初始化过（但未运行），也返回 true */
    if (ret == ESP_ERR_INVALID_STATE) {
        return true;
    }
    
    printf("Failed to initialize power monitor: %s\n", esp_err_to_name(ret));
    return false;
}

/*===========================================================================*/
/*                          Command Handlers                                  */
/*===========================================================================*/

static int cmd_power_status(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    ts_power_voltage_data_t voltage_data;
    ts_power_chip_data_t power_data;
    ts_power_monitor_stats_t stats;
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    Power Monitor Status                       ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    printf("Running: %s\n", ts_power_monitor_is_running() ? "Yes" : "No");
    printf("\n");
    
    /* 电压监控数据 */
    if (ts_power_monitor_get_voltage_data(&voltage_data) == ESP_OK) {
        printf("┌─ Voltage Monitoring ─────────────────────────────────────────┐\n");
        printf("│  Supply Voltage: %.2f V                                      \n", voltage_data.supply_voltage);
        printf("│  ADC Raw Value:  %d                                          \n", voltage_data.raw_adc);
        printf("│  ADC Voltage:    %d mV                                       \n", voltage_data.voltage_mv);
        printf("│  Timestamp:      %lu ms                                      \n", (unsigned long)voltage_data.timestamp);
        
        float min_thresh, max_thresh;
        if (ts_power_monitor_get_voltage_thresholds(&min_thresh, &max_thresh) == ESP_OK) {
            printf("│  Thresholds:     %.2f V - %.2f V                             \n", min_thresh, max_thresh);
        }
        
        uint32_t interval;
        if (ts_power_monitor_get_sample_interval(&interval) == ESP_OK) {
            printf("│  Sample Interval: %lu ms                                     \n", (unsigned long)interval);
        }
        printf("└──────────────────────────────────────────────────────────────┘\n");
        printf("\n");
    }
    
    /* 电源芯片数据 */
    if (ts_power_monitor_get_power_chip_data(&power_data) == ESP_OK && power_data.timestamp > 0) {
        printf("┌─ Power Chip Data ────────────────────────────────────────────┐\n");
        printf("│  Voltage:    %.2f V                                          \n", power_data.voltage);
        printf("│  Current:    %.3f A                                          \n", power_data.current);
        printf("│  Power:      %.2f W                                          \n", power_data.power);
        printf("│  Valid:      %s                                              \n", power_data.valid ? "Yes" : "No");
        printf("│  CRC:        %s                                              \n", power_data.crc_valid ? "OK" : "FAIL");
        printf("│  Raw Data:   0x%02X 0x%02X 0x%02X 0x%02X                      \n",
               power_data.raw_data[0], power_data.raw_data[1],
               power_data.raw_data[2], power_data.raw_data[3]);
        printf("│  Timestamp:  %lu ms                                          \n", (unsigned long)power_data.timestamp);
        printf("└──────────────────────────────────────────────────────────────┘\n");
        printf("\n");
    } else {
        printf("┌─ Power Chip Data ────────────────────────────────────────────┐\n");
        printf("│  No data received from power chip                            \n");
        printf("│  (Check GPIO47 UART connection)                              \n");
        printf("└──────────────────────────────────────────────────────────────┘\n");
        printf("\n");
    }
    
    /* 统计信息 */
    if (ts_power_monitor_get_stats(&stats) == ESP_OK) {
        printf("┌─ Statistics ─────────────────────────────────────────────────┐\n");
        printf("│  Uptime:              %llu ms (%.1f hours)                   \n", 
               stats.uptime_ms, stats.uptime_ms / 3600000.0);
        printf("│  Voltage Samples:     %lu                                    \n", (unsigned long)stats.voltage_samples);
        printf("│  Power Chip Packets:  %lu                                    \n", (unsigned long)stats.power_chip_packets);
        printf("│  CRC Errors:          %lu (%.1f%%)                           \n", 
               (unsigned long)stats.crc_errors,
               stats.power_chip_packets > 0 ? (stats.crc_errors * 100.0f / stats.power_chip_packets) : 0.0f);
        printf("│  Threshold Violations: %lu                                   \n", (unsigned long)stats.threshold_violations);
        printf("│  Average Voltage:     %.2f V                                 \n", stats.avg_voltage);
        printf("│  Average Current:     %.3f A                                 \n", stats.avg_current);
        printf("│  Average Power:       %.2f W                                 \n", stats.avg_power);
        printf("└──────────────────────────────────────────────────────────────┘\n");
    }
    
    return 0;
}

static int cmd_power_voltage(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    /* 直接读取一次 ADC */
    ts_power_voltage_data_t data;
    esp_err_t ret = ts_power_monitor_read_voltage_now(&data);
    if (ret != ESP_OK) {
        /* 回退到缓存数据 */
        ret = ts_power_monitor_get_voltage_data(&data);
    }
    
    if (ret != ESP_OK) {
        printf("Failed to get voltage data: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Supply Voltage: %.2f V\n", data.supply_voltage);
    printf("ADC Raw:        %d\n", data.raw_adc);
    printf("ADC Voltage:    %d mV\n", data.voltage_mv);
    printf("Timestamp:      %lu ms\n", (unsigned long)data.timestamp);
    
    return 0;
}

static int cmd_power_chip(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    ts_power_chip_data_t data;
    
    if (ts_power_monitor_get_power_chip_data(&data) != ESP_OK) {
        printf("Failed to get power chip data\n");
        return 1;
    }
    
    if (data.timestamp == 0) {
        printf("No power chip data available\n");
        printf("(Check GPIO47 UART connection)\n");
        return 1;
    }
    
    printf("Power Chip Data:\n");
    printf("  Voltage:   %.2f V\n", data.voltage);
    printf("  Current:   %.3f A\n", data.current);
    printf("  Power:     %.2f W\n", data.power);
    printf("  Valid:     %s\n", data.valid ? "Yes" : "No");
    printf("  CRC:       %s\n", data.crc_valid ? "OK" : "FAIL");
    printf("  Raw Data:  0x%02X 0x%02X 0x%02X 0x%02X\n",
           data.raw_data[0], data.raw_data[1], data.raw_data[2], data.raw_data[3]);
    printf("  Timestamp: %lu ms\n", (unsigned long)data.timestamp);
    
    return 0;
}

static int cmd_power_start(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    esp_err_t ret = ts_power_monitor_start();
    if (ret == ESP_OK) {
        printf("Power monitor started\n");
        return 0;
    } else {
        printf("Failed to start power monitor: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_stop(void)
{
    esp_err_t ret = ts_power_monitor_stop();
    if (ret == ESP_OK) {
        printf("Power monitor stopped\n");
        return 0;
    } else {
        printf("Failed to stop power monitor: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_threshold(float min_v, float max_v)
{
    esp_err_t ret = ts_power_monitor_set_voltage_thresholds(min_v, max_v);
    if (ret == ESP_OK) {
        printf("Voltage thresholds set: %.2f V - %.2f V\n", min_v, max_v);
        return 0;
    } else {
        printf("Failed to set thresholds: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_interval(uint32_t interval_ms)
{
    esp_err_t ret = ts_power_monitor_set_sample_interval(interval_ms);
    if (ret == ESP_OK) {
        printf("Sample interval set to %lu ms\n", (unsigned long)interval_ms);
        return 0;
    } else {
        printf("Failed to set interval: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_stats(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    ts_power_monitor_stats_t stats;
    
    if (ts_power_monitor_get_stats(&stats) != ESP_OK) {
        printf("Failed to get statistics\n");
        return 1;
    }
    
    printf("Power Monitor Statistics:\n");
    printf("=========================\n");
    printf("Uptime:              %llu ms (%.1f hours)\n", stats.uptime_ms, stats.uptime_ms / 3600000.0);
    printf("Voltage Samples:     %lu\n", (unsigned long)stats.voltage_samples);
    printf("Power Chip Packets:  %lu\n", (unsigned long)stats.power_chip_packets);
    printf("CRC Errors:          %lu (%.1f%%)\n", (unsigned long)stats.crc_errors,
           stats.power_chip_packets > 0 ? (stats.crc_errors * 100.0f / stats.power_chip_packets) : 0.0f);
    printf("Timeout Errors:      %lu\n", (unsigned long)stats.timeout_errors);
    printf("Threshold Violations: %lu\n", (unsigned long)stats.threshold_violations);
    printf("Average Voltage:     %.2f V\n", stats.avg_voltage);
    printf("Average Current:     %.3f A\n", stats.avg_current);
    printf("Average Power:       %.2f W\n", stats.avg_power);
    
    return 0;
}

static int cmd_power_reset(void)
{
    esp_err_t ret = ts_power_monitor_reset_stats();
    if (ret == ESP_OK) {
        printf("Statistics reset\n");
        return 0;
    } else {
        printf("Failed to reset statistics: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_debug(bool enable)
{
    esp_err_t ret = ts_power_monitor_set_debug_mode(enable);
    if (ret == ESP_OK) {
        printf("Protocol debug %s\n", enable ? "enabled" : "disabled");
        return 0;
    } else {
        printf("Failed to set debug mode: %s\n", esp_err_to_name(ret));
        return 1;
    }
}

static int cmd_power_test(void)
{
    if (!ensure_power_monitor_initialized()) {
        return 1;
    }
    
    printf("Testing ADC reading...\n");
    printf("======================\n");
    
    ts_power_voltage_data_t data;
    
    for (int i = 0; i < 10; i++) {
        if (ts_power_monitor_read_voltage_now(&data) == ESP_OK) {
            printf("Reading %d: raw=%d, mv=%d, actual=%.2f V\n",
                   i + 1, data.raw_adc, data.voltage_mv, data.supply_voltage);
        } else {
            printf("Reading %d: FAILED\n", i + 1);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return 0;
}

static void cmd_power_help(void)
{
    printf("\n");
    printf("==================== Power Monitor Commands ====================\n");
    printf("\n");
    printf("Basic Commands:\n");
    printf("  power status                - Show full power system status\n");
    printf("  power voltage               - Read supply voltage (GPIO18 ADC)\n");
    printf("  power chip                  - Read power chip data (GPIO47 UART)\n");
    printf("\n");
    printf("Monitoring Control:\n");
    printf("  power start                 - Start background monitoring task\n");
    printf("  power stop                  - Stop background monitoring task\n");
    printf("  power threshold <min> <max> - Set voltage thresholds (V)\n");
    printf("  power interval <ms>         - Set sampling interval (100-60000 ms)\n");
    printf("\n");
    printf("Debug Tools:\n");
    printf("  power debug enable|disable  - Enable/disable protocol debug\n");
    printf("  power test                  - Test ADC reading\n");
    printf("  power stats                 - Show detailed statistics\n");
    printf("  power reset                 - Reset statistics\n");
    printf("  power help                  - Show this help\n");
    printf("\n");
    printf("Hardware Configuration:\n");
    printf("  GPIO18: Supply voltage monitor (ADC2_CH7, divider 11.4:1)\n");
    printf("  GPIO47: Power chip UART RX (9600 8N1, [0xFF][V][I][CRC])\n");
    printf("\n");
    printf("Examples:\n");
    printf("  power voltage               - Read current supply voltage\n");
    printf("  power threshold 10 28       - Set thresholds to 10V-28V\n");
    printf("  power interval 2000         - Set 2 second sample interval\n");
    printf("  power debug enable          - Enable protocol debugging\n");
    printf("\n");
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
            float min_v, max_v;
            if (ts_power_monitor_get_voltage_thresholds(&min_v, &max_v) == ESP_OK) {
                printf("Current thresholds: %.2f V - %.2f V\n", min_v, max_v);
            }
            printf("Usage: power threshold <min_voltage> <max_voltage>\n");
            return 1;
        }
    }
    else if (strcmp(subcmd, "interval") == 0) {
        if (argc >= 3) {
            uint32_t interval = atoi(argv[2]);
            return cmd_power_interval(interval);
        } else {
            uint32_t interval;
            if (ts_power_monitor_get_sample_interval(&interval) == ESP_OK) {
                printf("Current interval: %lu ms\n", (unsigned long)interval);
            }
            printf("Usage: power interval <milliseconds>\n");
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
            printf("Usage: power debug enable|disable\n");
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
        printf("Unknown command: %s\n", subcmd);
        printf("Use 'power help' for usage information\n");
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
        printf("Failed to register power command: %s\n", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

/* 服务注册接口 */
esp_err_t ts_power_monitor_register_commands(void)
{
    return ts_cmd_power_register();
}
