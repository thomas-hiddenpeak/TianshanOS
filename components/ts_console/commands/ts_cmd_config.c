/**
 * @file ts_cmd_config.c
 * @brief Configuration Console Commands
 * 
 * 实现 config 命令族：
 * - config --get -k key      获取配置值
 * - config --set -k key -v   设置配置值
 * - config --list            列出所有配置
 * - config --reset           重置配置
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_config.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define TAG "cmd_config"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *get;
    struct arg_lit *set;
    struct arg_lit *list;
    struct arg_lit *reset;
    struct arg_str *key;
    struct arg_str *value;
    struct arg_str *ns;
    struct arg_lit *persist;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_config_args;

/*===========================================================================*/
/*                          Command: config --get                             */
/*===========================================================================*/

static int do_config_get(const char *key, bool json)
{
    // 尝试不同类型获取配置
    int32_t i32_val;
    bool bool_val;
    char str_buf[128];
    
    // 先尝试整数
    if (ts_config_get_int32(key, &i32_val, 0) == ESP_OK) {
        if (json) {
            ts_console_printf("{\"key\":\"%s\",\"type\":\"int\",\"value\":%d}\n", 
                key, i32_val);
        } else {
            ts_console_printf("%s = %d\n", key, i32_val);
        }
        return 0;
    }
    
    // 尝试布尔
    if (ts_config_get_bool(key, &bool_val, false) == ESP_OK) {
        if (json) {
            ts_console_printf("{\"key\":\"%s\",\"type\":\"bool\",\"value\":%s}\n", 
                key, bool_val ? "true" : "false");
        } else {
            ts_console_printf("%s = %s\n", key, bool_val ? "true" : "false");
        }
        return 0;
    }
    
    // 尝试字符串
    if (ts_config_get_string(key, str_buf, sizeof(str_buf), "") == ESP_OK) {
        if (json) {
            ts_console_printf("{\"key\":\"%s\",\"type\":\"string\",\"value\":\"%s\"}\n", 
                key, str_buf);
        } else {
            ts_console_printf("%s = \"%s\"\n", key, str_buf);
        }
        return 0;
    }
    
    ts_console_error("Key '%s' not found\n", key);
    return 1;
}

/*===========================================================================*/
/*                          Command: config --set                             */
/*===========================================================================*/

static int do_config_set(const char *key, const char *value_str, bool persist)
{
    // 尝试解析值类型
    
    // 检查是否是布尔值
    if (strcasecmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0) {
        esp_err_t ret = ts_config_set_bool(key, true);
        if (ret != ESP_OK) {
            ts_console_error("Failed to set '%s'\n", key);
            return 1;
        }
    } else if (strcasecmp(value_str, "false") == 0 || strcmp(value_str, "0") == 0) {
        esp_err_t ret = ts_config_set_bool(key, false);
        if (ret != ESP_OK) {
            ts_console_error("Failed to set '%s'\n", key);
            return 1;
        }
    } else {
        // 检查是否是整数
        char *endptr;
        long lval = strtol(value_str, &endptr, 10);
        if (*endptr == '\0') {
            esp_err_t ret = ts_config_set_int32(key, (int32_t)lval);
            if (ret != ESP_OK) {
                ts_console_error("Failed to set '%s'\n", key);
                return 1;
            }
        } else {
            // 当作字符串处理
            esp_err_t ret = ts_config_set_string(key, value_str);
            if (ret != ESP_OK) {
                ts_console_error("Failed to set '%s'\n", key);
                return 1;
            }
        }
    }
    
    if (persist) {
        esp_err_t ret = ts_config_save();
        if (ret != ESP_OK) {
            ts_console_warn("Value set but failed to persist: %s\n", 
                esp_err_to_name(ret));
        } else {
            ts_console_success("Configuration saved: %s\n", key);
        }
    } else {
        ts_console_success("Configuration set: %s (not persisted)\n", key);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: config --list                            */
/*===========================================================================*/

static int do_config_list(const char *ns, bool json)
{
    // 暂时使用 ts_config_dump() 替代
    if (json) {
        ts_console_printf("{\"note\":\"List not implemented in JSON mode\"}\n");
    } else {
        if (ns) {
            ts_console_printf("Configuration [%s]:\n\n", ns);
        } else {
            ts_console_printf("All Configuration:\n\n");
        }
        
        // 调用配置系统的 dump 函数
        ts_config_dump();
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: config --reset                           */
/*===========================================================================*/

static int do_config_reset(const char *key, const char *ns)
{
    if (key) {
        esp_err_t ret = ts_config_delete(key);
        if (ret == ESP_OK) {
            ts_console_success("Reset: %s\n", key);
        } else {
            ts_console_error("Failed to reset '%s': %s\n", key, esp_err_to_name(ret));
            return 1;
        }
    } else if (ns) {
        ts_console_warn("Namespace reset not implemented yet\n");
        return 1;
    } else {
        ts_console_error("Specify --key or --namespace to reset\n");
        return 1;
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_config(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_config_args);
    
    if (s_config_args.help->count > 0) {
        ts_console_printf("Usage: config [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -g, --get           Get configuration value\n");
        ts_console_printf("  -s, --set           Set configuration value\n");
        ts_console_printf("  -l, --list          List configuration\n");
        ts_console_printf("      --reset         Reset to default\n");
        ts_console_printf("  -k, --key <key>     Configuration key\n");
        ts_console_printf("  -v, --value <val>   Configuration value\n");
        ts_console_printf("      --namespace <n> Configuration namespace\n");
        ts_console_printf("  -p, --persist       Persist to NVS\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  config --get --key system.language\n");
        ts_console_printf("  config --set --key fan.speed --value 75 --persist\n");
        ts_console_printf("  config --list --namespace fan\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_config_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_config_args.json->count > 0;
    bool persist = s_config_args.persist->count > 0;
    const char *key = s_config_args.key->count > 0 ? s_config_args.key->sval[0] : NULL;
    const char *value = s_config_args.value->count > 0 ? s_config_args.value->sval[0] : NULL;
    const char *ns = s_config_args.ns->count > 0 ? s_config_args.ns->sval[0] : NULL;
    
    if (s_config_args.get->count > 0) {
        if (!key) {
            ts_console_error("--key required for --get\n");
            return 1;
        }
        return do_config_get(key, json);
    }
    
    if (s_config_args.set->count > 0) {
        if (!key || !value) {
            ts_console_error("--key and --value required for --set\n");
            return 1;
        }
        return do_config_set(key, value, persist);
    }
    
    if (s_config_args.reset->count > 0) {
        return do_config_reset(key, ns);
    }
    
    // 默认列出配置
    return do_config_list(ns, json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_config_register(void)
{
    s_config_args.get     = arg_lit0("g", "get", "Get value");
    s_config_args.set     = arg_lit0("s", "set", "Set value");
    s_config_args.list    = arg_lit0("l", "list", "List config");
    s_config_args.reset   = arg_lit0(NULL, "reset", "Reset to default");
    s_config_args.key     = arg_str0("k", "key", "<key>", "Config key");
    s_config_args.value   = arg_str0("v", "value", "<value>", "Config value");
    s_config_args.ns      = arg_str0(NULL, "namespace", "<ns>", "Namespace");
    s_config_args.persist = arg_lit0("p", "persist", "Persist to NVS");
    s_config_args.json    = arg_lit0("j", "json", "JSON output");
    s_config_args.help    = arg_lit0("h", "help", "Show help");
    s_config_args.end     = arg_end(10);
    
    const ts_console_cmd_t cmd = {
        .command = "config",
        .help = "Configuration management",
        .hint = NULL,
        .category = TS_CMD_CAT_CONFIG,
        .func = cmd_config,
        .argtable = &s_config_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Config commands registered");
    }
    
    return ret;
}
