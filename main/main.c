/**
 * @file main.c
 * @brief TianShanOS Main Entry Point
 *
 * TianShanOS 主程序入口
 *
 * @author TianShanOS Team
 * @version 0.1.0
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "ts_core.h"
#include "ts_services.h"

static const char *TAG = "main";

/*
 * 极早期 GPIO 初始化（在 app_main 之前执行）
 * 
 * 使用 __attribute__((constructor)) 确保在 main 函数之前执行。
 * 这是确保关键 GPIO 引脚从上电开始就处于正确状态的最早时机。
 * 
 * 关键引脚:
 * - GPIO3 (AGX_FORCE_SHUTDOWN): LOW=允许开机, HIGH=强制关机
 *   必须保持 LOW，否则 AGX 无法启动
 * 
 * - GPIO1 (AGX_RESET): HIGH=复位, LOW=正常
 *   必须保持 LOW，避免意外复位
 * 
 * 注意: 这里只做最小初始化，完整的 GPIO 配置由 ts_hal 处理
 */
__attribute__((constructor(101)))
static void early_critical_gpio_init(void)
{
    /*
     * GPIO3: AGX_FORCE_SHUTDOWN
     * LOW = 允许 AGX 开机
     * HIGH = 强制关闭 AGX
     * 
     * 先设置电平再配置方向，避免毛刺
     */
    gpio_set_level(GPIO_NUM_3, 0);  // LOW = allow AGX boot
    gpio_config_t io_conf_gpio3 = {
        .pin_bit_mask = (1ULL << GPIO_NUM_3),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_gpio3);
    
    /*
     * GPIO1: AGX_RESET
     * HIGH = 复位 AGX
     * LOW = 正常运行
     */
    gpio_set_level(GPIO_NUM_1, 0);  // LOW = normal (no reset)
    gpio_config_t io_conf_gpio1 = {
        .pin_bit_mask = (1ULL << GPIO_NUM_1),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_gpio1);
}

/**
 * @brief 打印启动横幅
 */
static void print_banner(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                        ║\n");
    printf("║   ████████╗██╗ █████╗ ███╗   ██╗███████╗██╗  ██╗ █████╗ ███╗   ██╗     ║\n");
    printf("║   ╚══██╔══╝██║██╔══██╗████╗  ██║██╔════╝██║  ██║██╔══██╗████╗  ██║     ║\n");
    printf("║      ██║   ██║███████║██╔██╗ ██║███████╗███████║███████║██╔██╗ ██║     ║\n");
    printf("║      ██║   ██║██╔══██║██║╚██╗██║╚════██║██╔══██║██╔══██║██║╚██╗██║     ║\n");
    printf("║      ██║   ██║██║  ██║██║ ╚████║███████║██║  ██║██║  ██║██║ ╚████║     ║\n");
    printf("║      ╚═╝   ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝  ╚═══╝     ║\n");
    printf("║                                                                        ║\n");
    printf("║                          TianShanOS v%s                         ║\n", TIANSHAN_OS_VERSION_STRING);
    printf("║                     ESP32 Rack Management Operating System             ║\n");
    printf("║                                                                        ║\n");
    printf("╚════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/**
 * @brief 打印系统信息
 */
static void print_system_info(void)
{
    ESP_LOGI(TAG, "System Information:");
    ESP_LOGI(TAG, "  - IDF Version: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "  - Free Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "  - Min Free Heap: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    ESP_LOGI(TAG, "  - Chip: %s, %d cores, WiFi%s%s",
             (chip_info.model == CHIP_ESP32S3) ? "ESP32-S3" :
             (chip_info.model == CHIP_ESP32) ? "ESP32" : "Unknown",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
}

/**
 * @brief 应用程序主入口
 */
void app_main(void)
{
    esp_err_t ret;

    // 打印启动横幅
    print_banner();

    // 打印系统信息
    print_system_info();

    // 初始化 TianShanOS 核心
    ESP_LOGI(TAG, "Initializing TianShanOS...");
    ret = ts_core_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize TianShanOS core: %s", esp_err_to_name(ret));
        return;
    }

    // 注册所有核心服务
    ESP_LOGI(TAG, "Registering core services...");
    ret = ts_services_register_all();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register services: %s", esp_err_to_name(ret));
        return;
    }

    // 启动所有服务
    ESP_LOGI(TAG, "Starting TianShanOS services...");
    ret = ts_core_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TianShanOS: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "TianShanOS started successfully!");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);

    // 打印服务状态
    ts_service_dump();

    // 主循环
    ESP_LOGI(TAG, "Entering main loop...");

    while (1) {
        // 定期打印堆信息（调试用）
        vTaskDelay(pdMS_TO_TICKS(60000));  // 每分钟

#ifdef CONFIG_TS_LOG_DEBUG
        ESP_LOGI(TAG, "Heap: free=%lu, min=%lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
#endif
    }
}
