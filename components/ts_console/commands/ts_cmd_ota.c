/**
 * @file ts_cmd_ota.c
 * @brief TianShanOS OTA CLI Commands
 *
 * Commands:
 *   ota --status           显示 OTA 状态
 *   ota --progress         显示升级进度
 *   ota --version          显示固件版本
 *   ota --partitions       显示分区信息
 *   ota --server [url]     获取/设置 OTA 服务器地址
 *   ota --url <url>        从 URL 升级
 *   ota --file <path>      从 SD 卡升级
 *   ota --validate         标记固件有效
 *   ota --rollback         回滚到上一版本
 *   ota --abort            中止升级
 *   ota --save             持久化配置到 NVS
 */

#include <string.h>
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "ts_api.h"
#include "ts_ota.h"
#include "ts_console.h"
#include "cJSON.h"

static const char *TAG = "cmd_ota";

// ============================================================================
//                           Argument Definitions
// ============================================================================

static struct {
    struct arg_lit *status;
    struct arg_lit *progress;
    struct arg_lit *version;
    struct arg_lit *partitions;
    struct arg_str *server;
    struct arg_str *url;
    struct arg_str *file;
    struct arg_lit *validate;
    struct arg_lit *rollback;
    struct arg_lit *abort_ota;
    struct arg_lit *no_reboot;
    struct arg_lit *allow_downgrade;
    struct arg_lit *skip_verify;
    struct arg_lit *save;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_ota_args;

// ============================================================================
//                           Helper Functions
// ============================================================================

static const char *get_json_string(const cJSON *obj, const char *key, const char *default_val)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return default_val;
}

static double get_json_number(const cJSON *obj, const char *key, double default_val)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsNumber(item)) {
        return item->valuedouble;
    }
    return default_val;
}

static bool get_json_bool(const cJSON *obj, const char *key, bool default_val)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    if (item && cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return default_val;
}

// ============================================================================
//                           Command Handler
// ============================================================================

static int cmd_ota_handler(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_ota_args);

    if (s_ota_args.help->count > 0) {
        printf("用法: ota [选项]\n\n");
        printf("选项:\n");
        printf("  --status         显示 OTA 状态\n");
        printf("  --progress       显示升级进度\n");
        printf("  --version        显示固件版本\n");
        printf("  --partitions     显示分区信息\n");
        printf("  --server [url]   获取/设置 OTA 服务器地址\n");
        printf("  --url <url>      从 HTTPS URL 升级\n");
        printf("  --file <path>    从 SD 卡文件升级\n");
        printf("  --validate       标记当前固件有效（取消回滚）\n");
        printf("  --rollback       回滚到上一版本\n");
        printf("  --abort          中止当前升级\n");
        printf("  --no-reboot      升级后不自动重启\n");
        printf("  --allow-downgrade 允许降级\n");
        printf("  --skip-verify    跳过证书验证（仅调试）\n");
        printf("  --save           持久化配置到 NVS（与 --server 配合使用）\n");
        printf("  --json           JSON 格式输出\n");
        printf("\n示例:\n");
        printf("  ota --status\n");
        printf("  ota --server                                # 查看当前服务器\n");
        printf("  ota --server http://192.168.1.100:57807     # 设置服务器\n");
        printf("  ota --server http://192.168.1.100:57807 --save  # 设置并保存\n");
        printf("  ota --url https://example.com/firmware.bin\n");
        printf("  ota --file /sdcard/firmware.bin\n");
        printf("  ota --validate\n");
        printf("  ota --rollback\n");
        return 0;
    }

    if (nerrors > 0) {
        arg_print_errors(stderr, s_ota_args.end, "ota");
        return 1;
    }

    bool json_output = s_ota_args.json->count > 0;
    ts_api_result_t result;
    memset(&result, 0, sizeof(result));
    esp_err_t ret;

    // --status: 显示 OTA 状态
    if (s_ota_args.status->count > 0) {
        ret = ts_api_call("ota.status", NULL, &result);
        
        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "获取状态失败");
            ts_api_result_free(&result);
            return 1;
        }

        if (json_output) {
            char *json_str = cJSON_Print(result.data);
            if (json_str) {
                printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            cJSON *data = result.data;
            const char *state = get_json_string(data, "state", "unknown");
            cJSON *running = cJSON_GetObjectItem(data, "running");
            cJSON *next = cJSON_GetObjectItem(data, "next");

            printf("╔════════════════════════════════════════╗\n");
            printf("║           OTA 状态信息                  ║\n");
            printf("╠════════════════════════════════════════╣\n");
            printf("║ 当前状态: %-28s ║\n", state);
            
            if (running) {
                printf("╠════════════════════════════════════════╣\n");
                printf("║ 运行分区: %-28s ║\n", get_json_string(running, "label", "N/A"));
                printf("║ 版本:     %-28s ║\n", get_json_string(running, "version", "N/A"));
                printf("║ 项目:     %-28s ║\n", get_json_string(running, "project", "N/A"));
                printf("║ 编译日期: %-28s ║\n", get_json_string(running, "compile_date", "N/A"));
                printf("║ IDF版本:  %-28s ║\n", get_json_string(running, "idf_version", "N/A"));
            }

            if (next) {
                printf("╠════════════════════════════════════════╣\n");
                printf("║ 下一分区: %-28s ║\n", get_json_string(next, "label", "N/A"));
                printf("║ 可启动:   %-28s ║\n", get_json_bool(next, "bootable", false) ? "是" : "否");
                if (get_json_bool(next, "bootable", false)) {
                    printf("║ 版本:     %-28s ║\n", get_json_string(next, "version", "N/A"));
                }
            }

            bool pending = get_json_bool(data, "pending_verify", false);
            if (pending) {
                printf("╠════════════════════════════════════════╣\n");
                printf("║ ⚠️  新固件待验证                        ║\n");
                printf("║ 回滚超时: %d 秒                        ║\n", 
                       (int)get_json_number(data, "rollback_timeout", 0));
            }
            printf("╚════════════════════════════════════════╝\n");
        }

        ts_api_result_free(&result);
        return 0;
    }

    // --progress: 显示升级进度
    if (s_ota_args.progress->count > 0) {
        ret = ts_api_call("ota.progress", NULL, &result);
        
        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "获取进度失败");
            ts_api_result_free(&result);
            return 1;
        }

        if (json_output) {
            char *json_str = cJSON_Print(result.data);
            if (json_str) {
                printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            cJSON *data = result.data;
            const char *state = get_json_string(data, "state", "idle");
            int percent = (int)get_json_number(data, "percent", 0);
            size_t total = (size_t)get_json_number(data, "total_size", 0);
            size_t received = (size_t)get_json_number(data, "received_size", 0);
            const char *msg = get_json_string(data, "message", "");

            printf("状态: %s\n", state);
            if (strcmp(state, "idle") != 0) {
                printf("进度: %d%%\n", percent);
                if (total > 0) {
                    printf("已下载: %zu / %zu 字节\n", received, total);
                }
                if (msg[0]) {
                    printf("消息: %s\n", msg);
                }
                
                // 绘制进度条
                printf("[");
                int bar_width = 40;
                int filled = percent * bar_width / 100;
                for (int i = 0; i < bar_width; i++) {
                    if (i < filled) printf("█");
                    else printf("░");
                }
                printf("] %d%%\n", percent);
            }
        }

        ts_api_result_free(&result);
        return 0;
    }

    // --version: 显示固件版本
    if (s_ota_args.version->count > 0) {
        ret = ts_api_call("ota.version", NULL, &result);
        
        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "获取版本失败");
            ts_api_result_free(&result);
            return 1;
        }

        if (json_output) {
            char *json_str = cJSON_Print(result.data);
            if (json_str) {
                printf("%s\n", json_str);
                free(json_str);
            }
        } else {
            cJSON *data = result.data;
            printf("固件版本: %s\n", get_json_string(data, "version", "N/A"));
            printf("项目名称: %s\n", get_json_string(data, "project", "N/A"));
            printf("编译日期: %s\n", get_json_string(data, "compile_date", "N/A"));
            printf("编译时间: %s\n", get_json_string(data, "compile_time", "N/A"));
            printf("IDF 版本: %s\n", get_json_string(data, "idf_version", "N/A"));
        }

        ts_api_result_free(&result);
        return 0;
    }

    // --partitions: 显示分区信息
    if (s_ota_args.partitions->count > 0) {
        // 直接调用 OTA 模块的分区信息打印
        ts_ota_partition_info_t running, next;
        
        esp_err_t err = ts_ota_get_running_partition_info(&running);
        if (err != ESP_OK) {
            printf("错误: 获取分区信息失败\n");
            return 1;
        }
        
        ts_ota_get_next_partition_info(&next);

        printf("╔══════════════════════════════════════════════════════╗\n");
        printf("║                  OTA 分区信息                         ║\n");
        printf("╠══════════════════════════════════════════════════════╣\n");
        printf("║ 分区        地址         大小       状态              ║\n");
        printf("╠══════════════════════════════════════════════════════╣\n");
        printf("║ %-8s    0x%08lx   %-8lu   [运行中]           ║\n",
               running.label, (unsigned long)running.address, 
               (unsigned long)running.size);
        if (running.is_bootable) {
            printf("║   版本: %s                                          ║\n", running.version.version);
        }
        printf("╠══════════════════════════════════════════════════════╣\n");
        printf("║ %-8s    0x%08lx   %-8lu   %s               ║\n",
               next.label, (unsigned long)next.address,
               (unsigned long)next.size,
               next.is_bootable ? "[可启动]" : "[空闲]  ");
        if (next.is_bootable) {
            printf("║   版本: %s                                          ║\n", next.version.version);
        }
        printf("╚══════════════════════════════════════════════════════╝\n");
        return 0;
    }

    // --server: 获取/设置 OTA 服务器
    if (s_ota_args.server->count > 0) {
        const char *new_url = s_ota_args.server->sval[0];
        bool do_save = s_ota_args.save->count > 0;
        
        // 如果提供了 URL，则设置
        if (new_url && new_url[0]) {
            cJSON *params = cJSON_CreateObject();
            cJSON_AddStringToObject(params, "url", new_url);
            cJSON_AddBoolToObject(params, "save", do_save);
            
            ret = ts_api_call("ota.server.set", params, &result);
            cJSON_Delete(params);
            
            if (ret != ESP_OK || result.code != TS_API_OK) {
                printf("错误: %s\n", result.message ? result.message : "设置失败");
                ts_api_result_free(&result);
                return 1;
            }
            
            printf("OTA 服务器已设置: %s\n", new_url);
            if (do_save) {
                printf("✓ 配置已保存到 NVS\n");
            } else {
                printf("提示: 使用 --save 持久化配置\n");
            }
            
            ts_api_result_free(&result);
        } else {
            // 没有提供 URL，显示当前设置
            ret = ts_api_call("ota.server.get", NULL, &result);
            
            if (ret != ESP_OK || result.code != TS_API_OK) {
                printf("错误: %s\n", result.message ? result.message : "获取失败");
                ts_api_result_free(&result);
                return 1;
            }
            
            if (json_output) {
                char *json_str = cJSON_Print(result.data);
                if (json_str) {
                    printf("%s\n", json_str);
                    free(json_str);
                }
            } else {
                const char *url = get_json_string(result.data, "url", "");
                if (url[0]) {
                    printf("OTA 服务器: %s\n", url);
                } else {
                    printf("OTA 服务器: (未设置)\n");
                }
            }
            
            ts_api_result_free(&result);
        }
        return 0;
    }

    // --url: 从 URL 升级
    if (s_ota_args.url->count > 0) {
        const char *url = s_ota_args.url->sval[0];
        
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "url", url);
        cJSON_AddBoolToObject(params, "auto_reboot", s_ota_args.no_reboot->count == 0);
        cJSON_AddBoolToObject(params, "allow_downgrade", s_ota_args.allow_downgrade->count > 0);
        cJSON_AddBoolToObject(params, "skip_verify", s_ota_args.skip_verify->count > 0);

        printf("正在从 URL 启动 OTA...\n");
        printf("URL: %s\n", url);
        
        ret = ts_api_call("ota.start_url", params, &result);
        cJSON_Delete(params);

        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "启动 OTA 失败");
            ts_api_result_free(&result);
            return 1;
        }

        printf("OTA 已启动，使用 'ota --progress' 查看进度\n");
        ts_api_result_free(&result);
        return 0;
    }

    // --file: 从 SD 卡升级
    if (s_ota_args.file->count > 0) {
        const char *path = s_ota_args.file->sval[0];
        
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "path", path);
        cJSON_AddBoolToObject(params, "auto_reboot", s_ota_args.no_reboot->count == 0);
        cJSON_AddBoolToObject(params, "allow_downgrade", s_ota_args.allow_downgrade->count > 0);

        printf("正在从 SD 卡启动 OTA...\n");
        printf("文件: %s\n", path);
        
        ret = ts_api_call("ota.start_file", params, &result);
        cJSON_Delete(params);

        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "启动 OTA 失败");
            ts_api_result_free(&result);
            return 1;
        }

        printf("OTA 已启动，使用 'ota --progress' 查看进度\n");
        ts_api_result_free(&result);
        return 0;
    }

    // --validate: 标记固件有效
    if (s_ota_args.validate->count > 0) {
        printf("正在标记固件为有效...\n");
        
        ret = ts_api_call("ota.validate", NULL, &result);

        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "验证失败");
            ts_api_result_free(&result);
            return 1;
        }

        printf("✓ 固件已标记为有效，回滚功能已禁用\n");
        ts_api_result_free(&result);
        return 0;
    }

    // --rollback: 回滚
    if (s_ota_args.rollback->count > 0) {
        printf("准备回滚到上一版本...\n");
        
        ret = ts_api_call("ota.rollback", NULL, &result);

        if (ret != ESP_OK || result.code != TS_API_OK) {
            if (result.code == TS_API_ERR_NOT_FOUND) {
                printf("错误: 没有可用的回滚分区\n");
            } else {
                printf("错误: %s\n", result.message ? result.message : "回滚失败");
            }
            ts_api_result_free(&result);
            return 1;
        }

        printf("回滚成功，设备即将重启...\n");
        ts_api_result_free(&result);
        return 0;
    }

    // --abort: 中止升级
    if (s_ota_args.abort_ota->count > 0) {
        printf("正在中止 OTA...\n");
        
        ret = ts_api_call("ota.abort", NULL, &result);

        if (ret != ESP_OK || result.code != TS_API_OK) {
            printf("错误: %s\n", result.message ? result.message : "中止失败");
            ts_api_result_free(&result);
            return 1;
        }

        printf("✓ OTA 已中止\n");
        ts_api_result_free(&result);
        return 0;
    }

    // 默认显示帮助
    printf("用法: ota --status | --server [url] | --url <url> | --file <path> | --validate | --rollback\n");
    printf("使用 'ota --help' 查看详细帮助\n");
    return 0;
}

// ============================================================================
//                           Command Registration
// ============================================================================

esp_err_t ts_cmd_ota_register(void)
{
    s_ota_args.status = arg_lit0(NULL, "status", "显示 OTA 状态");
    s_ota_args.progress = arg_lit0(NULL, "progress", "显示升级进度");
    s_ota_args.version = arg_lit0(NULL, "version", "显示固件版本");
    s_ota_args.partitions = arg_lit0(NULL, "partitions", "显示分区信息");
    s_ota_args.server = arg_str0(NULL, "server", "[url]", "获取/设置 OTA 服务器");
    s_ota_args.url = arg_str0(NULL, "url", "<url>", "从 HTTPS URL 升级");
    s_ota_args.file = arg_str0(NULL, "file", "<path>", "从 SD 卡文件升级");
    s_ota_args.validate = arg_lit0(NULL, "validate", "标记固件有效");
    s_ota_args.rollback = arg_lit0(NULL, "rollback", "回滚到上一版本");
    s_ota_args.abort_ota = arg_lit0(NULL, "abort", "中止升级");
    s_ota_args.no_reboot = arg_lit0(NULL, "no-reboot", "升级后不自动重启");
    s_ota_args.allow_downgrade = arg_lit0(NULL, "allow-downgrade", "允许降级");
    s_ota_args.skip_verify = arg_lit0(NULL, "skip-verify", "跳过证书验证");
    s_ota_args.save = arg_lit0(NULL, "save", "持久化配置到 NVS");
    s_ota_args.json = arg_lit0("j", "json", "JSON 格式输出");
    s_ota_args.help = arg_lit0("h", "help", "显示帮助");
    s_ota_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "ota",
        .help = "OTA 固件升级管理",
        .hint = NULL,
        .func = &cmd_ota_handler,
        .argtable = &s_ota_args,
    };

    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA command registered");
    }
    return ret;
}
