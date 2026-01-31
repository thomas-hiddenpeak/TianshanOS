/**
 * @file ts_ssh_log_watch.c
 * @brief SSH Service Log Watcher Implementation
 * 
 * Uses a dedicated task with large stack (8KB) for SSH operations
 * instead of running in esp_timer callback to avoid stack overflow.
 */

#include "ts_ssh_log_watch.h"
#include "ts_ssh_client.h"
#include "ts_ssh_hosts_config.h"
#include "ts_keystore.h"
#include "ts_variable.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ssh_log_watch";

/* Task stack size - SSH operations need large stack */
#define LOG_WATCH_TASK_STACK_SIZE  (8 * 1024)
#define LOG_WATCH_TASK_PRIORITY    5

/*===========================================================================*/
/*                          Internal Types                                    */
/*===========================================================================*/

typedef struct ts_ssh_log_watch_task {
    ts_ssh_log_watch_config_t config;
    uint32_t start_time;
    bool is_running;
    bool stop_requested;
    TaskHandle_t task_handle;
    struct ts_ssh_log_watch_task *next;
} ts_ssh_log_watch_task_t;

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static ts_ssh_log_watch_task_t *s_task_list = NULL;
static SemaphoreHandle_t s_mutex = NULL;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 初始化互斥锁
 */
static void ensure_mutex_init(void) {
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * @brief 更新变量状态
 */
static void update_status_var(const char *var_name, const char *status) {
    char full_name[64];
    snprintf(full_name, sizeof(full_name), "%s.status", var_name);
    
    esp_err_t ret = ts_variable_set_string(full_name, status);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set variable %s: %s", full_name, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Variable %s = %s", full_name, status);
    }
}

/**
 * @brief 更新就绪时间
 */
static void update_ready_time(const char *var_name) {
    char full_name[64];
    snprintf(full_name, sizeof(full_name), "%s.ready_time", var_name);
    
    time_t now = time(NULL);
    ts_variable_set_int(full_name, (int32_t)now);
}

/**
 * @brief 执行 SSH 命令读取日志
 */
static esp_err_t read_log_via_ssh(const char *host_id, const char *log_file, 
                                    char *output, size_t output_size) {
    // 获取主机配置
    ts_ssh_host_config_t host_cfg;
    esp_err_t ret = ts_ssh_hosts_config_get(host_id, &host_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host %s not found", host_id);
        return ret;
    }
    
    // 加载私钥
    char *key_data = NULL;
    size_t key_len = 0;
    if (host_cfg.keyid[0]) {
        ret = ts_keystore_load_private_key(host_cfg.keyid, &key_data, &key_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to load key '%s': %s", host_cfg.keyid, esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGE(TAG, "Host %s has no key configured", host_id);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 构建读取日志命令（tail -n 50 避免输出过多）
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "tail -n 50 %s 2>/dev/null || echo 'LOG_NOT_FOUND'", log_file);
    
    // 配置 SSH 连接
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host_cfg.host;
    config.port = host_cfg.port;
    config.username = host_cfg.username;
    config.timeout_ms = 5000;
    config.auth_method = TS_SSH_AUTH_PUBLICKEY;
    config.auth.key.private_key = (const uint8_t *)key_data;
    config.auth.key.private_key_len = key_len;
    
    // 创建 SSH 会话
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK || !session) {
        ESP_LOGE(TAG, "Failed to create SSH session: %s", esp_err_to_name(ret));
        free(key_data);
        return ret != ESP_OK ? ret : ESP_ERR_NO_MEM;
    }
    
    // 连接
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SSH connect failed: %s", esp_err_to_name(ret));
        ts_ssh_session_destroy(session);
        free(key_data);
        return ret;
    }
    
    // 执行命令
    ts_ssh_exec_result_t result = {0};
    ret = ts_ssh_exec(session, cmd, &result);
    
    if (ret == ESP_OK) {
        // 复制输出
        if (result.stdout_data && result.stdout_len > 0) {
            size_t copy_len = result.stdout_len < output_size - 1 ? result.stdout_len : output_size - 1;
            memcpy(output, result.stdout_data, copy_len);
            output[copy_len] = '\0';
        } else {
            output[0] = '\0';
        }
        
        if (result.stdout_data) free(result.stdout_data);
        if (result.stderr_data) free(result.stderr_data);
    } else {
        ESP_LOGW(TAG, "SSH exec failed: %s", esp_err_to_name(ret));
    }
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    free(key_data);
    
    return ret;
}

/**
 * @brief Log watch task - runs SSH operations in dedicated task with large stack
 */
static void log_watch_task(void *arg) {
    ts_ssh_log_watch_task_t *task = (ts_ssh_log_watch_task_t *)arg;
    
    ESP_LOGI(TAG, "Log watch task started: var=%s, pattern='%s', timeout=%us",
             task->config.var_name, task->config.ready_pattern, task->config.timeout_sec);
    
    // 分配日志缓冲区（使用 PSRAM）
    char *log_output = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!log_output) {
        log_output = malloc(4096);
    }
    if (!log_output) {
        ESP_LOGE(TAG, "Failed to allocate log buffer");
        update_status_var(task->config.var_name, "error");
        task->is_running = false;
        vTaskDelete(NULL);
        return;
    }
    
    while (task->is_running && !task->stop_requested) {
        // 检查超时
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000 - task->start_time;
        if (elapsed >= task->config.timeout_sec) {
            ESP_LOGW(TAG, "Log watch timeout for %s after %u seconds", 
                     task->config.var_name, elapsed);
            update_status_var(task->config.var_name, "timeout");
            break;
        }
        
        // 读取日志
        esp_err_t ret = read_log_via_ssh(task->config.host_id, task->config.log_file,
                                           log_output, 4096);
        
        if (ret == ESP_OK) {
            // 先检查失败模式（优先级高于就绪模式）
            if (task->config.fail_pattern[0] && 
                strstr(log_output, task->config.fail_pattern) != NULL) {
                ESP_LOGE(TAG, "❌ Service failed detected: '%s' matched in log", 
                         task->config.fail_pattern);
                update_status_var(task->config.var_name, "failed");
                break;
            }
            // 检查是否包含就绪模式
            if (strstr(log_output, task->config.ready_pattern) != NULL) {
                ESP_LOGI(TAG, "✅ Service ready detected: '%s' matched in log", 
                         task->config.ready_pattern);
                update_status_var(task->config.var_name, "ready");
                update_ready_time(task->config.var_name);
                break;
            } else {
                ESP_LOGD(TAG, "Pattern '%s' not found yet (elapsed: %us/%us)", 
                         task->config.ready_pattern, elapsed, task->config.timeout_sec);
            }
        } else {
            ESP_LOGW(TAG, "Failed to read log (will retry): %s", esp_err_to_name(ret));
        }
        
        // 等待下次检查
        vTaskDelay(pdMS_TO_TICKS(task->config.check_interval_ms));
    }
    
    free(log_output);
    task->is_running = false;
    
    ESP_LOGI(TAG, "Log watch task finished for %s", task->config.var_name);
    
    // 自动清理：从链表移除
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        ts_ssh_log_watch_task_t **pp = &s_task_list;
        while (*pp) {
            if (*pp == task) {
                *pp = task->next;
                break;
            }
            pp = &(*pp)->next;
        }
        xSemaphoreGive(s_mutex);
    }
    
    free(task);
    vTaskDelete(NULL);
}

/*===========================================================================*/
/*                          Public API                                        */
/*===========================================================================*/

esp_err_t ts_ssh_log_watch_start(const ts_ssh_log_watch_config_t *config,
                                   ts_ssh_log_watch_handle_t *out_handle) {
    if (!config || !config->host_id[0] || !config->log_file[0] || 
        !config->ready_pattern[0] || !config->var_name[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ensure_mutex_init();
    
    // 分配任务结构（使用 PSRAM）
    ts_ssh_log_watch_task_t *task = heap_caps_calloc(1, sizeof(ts_ssh_log_watch_task_t),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!task) {
        task = calloc(1, sizeof(ts_ssh_log_watch_task_t)); // Fallback to DRAM
        if (!task) {
            ESP_LOGE(TAG, "Failed to allocate task");
            return ESP_ERR_NO_MEM;
        }
    }
    
    // 复制配置
    memcpy(&task->config, config, sizeof(ts_ssh_log_watch_config_t));
    task->start_time = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    task->is_running = true;
    task->stop_requested = false;
    
    // 设置初始状态
    update_status_var(config->var_name, "checking");
    
    // 创建专用任务（使用较大的栈）
    BaseType_t ret = xTaskCreatePinnedToCore(
        log_watch_task,
        "ssh_log_watch",
        LOG_WATCH_TASK_STACK_SIZE,
        task,
        LOG_WATCH_TASK_PRIORITY,
        &task->task_handle,
        1  // Pin to core 1 to avoid blocking core 0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log watch task");
        free(task);
        return ESP_ERR_NO_MEM;
    }
    
    // 加入任务链表
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    task->next = s_task_list;
    s_task_list = task;
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Started log watch: var=%s, pattern='%s', timeout=%us, interval=%ums",
             config->var_name, config->ready_pattern, 
             config->timeout_sec, config->check_interval_ms);
    
    if (out_handle) {
        *out_handle = task;
    }
    
    return ESP_OK;
}

esp_err_t ts_ssh_log_watch_stop(ts_ssh_log_watch_handle_t handle) {
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ts_ssh_log_watch_task_t *task = (ts_ssh_log_watch_task_t *)handle;
    
    // 请求任务停止
    task->stop_requested = true;
    
    // 等待任务自行退出（最多等待 2 秒）
    int wait_count = 0;
    while (task->is_running && wait_count < 20) {
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    
    // 如果任务还在运行，强制删除
    if (task->is_running && task->task_handle) {
        ESP_LOGW(TAG, "Force deleting log watch task");
        vTaskDelete(task->task_handle);
        
        // 从链表移除
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        ts_ssh_log_watch_task_t **pp = &s_task_list;
        while (*pp) {
            if (*pp == task) {
                *pp = task->next;
                break;
            }
            pp = &(*pp)->next;
        }
        xSemaphoreGive(s_mutex);
        
        free(task);
    }
    
    ESP_LOGI(TAG, "Stopped log watch task");
    return ESP_OK;
}

bool ts_ssh_log_watch_is_running(const char *var_name) {
    if (!var_name || !s_mutex) {
        return false;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    ts_ssh_log_watch_task_t *task = s_task_list;
    while (task) {
        if (strcmp(task->config.var_name, var_name) == 0 && task->is_running) {
            xSemaphoreGive(s_mutex);
            return true;
        }
        task = task->next;
    }
    
    xSemaphoreGive(s_mutex);
    return false;
}

void ts_ssh_log_watch_stop_all(void) {
    if (!s_mutex) {
        return;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    ts_ssh_log_watch_task_t *task = s_task_list;
    while (task) {
        ts_ssh_log_watch_task_t *next = task->next;
        
        task->stop_requested = true;
        
        // 等待一小段时间让任务退出
        xSemaphoreGive(s_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        
        if (task->is_running && task->task_handle) {
            vTaskDelete(task->task_handle);
            free(task);
        }
        
        task = next;
    }
    
    s_task_list = NULL;
    xSemaphoreGive(s_mutex);
    
    ESP_LOGI(TAG, "Stopped all log watch tasks");
}
