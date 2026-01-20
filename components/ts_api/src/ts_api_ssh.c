/**
 * @file ts_api_ssh.c
 * @brief SSH API Handlers
 * 
 * 提供 SSH 相关操作的 API 端点：
 * - ssh.exec - 执行远程命令
 * - ssh.test - 测试连接
 * - ssh.keygen - 生成密钥对
 * 
 * 注意：交互式 Shell 不通过 API 实现
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-20
 */

#include "ts_api.h"
#include "ts_ssh_client.h"
#include "ts_keystore.h"
#include "ts_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "api_ssh"

/* 静态缓冲区用于存储主机、用户名等 */
static char s_host_buf[64];
static char s_user_buf[64];
static char s_pass_buf[128];
static char *s_key_buf = NULL;
static size_t s_key_len = 0;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief 清理密钥缓冲区
 */
static void cleanup_key_buffer(void)
{
    if (s_key_buf) {
        memset(s_key_buf, 0, s_key_len);
        free(s_key_buf);
        s_key_buf = NULL;
        s_key_len = 0;
    }
}

/**
 * @brief 从参数中配置 SSH 会话
 */
static esp_err_t configure_ssh_from_params(const cJSON *params, ts_ssh_config_t *config)
{
    /* 主机和用户名 */
    const cJSON *host = cJSON_GetObjectItem(params, "host");
    const cJSON *user = cJSON_GetObjectItem(params, "user");
    const cJSON *port = cJSON_GetObjectItem(params, "port");
    
    if (!host || !cJSON_IsString(host) || !user || !cJSON_IsString(user)) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 复制到静态缓冲区 */
    strncpy(s_host_buf, host->valuestring, sizeof(s_host_buf) - 1);
    s_host_buf[sizeof(s_host_buf) - 1] = '\0';
    strncpy(s_user_buf, user->valuestring, sizeof(s_user_buf) - 1);
    s_user_buf[sizeof(s_user_buf) - 1] = '\0';
    
    config->host = s_host_buf;
    config->username = s_user_buf;
    config->port = cJSON_IsNumber(port) ? port->valueint : 22;
    
    /* 认证方式：密码或密钥 */
    const cJSON *password = cJSON_GetObjectItem(params, "password");
    const cJSON *keyid = cJSON_GetObjectItem(params, "keyid");
    const cJSON *keypath = cJSON_GetObjectItem(params, "keypath");
    
    cleanup_key_buffer();
    
    if (cJSON_IsString(password) && password->valuestring[0]) {
        strncpy(s_pass_buf, password->valuestring, sizeof(s_pass_buf) - 1);
        s_pass_buf[sizeof(s_pass_buf) - 1] = '\0';
        config->auth_method = TS_SSH_AUTH_PASSWORD;
        config->auth.password = s_pass_buf;
    } else if (cJSON_IsString(keyid) && keyid->valuestring[0]) {
        /* 从密钥存储读取私钥 */
        esp_err_t ret = ts_keystore_load_private_key(keyid->valuestring, &s_key_buf, &s_key_len);
        if (ret != ESP_OK) {
            TS_LOGE(TAG, "Failed to load key '%s': %s", keyid->valuestring, esp_err_to_name(ret));
            return ret;
        }
        config->auth_method = TS_SSH_AUTH_PUBLICKEY;
        config->auth.key.private_key = (const uint8_t *)s_key_buf;
        config->auth.key.private_key_len = s_key_len;
        config->auth.key.private_key_path = NULL;
        config->auth.key.passphrase = NULL;
    } else if (cJSON_IsString(keypath) && keypath->valuestring[0]) {
        config->auth_method = TS_SSH_AUTH_PUBLICKEY;
        config->auth.key.private_key = NULL;
        config->auth.key.private_key_len = 0;
        config->auth.key.private_key_path = keypath->valuestring;
        config->auth.key.passphrase = NULL;
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/*===========================================================================*/
/*                          API Handlers                                      */
/*===========================================================================*/

/**
 * @brief ssh.exec - Execute remote command
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx" | "keyid": "default" | "keypath": "/sdcard/id_rsa",
 *   "port": 22,
 *   "command": "ls -la",
 *   "timeout_ms": 30000
 * }
 */
static esp_err_t api_ssh_exec(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *cmd = cJSON_GetObjectItem(params, "command");
    if (!cmd || !cJSON_IsString(cmd)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'command' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 配置 SSH */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    esp_err_t ret = configure_ssh_from_params(params, &config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid SSH configuration");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 超时设置 */
    const cJSON *timeout = cJSON_GetObjectItem(params, "timeout_ms");
    if (cJSON_IsNumber(timeout)) {
        config.timeout_ms = timeout->valueint;
    }
    
    /* 创建会话 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 连接 */
    ret = ts_ssh_connect(session);
    if (ret != ESP_OK) {
        const char *err = ts_ssh_get_error(session);
        ts_api_result_error(result, TS_API_ERR_CONNECTION, err ? err : "Failed to connect");
        ts_ssh_session_destroy(session);
        cleanup_key_buffer();
        return ret;
    }
    
    /* 执行命令 */
    ts_ssh_exec_result_t exec_result = {0};
    ret = ts_ssh_exec(session, cmd->valuestring, &exec_result);
    
    if (ret == ESP_OK) {
        cJSON *data = cJSON_CreateObject();
        cJSON_AddNumberToObject(data, "exit_code", exec_result.exit_code);
        
        if (exec_result.stdout_data) {
            cJSON_AddStringToObject(data, "stdout", exec_result.stdout_data);
        } else {
            cJSON_AddStringToObject(data, "stdout", "");
        }
        
        if (exec_result.stderr_data) {
            cJSON_AddStringToObject(data, "stderr", exec_result.stderr_data);
        } else {
            cJSON_AddStringToObject(data, "stderr", "");
        }
        
        ts_api_result_ok(result, data);
        ts_ssh_exec_result_free(&exec_result);
    } else {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Command execution failed");
    }
    
    ts_ssh_disconnect(session);
    ts_ssh_session_destroy(session);
    cleanup_key_buffer();
    
    return ret;
}

/**
 * @brief ssh.test - Test SSH connection
 * 
 * Params: { 
 *   "host": "192.168.1.100", 
 *   "user": "root",
 *   "password": "xxx" | "keyid": "default" | "keypath": "/sdcard/id_rsa",
 *   "port": 22
 * }
 */
static esp_err_t api_ssh_test(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 配置 SSH */
    ts_ssh_config_t config = TS_SSH_DEFAULT_CONFIG();
    esp_err_t ret = configure_ssh_from_params(params, &config);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Invalid SSH configuration");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 创建会话 */
    ts_ssh_session_t session = NULL;
    ret = ts_ssh_session_create(&config, &session);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Failed to create session");
        cleanup_key_buffer();
        return ret;
    }
    
    /* 测试连接 */
    ret = ts_ssh_connect(session);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddBoolToObject(data, "success", (ret == ESP_OK));
    
    if (ret == ESP_OK) {
        cJSON_AddStringToObject(data, "host", config.host);
        cJSON_AddNumberToObject(data, "port", config.port);
        cJSON_AddStringToObject(data, "user", config.username);
        ts_ssh_disconnect(session);
    } else {
        const char *err = ts_ssh_get_error(session);
        cJSON_AddStringToObject(data, "error", err ? err : "Connection failed");
    }
    
    ts_ssh_session_destroy(session);
    cleanup_key_buffer();
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/**
 * @brief ssh.keygen - Generate SSH key pair
 * 
 * Params: { 
 *   "id": "mykey",
 *   "type": "ecdsa" | "rsa-2048" | "rsa-4096",
 *   "comment": "optional comment"
 * }
 */
static esp_err_t api_ssh_keygen(const cJSON *params, ts_api_result_t *result)
{
    if (!params) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing parameters");
        return ESP_ERR_INVALID_ARG;
    }
    
    const cJSON *id = cJSON_GetObjectItem(params, "id");
    if (!id || !cJSON_IsString(id)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, "Missing 'id' parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 检查密钥是否已存在 */
    if (ts_keystore_key_exists(id->valuestring)) {
        ts_api_result_error(result, TS_API_ERR_BUSY, "Key already exists");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* 解析密钥类型 */
    const cJSON *type_json = cJSON_GetObjectItem(params, "type");
    const char *type_str = cJSON_IsString(type_json) ? type_json->valuestring : "ecdsa";
    
    ts_keystore_key_type_t type = TS_KEYSTORE_TYPE_ECDSA_P256;
    if (strcmp(type_str, "rsa-2048") == 0) {
        type = TS_KEYSTORE_TYPE_RSA_2048;
    } else if (strcmp(type_str, "rsa-4096") == 0) {
        type = TS_KEYSTORE_TYPE_RSA_4096;
    } else if (strcmp(type_str, "ecdsa-p384") == 0) {
        type = TS_KEYSTORE_TYPE_ECDSA_P384;
    }
    
    /* 注释 */
    const cJSON *comment = cJSON_GetObjectItem(params, "comment");
    const char *comment_str = cJSON_IsString(comment) ? comment->valuestring : NULL;
    
    /* 生成密钥 */
    esp_err_t ret = ts_keystore_generate_key(id->valuestring, type, comment_str);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_INTERNAL, "Key generation failed");
        return ret;
    }
    
    /* 获取公钥 */
    char *pubkey = NULL;
    size_t pubkey_len = 0;
    ret = ts_keystore_load_public_key(id->valuestring, &pubkey, &pubkey_len);
    
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "id", id->valuestring);
    cJSON_AddStringToObject(data, "type", type_str);
    
    if (ret == ESP_OK && pubkey) {
        cJSON_AddStringToObject(data, "public_key", pubkey);
        free(pubkey);
    }
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

static const ts_api_endpoint_t ssh_endpoints[] = {
    {
        .name = "ssh.exec",
        .description = "Execute remote command via SSH",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_exec,
        .requires_auth = true,
    },
    {
        .name = "ssh.test",
        .description = "Test SSH connection",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_test,
        .requires_auth = true,
    },
    {
        .name = "ssh.keygen",
        .description = "Generate SSH key pair",
        .category = TS_API_CAT_SECURITY,
        .handler = api_ssh_keygen,
        .requires_auth = true,
    },
};

esp_err_t ts_api_ssh_register(void)
{
    TS_LOGI(TAG, "Registering SSH APIs...");
    return ts_api_register_multiple(ssh_endpoints, 
                                     sizeof(ssh_endpoints) / sizeof(ssh_endpoints[0]));
}
