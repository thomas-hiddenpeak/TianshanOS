/**
 * @file ts_cmd_system.c
 * @brief System Console Commands
 * 
 * 实现 system 命令族：
 * - system --info        显示系统信息
 * - system --version     显示版本
 * - system --uptime      显示运行时间
 * - system --memory      显示内存使用
 * - system --tasks       显示任务列表
 * - system --reboot      重启系统
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_system"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *info;
    struct arg_lit *version;
    struct arg_lit *uptime;
    struct arg_lit *memory;
    struct arg_lit *tasks;
    struct arg_lit *reboot;
    struct arg_int *delay;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_system_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static void format_uptime(uint64_t uptime_us, char *buf, size_t len)
{
    uint64_t seconds = uptime_us / 1000000;
    uint32_t days = seconds / 86400;
    uint32_t hours = (seconds % 86400) / 3600;
    uint32_t mins = (seconds % 3600) / 60;
    uint32_t secs = seconds % 60;
    
    if (days > 0) {
        snprintf(buf, len, "%lud %02lu:%02lu:%02lu", 
            (unsigned long)days, (unsigned long)hours, 
            (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buf, len, "%02lu:%02lu:%02lu", 
            (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
}

static void format_size(size_t bytes, char *buf, size_t len)
{
    if (bytes >= 1024 * 1024) {
        snprintf(buf, len, "%.1f MB", (float)bytes / (1024 * 1024));
    } else if (bytes >= 1024) {
        snprintf(buf, len, "%.1f KB", (float)bytes / 1024);
    } else {
        snprintf(buf, len, "%zu B", bytes);
    }
}

/*===========================================================================*/
/*                          Command: system --info                            */
/*===========================================================================*/

static int do_system_info(bool json)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    
    const esp_app_desc_t *app = esp_app_get_description();
    
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    
    uint64_t uptime_us = esp_timer_get_time();
    char uptime_str[32];
    format_uptime(uptime_us, uptime_str, sizeof(uptime_str));
    
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    char free_heap_str[16], min_heap_str[16], flash_str[16];
    format_size(free_heap, free_heap_str, sizeof(free_heap_str));
    format_size(min_heap, min_heap_str, sizeof(min_heap_str));
    format_size(flash_size, flash_str, sizeof(flash_str));
    
    const char *chip_model;
    switch (chip.model) {
        case CHIP_ESP32:   chip_model = "ESP32"; break;
        case CHIP_ESP32S2: chip_model = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_model = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_model = "ESP32-C3"; break;
        case CHIP_ESP32C6: chip_model = "ESP32-C6"; break;
        case CHIP_ESP32H2: chip_model = "ESP32-H2"; break;
        default:           chip_model = "Unknown"; break;
    }
    
    if (json) {
        ts_console_printf(
            "{\"app\":{\"name\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\"},"
            "\"chip\":{\"model\":\"%s\",\"cores\":%d,\"revision\":%d},"
            "\"flash_size\":%lu,"
            "\"heap\":{\"free\":%zu,\"min\":%zu},"
            "\"uptime_us\":%llu}\n",
            app->project_name, app->version, app->idf_ver,
            chip_model, chip.cores, chip.revision,
            (unsigned long)flash_size,
            free_heap, min_heap,
            (unsigned long long)uptime_us);
    } else {
        ts_console_printf("\n");
        ts_console_printf("╔══════════════════════════════════════════════════╗\n");
        ts_console_printf("║           TianShanOS System Information          ║\n");
        ts_console_printf("╚══════════════════════════════════════════════════╝\n\n");
        
        ts_console_printf("Application:\n");
        ts_console_printf("  Name:      %s\n", app->project_name);
        ts_console_printf("  Version:   %s\n", app->version);
        ts_console_printf("  IDF Ver:   %s\n", app->idf_ver);
        ts_console_printf("  Compiled:  %s %s\n", app->date, app->time);
        ts_console_printf("\n");
        
        ts_console_printf("Hardware:\n");
        ts_console_printf("  Chip:      %s\n", chip_model);
        ts_console_printf("  Cores:     %d\n", chip.cores);
        ts_console_printf("  Revision:  %d\n", chip.revision);
        ts_console_printf("  Flash:     %s\n", flash_str);
        ts_console_printf("\n");
        
        ts_console_printf("Runtime:\n");
        ts_console_printf("  Uptime:    %s\n", uptime_str);
        ts_console_printf("  Free Heap: %s\n", free_heap_str);
        ts_console_printf("  Min Heap:  %s\n", min_heap_str);
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: system --version                         */
/*===========================================================================*/

static int do_system_version(bool json)
{
    const esp_app_desc_t *app = esp_app_get_description();
    
    if (json) {
        ts_console_printf("{\"name\":\"%s\",\"version\":\"%s\",\"idf\":\"%s\"}\n",
            app->project_name, app->version, app->idf_ver);
    } else {
        ts_console_printf("%s v%s (ESP-IDF %s)\n", 
            app->project_name, app->version, app->idf_ver);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: system --uptime                          */
/*===========================================================================*/

static int do_system_uptime(bool json)
{
    uint64_t uptime_us = esp_timer_get_time();
    char uptime_str[32];
    format_uptime(uptime_us, uptime_str, sizeof(uptime_str));
    
    if (json) {
        ts_console_printf("{\"uptime_us\":%llu,\"uptime_str\":\"%s\"}\n",
            (unsigned long long)uptime_us, uptime_str);
    } else {
        ts_console_printf("Uptime: %s\n", uptime_str);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: system --memory                          */
/*===========================================================================*/

static int do_system_memory(bool json)
{
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    
    // PSRAM 信息
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    // DMA 内存
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t total_dma = heap_caps_get_total_size(MALLOC_CAP_DMA);
    
    if (json) {
        ts_console_printf(
            "{\"heap\":{\"free\":%zu,\"min\":%zu,\"total\":%zu},"
            "\"psram\":{\"free\":%zu,\"total\":%zu},"
            "\"dma\":{\"free\":%zu,\"total\":%zu}}\n",
            free_heap, min_heap, total_heap,
            free_psram, total_psram,
            free_dma, total_dma);
    } else {
        char buf1[16], buf2[16], buf3[16];
        
        ts_console_printf("Memory Usage:\n\n");
        ts_console_printf("%-10s  %12s  %12s  %8s\n", 
            "TYPE", "FREE", "TOTAL", "USED%");
        ts_console_printf("────────────────────────────────────────────────\n");
        
        if (total_heap > 0) {
            format_size(free_heap, buf1, sizeof(buf1));
            format_size(total_heap, buf2, sizeof(buf2));
            int used_pct = 100 - (free_heap * 100 / total_heap);
            ts_console_printf("%-10s  %12s  %12s  %7d%%\n", 
                "Heap", buf1, buf2, used_pct);
        }
        
        if (total_psram > 0) {
            format_size(free_psram, buf1, sizeof(buf1));
            format_size(total_psram, buf2, sizeof(buf2));
            int used_pct = 100 - (free_psram * 100 / total_psram);
            ts_console_printf("%-10s  %12s  %12s  %7d%%\n", 
                "PSRAM", buf1, buf2, used_pct);
        }
        
        if (total_dma > 0) {
            format_size(free_dma, buf1, sizeof(buf1));
            format_size(total_dma, buf2, sizeof(buf2));
            int used_pct = 100 - (free_dma * 100 / total_dma);
            ts_console_printf("%-10s  %12s  %12s  %7d%%\n", 
                "DMA", buf1, buf2, used_pct);
        }
        
        format_size(min_heap, buf1, sizeof(buf1));
        ts_console_printf("\nMinimum free heap ever: %s\n", buf1);
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: system --tasks                           */
/*===========================================================================*/

static int do_system_tasks(bool json)
{
    uint32_t num_tasks = uxTaskGetNumberOfTasks();
    
    if (json) {
        ts_console_printf("{\"task_count\":%lu,\"tasks\":[", (unsigned long)num_tasks);
    } else {
        ts_console_printf("Tasks (%lu total):\n\n", (unsigned long)num_tasks);
        ts_console_printf("%-20s  %5s  %6s  %10s\n", 
            "NAME", "PRI", "STATE", "STACK");
        ts_console_printf("──────────────────────────────────────────────\n");
    }
    
#if configUSE_TRACE_FACILITY
    TaskStatus_t *tasks = malloc(num_tasks * sizeof(TaskStatus_t));
    if (!tasks) {
        ts_console_error("Out of memory\n");
        return 1;
    }
    
    uint32_t total_runtime;
    uint32_t count = uxTaskGetSystemState(tasks, num_tasks, &total_runtime);
    
    bool first = true;
    for (uint32_t i = 0; i < count; i++) {
        const char *state_str;
        switch (tasks[i].eCurrentState) {
            case eRunning:   state_str = "RUN"; break;
            case eReady:     state_str = "READY"; break;
            case eBlocked:   state_str = "BLOCK"; break;
            case eSuspended: state_str = "SUSP"; break;
            case eDeleted:   state_str = "DEL"; break;
            default:         state_str = "?"; break;
        }
        
        if (json) {
            if (!first) ts_console_printf(",");
            ts_console_printf("{\"name\":\"%s\",\"priority\":%lu,\"state\":\"%s\",\"stack\":%lu}",
                tasks[i].pcTaskName,
                (unsigned long)tasks[i].uxCurrentPriority,
                state_str,
                (unsigned long)tasks[i].usStackHighWaterMark);
            first = false;
        } else {
            ts_console_printf("%-20s  %5lu  %6s  %10lu\n",
                tasks[i].pcTaskName,
                (unsigned long)tasks[i].uxCurrentPriority,
                state_str,
                (unsigned long)tasks[i].usStackHighWaterMark);
        }
    }
    
    free(tasks);
#else
    (void)num_tasks;
    ts_console_printf("Task tracing not enabled\n");
#endif
    
    if (json) {
        ts_console_printf("]}\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: system --reboot                          */
/*===========================================================================*/

static int do_system_reboot(int delay_sec)
{
    if (delay_sec > 0) {
        ts_console_printf("System will reboot in %d seconds...\n", delay_sec);
        vTaskDelay(pdMS_TO_TICKS(delay_sec * 1000));
    } else {
        ts_console_printf("Rebooting...\n");
        vTaskDelay(pdMS_TO_TICKS(100));  // 让消息打印出来
    }
    
    esp_restart();
    
    // 不会执行到这里
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_system(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_system_args);
    
    // 显示帮助
    if (s_system_args.help->count > 0) {
        ts_console_printf("Usage: system [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -i, --info          Show system information\n");
        ts_console_printf("  -V, --version       Show version\n");
        ts_console_printf("  -u, --uptime        Show uptime\n");
        ts_console_printf("  -m, --memory        Show memory usage\n");
        ts_console_printf("  -t, --tasks         Show task list\n");
        ts_console_printf("  -r, --reboot        Reboot system\n");
        ts_console_printf("      --delay <sec>   Delay before reboot\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  system --info\n");
        ts_console_printf("  system --memory --json\n");
        ts_console_printf("  system --reboot --delay 5\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_system_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_system_args.json->count > 0;
    
    // 重启命令优先
    if (s_system_args.reboot->count > 0) {
        int delay = s_system_args.delay->count > 0 ? 
                    s_system_args.delay->ival[0] : 0;
        return do_system_reboot(delay);
    }
    
    if (s_system_args.version->count > 0) {
        return do_system_version(json);
    }
    
    if (s_system_args.uptime->count > 0) {
        return do_system_uptime(json);
    }
    
    if (s_system_args.memory->count > 0) {
        return do_system_memory(json);
    }
    
    if (s_system_args.tasks->count > 0) {
        return do_system_tasks(json);
    }
    
    // 默认显示系统信息
    return do_system_info(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_system_register(void)
{
    s_system_args.info    = arg_lit0("i", "info", "Show system info");
    s_system_args.version = arg_lit0("V", "version", "Show version");
    s_system_args.uptime  = arg_lit0("u", "uptime", "Show uptime");
    s_system_args.memory  = arg_lit0("m", "memory", "Show memory usage");
    s_system_args.tasks   = arg_lit0("t", "tasks", "Show tasks");
    s_system_args.reboot  = arg_lit0("r", "reboot", "Reboot system");
    s_system_args.delay   = arg_int0(NULL, "delay", "<sec>", "Delay before reboot");
    s_system_args.json    = arg_lit0("j", "json", "JSON output");
    s_system_args.help    = arg_lit0("h", "help", "Show help");
    s_system_args.end     = arg_end(10);
    
    const ts_console_cmd_t cmd = {
        .command = "system",
        .help = "System information and control",
        .hint = NULL,
        .category = TS_CMD_CAT_SYSTEM,
        .func = cmd_system,
        .argtable = &s_system_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "System commands registered");
    }
    
    return ret;
}
