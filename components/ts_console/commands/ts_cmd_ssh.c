/**
 * @file ts_cmd_ssh.c
 * @brief SSH Console Commands
 * 
 * 实现 ssh 命令：
 * - ssh --host <ip> --user <user> --password <pwd> --exec <cmd>
 * - ssh --test --host <ip> --user <user> --password <pwd>
 * - ssh --keygen --type rsa2048 --output /sdcard/id_rsa
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-18
 */

#include "ts_console.h"
#include "ts_log.h"
#include "ts_ssh_client.h"
#include "ts_ssh_shell.h"
#include "ts_port_forward.h"
#include "ts_crypto.h"
#include "argtable3/argtable3.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_ssh"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_str *host;       /**< SSH 服务器地址 */
    struct arg_int *port;       /**< SSH 端口 (默认 22) */
    struct arg_str *user;       /**< 用户名 */
    struct arg_str *password;   /**< 密码 */
    struct arg_str *key;        /**< 私钥文件路径 (PEM 格式) */
    struct arg_str *exec;       /**< 要执行的命令 */
    struct arg_lit *test;       /**< 测试连接 */
    struct arg_lit *shell;      /**< 交互式 Shell */
    struct arg_str *forward;    /**< 端口转发 L<local>:<remote_host>:<remote_port> */
    struct arg_lit *keygen;     /**< 生成密钥对 */
    struct arg_lit *copyid;     /**< 部署公钥到远程服务器 */
    struct arg_str *type;       /**< 密钥类型 (rsa2048, rsa4096, ec256, ec384) */
    struct arg_str *output;     /**< 密钥输出路径 */
    struct arg_str *comment;    /**< 密钥注释 */
    struct arg_int *timeout;    /**< 超时时间（秒） */
    struct arg_lit *verbose;    /**< 详细输出 */
    struct arg_lit *help;
    struct arg_end *end;
} s_ssh_args;

/*===========================================================================*/
/*                          私钥文件读取辅助函数                              */
/*===========================================================================*/

/**
 * @brief 从文件读取私钥内容
 * @param path 私钥文件路径 (支持 /sdcard/ 或 /spiffs/)
 * @param key_data 输出：密钥数据（调用者需 free）
 * @param key_len 输出：密钥长度
 * @return esp_err_t ESP_OK 成功
 */
static esp_err_t load_private_key(const char *path, uint8_t **key_data, size_t *key_len)
{
    if (!path || !key_data || !key_len) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        ts_console_printf("Error: Cannot open key file: %s\n", path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 16384) {  /* 最大 16KB */
        ts_console_printf("Error: Invalid key file size: %ld\n", fsize);
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 分配内存并读取 */
    uint8_t *data = malloc(fsize + 1);
    if (!data) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(data, 1, fsize, fp);
    fclose(fp);

    if (read_len != (size_t)fsize) {
        free(data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    data[fsize] = '\0';  /* Null-terminate for safety */
    *key_data = data;
    *key_len = fsize;

    return ESP_OK;
}

/*===========================================================================*/
/*                          流式输出回调                                      */
/*===========================================================================*/

/** 用于传递给回调的上下文 */
typedef struct {
    ts_ssh_session_t session;
    bool verbose;
} stream_context_t;

static void stream_output_callback(const char *data, size_t len, bool is_stderr, void *user_data)
{
    stream_context_t *ctx = (stream_context_t *)user_data;
    
    /* 检查中断请求 */
    if (ts_console_interrupted()) {
        ts_ssh_abort(ctx->session);
        return;
    }
    
    /* 打印输出 */
    if (is_stderr && ctx->verbose) {
        ts_console_printf("\033[31m");  /* 红色表示 stderr */
    }
    
    /* 直接输出数据 */
    for (size_t i = 0; i < len; i++) {
        ts_console_printf("%c", data[i]);
    }
    
    if (is_stderr && ctx->verbose) {
        ts_console_printf("\033[0m");   /* 重置颜色 */
    }
}

/*===========================================================================*/
/*                          Ctrl+C 检测任务                                   */
/*===========================================================================*/

typedef struct {
    ts_ssh_session_t session;
    volatile bool running;
} interrupt_monitor_t;

static void interrupt_monitor_task(void *arg)
{
    interrupt_monitor_t *mon = (interrupt_monitor_t *)arg;
    
    while (mon->running) {
        /* 非阻塞检查 UART 输入 */
        uint8_t ch;
        int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            if (ch == 0x03) {  /* Ctrl+C = ASCII 0x03 */
                ts_console_printf("\n^C\n");
                ts_console_request_interrupt();
                ts_ssh_abort(mon->session);
                break;
            } else if (ch == 0x1C) {  /* Ctrl+\ = ASCII 0x1C (SIGQUIT) */
                ts_console_printf("\n^\\\n");
                ts_console_request_interrupt();
                ts_ssh_abort(mon->session);
                break;
            }
        }
    }
    
    vTaskDelete(NULL);
}

/*===========================================================================*/
/*                          Command: ssh --exec                               */
/*===========================================================================*/

static int do_ssh_exec(const char *host, int port, const char *user, 
                       const char *password, const char *key_path, 
                       const char *command, int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.timeout_ms = timeout_sec * 1000;
    
    /* 选择认证方式 */
    if (key_path) {
        /* 公钥认证 - 使用文件路径（更可靠） */
        config.auth_method = TS_SSH_AUTH_PUBLICKEY;
        config.auth.key.private_key_path = key_path;
        config.auth.key.private_key = NULL;
        config.auth.key.private_key_len = 0;
        config.auth.key.passphrase = NULL;  /* TODO: 支持加密私钥 */
        
        if (verbose) {
            ts_console_printf("Using public key authentication\n");
            ts_console_printf("Key file: %s\n", key_path);
        }
    } else {
        /* 密码认证 */
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = password;
    }
    
    if (verbose) {
        ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    }
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    if (verbose) {
        ts_console_printf("Connected. Executing command: %s\n", command);
        ts_console_printf("(Press Ctrl+C to abort)\n\n");
    }
    
    /* 启动中断监测任务 */
    interrupt_monitor_t monitor = {
        .session = session,
        .running = true
    };
    TaskHandle_t monitor_task = NULL;
    ts_console_clear_interrupt();
    
    xTaskCreate(interrupt_monitor_task, "ssh_mon", 3072, &monitor, 5, &monitor_task);
    
    /* 执行命令（流式输出） */
    int exit_code = -1;
    stream_context_t ctx = {
        .session = session,
        .verbose = verbose
    };
    
    ret = ts_ssh_exec_stream(session, command, stream_output_callback, &ctx, &exit_code);
    
    /* 停止监测任务 */
    monitor.running = false;
    vTaskDelay(pdMS_TO_TICKS(100));  /* 等待任务退出 */
    
    if (ret == ESP_ERR_TIMEOUT) {
        ts_console_printf("\n--- Command aborted by user ---\n");
    } else if (ret != ESP_OK) {
        ts_console_printf("\nError: Failed to execute command - %s\n", 
                          ts_ssh_get_error(session));
    } else if (verbose) {
        ts_console_printf("\n--- Command completed with exit code: %d ---\n", exit_code);
    }
    
    /* 断开连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    return (ret == ESP_OK) ? exit_code : 1;
}

/*===========================================================================*/
/*                          Command: ssh --shell                              */
/*===========================================================================*/

/** Shell 输出回调 */
static void shell_output_callback(const char *data, size_t len, void *user_data)
{
    (void)user_data;
    /* 直接输出到 stdout，更高效 */
    fwrite(data, 1, len, stdout);
    fflush(stdout);  /* 立即刷新，确保显示 */
}

/** Shell 输入回调 */
static const char *shell_input_callback(size_t *out_len, void *user_data)
{
    static char input_buf[64];
    ts_ssh_shell_t shell = (ts_ssh_shell_t)user_data;
    
    *out_len = 0;
    
    /* 非阻塞读取 UART（0ms 超时 = 立即返回） */
    uint8_t ch;
    int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, 0);
    if (len > 0) {
        /* 检查特殊控制字符 */
        if (ch == 0x03) {  /* Ctrl+C */
            /* 发送 SIGINT 到远程 */
            ts_ssh_shell_send_signal(shell, "INT");
            return NULL;
        } else if (ch == 0x1C) {  /* Ctrl+\ - 退出 Shell */
            ts_console_printf("\n^\\  (Exit shell)\n");
            ts_console_request_interrupt();
            return NULL;
        }
        
        input_buf[0] = ch;
        *out_len = 1;
        return input_buf;
    }
    
    return NULL;
}

static int do_ssh_shell(const char *host, int port, const char *user,
                        const char *password, const char *key_path,
                        int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    ts_ssh_shell_t shell = NULL;
    esp_err_t ret;
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.timeout_ms = timeout_sec * 1000;
    
    /* 选择认证方式 */
    if (key_path) {
        config.auth_method = TS_SSH_AUTH_PUBLICKEY;
        config.auth.key.private_key_path = key_path;
        config.auth.key.private_key = NULL;
        config.auth.key.private_key_len = 0;
        config.auth.key.passphrase = NULL;
        
        if (verbose) {
            ts_console_printf("Using public key authentication\n");
        }
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = password;
    }
    
    ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 打开交互式 Shell */
    ts_shell_config_t shell_config = TS_SHELL_DEFAULT_CONFIG();
    shell_config.term_width = 80;
    shell_config.term_height = 24;
    shell_config.read_timeout_ms = 50;
    
    ret = ts_ssh_shell_open(session, &shell_config, &shell);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to open shell\n");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    ts_console_printf("Connected. Interactive shell started.\n");
    ts_console_printf("(Press Ctrl+\\ to exit, Ctrl+C to send SIGINT)\n\n");
    ts_console_clear_interrupt();
    
    /* 运行交互式 Shell 循环 */
    ret = ts_ssh_shell_run(shell, shell_output_callback, shell_input_callback, shell);
    
    int exit_code = ts_ssh_shell_get_exit_code(shell);
    
    /* 清理 */
    ts_ssh_shell_close(shell);
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    ts_console_printf("\n--- Shell closed (exit code: %d) ---\n", exit_code);
    
    return (ret == ESP_OK) ? 0 : 1;
}

/*===========================================================================*/
/*                          Command: ssh --forward                            */
/*===========================================================================*/

static int do_ssh_forward(const char *host, int port, const char *user,
                          const char *password, const char *key_path,
                          const char *forward_spec,
                          int timeout_sec, bool verbose)
{
    ts_ssh_session_t session = NULL;
    ts_port_forward_t forward = NULL;
    esp_err_t ret;
    
    /* 解析端口转发规格: L<local_port>:<remote_host>:<remote_port> */
    if (forward_spec[0] != 'L' && forward_spec[0] != 'l') {
        ts_console_printf("Error: Forward spec must start with 'L' (local forward)\n");
        ts_console_printf("Format: L<local_port>:<remote_host>:<remote_port>\n");
        ts_console_printf("Example: L8080:localhost:80\n");
        return 1;
    }
    
    int local_port = 0;
    char remote_host[128] = {0};
    int remote_port = 0;
    
    if (sscanf(forward_spec + 1, "%d:%127[^:]:%d", &local_port, remote_host, &remote_port) != 3) {
        ts_console_printf("Error: Invalid forward spec format\n");
        ts_console_printf("Format: L<local_port>:<remote_host>:<remote_port>\n");
        ts_console_printf("Example: L8080:localhost:80\n");
        return 1;
    }
    
    if (local_port <= 0 || local_port > 65535 || remote_port <= 0 || remote_port > 65535) {
        ts_console_printf("Error: Invalid port number\n");
        return 1;
    }
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.timeout_ms = timeout_sec * 1000;
    
    /* 选择认证方式 */
    if (key_path) {
        config.auth_method = TS_SSH_AUTH_PUBLICKEY;
        config.auth.key.private_key_path = key_path;
        config.auth.key.private_key = NULL;
        config.auth.key.private_key_len = 0;
        config.auth.key.passphrase = NULL;
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = password;
    }
    
    ts_console_printf("Connecting to %s@%s:%d...\n", user, host, port);
    
    /* 创建会话 */
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create SSH session\n");
        return 1;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 创建端口转发 */
    ts_forward_config_t fwd_config = TS_FORWARD_DEFAULT_CONFIG();
    fwd_config.direction = TS_FORWARD_LOCAL;
    fwd_config.local_host = "0.0.0.0";  /* 允许所有接口访问 */
    fwd_config.local_port = local_port;
    fwd_config.remote_host = remote_host;
    fwd_config.remote_port = remote_port;
    
    ret = ts_port_forward_create(session, &fwd_config, &forward);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to create port forward\n");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    /* 启动转发 */
    ret = ts_port_forward_start(forward);
    if (ret != ESP_OK) {
        ts_console_printf("Error: Failed to start port forward\n");
        ts_port_forward_destroy(forward);
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    ts_console_printf("\n");
    ts_console_printf("╔════════════════════════════════════════════╗\n");
    ts_console_printf("║         SSH Port Forwarding Active         ║\n");
    ts_console_printf("╠════════════════════════════════════════════╣\n");
    ts_console_printf("║  Local:   0.0.0.0:%-5d                    ║\n", local_port);
    ts_console_printf("║  Remote:  %s:%-5d", remote_host, remote_port);
    /* 填充空格 */
    int len = strlen(remote_host) + 6;
    for (int i = len; i < 25; i++) ts_console_printf(" ");
    ts_console_printf("║\n");
    ts_console_printf("╠════════════════════════════════════════════╣\n");
    ts_console_printf("║  Press Ctrl+C to stop forwarding           ║\n");
    ts_console_printf("╚════════════════════════════════════════════╝\n\n");
    
    ts_console_clear_interrupt();
    
    /* 等待用户中断 */
    while (!ts_console_interrupted()) {
        /* 检查 UART 输入 */
        uint8_t ch;
        int len = uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, &ch, 1, pdMS_TO_TICKS(500));
        if (len > 0 && (ch == 0x03 || ch == 0x1C)) {
            ts_console_printf("\n^C\n");
            break;
        }
        
        /* 显示统计（每 5 秒） */
        if (verbose) {
            static int counter = 0;
            if (++counter >= 10) {
                counter = 0;
                ts_forward_stats_t stats;
                if (ts_port_forward_get_stats(forward, &stats) == ESP_OK) {
                    ts_console_printf("Stats: %u active, %u total, TX: %llu, RX: %llu\r",
                                      stats.active_connections, stats.total_connections,
                                      stats.bytes_sent, stats.bytes_received);
                }
            }
        }
    }
    
    /* 停止转发 */
    ts_console_printf("Stopping port forward...\n");
    ts_port_forward_stop(forward);
    ts_port_forward_destroy(forward);
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    ts_console_clear_interrupt();
    
    ts_console_printf("Port forwarding stopped.\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: ssh --test                               */
/*===========================================================================*/

static int do_ssh_test(const char *host, int port, const char *user, 
                       const char *password, const char *key_path, int timeout_sec)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;
    
    ts_console_printf("\nSSH Connection Test\n");
    ts_console_printf("═══════════════════════════════════════\n");
    ts_console_printf("  Host:     %s\n", host);
    ts_console_printf("  Port:     %d\n", port);
    ts_console_printf("  User:     %s\n", user);
    ts_console_printf("  Auth:     %s\n", key_path ? "Public Key" : "Password");
    ts_console_printf("  Timeout:  %d seconds\n", timeout_sec);
    ts_console_printf("═══════════════════════════════════════\n\n");
    
    /* 配置 SSH 连接 */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.timeout_ms = timeout_sec * 1000;
    
    /* 选择认证方式 */
    if (key_path) {
        config.auth_method = TS_SSH_AUTH_PUBLICKEY;
        config.auth.key.private_key_path = key_path;
        config.auth.key.private_key = NULL;
        config.auth.key.private_key_len = 0;
        config.auth.key.passphrase = NULL;
    } else {
        config.auth_method = TS_SSH_AUTH_PASSWORD;
        config.auth.password = password;
    }
    
    ts_console_printf("[1/3] Creating session... ");
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        return 1;
    }
    ts_console_printf("OK\n");
    
    ts_console_printf("[2/3] Connecting and authenticating... ");
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        return 1;
    }
    ts_console_printf("OK\n");
    
    ts_console_printf("[3/3] Testing command execution... ");
    ts_ssh_exec_result_t result;
    ret = ts_ssh_exec(session, "echo 'TianShanOS SSH test'", &result);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        return 1;
    }
    
    bool test_ok = (result.exit_code == 0 && 
                    result.stdout_data && 
                    strstr(result.stdout_data, "TianShanOS") != NULL);
    ts_ssh_exec_result_free(&result);
    
    if (test_ok) {
        ts_console_printf("OK\n");
    } else {
        ts_console_printf("FAILED\n");
    }
    
    /* 断开连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    ts_console_printf("\n");
    if (test_ok) {
        ts_console_printf("✓ SSH connection test PASSED\n");
        return 0;
    } else {
        ts_console_printf("✗ SSH connection test FAILED\n");
        return 1;
    }
}

/*===========================================================================*/
/*                          Command: ssh --copy-id                            */
/*===========================================================================*/

/**
 * @brief 读取公钥文件内容
 */
static esp_err_t load_public_key(const char *path, char **pubkey_data)
{
    if (!path || !pubkey_data) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 构建公钥文件路径 (.pub) */
    char pub_path[256];
    snprintf(pub_path, sizeof(pub_path), "%s.pub", path);

    FILE *fp = fopen(pub_path, "r");
    if (!fp) {
        ts_console_printf("Error: Cannot open public key file: %s\n", pub_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* 获取文件大小 */
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 8192) {
        ts_console_printf("Error: Invalid public key file size: %ld\n", fsize);
        fclose(fp);
        return ESP_ERR_INVALID_SIZE;
    }

    /* 分配内存并读取 */
    char *data = malloc(fsize + 1);
    if (!data) {
        fclose(fp);
        return ESP_ERR_NO_MEM;
    }

    size_t read_len = fread(data, 1, fsize, fp);
    fclose(fp);

    if (read_len != (size_t)fsize) {
        free(data);
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* 去除末尾换行符 */
    data[fsize] = '\0';
    while (fsize > 0 && (data[fsize - 1] == '\n' || data[fsize - 1] == '\r')) {
        data[--fsize] = '\0';
    }

    *pubkey_data = data;
    return ESP_OK;
}

static int do_ssh_copy_id(const char *host, int port, const char *user, 
                          const char *password, const char *key_path, int timeout_sec)
{
    ts_ssh_session_t session = NULL;
    esp_err_t ret;
    char *pubkey_data = NULL;
    
    ts_console_printf("\nSSH Public Key Deployment\n");
    ts_console_printf("═══════════════════════════════════════\n");
    ts_console_printf("  Host:     %s\n", host);
    ts_console_printf("  Port:     %d\n", port);
    ts_console_printf("  User:     %s\n", user);
    ts_console_printf("  Key:      %s.pub\n", key_path);
    ts_console_printf("═══════════════════════════════════════\n\n");
    
    /* 读取公钥文件 */
    ts_console_printf("[1/4] Reading public key... ");
    ret = load_public_key(key_path, &pubkey_data);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        return 1;
    }
    ts_console_printf("OK\n");
    
    /* 配置 SSH 连接（使用密码认证） */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    config.host = host;
    config.port = port;
    config.username = user;
    config.timeout_ms = timeout_sec * 1000;
    config.auth_method = TS_SSH_AUTH_PASSWORD;
    config.auth.password = password;
    
    ts_console_printf("[2/4] Connecting with password... ");
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED (session create)\n");
        free(pubkey_data);
        return 1;
    }
    
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return 1;
    }
    ts_console_printf("OK\n");
    
    /* 构建部署命令 */
    ts_console_printf("[3/4] Deploying public key... ");
    
    /* 命令：创建 .ssh 目录并追加公钥到 authorized_keys */
    char *deploy_cmd = malloc(strlen(pubkey_data) + 512);
    if (!deploy_cmd) {
        ts_console_printf("FAILED (out of memory)\n");
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return 1;
    }
    
    snprintf(deploy_cmd, strlen(pubkey_data) + 512,
             "mkdir -p ~/.ssh && chmod 700 ~/.ssh && "
             "echo '%s' >> ~/.ssh/authorized_keys && "
             "chmod 600 ~/.ssh/authorized_keys && "
             "echo 'Key deployed successfully'",
             pubkey_data);
    
    ts_ssh_exec_result_t result;
    ret = ts_ssh_exec(session, deploy_cmd, &result);
    free(deploy_cmd);
    
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", ts_ssh_get_error(session));
        ts_ssh_disconnect(session);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        return 1;
    }
    
    bool deploy_ok = (result.exit_code == 0);
    if (result.stderr_data && strlen(result.stderr_data) > 0) {
        ts_console_printf("WARNING\n");
        ts_console_printf("  stderr: %s\n", result.stderr_data);
    } else if (deploy_ok) {
        ts_console_printf("OK\n");
    } else {
        ts_console_printf("FAILED (exit code: %d)\n", result.exit_code);
    }
    ts_ssh_exec_result_free(&result);
    
    /* 断开密码连接 */
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    
    if (!deploy_ok) {
        free(pubkey_data);
        return 1;
    }
    
    /* 验证公钥认证 */
    ts_console_printf("[4/4] Verifying public key auth... ");
    
    /* 使用公钥认证重新连接（使用文件路径方式） */
    ts_ssh_config_t verify_config = TS_SSH_DEFAULT_CONFIG();
    verify_config.host = host;
    verify_config.port = port;
    verify_config.username = user;
    verify_config.timeout_ms = timeout_sec * 1000;
    verify_config.auth_method = TS_SSH_AUTH_PUBLICKEY;
    verify_config.auth.key.private_key_path = key_path;
    verify_config.auth.key.private_key = NULL;
    verify_config.auth.key.private_key_len = 0;
    verify_config.auth.key.passphrase = NULL;
    
    ret = ts_ssh_session_create(&verify_config, &session);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED (session)\n");
        free(pubkey_data);
        return 1;
    }
    
    ret = ts_ssh_connect(session);
    
    if (ret != ESP_OK) {
        const char *error_msg = ts_ssh_get_error(session);
        
        /* 检查是否是密钥类型不支持的错误（libssh2 mbedTLS 只支持 RSA） */
        if (error_msg && strstr(error_msg, "Key type not supported")) {
            ts_console_printf("SKIPPED\n");
            ts_console_printf("  Note: libssh2 only supports RSA keys for authentication\n");
            ts_ssh_session_destroy(session);
            free(pubkey_data);
            ts_console_printf("\n✓ Public key deployed successfully!\n");
            ts_console_printf("\n⚠ Verification skipped (ECDSA not supported by libssh2)\n");
            ts_console_printf("  The key has been added to authorized_keys.\n");
            ts_console_printf("  For full TianShanOS SSH client support, use RSA keys:\n");
            ts_console_printf("    ssh --keygen --type rsa2048 --output /sdcard/id_rsa\n");
            return 0;
        }
        
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: %s\n", error_msg);
        ts_ssh_session_destroy(session);
        free(pubkey_data);
        ts_console_printf("\n⚠ Key deployed but verification failed\n");
        return 1;
    }
    
    ts_console_printf("OK\n");
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    free(pubkey_data);
    
    ts_console_printf("\n✓ Public key authentication configured successfully!\n");
    ts_console_printf("\nYou can now connect without password:\n");
    ts_console_printf("  ssh --host %s --user %s --key %s --exec <cmd>\n", host, user, key_path);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: ssh --keygen                             */
/*===========================================================================*/

/**
 * @brief 解析密钥类型字符串
 */
static bool parse_key_type(const char *type_str, ts_crypto_key_type_t *type)
{
    if (!type_str || !type) return false;
    
    if (strcmp(type_str, "rsa2048") == 0 || strcmp(type_str, "rsa") == 0) {
        *type = TS_CRYPTO_KEY_RSA_2048;
        return true;
    } else if (strcmp(type_str, "rsa4096") == 0) {
        *type = TS_CRYPTO_KEY_RSA_4096;
        return true;
    } else if (strcmp(type_str, "ec256") == 0 || strcmp(type_str, "ecdsa") == 0) {
        *type = TS_CRYPTO_KEY_EC_P256;
        return true;
    } else if (strcmp(type_str, "ec384") == 0) {
        *type = TS_CRYPTO_KEY_EC_P384;
        return true;
    }
    return false;
}

/**
 * @brief 获取密钥类型的描述字符串
 */
static const char *get_key_type_desc(ts_crypto_key_type_t type)
{
    switch (type) {
        case TS_CRYPTO_KEY_RSA_2048: return "RSA 2048-bit";
        case TS_CRYPTO_KEY_RSA_4096: return "RSA 4096-bit";
        case TS_CRYPTO_KEY_EC_P256:  return "ECDSA P-256 (secp256r1)";
        case TS_CRYPTO_KEY_EC_P384:  return "ECDSA P-384 (secp384r1)";
        default: return "Unknown";
    }
}

static int do_ssh_keygen(const char *type_str, const char *output_path, const char *comment)
{
    ts_crypto_key_type_t key_type;
    ts_keypair_t keypair = NULL;
    esp_err_t ret;
    
    /* 解析密钥类型 */
    if (!parse_key_type(type_str, &key_type)) {
        ts_console_printf("Error: Invalid key type '%s'\n", type_str);
        ts_console_printf("Supported types: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384\n");
        return 1;
    }
    
    ts_console_printf("\nSSH Key Generation\n");
    ts_console_printf("═══════════════════════════════════════\n");
    ts_console_printf("  Type:     %s\n", get_key_type_desc(key_type));
    ts_console_printf("  Output:   %s\n", output_path);
    if (comment) {
        ts_console_printf("  Comment:  %s\n", comment);
    }
    ts_console_printf("═══════════════════════════════════════\n\n");
    
    /* 生成密钥对 */
    ts_console_printf("[1/4] Generating key pair... ");
    
    /* RSA 4096 需要较长时间，给出提示 */
    if (key_type == TS_CRYPTO_KEY_RSA_4096) {
        ts_console_printf("(this may take 30-60 seconds)\n      ");
    }
    
    ret = ts_crypto_keypair_generate(key_type, &keypair);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: Failed to generate key pair (%s)\n", esp_err_to_name(ret));
        return 1;
    }
    ts_console_printf("OK\n");
    
    /* 导出私钥 (PEM 格式) */
    ts_console_printf("[2/4] Saving private key... ");
    
    char *private_pem = malloc(8192);
    if (!private_pem) {
        ts_console_printf("FAILED (out of memory)\n");
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    size_t pem_len = 8192;
    ret = ts_crypto_keypair_export_private(keypair, private_pem, &pem_len);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        free(private_pem);
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    /* 写入私钥文件 */
    FILE *fp = fopen(output_path, "w");
    if (!fp) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: Cannot create file %s\n", output_path);
        free(private_pem);
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    fprintf(fp, "%s", private_pem);
    fclose(fp);
    free(private_pem);
    ts_console_printf("OK\n");
    
    /* 导出公钥 (OpenSSH 格式) */
    ts_console_printf("[3/4] Saving public key... ");
    
    char *openssh_pub = malloc(4096);
    if (!openssh_pub) {
        ts_console_printf("FAILED (out of memory)\n");
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    size_t openssh_len = 4096;
    const char *key_comment = comment ? comment : "TianShanOS-generated-key";
    ret = ts_crypto_keypair_export_openssh(keypair, openssh_pub, &openssh_len, key_comment);
    if (ret != ESP_OK) {
        ts_console_printf("FAILED\n");
        free(openssh_pub);
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    /* 写入公钥文件 (.pub) */
    char pub_path[256];
    snprintf(pub_path, sizeof(pub_path), "%s.pub", output_path);
    
    fp = fopen(pub_path, "w");
    if (!fp) {
        ts_console_printf("FAILED\n");
        ts_console_printf("  Error: Cannot create file %s\n", pub_path);
        free(openssh_pub);
        ts_crypto_keypair_free(keypair);
        return 1;
    }
    
    fprintf(fp, "%s", openssh_pub);
    fclose(fp);
    ts_console_printf("OK\n");
    
    /* 显示公钥指纹 */
    ts_console_printf("[4/4] Key generation complete!\n\n");
    
    ts_console_printf("Files created:\n");
    ts_console_printf("  Private key: %s\n", output_path);
    ts_console_printf("  Public key:  %s\n", pub_path);
    ts_console_printf("\nPublic key (for authorized_keys):\n");
    ts_console_printf("─────────────────────────────────────────\n");
    ts_console_printf("%s", openssh_pub);
    ts_console_printf("─────────────────────────────────────────\n");
    
    ts_console_printf("\nUsage:\n");
    ts_console_printf("  1. Copy the public key above to remote server's ~/.ssh/authorized_keys\n");
    ts_console_printf("  2. Use: ssh --host <ip> --user <user> --key %s --exec <cmd>\n", output_path);
    
    free(openssh_pub);
    ts_crypto_keypair_free(keypair);
    
    return 0;
}

/*===========================================================================*/
/*                          Command Handler                                   */
/*===========================================================================*/

static int ssh_cmd_handler(int argc, char **argv)
{
    /* 解析参数 */
    int nerrors = arg_parse(argc, argv, (void **)&s_ssh_args);
    
    /* 显示帮助 */
    if (s_ssh_args.help->count > 0) {
        ts_console_printf("\nUsage: ssh [options]\n\n");
        ts_console_printf("SSH client for remote operations and key management\n\n");
        ts_console_printf("Connection Options:\n");
        ts_console_printf("  --host <ip>       Remote host address\n");
        ts_console_printf("  --port <num>      SSH port (default: 22)\n");
        ts_console_printf("  --user <name>     Username\n");
        ts_console_printf("  --password <pwd>  Password (for password auth)\n");
        ts_console_printf("  --key <path>      Private key file (for public key auth)\n");
        ts_console_printf("  --exec <cmd>      Execute command on remote host\n");
        ts_console_printf("  --shell           Open interactive shell\n");
        ts_console_printf("  --forward <spec>  Port forwarding: L<local>:<remote_host>:<remote_port>\n");
        ts_console_printf("  --test            Test SSH connection\n");
        ts_console_printf("  --timeout <sec>   Connection timeout in seconds (default: 10)\n");
        ts_console_printf("  --verbose         Show detailed output\n");
        ts_console_printf("\nKey Management Options:\n");
        ts_console_printf("  --keygen          Generate SSH key pair\n");
        ts_console_printf("  --copyid          Deploy public key to remote server\n");
        ts_console_printf("  --type <type>     Key type: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384\n");
        ts_console_printf("  --output <path>   Output file path for private key\n");
        ts_console_printf("  --comment <text>  Comment for the public key\n");
        ts_console_printf("\nGeneral:\n");
        ts_console_printf("  --help            Show this help\n");
        ts_console_printf("\nExamples:\n");
        ts_console_printf("  # Generate RSA key pair\n");
        ts_console_printf("  ssh --keygen --type rsa2048 --output /sdcard/id_rsa\n");
        ts_console_printf("  ssh --keygen --type ecdsa --output /sdcard/id_ecdsa --comment \"AGX key\"\n");
        ts_console_printf("  \n");
        ts_console_printf("  # Deploy public key to server (like ssh-copy-id)\n");
        ts_console_printf("  ssh --copyid --host 192.168.1.100 --user nvidia --password secret --key /sdcard/id_ecdsa\n");
        ts_console_printf("  \n");
        ts_console_printf("  # Password authentication\n");
        ts_console_printf("  ssh --host 192.168.1.100 --user root --password secret --exec \"uptime\"\n");
        ts_console_printf("  \n");
        ts_console_printf("  # Public key authentication\n");
        ts_console_printf("  ssh --host 192.168.1.100 --user root --key /sdcard/id_rsa --exec \"uptime\"\n");
        ts_console_printf("  ssh --host agx --user nvidia --key /sdcard/id_ecdsa --shell\n");
        ts_console_printf("  \n");
        ts_console_printf("  # Test connection\n");
        ts_console_printf("  ssh --test --host 192.168.1.100 --user root --key /sdcard/id_rsa\n");
        return 0;
    }
    
    /* 参数错误检查 */
    if (nerrors > 0) {
        arg_print_errors(stderr, s_ssh_args.end, "ssh");
        ts_console_printf("Use 'ssh --help' for usage information\n");
        return 1;
    }
    
    /* 检查是否是密钥生成模式 */
    if (s_ssh_args.keygen->count > 0) {
        /* --keygen 模式：必须指定 --type 和 --output */
        if (s_ssh_args.type->count == 0) {
            ts_console_printf("Error: --type is required for key generation\n");
            ts_console_printf("Supported types: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384\n");
            return 1;
        }
        if (s_ssh_args.output->count == 0) {
            ts_console_printf("Error: --output is required for key generation\n");
            ts_console_printf("Example: --output /sdcard/id_rsa\n");
            return 1;
        }
        
        const char *comment = (s_ssh_args.comment->count > 0) ? s_ssh_args.comment->sval[0] : NULL;
        return do_ssh_keygen(s_ssh_args.type->sval[0], s_ssh_args.output->sval[0], comment);
    }
    
    /* 检查是否是公钥部署模式 */
    if (s_ssh_args.copyid->count > 0) {
        /* --copy-id 模式：必须指定 --host, --user, --password, --key */
        if (s_ssh_args.host->count == 0) {
            ts_console_printf("Error: --host is required for --copy-id\n");
            return 1;
        }
        if (s_ssh_args.user->count == 0) {
            ts_console_printf("Error: --user is required for --copy-id\n");
            return 1;
        }
        if (s_ssh_args.password->count == 0) {
            ts_console_printf("Error: --password is required for --copy-id (initial auth)\n");
            return 1;
        }
        if (s_ssh_args.key->count == 0) {
            ts_console_printf("Error: --key is required for --copy-id (public key path without .pub)\n");
            return 1;
        }
        
        int port = (s_ssh_args.port->count > 0) ? s_ssh_args.port->ival[0] : 22;
        int timeout = (s_ssh_args.timeout->count > 0) ? s_ssh_args.timeout->ival[0] : 10;
        
        return do_ssh_copy_id(s_ssh_args.host->sval[0], port, 
                              s_ssh_args.user->sval[0], s_ssh_args.password->sval[0],
                              s_ssh_args.key->sval[0], timeout);
    }
    
    /* 连接模式：必需参数检查 */
    if (s_ssh_args.host->count == 0) {
        ts_console_printf("Error: --host is required\n");
        return 1;;
    }
    
    if (s_ssh_args.user->count == 0) {
        ts_console_printf("Error: --user is required\n");
        return 1;
    }
    
    /* 认证方式检查：必须提供 password 或 key 之一 */
    if (s_ssh_args.password->count == 0 && s_ssh_args.key->count == 0) {
        ts_console_printf("Error: --password or --key is required\n");
        return 1;
    }
    
    /* 获取参数值 */
    const char *host = s_ssh_args.host->sval[0];
    int port = (s_ssh_args.port->count > 0) ? s_ssh_args.port->ival[0] : 22;
    const char *user = s_ssh_args.user->sval[0];
    const char *password = (s_ssh_args.password->count > 0) ? s_ssh_args.password->sval[0] : NULL;
    const char *key_path = (s_ssh_args.key->count > 0) ? s_ssh_args.key->sval[0] : NULL;
    int timeout = (s_ssh_args.timeout->count > 0) ? s_ssh_args.timeout->ival[0] : 10;
    bool verbose = (s_ssh_args.verbose->count > 0);
    
    /* 执行操作（按优先级） */
    if (s_ssh_args.shell->count > 0) {
        /* 交互式 Shell */
        return do_ssh_shell(host, port, user, password, key_path, timeout, verbose);
    } else if (s_ssh_args.forward->count > 0) {
        /* 端口转发 */
        return do_ssh_forward(host, port, user, password, key_path,
                              s_ssh_args.forward->sval[0], timeout, verbose);
    } else if (s_ssh_args.exec->count > 0) {
        /* 执行命令 */
        const char *command = s_ssh_args.exec->sval[0];
        return do_ssh_exec(host, port, user, password, key_path, command, timeout, verbose);
    } else if (s_ssh_args.test->count > 0) {
        /* 测试连接 */
        return do_ssh_test(host, port, user, password, key_path, timeout);
    } else {
        ts_console_printf("Error: Specify --exec, --shell, --forward, --test, or --keygen\n");
        ts_console_printf("Use 'ssh --help' for usage information\n");
        return 1;
    }
}

/*===========================================================================*/
/*                          Command Registration                              */
/*===========================================================================*/

esp_err_t ts_cmd_ssh_register(void)
{
    /* 连接相关参数 */
    s_ssh_args.host = arg_str0(NULL, "host", "<ip>", "Remote host address");
    s_ssh_args.port = arg_int0(NULL, "port", "<num>", "SSH port (default: 22)");
    s_ssh_args.user = arg_str0(NULL, "user", "<name>", "Username");
    s_ssh_args.password = arg_str0(NULL, "password", "<pwd>", "Password");
    s_ssh_args.key = arg_str0(NULL, "key", "<path>", "Private key file (PEM)");
    s_ssh_args.exec = arg_str0(NULL, "exec", "<cmd>", "Command to execute");
    s_ssh_args.test = arg_lit0(NULL, "test", "Test SSH connection");
    s_ssh_args.shell = arg_lit0(NULL, "shell", "Open interactive shell");
    s_ssh_args.forward = arg_str0(NULL, "forward", "<spec>", "Port forward: L<local>:<host>:<port>");
    s_ssh_args.timeout = arg_int0(NULL, "timeout", "<sec>", "Timeout in seconds");
    s_ssh_args.verbose = arg_lit0("v", "verbose", "Verbose output");
    
    /* 密钥管理参数 */
    s_ssh_args.keygen = arg_lit0(NULL, "keygen", "Generate SSH key pair");
    s_ssh_args.copyid = arg_lit0(NULL, "copyid", "Deploy public key to remote server");
    s_ssh_args.type = arg_str0(NULL, "type", "<type>", "Key type: rsa, rsa2048, rsa4096, ecdsa, ec256, ec384");
    s_ssh_args.output = arg_str0(NULL, "output", "<path>", "Output file path for private key");
    s_ssh_args.comment = arg_str0(NULL, "comment", "<text>", "Comment for the public key");
    
    /* 通用参数 */
    s_ssh_args.help = arg_lit0("h", "help", "Show help");
    s_ssh_args.end = arg_end(10);
    
    /* 注册命令 */
    const esp_console_cmd_t cmd = {
        .command = "ssh",
        .help = "SSH client and key management. Use 'ssh --help' for details.",
        .hint = NULL,
        .func = ssh_cmd_handler,
        .argtable = &s_ssh_args,
    };
    
    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to register ssh command: %s", esp_err_to_name(ret));
    } else {
        TS_LOGI(TAG, "Registered command: ssh");
    }
    
    return ret;
}
