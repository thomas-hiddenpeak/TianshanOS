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
 * @brief 执行 SSH 命令读取日志（改用 grep 在远程搜索模式）
 * @param host_id 主机 ID
 * @param log_file 日志文件路径
 * @param ready_pattern 就绪模式（为空则跳过检查）
 * @param fail_pattern 失败模式（为空则跳过检查）
 * @param result 输出：0=未找到, 1=找到ready, -1=找到fail, -2=文件不存在
 */
static esp_err_t check_log_pattern_via_ssh(const char *host_id, const char *log_file,
                                            const char *ready_pattern, const char *fail_pattern,
                                            int *result) {
    ESP_LOGI(TAG, "Checking log: host_id='%s', log_file='%s'", host_id, log_file);
    *result = 0;
    
    // 获取主机配置
    ts_ssh_host_config_t host_cfg;
    esp_err_t ret = ts_ssh_hosts_config_get(host_id, &host_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Host '%s' not found: %s", host_id, esp_err_to_name(ret));
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
    
    // 构建检查命令：先检查文件是否存在，再用 grep 搜索模式
    // grep -q 静默模式，找到返回 0，未找到返回 1
    // 输出格式: "EXISTS:READY:FAIL" (1/0 表示是否匹配)
    char cmd[1024];
    
    // 转义 pattern 中的单引号（简单替换为 '\''）
    char escaped_ready[256] = {0};
    char escaped_fail[256] = {0};
    
    // 简单复制，实际使用时 pattern 不应包含单引号
    if (ready_pattern && ready_pattern[0]) {
        strncpy(escaped_ready, ready_pattern, sizeof(escaped_ready) - 1);
    }
    if (fail_pattern && fail_pattern[0]) {
        strncpy(escaped_fail, fail_pattern, sizeof(escaped_fail) - 1);
    }
    
    // 构建命令：检查文件存在、grep ready pattern、grep fail pattern
    // 使用 grep -F 进行固定字符串匹配（不解释正则）
    if (escaped_fail[0] && escaped_ready[0]) {
        snprintf(cmd, sizeof(cmd),
            "if [ ! -f '%s' ]; then echo 'NOTFOUND'; "
            "elif grep -qF '%s' '%s' 2>/dev/null; then echo 'FAIL'; "
            "elif grep -qF '%s' '%s' 2>/dev/null; then echo 'READY'; "
            "else echo 'WAITING'; fi",
            log_file, escaped_fail, log_file, escaped_ready, log_file);
    } else if (escaped_ready[0]) {
        snprintf(cmd, sizeof(cmd),
            "if [ ! -f '%s' ]; then echo 'NOTFOUND'; "
            "elif grep -qF '%s' '%s' 2>/dev/null; then echo 'READY'; "
            "else echo 'WAITING'; fi",
            log_file, escaped_ready, log_file);
    } else {
        // 没有 pattern，只检查文件是否存在
        snprintf(cmd, sizeof(cmd),
            "[ -f '%s' ] && echo 'EXISTS' || echo 'NOTFOUND'", log_file);
    }
    
    ESP_LOGD(TAG, "Check command: %s", cmd);
    
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
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, cmd, &exec_result);
    
    if (ret == ESP_OK && exec_result.stdout_data) {
        // 解析输出结果
        const char *output = exec_result.stdout_data;
        ESP_LOGI(TAG, "Check result: '%s'", output);
        
        if (strstr(output, "NOTFOUND")) {
            *result = -2;  // 文件不存在
        } else if (strstr(output, "FAIL")) {
            *result = -1;  // 找到失败模式
        } else if (strstr(output, "READY")) {
            *result = 1;   // 找到就绪模式
        } else {
            *result = 0;   // 等待中
        }
        
        free(exec_result.stdout_data);
        if (exec_result.stderr_data) free(exec_result.stderr_data);
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
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Log watch task started:");
    ESP_LOGI(TAG, "  var_name: '%s'", task->config.var_name);
    ESP_LOGI(TAG, "  host_id: '%s'", task->config.host_id);
    ESP_LOGI(TAG, "  log_file: '%s'", task->config.log_file);
    ESP_LOGI(TAG, "  ready_pattern: '%s'", task->config.ready_pattern);
    ESP_LOGI(TAG, "  fail_pattern: '%s'", task->config.fail_pattern);
    ESP_LOGI(TAG, "  timeout: %u sec, interval: %u ms", task->config.timeout_sec, task->config.check_interval_ms);
    ESP_LOGI(TAG, "========================================");
    
    while (task->is_running && !task->stop_requested) {
        // 检查超时
        uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000 - task->start_time;
        if (elapsed >= task->config.timeout_sec) {
            ESP_LOGW(TAG, "Log watch timeout for %s after %" PRIu32 " seconds", 
                     task->config.var_name, elapsed);
            update_status_var(task->config.var_name, "timeout");
            break;
        }
        
        // 使用 grep 在远程检查模式
        int check_result = 0;
        esp_err_t ret = check_log_pattern_via_ssh(
            task->config.host_id, 
            task->config.log_file,
            task->config.ready_pattern,
            task->config.fail_pattern,
            &check_result
        );
        
        if (ret == ESP_OK) {
            switch (check_result) {
                case 1:  // READY
                    ESP_LOGI(TAG, "✅ Service ready: pattern '%s' found in log", 
                             task->config.ready_pattern);
                    update_status_var(task->config.var_name, "ready");
                    update_ready_time(task->config.var_name);
                    goto task_done;
                    
                case -1:  // FAIL
                    ESP_LOGE(TAG, "❌ Service failed: pattern '%s' found in log", 
                             task->config.fail_pattern);
                    update_status_var(task->config.var_name, "failed");
                    goto task_done;
                    
                case -2:  // NOTFOUND
                    ESP_LOGD(TAG, "Log file not found yet, waiting... (elapsed=%" PRIu32 "s)", elapsed);
                    break;
                    
                default:  // WAITING
                    ESP_LOGI(TAG, "Waiting for pattern '%s' (elapsed=%" PRIu32 "s/%us)", 
                             task->config.ready_pattern, elapsed, task->config.timeout_sec);
                    break;
            }
        } else {
            ESP_LOGW(TAG, "Check failed (will retry): %s", esp_err_to_name(ret));
        }
        
        // 等待下次检查
        vTaskDelay(pdMS_TO_TICKS(task->config.check_interval_ms));
    }
    
task_done:
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
    
    // 检查是否已有同名变量的监控任务运行中
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    ts_ssh_log_watch_task_t *existing = s_task_list;
    while (existing) {
        if (strcmp(existing->config.var_name, config->var_name) == 0 && existing->is_running) {
            xSemaphoreGive(s_mutex);
            ESP_LOGW(TAG, "Log watch for '%s' already running, stopping old one first", config->var_name);
            // 停止旧任务
            existing->stop_requested = true;
            // 等待旧任务结束（最多等 2 秒）
            for (int i = 0; i < 20 && existing->is_running; i++) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            break;
        }
        existing = existing->next;
    }
    xSemaphoreGive(s_mutex);
    
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
    // 任务名包含变量名前缀以便调试识别
    char task_name[16];
    snprintf(task_name, sizeof(task_name), "lw_%.11s", config->var_name);
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        log_watch_task,
        task_name,
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

int ts_ssh_log_watch_get_active_count(void) {
    if (!s_mutex) {
        return 0;
    }
    
    int count = 0;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    ts_ssh_log_watch_task_t *task = s_task_list;
    while (task) {
        if (task->is_running) {
            count++;
        }
        task = task->next;
    }
    
    xSemaphoreGive(s_mutex);
    return count;
}

void ts_ssh_log_watch_list(void) {
    if (!s_mutex) {
        ESP_LOGI(TAG, "No active log watch tasks");
        return;
    }
    
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    
    int count = 0;
    ts_ssh_log_watch_task_t *task = s_task_list;
    
    ESP_LOGI(TAG, "=== Active Log Watch Tasks ===");
    while (task) {
        if (task->is_running) {
            uint32_t elapsed = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000 - task->start_time;
            ESP_LOGI(TAG, "  [%d] var=%s, pattern='%s', elapsed=%" PRIu32 "s/%us, log=%s",
                     ++count, task->config.var_name, task->config.ready_pattern,
                     elapsed, task->config.timeout_sec, task->config.log_file);
        }
        task = task->next;
    }
    
    if (count == 0) {
        ESP_LOGI(TAG, "  (none)");
    }
    ESP_LOGI(TAG, "==============================");
    
    xSemaphoreGive(s_mutex);
}
