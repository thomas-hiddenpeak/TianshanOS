/**
 * @file ts_cmd_fan.c
 * @brief Fan Control Console Commands
 * 
 * 实现 fan 命令族：
 * - fan --status          显示风扇状态
 * - fan --set --id X -S Y 设置风扇速度
 * - fan --mode --id X     设置风扇模式
 * - fan --enable/disable  启用/禁用风扇
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_fan.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_fan"

/* 模式名称转换 */
static const char *mode_to_str(ts_fan_mode_t mode)
{
    switch (mode) {
        case TS_FAN_MODE_OFF:    return "off";
        case TS_FAN_MODE_MANUAL: return "manual";
        case TS_FAN_MODE_AUTO:   return "auto";
        default:                 return "unknown";
    }
}

static ts_fan_mode_t str_to_mode(const char *str)
{
    if (strcmp(str, "off") == 0) return TS_FAN_MODE_OFF;
    if (strcmp(str, "manual") == 0) return TS_FAN_MODE_MANUAL;
    if (strcmp(str, "auto") == 0) return TS_FAN_MODE_AUTO;
    return TS_FAN_MODE_OFF;
}

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *set;
    struct arg_lit *mode;
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_lit *save;
    struct arg_int *id;
    struct arg_int *speed;
    struct arg_str *mode_val;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_fan_args;

/*===========================================================================*/
/*                          Command: fan --status                             */
/*===========================================================================*/

static int do_fan_status(int fan_id, bool json)
{
    ts_fan_status_t status;
    
    if (fan_id >= 0) {
        // 单个风扇状态
        if (fan_id >= TS_FAN_MAX) {
            ts_console_error("Invalid fan ID: %d (max: %d)\n", fan_id, TS_FAN_MAX - 1);
            return 1;
        }
        
        esp_err_t ret = ts_fan_get_status(fan_id, &status);
        if (ret != ESP_OK) {
            ts_console_error("Failed to get fan %d status: %s\n", fan_id, esp_err_to_name(ret));
            return 1;
        }
        
        if (json) {
            ts_console_printf(
                "{\"id\":%d,\"running\":%s,\"duty\":%d,\"rpm\":%d,\"mode\":\"%s\"}\n",
                fan_id, status.is_running ? "true" : "false",
                status.duty_percent, status.rpm, mode_to_str(status.mode));
        } else {
            ts_console_printf("Fan %d:\n", fan_id);
            ts_console_printf("  Running:  %s\n", status.is_running ? "Yes" : "No");
            ts_console_printf("  Duty:     %d%%\n", status.duty_percent);
            ts_console_printf("  RPM:      %d\n", status.rpm);
            ts_console_printf("  Mode:     %s\n", mode_to_str(status.mode));
        }
    } else {
        // 所有风扇状态
        if (json) {
            ts_console_printf("{\"fans\":[");
            for (int i = 0; i < TS_FAN_MAX; i++) {
                if (i > 0) ts_console_printf(",");
                if (ts_fan_get_status(i, &status) == ESP_OK) {
                    ts_console_printf(
                        "{\"id\":%d,\"running\":%s,\"duty\":%d,\"rpm\":%d,\"mode\":\"%s\"}",
                        i, status.is_running ? "true" : "false",
                        status.duty_percent, status.rpm, mode_to_str(status.mode));
                } else {
                    ts_console_printf("{\"id\":%d,\"error\":true}", i);
                }
            }
            ts_console_printf("]}\n");
        } else {
            ts_console_printf("Fan Status:\n\n");
            ts_console_printf("%-4s  %-8s  %6s  %6s  %s\n",
                "ID", "RUNNING", "DUTY", "RPM", "MODE");
            ts_console_printf("──────────────────────────────────────\n");
            
            for (int i = 0; i < TS_FAN_MAX; i++) {
                if (ts_fan_get_status(i, &status) == ESP_OK) {
                    ts_console_printf("%-4d  %-8s  %5d%%  %6d  %s\n",
                        i,
                        status.is_running ? "Yes" : "No",
                        status.duty_percent,
                        status.rpm,
                        mode_to_str(status.mode));
                } else {
                    ts_console_printf("%-4d  %-8s\n", i, "Error");
                }
            }
            ts_console_printf("\n");
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: fan --set                                */
/*===========================================================================*/

static int do_fan_set_speed(int fan_id, int speed)
{
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_console_error("Invalid fan ID: %d\n", fan_id);
        return 1;
    }
    
    if (speed < 0 || speed > 100) {
        ts_console_error("Speed must be 0-100\n");
        return 1;
    }
    
    // 设置为手动模式并设定占空比
    esp_err_t ret = ts_fan_set_mode(fan_id, TS_FAN_MODE_MANUAL);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set mode: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ret = ts_fan_set_duty(fan_id, (uint8_t)speed);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set duty: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Fan %d speed set to %d%%\n", fan_id, speed);
    return 0;
}

/*===========================================================================*/
/*                          Command: fan --mode                               */
/*===========================================================================*/

static int do_fan_set_mode_cmd(int fan_id, const char *mode)
{
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_console_error("Invalid fan ID: %d\n", fan_id);
        return 1;
    }
    
    ts_fan_mode_t fan_mode;
    if (strcmp(mode, "auto") == 0) {
        fan_mode = TS_FAN_MODE_AUTO;
    } else if (strcmp(mode, "manual") == 0) {
        fan_mode = TS_FAN_MODE_MANUAL;
    } else if (strcmp(mode, "off") == 0) {
        fan_mode = TS_FAN_MODE_OFF;
    } else {
        ts_console_error("Invalid mode: %s (use: auto, manual, off)\n", mode);
        return 1;
    }
    
    esp_err_t ret = ts_fan_set_mode(fan_id, fan_mode);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set mode: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Fan %d mode set to %s\n", fan_id, mode);
    return 0;
}

/*===========================================================================*/
/*                          Command: fan --enable/--disable                   */
/*===========================================================================*/

static int do_fan_enable(int fan_id, bool enable)
{
    if (fan_id < 0 || fan_id >= TS_FAN_MAX) {
        ts_console_error("Invalid fan ID: %d\n", fan_id);
        return 1;
    }
    
    ts_fan_mode_t mode = enable ? TS_FAN_MODE_AUTO : TS_FAN_MODE_OFF;
    esp_err_t ret = ts_fan_set_mode(fan_id, mode);
    if (ret != ESP_OK) {
        ts_console_error("Failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("Fan %d %s\n", fan_id, enable ? "enabled" : "disabled");
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_fan(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_fan_args);
    
    if (s_fan_args.help->count > 0) {
        ts_console_printf("Usage: fan [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -s, --status        Show fan status\n");
        ts_console_printf("      --set           Set fan speed\n");
        ts_console_printf("  -m, --mode          Set fan mode\n");
        ts_console_printf("      --enable        Enable fan\n");
        ts_console_printf("      --disable       Disable fan\n");
        ts_console_printf("      --save          Save configuration\n");
        ts_console_printf("  -i, --id <n>        Fan ID (0-%d)\n", TS_FAN_MAX - 1);
        ts_console_printf("  -S, --speed <0-100> Fan speed percentage\n");
        ts_console_printf("      --value <mode>  Mode: auto, manual, off\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  fan --status\n");
        ts_console_printf("  fan --set --id 0 --speed 75\n");
        ts_console_printf("  fan --mode --id 0 --value auto\n");
        ts_console_printf("  fan --enable --id 1\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_fan_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_fan_args.json->count > 0;
    int fan_id = s_fan_args.id->count > 0 ? s_fan_args.id->ival[0] : -1;
    
    // 设置速度
    if (s_fan_args.set->count > 0) {
        if (fan_id < 0) {
            ts_console_error("--id required for --set\n");
            return 1;
        }
        if (s_fan_args.speed->count == 0) {
            ts_console_error("--speed required for --set\n");
            return 1;
        }
        return do_fan_set_speed(fan_id, s_fan_args.speed->ival[0]);
    }
    
    // 设置模式
    if (s_fan_args.mode->count > 0) {
        if (fan_id < 0) {
            ts_console_error("--id required for --mode\n");
            return 1;
        }
        if (s_fan_args.mode_val->count == 0) {
            ts_console_error("--value required for --mode\n");
            return 1;
        }
        return do_fan_set_mode_cmd(fan_id, s_fan_args.mode_val->sval[0]);
    }
    
    // 启用
    if (s_fan_args.enable->count > 0) {
        if (fan_id < 0) {
            ts_console_error("--id required for --enable\n");
            return 1;
        }
        return do_fan_enable(fan_id, true);
    }
    
    // 禁用
    if (s_fan_args.disable->count > 0) {
        if (fan_id < 0) {
            ts_console_error("--id required for --disable\n");
            return 1;
        }
        return do_fan_enable(fan_id, false);
    }
    
    // 保存配置
    if (s_fan_args.save->count > 0) {
        esp_err_t ret = ts_fan_save_config();
        if (ret == ESP_OK) {
            ts_console_success("Fan configuration saved\n");
        } else {
            ts_console_error("Failed to save fan config: %s\n", esp_err_to_name(ret));
            return 1;
        }
        return 0;
    }
    
    // 默认显示状态
    return do_fan_status(fan_id, json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_fan_register(void)
{
    s_fan_args.status   = arg_lit0("s", "status", "Show status");
    s_fan_args.set      = arg_lit0(NULL, "set", "Set speed");
    s_fan_args.mode     = arg_lit0("m", "mode", "Set mode");
    s_fan_args.enable   = arg_lit0(NULL, "enable", "Enable fan");
    s_fan_args.disable  = arg_lit0(NULL, "disable", "Disable fan");
    s_fan_args.save     = arg_lit0(NULL, "save", "Save config");
    s_fan_args.id       = arg_int0("i", "id", "<n>", "Fan ID");
    s_fan_args.speed    = arg_int0("S", "speed", "<0-100>", "Speed %");
    s_fan_args.mode_val = arg_str0(NULL, "value", "<mode>", "Mode value");
    s_fan_args.json     = arg_lit0("j", "json", "JSON output");
    s_fan_args.help     = arg_lit0("h", "help", "Show help");
    s_fan_args.end      = arg_end(12);
    
    const ts_console_cmd_t cmd = {
        .command = "fan",
        .help = "Fan control and monitoring",
        .hint = NULL,
        .category = TS_CMD_CAT_FAN,
        .func = cmd_fan,
        .argtable = &s_fan_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Fan commands registered");
    }
    
    return ret;
}
