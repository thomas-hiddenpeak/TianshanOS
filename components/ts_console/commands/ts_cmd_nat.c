/**
 * @file ts_cmd_nat.c
 * @brief NAT Gateway CLI Command
 * 
 * 命令语法：
 *   nat --status           显示 NAT 状态
 *   nat --enable           启用 NAT 网关
 *   nat --disable          禁用 NAT 网关
 *   nat --save             保存配置到 NVS
 *   nat --json             JSON 格式输出
 */

#include "ts_cmd_all.h"
#include "ts_nat.h"
#include "ts_net_manager.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "cmd_nat";

/* 命令参数定义 */
static struct {
    struct arg_lit *status;
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_lit *save;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_args;

/* ============================================================================
 * 命令处理函数
 * ========================================================================== */

static int do_nat_status(bool json_output)
{
    ts_nat_status_t status;
    ts_nat_get_status(&status);
    
    if (json_output) {
        printf("{\"enabled\":%s,\"wifi_connected\":%s,\"eth_up\":%s}\n",
               status.state == TS_NAT_STATE_ENABLED ? "true" : "false",
               status.wifi_connected ? "true" : "false",
               status.eth_up ? "true" : "false");
    } else {
        printf("\n");
        printf("NAT Gateway Status\n");
        printf("==================\n");
        printf("  State:          %s\n", 
               status.state == TS_NAT_STATE_ENABLED ? "ENABLED" :
               status.state == TS_NAT_STATE_ERROR ? "ERROR" : "DISABLED");
        printf("  WiFi STA:       %s\n", 
               status.wifi_connected ? "Connected" : "Disconnected");
        printf("  Ethernet:       %s\n", 
               status.eth_up ? "Link Up" : "Link Down");
        printf("\n");
        
        if (status.state == TS_NAT_STATE_ENABLED) {
            printf("  NAT is active: ETH devices can access internet via WiFi\n");
        } else if (status.wifi_connected) {
            printf("  WiFi connected. Run 'nat --enable' to start NAT gateway.\n");
        } else {
            printf("  Connect to WiFi first with 'wifi --connect'\n");
        }
        printf("\n");
    }
    
    return 0;
}

static int do_nat_enable(bool json_output)
{
    esp_err_t ret = ts_nat_enable();
    
    if (json_output) {
        printf("{\"success\":%s,\"error\":\"%s\"}\n",
               ret == ESP_OK ? "true" : "false",
               ret == ESP_OK ? "" : esp_err_to_name(ret));
    } else {
        if (ret == ESP_OK) {
            printf("NAT gateway enabled\n");
            printf("ETH devices (e.g. Jetson) can now access internet via WiFi\n");
            printf("\nEnsure ETH device gateway is set to ESP32's ETH IP (e.g. 10.10.99.97)\n");
        } else if (ret == ESP_ERR_INVALID_STATE) {
            printf("Error: WiFi STA not connected\n");
            printf("Connect to WiFi first: wifi --connect --ssid <SSID> --pass <password>\n");
        } else {
            printf("Error: %s\n", esp_err_to_name(ret));
        }
    }
    
    return ret == ESP_OK ? 0 : 1;
}

static int do_nat_disable(bool json_output)
{
    esp_err_t ret = ts_nat_disable();
    
    if (json_output) {
        printf("{\"success\":%s}\n", ret == ESP_OK ? "true" : "false");
    } else {
        if (ret == ESP_OK) {
            printf("NAT gateway disabled\n");
        } else {
            printf("Error: %s\n", esp_err_to_name(ret));
        }
    }
    
    return ret == ESP_OK ? 0 : 1;
}

static int do_nat_save(bool json_output)
{
    esp_err_t ret = ts_nat_save_config();
    
    if (json_output) {
        printf("{\"success\":%s}\n", ret == ESP_OK ? "true" : "false");
    } else {
        if (ret == ESP_OK) {
            printf("NAT configuration saved\n");
        } else {
            printf("Error saving config: %s\n", esp_err_to_name(ret));
        }
    }
    
    return ret == ESP_OK ? 0 : 1;
}

/* ============================================================================
 * 命令入口
 * ========================================================================== */

static int cmd_nat_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_args);
    
    /* 帮助 */
    if (s_args.help->count > 0) {
        printf("Usage: nat [options]\n\n");
        printf("Options:\n");
        printf("  --status        Show NAT gateway status\n");
        printf("  --enable        Enable NAT gateway (WiFi -> ETH)\n");
        printf("  --disable       Disable NAT gateway\n");
        printf("  --save          Save configuration to NVS\n");
        printf("  --json, -j      JSON output format\n");
        printf("  --help, -h      Show this help\n");
        printf("\n");
        printf("NAT gateway allows ETH devices (e.g. Jetson AGX) to access\n");
        printf("the internet through ESP32's WiFi connection.\n");
        printf("\n");
        printf("Example:\n");
        printf("  1. Connect WiFi: wifi --connect --ssid MyWiFi --pass secret\n");
        printf("  2. Enable NAT:   nat --enable\n");
        printf("  3. On Jetson:    Set gateway to ESP32's ETH IP (10.10.99.97)\n");
        return 0;
    }
    
    if (nerrors > 0) {
        arg_print_errors(stderr, s_args.end, "nat");
        return 1;
    }
    
    bool json = s_args.json->count > 0;
    
    /* 处理命令 */
    if (s_args.enable->count > 0) {
        return do_nat_enable(json);
    }
    
    if (s_args.disable->count > 0) {
        return do_nat_disable(json);
    }
    
    if (s_args.save->count > 0) {
        return do_nat_save(json);
    }
    
    /* 默认显示状态 */
    return do_nat_status(json);
}

/* ============================================================================
 * 命令注册
 * ========================================================================== */

void ts_cmd_nat_register(void)
{
    /* 初始化 NAT 模块 */
    ts_nat_init();
    
    /* 定义参数 */
    s_args.status = arg_lit0("s", "status", "Show NAT status");
    s_args.enable = arg_lit0(NULL, "enable", "Enable NAT gateway");
    s_args.disable = arg_lit0(NULL, "disable", "Disable NAT gateway");
    s_args.save = arg_lit0(NULL, "save", "Save config to NVS");
    s_args.json = arg_lit0("j", "json", "JSON output");
    s_args.help = arg_lit0("h", "help", "Show help");
    s_args.end = arg_end(5);
    
    /* 注册命令 */
    const esp_console_cmd_t cmd = {
        .command = "nat",
        .help = "NAT gateway management (ETH <-> WiFi)",
        .hint = NULL,
        .func = &cmd_nat_handler,
        .argtable = &s_args,
    };
    
    esp_console_cmd_register(&cmd);
}
