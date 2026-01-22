/**
 * @file ts_ota_https.c
 * @brief TianShanOS OTA HTTPS Download Implementation
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "ts_ota.h"
#include "ts_event.h"

static const char *TAG = "ts_ota_https";

// OTA task handle
static TaskHandle_t s_ota_task_handle = NULL;
static ts_ota_config_t s_ota_config;
static bool s_ota_running = false;

// Forward declarations
static void https_ota_task(void *arg);
static esp_err_t http_event_handler(esp_http_client_event_t *evt);

/**
 * @brief Start HTTPS OTA update
 */
esp_err_t ts_ota_start_https(const ts_ota_config_t *config)
{
    if (!config || !config->url) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ota_running) {
        ESP_LOGE(TAG, "OTA already running");
        return ESP_ERR_INVALID_STATE;
    }

    // Copy config
    memcpy(&s_ota_config, config, sizeof(ts_ota_config_t));
    
    // Create OTA task
    BaseType_t ret = xTaskCreate(
        https_ota_task,
        "ota_https",
        CONFIG_TS_OTA_TASK_STACK_SIZE,
        NULL,
        CONFIG_TS_OTA_TASK_PRIORITY,
        &s_ota_task_handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create OTA task");
        return ESP_ERR_NO_MEM;
    }

    s_ota_running = true;
    return ESP_OK;
}

/**
 * @brief HTTP event handler for progress tracking
 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER: %s: %s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * @brief HTTPS OTA download task
 */
static void https_ota_task(void *arg)
{
    esp_err_t ret;
    
    ESP_LOGI(TAG, "Starting HTTPS OTA from: %s", s_ota_config.url);

    // Configure HTTP client
    esp_http_client_config_t http_config = {
        .url = s_ota_config.url,
        .event_handler = http_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = 30000,
    };

    // Certificate configuration
    if (s_ota_config.cert_pem) {
        http_config.cert_pem = s_ota_config.cert_pem;
    } else if (!s_ota_config.skip_cert_verify) {
        // Use certificate bundle for verification
        http_config.crt_bundle_attach = esp_crt_bundle_attach;
    }

    if (s_ota_config.skip_cert_verify) {
        http_config.skip_cert_common_name_check = true;
        ESP_LOGW(TAG, "Certificate verification disabled - NOT RECOMMENDED for production!");
    }

    // Configure HTTPS OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    
    ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Get image info
    esp_app_desc_t app_desc;
    ret = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    ESP_LOGI(TAG, "New firmware: %s, version: %s", app_desc.project_name, app_desc.version);

    // Version check (if enabled)
    #if CONFIG_TS_OTA_VERSION_CHECK
    if (!s_ota_config.allow_downgrade) {
        const esp_app_desc_t *running_app = esp_app_get_description();
        int cmp = ts_ota_compare_versions(app_desc.version, running_app->version);
        if (cmp <= 0) {
            ESP_LOGW(TAG, "New version (%s) is not newer than current (%s)",
                     app_desc.version, running_app->version);
            if (cmp < 0) {
                ESP_LOGE(TAG, "Downgrade not allowed");
                esp_https_ota_abort(https_ota_handle);
                ret = ESP_ERR_INVALID_VERSION;
                goto cleanup;
            }
        }
    }
    #endif

    // Get total size
    int image_size = esp_https_ota_get_image_size(https_ota_handle);
    ESP_LOGI(TAG, "Image size: %d bytes", image_size);

    // Download and write firmware
    size_t received = 0;
    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Update progress
        received = esp_https_ota_get_image_len_read(https_ota_handle);
        
        if (image_size > 0) {
            int percent = (received * 100) / image_size;
            ESP_LOGI(TAG, "Downloaded: %zu / %d bytes (%d%%)", received, image_size, percent);
        } else {
            ESP_LOGI(TAG, "Downloaded: %zu bytes", received);
        }

        // Call progress callback
        if (s_ota_config.progress_cb) {
            ts_ota_progress_t progress = {
                .state = TS_OTA_STATE_DOWNLOADING,
                .error = TS_OTA_ERR_NONE,
                .total_size = image_size,
                .received_size = received,
                .progress_percent = (image_size > 0) ? (received * 100 / image_size) : 0,
                .status_msg = "正在下载..."
            };
            s_ota_config.progress_cb(&progress, s_ota_config.user_data);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        goto cleanup;
    }

    // Verify and finish
    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Complete data was not received");
        esp_https_ota_abort(https_ota_handle);
        ret = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }

    ret = esp_https_ota_finish(https_ota_handle);
    if (ret != ESP_OK) {
        // ESP_ERR_OTA_VALIDATE_FAILED is 0x1509
        if (ret == (esp_err_t)0x1509) {
            ESP_LOGE(TAG, "Image validation failed");
        } else {
            ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
        }
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA update successful!");

    // Post completion event
    ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_COMPLETED, NULL, 0, 0);

    // Notify callback
    if (s_ota_config.progress_cb) {
        ts_ota_progress_t progress = {
            .state = TS_OTA_STATE_PENDING_REBOOT,
            .error = TS_OTA_ERR_NONE,
            .total_size = image_size,
            .received_size = received,
            .progress_percent = 100,
            .status_msg = "升级完成，等待重启"
        };
        s_ota_config.progress_cb(&progress, s_ota_config.user_data);
    }

    // Auto reboot if requested
    if (s_ota_config.auto_reboot) {
        ESP_LOGI(TAG, "Rebooting in 2 seconds...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }

cleanup:
    if (ret != ESP_OK) {
        // Post failure event
        ts_ota_error_t error = TS_OTA_ERR_DOWNLOAD_FAILED;
        ts_event_post(TS_EVENT_BASE_OTA, TS_EVENT_OTA_FAILED, &error, sizeof(error), 0);

        if (s_ota_config.progress_cb) {
            ts_ota_progress_t progress = {
                .state = TS_OTA_STATE_ERROR,
                .error = error,
                .status_msg = "下载失败"
            };
            s_ota_config.progress_cb(&progress, s_ota_config.user_data);
        }
    }

    s_ota_running = false;
    s_ota_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Check for updates from URL
 */
esp_err_t ts_ota_check_update_https(const char *url, bool *available,
                                     char *new_version, size_t new_version_len)
{
    if (!url || !available) {
        return ESP_ERR_INVALID_ARG;
    }

    *available = false;

    // This would typically check a version manifest or firmware header
    // For now, we'll just try to get the firmware header

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        return ESP_ERR_NO_MEM;
    }

    // Set range header to get only the app description
    esp_http_client_set_header(client, "Range", "bytes=0-288");
    
    esp_err_t ret = esp_http_client_perform(client);
    if (ret == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200 || status == 206) {
            // In a real implementation, we'd parse the firmware header
            // and compare versions
            *available = true;
            if (new_version && new_version_len > 0) {
                strncpy(new_version, "检测到新版本", new_version_len - 1);
            }
        }
    }

    esp_http_client_cleanup(client);
    return ret;
}
