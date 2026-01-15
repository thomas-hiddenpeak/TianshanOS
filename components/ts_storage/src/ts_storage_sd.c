/**
 * @file ts_storage_sd.c
 * @brief SD Card Storage Implementation
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_storage.h"
#include "ts_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include <string.h>

#define TAG "storage_sd"

/**
 * @brief 安全喂看门狗 - 仅当任务已注册时才调用
 * 
 * 在 ESP-IDF v5.x 中，如果任务未注册到 TWDT，调用 esp_task_wdt_reset() 会报错。
 * 使用 taskYIELD() 作为替代方案，让出 CPU 使 IDLE 任务有机会喂狗。
 */
static inline void sd_yield_to_wdt(void)
{
    taskYIELD();
}

/*===========================================================================*/
/*                          External Functions                                */
/*===========================================================================*/

extern void ts_storage_set_sd_mounted(bool mounted, const char *mount_point);

/*===========================================================================*/
/*                          Private Data                                      */
/*===========================================================================*/

static char s_mount_point[32] = "";
static sdmmc_card_t *s_card = NULL;

/*===========================================================================*/
/*                          SD Card Operations                                */
/*===========================================================================*/

esp_err_t ts_storage_mount_sd(const ts_sd_config_t *config)
{
    if (s_card != NULL) {
        TS_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }
    
    ts_sd_config_t cfg;
    
    if (config) {
        cfg = *config;
    } else {
        /* Use defaults from Kconfig */
#ifdef CONFIG_TS_STORAGE_SD_MOUNT_POINT
        cfg.mount_point = CONFIG_TS_STORAGE_SD_MOUNT_POINT;
#else
        cfg.mount_point = "/sdcard";
#endif

#ifdef CONFIG_TS_STORAGE_SD_MAX_FREQ_KHZ
        cfg.max_freq_khz = CONFIG_TS_STORAGE_SD_MAX_FREQ_KHZ;
#else
        cfg.max_freq_khz = 20000;
#endif

#if defined(CONFIG_TS_STORAGE_SD_MODE_SPI)
        cfg.mode = TS_SD_MODE_SPI;
#elif defined(CONFIG_TS_STORAGE_SD_MODE_SDIO_1BIT)
        cfg.mode = TS_SD_MODE_SDIO_1BIT;
#else
        cfg.mode = TS_SD_MODE_SDIO_4BIT;
#endif
        cfg.format_if_mount_failed = false;
        
        /* SDIO GPIO configuration from Kconfig */
#ifdef CONFIG_TS_STORAGE_SD_CMD_GPIO
        cfg.pin_cmd = CONFIG_TS_STORAGE_SD_CMD_GPIO;
#else
        cfg.pin_cmd = -1;
#endif
#ifdef CONFIG_TS_STORAGE_SD_CLK_GPIO
        cfg.pin_clk = CONFIG_TS_STORAGE_SD_CLK_GPIO;
#else
        cfg.pin_clk = -1;
#endif
#ifdef CONFIG_TS_STORAGE_SD_D0_GPIO
        cfg.pin_d0 = CONFIG_TS_STORAGE_SD_D0_GPIO;
#else
        cfg.pin_d0 = -1;
#endif
#ifdef CONFIG_TS_STORAGE_SD_D1_GPIO
        cfg.pin_d1 = CONFIG_TS_STORAGE_SD_D1_GPIO;
#else
        cfg.pin_d1 = -1;
#endif
#ifdef CONFIG_TS_STORAGE_SD_D2_GPIO
        cfg.pin_d2 = CONFIG_TS_STORAGE_SD_D2_GPIO;
#else
        cfg.pin_d2 = -1;
#endif
#ifdef CONFIG_TS_STORAGE_SD_D3_GPIO
        cfg.pin_d3 = CONFIG_TS_STORAGE_SD_D3_GPIO;
#else
        cfg.pin_d3 = -1;
#endif
    }
    
    TS_LOGI(TAG, "Mounting SD card at %s (mode: %d, freq: %d kHz)", 
            cfg.mount_point, cfg.mode, cfg.max_freq_khz);
    TS_LOGI(TAG, "SD GPIO: CMD=%d, CLK=%d, D0=%d, D1=%d, D2=%d, D3=%d",
            cfg.pin_cmd, cfg.pin_clk, cfg.pin_d0, cfg.pin_d1, cfg.pin_d2, cfg.pin_d3);
    
    // 让出 CPU，防止长时间操作阻塞其他任务
    sd_yield_to_wdt();
    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = cfg.format_if_mount_failed,
        .max_files = 10,
        .allocation_unit_size = 8 * 1024,
        .disk_status_check_enable = true
    };
    
    esp_err_t ret;
    
    if (cfg.mode == TS_SD_MODE_SPI) {
        /* SPI mode */
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.max_freq_khz = cfg.max_freq_khz;
        
        spi_bus_config_t bus_cfg = {
            .mosi_io_num = cfg.pin_mosi,
            .miso_io_num = cfg.pin_miso,
            .sclk_io_num = cfg.pin_clk,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
        };
        
        ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
            return ret;
        }
        
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = cfg.pin_cs;
        slot_config.host_id = host.slot;
        
        ret = esp_vfs_fat_sdspi_mount(cfg.mount_point, &host, &slot_config, 
                                       &mount_config, &s_card);
    } else {
        /* SDIO mode */
        sdmmc_host_t host = SDMMC_HOST_DEFAULT();
        host.max_freq_khz = cfg.max_freq_khz;
        
        sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
        
        if (cfg.mode == TS_SD_MODE_SDIO_1BIT) {
            slot_config.width = 1;
        } else {
            slot_config.width = 4;
        }
        
        /* Set GPIO pins if specified */
        if (cfg.pin_cmd >= 0) slot_config.cmd = cfg.pin_cmd;
        if (cfg.pin_clk >= 0) slot_config.clk = cfg.pin_clk;
        if (cfg.pin_d0 >= 0) slot_config.d0 = cfg.pin_d0;
        if (cfg.pin_d1 >= 0) slot_config.d1 = cfg.pin_d1;
        if (cfg.pin_d2 >= 0) slot_config.d2 = cfg.pin_d2;
        if (cfg.pin_d3 >= 0) slot_config.d3 = cfg.pin_d3;
        
        slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
        
        // 让出 CPU
        sd_yield_to_wdt();
        
        ret = esp_vfs_fat_sdmmc_mount(cfg.mount_point, &host, &slot_config, 
                                       &mount_config, &s_card);
        
        // 挂载后让出 CPU
        sd_yield_to_wdt();
    }
    
    if (ret != ESP_OK) {
        // 让出 CPU，确保错误处理期间不阻塞
        sd_yield_to_wdt();
        
        // 根据错误类型提供更友好的错误信息
        switch (ret) {
            case ESP_ERR_TIMEOUT:
                TS_LOGW(TAG, "No SD card detected - slot may be empty");
                break;
            case ESP_ERR_INVALID_STATE:
                TS_LOGW(TAG, "SD card slot already in use or hardware conflict");
                break;
            case ESP_ERR_NOT_FOUND:
                TS_LOGW(TAG, "SD card not responding or unsupported");
                break;
            case ESP_ERR_NOT_SUPPORTED:
                TS_LOGW(TAG, "SD card format not supported - may need formatting");
                break;
            case ESP_FAIL:
                TS_LOGE(TAG, "Failed to mount filesystem");
                break;
            default:
                TS_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
                break;
        }
        s_card = NULL;
        // 给 SDMMC 驱动时间完成清理
        vTaskDelay(pdMS_TO_TICKS(100));
        return ret;
    }
    
    /* Store mount info */
    strncpy(s_mount_point, cfg.mount_point, sizeof(s_mount_point) - 1);
    
    ts_storage_set_sd_mounted(true, s_mount_point);
    
    /* Log card info */
    TS_LOGI(TAG, "SD card mounted:");
    TS_LOGI(TAG, "  Name: %s", s_card->cid.name);
    TS_LOGI(TAG, "  Type: %s", (s_card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC");
    TS_LOGI(TAG, "  Speed: %s", (s_card->csd.tr_speed > 25000000) ? "high speed" : "default");
    TS_LOGI(TAG, "  Size: %lluMB", ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    
    return ESP_OK;
}

esp_err_t ts_storage_unmount_sd(void)
{
    if (s_card == NULL) {
        return ESP_OK;
    }
    
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to unmount SD card: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_card = NULL;
    ts_storage_set_sd_mounted(false, NULL);
    
    TS_LOGI(TAG, "SD card unmounted");
    
    return ESP_OK;
}

bool ts_storage_sd_mounted(void)
{
    return (s_card != NULL);
}

esp_err_t ts_storage_sd_stats(ts_storage_stats_t *stats)
{
    if (stats == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    FATFS *fs;
    DWORD free_clusters;
    
    FRESULT res = f_getfree(s_mount_point, &free_clusters, &fs);
    if (res != FR_OK) {
        return ESP_FAIL;
    }
    
    uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors = free_clusters * fs->csize;
    uint32_t sector_size = s_card->csd.sector_size;
    
    stats->total_bytes = total_sectors * sector_size;
    stats->free_bytes = free_sectors * sector_size;
    stats->used_bytes = stats->total_bytes - stats->free_bytes;
    
    return ESP_OK;
}

esp_err_t ts_storage_format_sd(void)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    TS_LOGW(TAG, "Formatting SD card...");
    
    /* Unmount first */
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Format */
    const size_t workbuf_size = 4096;
    void *workbuf = malloc(workbuf_size);
    if (workbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    MKFS_PARM mkfs_parm = {
        .fmt = FM_FAT32,
        .n_fat = 0,
        .align = 0,
        .n_root = 0,
        .au_size = 0
    };
    FRESULT res = f_mkfs("", &mkfs_parm, workbuf, workbuf_size);
    free(workbuf);
    
    if (res != FR_OK) {
        TS_LOGE(TAG, "Failed to format: %d", res);
        return ESP_FAIL;
    }
    
    /* Remount */
    s_card = NULL;
    ret = ts_storage_mount_sd(NULL);
    
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "SD card formatted and remounted");
    }
    
    return ret;
}

esp_err_t ts_storage_sd_info(uint64_t *capacity, size_t *sector_size)
{
    if (s_card == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (capacity) {
        *capacity = (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
    }
    
    if (sector_size) {
        *sector_size = s_card->csd.sector_size;
    }
    
    return ESP_OK;
}
