/**
 * @file ts_webui_ws.c
 * @brief WebSocket Implementation with Terminal Support (including SSH Shell)
 */

#include "ts_webui.h"
#include "ts_http_server.h"
#include "ts_log.h"
#include "ts_event.h"
#include "ts_console.h"
#include "ts_power_policy.h"
#include "ts_ssh_client.h"
#include "ts_ssh_shell.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "webui_ws"

#ifdef CONFIG_TS_WEBUI_WS_MAX_CLIENTS
#define MAX_WS_CLIENTS CONFIG_TS_WEBUI_WS_MAX_CLIENTS
#else
#define MAX_WS_CLIENTS 4
#endif

/* 终端输出缓冲区大小 */
#define TERMINAL_OUTPUT_BUF_SIZE 4096

/* SSH Shell 输出缓冲区大小 */
#define SSH_OUTPUT_BUF_SIZE 2048

typedef enum {
    WS_CLIENT_TYPE_EVENT,      // 普通事件订阅客户端
    WS_CLIENT_TYPE_TERMINAL,   // 终端会话客户端
    WS_CLIENT_TYPE_SSH_SHELL   // SSH Shell 会话客户端
} ws_client_type_t;

typedef struct {
    bool active;
    int fd;
    httpd_handle_t hd;
    ws_client_type_t type;
} ws_client_t;

static ws_client_t s_clients[MAX_WS_CLIENTS];
static httpd_handle_t s_server = NULL;
static SemaphoreHandle_t s_terminal_mutex = NULL;
static int s_terminal_client_fd = -1;  // 当前终端会话的 fd

/* 终端输出缓冲区 */
static char *s_terminal_output_buf = NULL;
static size_t s_terminal_output_len = 0;
static SemaphoreHandle_t s_output_mutex = NULL;

/* SSH Shell 会话状态 */
static ts_ssh_session_t s_ssh_session = NULL;
static ts_ssh_shell_t s_ssh_shell = NULL;
static int s_ssh_client_fd = -1;
static TaskHandle_t s_ssh_poll_task = NULL;
static volatile bool s_ssh_running = false;

/* 电压保护事件处理器句柄 */
static ts_event_handler_handle_t s_power_event_handle = NULL;

/* 电压保护状态字符串 */
static const char *power_state_to_string(ts_power_policy_state_t state)
{
    switch (state) {
        case TS_POWER_POLICY_STATE_NORMAL:      return "NORMAL";
        case TS_POWER_POLICY_STATE_LOW_VOLTAGE: return "LOW_VOLTAGE";
        case TS_POWER_POLICY_STATE_SHUTDOWN:    return "SHUTDOWN";
        case TS_POWER_POLICY_STATE_PROTECTED:   return "PROTECTED";
        case TS_POWER_POLICY_STATE_RECOVERY:    return "RECOVERY";
        default: return "UNKNOWN";
    }
}

/* 电压保护事件处理回调 */
static void power_policy_event_handler(const ts_event_t *event, void *user_data)
{
    if (!event || !event->data) return;
    
    ts_power_policy_status_t *status = (ts_power_policy_status_t *)event->data;
    
    // 构造事件消息
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "power_event");
    cJSON_AddStringToObject(msg, "state", power_state_to_string(status->state));
    cJSON_AddNumberToObject(msg, "voltage", status->current_voltage);
    cJSON_AddNumberToObject(msg, "countdown", status->countdown_remaining_sec);
    cJSON_AddNumberToObject(msg, "protection_count", status->protection_count);
    
    // 根据事件 ID 添加具体事件类型（使用 ts_power_policy_event_t 枚举值）
    const char *event_name = "unknown";
    switch (event->id) {
        case TS_POWER_POLICY_EVENT_STATE_CHANGED:    event_name = "state_changed"; break;
        case TS_POWER_POLICY_EVENT_LOW_VOLTAGE:      event_name = "low_voltage"; break;
        case TS_POWER_POLICY_EVENT_COUNTDOWN_TICK:   event_name = "countdown_tick"; break;
        case TS_POWER_POLICY_EVENT_SHUTDOWN_START:   event_name = "shutdown_start"; break;
        case TS_POWER_POLICY_EVENT_PROTECTED:        event_name = "protected"; break;
        case TS_POWER_POLICY_EVENT_RECOVERY_START:   event_name = "recovery_start"; break;
        case TS_POWER_POLICY_EVENT_RECOVERY_COMPLETE: event_name = "recovery_complete"; break;
        case TS_POWER_POLICY_EVENT_DEBUG_TICK:       event_name = "debug_tick"; break;
    }
    cJSON_AddStringToObject(msg, "event", event_name);
    
    char *json = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (json) {
        // 广播给所有连接的客户端
        ts_webui_broadcast(json);
        free(json);
    }
}

/*===========================================================================*/
/*                          SSH Shell Functions                               */
/*===========================================================================*/

/* 发送 SSH 输出到 WebSocket 客户端 */
static void ssh_send_output(const char *data, size_t len)
{
    if (s_ssh_client_fd < 0 || !s_server || !data || len == 0) return;
    
    // 查找客户端
    httpd_handle_t hd = NULL;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == s_ssh_client_fd) {
            hd = s_clients[i].hd;
            break;
        }
    }
    if (!hd) return;
    
    // 构造消息（base64 可以处理二进制，但这里假设是文本）
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "ssh_output");
    
    // 复制数据并确保 null 结尾
    char *buf = malloc(len + 1);
    if (buf) {
        memcpy(buf, data, len);
        buf[len] = '\0';
        cJSON_AddStringToObject(msg, "data", buf);
        free(buf);
    }
    
    char *json = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (json) {
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len = strlen(json)
        };
        httpd_ws_send_frame_async(hd, s_ssh_client_fd, &ws_pkt);
        free(json);
    }
}

/* 发送 SSH 状态消息 */
static void ssh_send_status(const char *status, const char *message)
{
    if (s_ssh_client_fd < 0 || !s_server) return;
    
    httpd_handle_t hd = NULL;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == s_ssh_client_fd) {
            hd = s_clients[i].hd;
            break;
        }
    }
    if (!hd) return;
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "ssh_status");
    cJSON_AddStringToObject(msg, "status", status);
    if (message) {
        cJSON_AddStringToObject(msg, "message", message);
    }
    
    char *json = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (json) {
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len = strlen(json)
        };
        httpd_ws_send_frame_async(hd, s_ssh_client_fd, &ws_pkt);
        free(json);
    }
}

/* 前向声明 */
static void ssh_cleanup(void);

/* SSH Shell 轮询任务 */
static void ssh_poll_task(void *arg)
{
    char buf[SSH_OUTPUT_BUF_SIZE];
    bool need_cleanup = false;
    
    TS_LOGI(TAG, "SSH poll task started");
    
    while (s_ssh_running && s_ssh_shell) {
        size_t read_len = 0;
        esp_err_t ret = ts_ssh_shell_read(s_ssh_shell, buf, sizeof(buf) - 1, &read_len);
        
        if (ret == ESP_OK && read_len > 0) {
            buf[read_len] = '\0';
            ssh_send_output(buf, read_len);
        }
        
        // 检查 shell 是否还活跃
        if (!ts_ssh_shell_is_active(s_ssh_shell)) {
            TS_LOGI(TAG, "SSH shell closed by remote");
            ssh_send_status("closed", "SSH session closed");
            need_cleanup = true;  // 标记需要清理
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(20));  // 50Hz 轮询
    }
    
    TS_LOGI(TAG, "SSH poll task ended");
    s_ssh_poll_task = NULL;
    
    // 如果是因为远程关闭而退出，需要清理 SSH 会话
    if (need_cleanup && s_ssh_session) {
        // 清理 shell（已经关闭，只需释放资源）
        if (s_ssh_shell) {
            ts_ssh_shell_close(s_ssh_shell);
            s_ssh_shell = NULL;
        }
        // 清理 session
        if (s_ssh_session) {
            ts_ssh_disconnect(s_ssh_session);
            ts_ssh_session_destroy(s_ssh_session);
            s_ssh_session = NULL;
        }
        // 更新客户端类型
        if (s_ssh_client_fd >= 0) {
            for (int i = 0; i < MAX_WS_CLIENTS; i++) {
                if (s_clients[i].active && s_clients[i].fd == s_ssh_client_fd) {
                    s_clients[i].type = WS_CLIENT_TYPE_TERMINAL;
                    break;
                }
            }
            s_ssh_client_fd = -1;
        }
        TS_LOGI(TAG, "SSH session cleaned up after remote close");
    }
    
    vTaskDelete(NULL);
}

/* 清理 SSH 会话 */
static void ssh_cleanup(void)
{
    s_ssh_running = false;
    
    if (s_ssh_poll_task) {
        // 等待任务结束
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    if (s_ssh_shell) {
        ts_ssh_shell_close(s_ssh_shell);
        s_ssh_shell = NULL;
    }
    
    if (s_ssh_session) {
        ts_ssh_disconnect(s_ssh_session);
        ts_ssh_session_destroy(s_ssh_session);
        s_ssh_session = NULL;
    }
    
    // 更新客户端类型
    if (s_ssh_client_fd >= 0) {
        for (int i = 0; i < MAX_WS_CLIENTS; i++) {
            if (s_clients[i].active && s_clients[i].fd == s_ssh_client_fd) {
                s_clients[i].type = WS_CLIENT_TYPE_TERMINAL;
                break;
            }
        }
    }
    s_ssh_client_fd = -1;
    
    TS_LOGI(TAG, "SSH session cleaned up");
}

/* 处理 SSH Shell 连接请求 */
static void handle_ssh_connect(httpd_req_t *req, cJSON *params)
{
    int fd = httpd_req_to_sockfd(req);
    
    // 检查是否已有 SSH 会话
    if (s_ssh_session != NULL) {
        ssh_send_status("error", "Another SSH session is active");
        return;
    }
    
    // 解析参数
    cJSON *host = cJSON_GetObjectItem(params, "host");
    cJSON *port = cJSON_GetObjectItem(params, "port");
    cJSON *user = cJSON_GetObjectItem(params, "user");
    cJSON *password = cJSON_GetObjectItem(params, "password");
    
    if (!host || !cJSON_IsString(host) || !user || !cJSON_IsString(user)) {
        ssh_send_status("error", "Missing host or user");
        return;
    }
    
    int ssh_port = (port && cJSON_IsNumber(port)) ? port->valueint : 22;
    const char *ssh_password = (password && cJSON_IsString(password)) ? password->valuestring : "";
    
    TS_LOGI(TAG, "SSH connect: %s@%s:%d", user->valuestring, host->valuestring, ssh_port);
    
    // 设置 SSH 客户端 fd
    s_ssh_client_fd = fd;
    
    // 更新客户端类型
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            s_clients[i].type = WS_CLIENT_TYPE_SSH_SHELL;
            break;
        }
    }
    
    // 发送连接中状态
    ssh_send_status("connecting", "Connecting to SSH server...");
    
    // 配置 SSH
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host->valuestring;
    config.port = ssh_port;
    config.username = user->valuestring;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = ssh_password;
    config.timeout_ms = 10000;
    
    // 创建会话
    esp_err_t ret = ts_ssh_session_create(&config, &s_ssh_session);
    if (ret != ESP_OK) {
        ssh_send_status("error", "Failed to create SSH session");
        ssh_cleanup();
        return;
    }
    
    // 连接
    ret = ts_ssh_connect(s_ssh_session);
    if (ret != ESP_OK) {
        char err_msg[128];
        snprintf(err_msg, sizeof(err_msg), "Connection failed: %s", ts_ssh_get_error(s_ssh_session));
        ssh_send_status("error", err_msg);
        ssh_cleanup();
        return;
    }
    
    // 打开 Shell
    ts_shell_config_t shell_config = TS_SHELL_DEFAULT_CONFIG();
    shell_config.term_width = 80;
    shell_config.term_height = 24;
    shell_config.read_timeout_ms = 50;
    
    ret = ts_ssh_shell_open(s_ssh_session, &shell_config, &s_ssh_shell);
    if (ret != ESP_OK) {
        ssh_send_status("error", "Failed to open shell");
        ssh_cleanup();
        return;
    }
    
    // 发送连接成功状态
    ssh_send_status("connected", "SSH shell ready");
    
    // 启动轮询任务（栈需要足够大以容纳 libssh2 缓冲区）
    s_ssh_running = true;
    xTaskCreate(ssh_poll_task, "ssh_poll", 8192, NULL, 5, &s_ssh_poll_task);
}

/* 处理 SSH Shell 输入 */
static void handle_ssh_input(const char *data)
{
    if (!s_ssh_shell || !data) return;
    
    size_t len = strlen(data);
    if (len == 0) return;
    
    ts_ssh_shell_write(s_ssh_shell, data, len, NULL);
}

/* 处理 SSH Shell 断开 */
static void handle_ssh_disconnect(void)
{
    if (s_ssh_session) {
        ssh_send_status("disconnecting", "Closing SSH session...");
        ssh_cleanup();
    }
}

/* 处理 SSH Shell 信号 */
static void handle_ssh_signal(const char *signal)
{
    if (s_ssh_shell && signal) {
        ts_ssh_shell_send_signal(s_ssh_shell, signal);
    }
}

/* 处理 SSH Shell 窗口大小调整 */
static void handle_ssh_resize(int width, int height)
{
    if (s_ssh_shell && width > 0 && height > 0) {
        ts_ssh_shell_resize(s_ssh_shell, width, height);
    }
}

/*===========================================================================*/
/*                          Terminal Functions                                */
/*===========================================================================*/

/* 终端输出回调 - 收集到缓冲区 */
static void terminal_output_cb(const char *data, size_t len, void *user_data)
{
    if (!data || len == 0) return;
    if (!s_terminal_output_buf || !s_output_mutex) return;
    
    if (xSemaphoreTake(s_output_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        size_t space = TERMINAL_OUTPUT_BUF_SIZE - s_terminal_output_len - 1;
        size_t copy_len = len < space ? len : space;
        
        if (copy_len > 0) {
            memcpy(s_terminal_output_buf + s_terminal_output_len, data, copy_len);
            s_terminal_output_len += copy_len;
            s_terminal_output_buf[s_terminal_output_len] = '\0';
            TS_LOGD(TAG, "Output collected: %zu bytes, total: %zu", copy_len, s_terminal_output_len);
        }
        xSemaphoreGive(s_output_mutex);
    } else {
        TS_LOGW(TAG, "Failed to acquire output mutex");
    }
}

static void add_client(httpd_handle_t hd, int fd, ws_client_type_t type)
{
    // 首先检查是否已存在相同 fd 的客户端（重连情况）
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            // 已存在，更新类型
            s_clients[i].type = type;
            s_clients[i].hd = hd;
            TS_LOGI(TAG, "WebSocket client reconnected (fd=%d, type=%s)", 
                    fd, type == WS_CLIENT_TYPE_TERMINAL ? "terminal" : "event");
            return;
        }
    }
    
    // 查找空槽位
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (!s_clients[i].active) {
            s_clients[i].active = true;
            s_clients[i].fd = fd;
            s_clients[i].hd = hd;
            s_clients[i].type = type;
            TS_LOGI(TAG, "WebSocket client connected (fd=%d, type=%s)", 
                    fd, type == WS_CLIENT_TYPE_TERMINAL ? "terminal" : "event");
            return;
        }
    }
    TS_LOGW(TAG, "No free WebSocket slots");
}

/* 处理终端命令执行 */
static void handle_terminal_command(httpd_req_t *req, const char *command)
{
    int fd = httpd_req_to_sockfd(req);
    
    // 检查是否是当前终端会话
    if (s_terminal_client_fd != fd) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "type", "error");
        cJSON_AddStringToObject(err, "message", "Not a terminal session");
        char *json = cJSON_PrintUnformatted(err);
        cJSON_Delete(err);
        
        if (json) {
            httpd_ws_frame_t ws_pkt = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)json,
                .len = strlen(json)
            };
            httpd_ws_send_frame(req, &ws_pkt);
            free(json);
        }
        return;
    }
    
    TS_LOGI(TAG, "Terminal exec: %s", command);
    
    // 清空输出缓冲区
    if (s_output_mutex && xSemaphoreTake(s_output_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_terminal_output_len = 0;
        if (s_terminal_output_buf) {
            s_terminal_output_buf[0] = '\0';
        }
        xSemaphoreGive(s_output_mutex);
    }
    
    // 执行命令
    ts_cmd_result_t result;
    ts_console_exec(command, &result);
    
    TS_LOGI(TAG, "Command finished, output len: %zu", s_terminal_output_len);
    
    // 获取输出并发送
    if (s_output_mutex && xSemaphoreTake(s_output_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        TS_LOGI(TAG, "Sending output: %zu bytes", s_terminal_output_len);
        if (s_terminal_output_len > 0 && s_terminal_output_buf) {
            cJSON *output_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(output_msg, "type", "output");
            cJSON_AddStringToObject(output_msg, "data", s_terminal_output_buf);
            char *json = cJSON_PrintUnformatted(output_msg);
            cJSON_Delete(output_msg);
            
            if (json) {
                httpd_ws_frame_t ws_pkt = {
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)json,
                    .len = strlen(json)
                };
                httpd_ws_send_frame(req, &ws_pkt);
                free(json);
            }
        }
        xSemaphoreGive(s_output_mutex);
    }
    
    // 发送命令完成消息
    cJSON *done = cJSON_CreateObject();
    cJSON_AddStringToObject(done, "type", "done");
    cJSON_AddNumberToObject(done, "code", result.code);
    char *json = cJSON_PrintUnformatted(done);
    cJSON_Delete(done);
    
    if (json) {
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len = strlen(json)
        };
        httpd_ws_send_frame(req, &ws_pkt);
        free(json);
    }
}

/* 启动终端会话 */
static void start_terminal_session(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    
    // 检查是否有其他终端会话
    if (s_terminal_client_fd >= 0 && s_terminal_client_fd != fd) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddStringToObject(err, "type", "error");
        cJSON_AddStringToObject(err, "message", "Another terminal session is active");
        char *json = cJSON_PrintUnformatted(err);
        cJSON_Delete(err);
        
        if (json) {
            httpd_ws_frame_t ws_pkt = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)json,
                .len = strlen(json)
            };
            httpd_ws_send_frame(req, &ws_pkt);
            free(json);
        }
        return;
    }
    
    // 设置为终端客户端
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            s_clients[i].type = WS_CLIENT_TYPE_TERMINAL;
            break;
        }
    }
    
    // 设置输出回调
    s_terminal_client_fd = fd;
    ts_console_set_output_cb(terminal_output_cb, NULL);
    
    // 发送确认消息和欢迎信息
    cJSON *welcome = cJSON_CreateObject();
    cJSON_AddStringToObject(welcome, "type", "connected");
    cJSON_AddStringToObject(welcome, "message", "Terminal session started");
    cJSON_AddStringToObject(welcome, "prompt", "tianshan> ");
    char *json = cJSON_PrintUnformatted(welcome);
    cJSON_Delete(welcome);
    
    if (json) {
        httpd_ws_frame_t ws_pkt = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)json,
            .len = strlen(json)
        };
        httpd_ws_send_frame(req, &ws_pkt);
        free(json);
    }
    
    TS_LOGI(TAG, "Terminal session started (fd=%d)", fd);
}

/* 清理断开的客户端 */
static void cleanup_disconnected_client(int fd)
{
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active && s_clients[i].fd == fd) {
            // 如果是终端客户端，清理输出回调
            if (s_clients[i].type == WS_CLIENT_TYPE_TERMINAL && s_terminal_client_fd == fd) {
                ts_console_clear_output_cb();
                s_terminal_client_fd = -1;
                if (s_terminal_mutex) {
                    xSemaphoreGive(s_terminal_mutex);
                }
            }
            // 如果是 SSH Shell 客户端，清理 SSH 会话
            if (s_clients[i].type == WS_CLIENT_TYPE_SSH_SHELL && s_ssh_client_fd == fd) {
                ssh_cleanup();
            }
            s_clients[i].active = false;
            TS_LOGI(TAG, "WebSocket client disconnected (fd=%d)", fd);
            return;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        TS_LOGI(TAG, "WebSocket handshake");
        // 初次连接时添加为事件客户端
        add_client(req->handle, httpd_req_to_sockfd(req), WS_CLIENT_TYPE_EVENT);
        return ESP_OK;
    }
    
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    // Get frame info
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "ws_recv_frame error: %s", esp_err_to_name(ret));
        cleanup_disconnected_client(httpd_req_to_sockfd(req));
        return ret;
    }
    
    // 处理关闭帧
    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        TS_LOGI(TAG, "WebSocket close frame received");
        cleanup_disconnected_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }
    
    if (ws_pkt.len == 0) {
        return ESP_OK;
    }
    
    // Allocate buffer for payload
    uint8_t *buf = malloc(ws_pkt.len + 1);
    if (!buf) {
        return ESP_ERR_NO_MEM;
    }
    ws_pkt.payload = buf;
    
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }
    buf[ws_pkt.len] = '\0';
    
    TS_LOGD(TAG, "WS recv: %s", (char *)buf);
    
    // Parse message
    cJSON *msg = cJSON_Parse((char *)buf);
    free(buf);
    
    if (msg) {
        cJSON *type = cJSON_GetObjectItem(msg, "type");
        if (type && cJSON_IsString(type)) {
            TS_LOGI(TAG, "WS msg type=%s from fd=%d", type->valuestring, httpd_req_to_sockfd(req));
            
            if (strcmp(type->valuestring, "ping") == 0) {
                // Respond to ping
                cJSON *pong = cJSON_CreateObject();
                cJSON_AddStringToObject(pong, "type", "pong");
                char *pong_str = cJSON_PrintUnformatted(pong);
                cJSON_Delete(pong);
                
                ws_pkt.payload = (uint8_t *)pong_str;
                ws_pkt.len = strlen(pong_str);
                httpd_ws_send_frame(req, &ws_pkt);
                free(pong_str);
            }
            else if (strcmp(type->valuestring, "subscribe") == 0) {
                // 保持为事件订阅客户端（已在握手时添加）
                TS_LOGI(TAG, "Client subscribed to events");
            }
            else if (strcmp(type->valuestring, "terminal_start") == 0) {
                // 启动终端会话
                start_terminal_session(req);
            }
            else if (strcmp(type->valuestring, "terminal_input") == 0) {
                // 终端命令输入
                cJSON *data = cJSON_GetObjectItem(msg, "data");
                if (data && cJSON_IsString(data)) {
                    handle_terminal_command(req, data->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "terminal_interrupt") == 0) {
                // 发送中断信号 (Ctrl+C)
                ts_console_request_interrupt();
                TS_LOGI(TAG, "Terminal interrupt requested");
            }
            else if (strcmp(type->valuestring, "terminal_stop") == 0) {
                // 停止终端会话
                int fd = httpd_req_to_sockfd(req);
                if (s_terminal_client_fd == fd) {
                    ts_console_clear_output_cb();
                    s_terminal_client_fd = -1;
                    // 恢复为普通事件客户端
                    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
                        if (s_clients[i].active && s_clients[i].fd == fd) {
                            s_clients[i].type = WS_CLIENT_TYPE_EVENT;
                            break;
                        }
                    }
                    TS_LOGI(TAG, "Terminal session stopped");
                }
            }
            /* SSH Shell 消息处理 */
            else if (strcmp(type->valuestring, "ssh_connect") == 0) {
                // SSH 连接请求
                handle_ssh_connect(req, msg);
            }
            else if (strcmp(type->valuestring, "ssh_input") == 0) {
                // SSH 输入
                cJSON *data = cJSON_GetObjectItem(msg, "data");
                if (data && cJSON_IsString(data)) {
                    handle_ssh_input(data->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "ssh_disconnect") == 0) {
                // SSH 断开
                handle_ssh_disconnect();
            }
            else if (strcmp(type->valuestring, "ssh_signal") == 0) {
                // SSH 信号 (如 INT, TERM)
                cJSON *sig = cJSON_GetObjectItem(msg, "signal");
                if (sig && cJSON_IsString(sig)) {
                    handle_ssh_signal(sig->valuestring);
                }
            }
            else if (strcmp(type->valuestring, "ssh_resize") == 0) {
                // SSH 窗口大小调整
                cJSON *width = cJSON_GetObjectItem(msg, "width");
                cJSON *height = cJSON_GetObjectItem(msg, "height");
                if (width && height && cJSON_IsNumber(width) && cJSON_IsNumber(height)) {
                    handle_ssh_resize(width->valueint, height->valueint);
                }
            }
        }
        cJSON_Delete(msg);
    }
    
    return ESP_OK;
}

esp_err_t ts_webui_ws_init(void)
{
    TS_LOGI(TAG, "Initializing WebSocket with Terminal support");
    
    memset(s_clients, 0, sizeof(s_clients));
    s_terminal_client_fd = -1;
    
    // 创建终端会话互斥锁
    if (!s_terminal_mutex) {
        s_terminal_mutex = xSemaphoreCreateMutex();
    }
    
    // 创建输出缓冲区互斥锁
    if (!s_output_mutex) {
        s_output_mutex = xSemaphoreCreateMutex();
    }
    
    // 分配终端输出缓冲区
    if (!s_terminal_output_buf) {
        s_terminal_output_buf = malloc(TERMINAL_OUTPUT_BUF_SIZE);
        if (!s_terminal_output_buf) {
            TS_LOGE(TAG, "Failed to allocate terminal output buffer");
            return ESP_ERR_NO_MEM;
        }
        s_terminal_output_buf[0] = '\0';
        s_terminal_output_len = 0;
    }
    
    // Get HTTP server handle
    httpd_handle_t server = ts_http_server_get_handle();
    if (!server) {
        TS_LOGE(TAG, "HTTP server not started");
        return ESP_ERR_INVALID_STATE;
    }
    
    s_server = server;
    
    // Register WebSocket URI handler
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
        .handle_ws_control_frames = true
    };
    
    esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register WebSocket handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 注册电压保护事件处理器
    if (!s_power_event_handle) {
        ret = ts_event_register(TS_EVENT_BASE_POWER, TS_EVENT_ANY_ID, 
                                 power_policy_event_handler, NULL, &s_power_event_handle);
        if (ret == ESP_OK) {
            TS_LOGI(TAG, "Power policy event handler registered");
        } else {
            TS_LOGW(TAG, "Failed to register power event handler: %s", esp_err_to_name(ret));
        }
    }
    
    TS_LOGI(TAG, "WebSocket handler registered at /ws");
    return ESP_OK;
}

esp_err_t ts_webui_broadcast(const char *message)
{
    if (!message) return ESP_ERR_INVALID_ARG;
    
    httpd_ws_frame_t ws_pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)message,
        .len = strlen(message)
    };
    
    int sent = 0;
    for (int i = 0; i < MAX_WS_CLIENTS; i++) {
        if (s_clients[i].active) {
            esp_err_t ret = httpd_ws_send_frame_async(s_clients[i].hd, s_clients[i].fd, &ws_pkt);
            if (ret == ESP_OK) {
                sent++;
            } else {
                // Client probably disconnected - cleanup resources
                int fd = s_clients[i].fd;
                TS_LOGW(TAG, "Client fd=%d send failed, cleaning up", fd);
                cleanup_disconnected_client(fd);
            }
        }
    }
    
    TS_LOGD(TAG, "Broadcast to %d clients", sent);
    return ESP_OK;
}

esp_err_t ts_webui_broadcast_event(const char *event_type, const char *data)
{
    if (!event_type) return ESP_ERR_INVALID_ARG;
    
    cJSON *msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "event");
    cJSON_AddStringToObject(msg, "event", event_type);
    if (data) {
        cJSON *data_obj = cJSON_Parse(data);
        if (data_obj) {
            cJSON_AddItemToObject(msg, "data", data_obj);
        } else {
            cJSON_AddStringToObject(msg, "data", data);
        }
    }
    
    char *json = cJSON_PrintUnformatted(msg);
    cJSON_Delete(msg);
    
    if (json) {
        esp_err_t ret = ts_webui_broadcast(json);
        free(json);
        return ret;
    }
    
    return ESP_ERR_NO_MEM;
}
