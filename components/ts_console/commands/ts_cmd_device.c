/**
 * @file ts_cmd_device.c
 * @brief Device Control Console Commands
 * 
 * 实现 device 命令族：
 * - device --agx          AGX 控制
 * - device --lpmu         LPMU 控制
 * - device --usb-mux      USB MUX 控制
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_device"

/* 模拟设备状态（后续接入真实驱动） */
typedef struct {
    bool power_on;
    const char *status;
} device_state_t;

static device_state_t s_agx = { .power_on = false, .status = "OFF" };
static device_state_t s_lpmu = { .power_on = false, .status = "OFF" };
static const char *s_usb_mux_target = "esp32";

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *agx;
    struct arg_lit *lpmu;
    struct arg_lit *usb_mux;
    struct arg_str *power;
    struct arg_str *target;
    struct arg_lit *status;
    struct arg_lit *reset;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_device_args;

/*===========================================================================*/
/*                          Command: device --agx                             */
/*===========================================================================*/

static int do_agx_control(const char *power, bool status_only, bool reset, bool json)
{
    if (status_only || (!power && !reset)) {
        // 显示状态
        if (json) {
            ts_console_printf("{\"device\":\"agx\",\"power\":%s,\"status\":\"%s\"}\n",
                s_agx.power_on ? "true" : "false", s_agx.status);
        } else {
            ts_console_printf("AGX Status:\n");
            ts_console_printf("  Power:  %s%s\033[0m\n",
                s_agx.power_on ? "\033[32m" : "\033[33m",
                s_agx.power_on ? "ON" : "OFF");
            ts_console_printf("  Status: %s\n", s_agx.status);
        }
        return 0;
    }
    
    if (reset) {
        ts_console_printf("Resetting AGX...\n");
        // TODO: 调用真实驱动
        s_agx.status = "RESETTING";
        vTaskDelay(pdMS_TO_TICKS(100));
        s_agx.status = s_agx.power_on ? "RUNNING" : "OFF";
        ts_console_success("AGX reset complete\n");
        return 0;
    }
    
    if (power) {
        if (strcmp(power, "on") == 0) {
            ts_console_printf("Powering on AGX...\n");
            // TODO: 调用真实驱动
            s_agx.power_on = true;
            s_agx.status = "BOOTING";
            ts_console_success("AGX power on\n");
        } else if (strcmp(power, "off") == 0) {
            ts_console_printf("Powering off AGX...\n");
            // TODO: 调用真实驱动
            s_agx.power_on = false;
            s_agx.status = "OFF";
            ts_console_success("AGX power off\n");
        } else if (strcmp(power, "restart") == 0) {
            ts_console_printf("Restarting AGX...\n");
            // TODO: 调用真实驱动
            s_agx.status = "RESTARTING";
            ts_console_success("AGX restart initiated\n");
        } else {
            ts_console_error("Invalid power option: %s (use: on, off, restart)\n", power);
            return 1;
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: device --lpmu                            */
/*===========================================================================*/

static int do_lpmu_control(const char *power, bool status_only, bool reset, bool json)
{
    if (status_only || (!power && !reset)) {
        // 显示状态
        if (json) {
            ts_console_printf("{\"device\":\"lpmu\",\"power\":%s,\"status\":\"%s\"}\n",
                s_lpmu.power_on ? "true" : "false", s_lpmu.status);
        } else {
            ts_console_printf("LPMU Status:\n");
            ts_console_printf("  Power:  %s%s\033[0m\n",
                s_lpmu.power_on ? "\033[32m" : "\033[33m",
                s_lpmu.power_on ? "ON" : "OFF");
            ts_console_printf("  Status: %s\n", s_lpmu.status);
        }
        return 0;
    }
    
    if (reset) {
        ts_console_printf("Resetting LPMU...\n");
        // TODO: 调用真实驱动
        s_lpmu.status = "RESETTING";
        vTaskDelay(pdMS_TO_TICKS(100));
        s_lpmu.status = s_lpmu.power_on ? "READY" : "OFF";
        ts_console_success("LPMU reset complete\n");
        return 0;
    }
    
    if (power) {
        if (strcmp(power, "on") == 0) {
            ts_console_printf("Powering on LPMU...\n");
            // TODO: 调用真实驱动
            s_lpmu.power_on = true;
            s_lpmu.status = "READY";
            ts_console_success("LPMU power on\n");
        } else if (strcmp(power, "off") == 0) {
            ts_console_printf("Powering off LPMU...\n");
            // TODO: 调用真实驱动
            s_lpmu.power_on = false;
            s_lpmu.status = "OFF";
            ts_console_success("LPMU power off\n");
        } else {
            ts_console_error("Invalid power option: %s (use: on, off)\n", power);
            return 1;
        }
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: device --usb-mux                         */
/*===========================================================================*/

static int do_usb_mux_control(const char *target, bool status_only, bool json)
{
    if (status_only || !target) {
        // 显示状态
        if (json) {
            ts_console_printf("{\"device\":\"usb-mux\",\"target\":\"%s\"}\n", s_usb_mux_target);
        } else {
            ts_console_printf("USB MUX Status:\n");
            ts_console_printf("  Target: %s\n", s_usb_mux_target);
            ts_console_printf("\n");
            ts_console_printf("Available targets: esp32, agx, lpmu\n");
        }
        return 0;
    }
    
    // 验证目标
    if (strcmp(target, "esp32") != 0 && 
        strcmp(target, "agx") != 0 && 
        strcmp(target, "lpmu") != 0) {
        ts_console_error("Invalid target: %s (use: esp32, agx, lpmu)\n", target);
        return 1;
    }
    
    ts_console_printf("Switching USB MUX to %s...\n", target);
    // TODO: 调用真实驱动
    s_usb_mux_target = target;  // 注意：实际代码需要复制字符串
    ts_console_success("USB MUX switched to %s\n", target);
    
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_device(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_device_args);
    
    if (s_device_args.help->count > 0) {
        ts_console_printf("Usage: device [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("      --agx           AGX control\n");
        ts_console_printf("      --lpmu          LPMU control\n");
        ts_console_printf("      --usb-mux       USB MUX control\n");
        ts_console_printf("      --power <op>    Power: on, off, restart\n");
        ts_console_printf("      --target <dev>  Target: esp32, agx, lpmu\n");
        ts_console_printf("  -s, --status        Show status\n");
        ts_console_printf("      --reset         Reset device\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  device --agx --status\n");
        ts_console_printf("  device --agx --power on\n");
        ts_console_printf("  device --lpmu --reset\n");
        ts_console_printf("  device --usb-mux --target agx\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_device_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_device_args.json->count > 0;
    bool status_only = s_device_args.status->count > 0;
    bool reset = s_device_args.reset->count > 0;
    const char *power = s_device_args.power->count > 0 ? 
                        s_device_args.power->sval[0] : NULL;
    const char *target = s_device_args.target->count > 0 ? 
                         s_device_args.target->sval[0] : NULL;
    
    if (s_device_args.agx->count > 0) {
        return do_agx_control(power, status_only, reset, json);
    }
    
    if (s_device_args.lpmu->count > 0) {
        return do_lpmu_control(power, status_only, reset, json);
    }
    
    if (s_device_args.usb_mux->count > 0) {
        return do_usb_mux_control(target, status_only, json);
    }
    
    // 无设备选择时显示所有设备状态
    if (json) {
        ts_console_printf("{\"devices\":[");
        ts_console_printf("{\"name\":\"agx\",\"power\":%s,\"status\":\"%s\"},",
            s_agx.power_on ? "true" : "false", s_agx.status);
        ts_console_printf("{\"name\":\"lpmu\",\"power\":%s,\"status\":\"%s\"},",
            s_lpmu.power_on ? "true" : "false", s_lpmu.status);
        ts_console_printf("{\"name\":\"usb-mux\",\"target\":\"%s\"}", s_usb_mux_target);
        ts_console_printf("]}\n");
    } else {
        ts_console_printf("Device Status:\n\n");
        ts_console_printf("%-12s  %-8s  %s\n", "DEVICE", "POWER", "STATUS");
        ts_console_printf("────────────────────────────────────────\n");
        ts_console_printf("%-12s  %-8s  %s\n", "AGX", 
            s_agx.power_on ? "ON" : "OFF", s_agx.status);
        ts_console_printf("%-12s  %-8s  %s\n", "LPMU", 
            s_lpmu.power_on ? "ON" : "OFF", s_lpmu.status);
        ts_console_printf("%-12s  %-8s  %s\n", "USB-MUX", 
            "-", s_usb_mux_target);
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_device_register(void)
{
    s_device_args.agx     = arg_lit0(NULL, "agx", "AGX control");
    s_device_args.lpmu    = arg_lit0(NULL, "lpmu", "LPMU control");
    s_device_args.usb_mux = arg_lit0(NULL, "usb-mux", "USB MUX control");
    s_device_args.power   = arg_str0(NULL, "power", "<op>", "on/off/restart");
    s_device_args.target  = arg_str0(NULL, "target", "<dev>", "esp32/agx/lpmu");
    s_device_args.status  = arg_lit0("s", "status", "Show status");
    s_device_args.reset   = arg_lit0(NULL, "reset", "Reset device");
    s_device_args.json    = arg_lit0("j", "json", "JSON output");
    s_device_args.help    = arg_lit0("h", "help", "Show help");
    s_device_args.end     = arg_end(10);
    
    const ts_console_cmd_t cmd = {
        .command = "device",
        .help = "Device control (AGX, LPMU, USB-MUX)",
        .hint = NULL,
        .category = TS_CMD_CAT_DEVICE,
        .func = cmd_device,
        .argtable = &s_device_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Device commands registered");
    }
    
    return ret;
}
