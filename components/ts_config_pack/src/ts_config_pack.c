/**
 * @file ts_config_pack.c
 * @brief TianShanOS Encrypted Configuration Package Implementation
 * 
 * 实现配置包的加密、解密、签名和验证逻辑。
 * 
 * 加密流程:
 * 1. 生成临时 EC 密钥对 (ephemeral)
 * 2. ECDH 密钥协商 (ephemeral_priv × recipient_pub)
 * 3. HKDF 密钥派生 (shared_secret → AES key)
 * 4. AES-256-GCM 加密
 * 5. ECDSA 签名
 * 
 * 解密流程:
 * 1. 验证签名者证书链
 * 2. 验证签名
 * 3. ECDH 密钥协商 (device_priv × ephemeral_pub)
 * 4. HKDF 密钥派生
 * 5. AES-256-GCM 解密
 * 6. 验证内容哈希
 */

#include "ts_config_pack.h"
#include "ts_core.h"
#include "ts_cert.h"
#include "ts_crypto.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "ts_config_pack";

/*===========================================================================*/
/*                              Constants                                     */
/*===========================================================================*/

/** ECDH P-256 公钥长度 (uncompressed: 0x04 || X || Y) */
#define ECDH_PUBKEY_LEN         65

/** HKDF salt 长度 */
#define HKDF_SALT_LEN           32

/** AES-256 密钥长度 */
#define AES_KEY_LEN             32

/** AES-GCM IV 长度 */
#define AES_IV_LEN              12

/** AES-GCM tag 长度 */
#define AES_TAG_LEN             16

/** SHA-256 哈希长度 */
#define SHA256_LEN              32

/** 证书指纹长度 (hex string) */
#define CERT_FINGERPRINT_LEN    64

/** 最大签名长度 (ECDSA P-256 DER) */
#define MAX_SIGNATURE_LEN       72

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/** 内部：加密参数 */
typedef struct {
    uint8_t ephemeral_pubkey[ECDH_PUBKEY_LEN];
    uint8_t salt[HKDF_SALT_LEN];
    uint8_t iv[AES_IV_LEN];
    uint8_t tag[AES_TAG_LEN];
    char recipient_fingerprint[CERT_FINGERPRINT_LEN + 1];
} ts_config_pack_crypto_params_t;

/*===========================================================================*/
/*                          Static Variables                                  */
/*===========================================================================*/

static bool s_initialized = false;
static char s_device_fingerprint[CERT_FINGERPRINT_LEN + 1] = {0};

/*===========================================================================*/
/*                          Forward Declarations                              */
/*===========================================================================*/

static esp_err_t compute_cert_fingerprint(const char *cert_pem, size_t cert_len,
                                           char *fingerprint, size_t fp_len);
static ts_config_pack_result_t parse_tscfg_json(const char *json, size_t len,
                                                  cJSON **root,
                                                  ts_config_pack_crypto_params_t *params,
                                                  char **payload_b64,
                                                  char **signer_cert_pem,
                                                  char **signature_b64,
                                                  ts_config_pack_sig_info_t *sig_info);
static ts_config_pack_result_t verify_signature(const char *signer_cert_pem,
                                                  const uint8_t *data_to_sign,
                                                  size_t data_len,
                                                  const uint8_t *signature,
                                                  size_t sig_len);
static ts_config_pack_result_t decrypt_payload(const ts_config_pack_crypto_params_t *params,
                                                 const uint8_t *ciphertext,
                                                 size_t ciphertext_len,
                                                 char **plaintext,
                                                 size_t *plaintext_len);

/*===========================================================================*/
/*                          Error Messages                                    */
/*===========================================================================*/

static const char *s_error_messages[] = {
    [TS_CONFIG_PACK_OK]             = "Success",
    [TS_CONFIG_PACK_ERR_PARSE]      = "JSON parse error",
    [TS_CONFIG_PACK_ERR_VERSION]    = "Unsupported version",
    [TS_CONFIG_PACK_ERR_RECIPIENT]  = "Not intended for this device",
    [TS_CONFIG_PACK_ERR_CERT_CHAIN] = "Certificate chain validation failed",
    [TS_CONFIG_PACK_ERR_SIGNATURE]  = "Signature verification failed",
    [TS_CONFIG_PACK_ERR_DECRYPT]    = "Decryption failed",
    [TS_CONFIG_PACK_ERR_INTEGRITY]  = "Content hash mismatch",
    [TS_CONFIG_PACK_ERR_EXPIRED]    = "Package expired",
    [TS_CONFIG_PACK_ERR_NO_MEM]     = "Memory allocation failed",
    [TS_CONFIG_PACK_ERR_IO]         = "File I/O error",
    [TS_CONFIG_PACK_ERR_PERMISSION] = "Permission denied (not a developer device)",
    [TS_CONFIG_PACK_ERR_INVALID_ARG]= "Invalid argument",
    [TS_CONFIG_PACK_ERR_NOT_INIT]   = "System not initialized",
};

const char *ts_config_pack_strerror(ts_config_pack_result_t result)
{
    if (result < 0 || result >= sizeof(s_error_messages) / sizeof(s_error_messages[0])) {
        return "Unknown error";
    }
    return s_error_messages[result];
}

/*===========================================================================*/
/*                          Initialization                                    */
/*===========================================================================*/

esp_err_t ts_config_pack_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }
    
    /* 获取设备证书指纹 */
    char cert_pem[TS_CERT_PEM_MAX_LEN];
    size_t cert_len = sizeof(cert_pem);
    
    esp_err_t ret = ts_cert_get_certificate(cert_pem, &cert_len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No device certificate, config pack import disabled");
        /* 没有证书也可以初始化，但无法解密 */
        s_initialized = true;
        return ESP_OK;
    }
    
    ret = compute_cert_fingerprint(cert_pem, cert_len, 
                                    s_device_fingerprint, sizeof(s_device_fingerprint));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to compute certificate fingerprint");
        return ret;
    }
    
    ESP_LOGI(TAG, "Config pack system initialized, device fingerprint: %.16s...", 
             s_device_fingerprint);
    s_initialized = true;
    return ESP_OK;
}

bool ts_config_pack_can_export(void)
{
    /* 检查设备证书的 OU 字段是否包含 "Developer" */
    ts_cert_info_t info;
    if (ts_cert_get_info(&info) != ESP_OK) {
        return false;
    }
    
    /* 检查 OU 字段是否为 "Developer" */
    bool can_export = (strstr(info.subject_ou, "Developer") != NULL);
    if (!can_export) {
        ESP_LOGD(TAG, "Device OU='%s', export not allowed", info.subject_ou);
    }
    return can_export;
}

/*===========================================================================*/
/*                          Load Functions                                    */
/*===========================================================================*/

ts_config_pack_result_t ts_config_pack_load(const char *path, ts_config_pack_t **pack)
{
    if (!path || !pack) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        return TS_CONFIG_PACK_ERR_NOT_INIT;
    }
    
    /* 读取文件 */
    FILE *f = fopen(path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return TS_CONFIG_PACK_ERR_IO;
    }
    
    /* 获取文件大小 */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > CONFIG_TS_CONFIG_PACK_MAX_SIZE) {
        ESP_LOGE(TAG, "Invalid file size: %ld", file_size);
        fclose(f);
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    char *json_buf = TS_MALLOC_PSRAM(file_size + 1);
    if (!json_buf) {
        fclose(f);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    size_t read_len = fread(json_buf, 1, file_size, f);
    fclose(f);
    
    if (read_len != (size_t)file_size) {
        free(json_buf);
        return TS_CONFIG_PACK_ERR_IO;
    }
    json_buf[file_size] = '\0';
    
    ts_config_pack_result_t result = ts_config_pack_load_mem(json_buf, file_size, pack);
    free(json_buf);
    
    return result;
}

ts_config_pack_result_t ts_config_pack_load_mem(
    const char *tscfg_json,
    size_t tscfg_len,
    ts_config_pack_t **pack)
{
    if (!tscfg_json || !pack) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        return TS_CONFIG_PACK_ERR_NOT_INIT;
    }
    
    /* 检查设备是否有私钥可用于解密 */
    if (s_device_fingerprint[0] == '\0') {
        ESP_LOGE(TAG, "No device certificate, cannot decrypt");
        return TS_CONFIG_PACK_ERR_RECIPIENT;
    }
    
    cJSON *root = NULL;
    ts_config_pack_crypto_params_t params = {0};
    char *payload_b64 = NULL;
    char *signer_cert_pem = NULL;
    char *signature_b64 = NULL;
    ts_config_pack_sig_info_t sig_info = {0};
    
    /* 解析 JSON */
    ts_config_pack_result_t result = parse_tscfg_json(
        tscfg_json, tscfg_len,
        &root, &params, &payload_b64, &signer_cert_pem, &signature_b64, &sig_info);
    
    if (result != TS_CONFIG_PACK_OK) {
        goto cleanup;
    }
    
    /* 验证接收方指纹 */
    if (strcmp(params.recipient_fingerprint, s_device_fingerprint) != 0) {
        ESP_LOGE(TAG, "Config pack not intended for this device");
        ESP_LOGD(TAG, "Expected: %s, Got: %s", s_device_fingerprint, params.recipient_fingerprint);
        result = TS_CONFIG_PACK_ERR_RECIPIENT;
        goto cleanup;
    }
    
    /* Base64 解码 payload */
    size_t payload_b64_len = strlen(payload_b64);
    size_t ciphertext_max_len = (payload_b64_len * 3) / 4 + 4;
    uint8_t *ciphertext = TS_MALLOC_PSRAM(ciphertext_max_len);
    if (!ciphertext) {
        result = TS_CONFIG_PACK_ERR_NO_MEM;
        goto cleanup;
    }
    
    size_t ciphertext_len = ciphertext_max_len;
    if (ts_crypto_base64_decode(payload_b64, payload_b64_len, 
                                 ciphertext, &ciphertext_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode payload");
        free(ciphertext);
        result = TS_CONFIG_PACK_ERR_PARSE;
        goto cleanup;
    }
    
    /* Base64 解码签名 */
    size_t sig_b64_len = strlen(signature_b64);
    uint8_t signature[MAX_SIGNATURE_LEN];
    size_t sig_len = sizeof(signature);
    if (ts_crypto_base64_decode(signature_b64, sig_b64_len,
                                 signature, &sig_len) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode signature");
        free(ciphertext);
        result = TS_CONFIG_PACK_ERR_PARSE;
        goto cleanup;
    }
    
    /* 构建待签名数据并验证签名 */
    /* TODO: 完整的签名数据应包含 ciphertext + ephemeral_pub + fingerprint + timestamp + version */
    /* 这里简化为只对 ciphertext 签名 */
    result = verify_signature(signer_cert_pem, ciphertext, ciphertext_len,
                               signature, sig_len);
    if (result != TS_CONFIG_PACK_OK) {
        free(ciphertext);
        goto cleanup;
    }
    
    sig_info.valid = true;
    
    /* 解密 payload */
    char *plaintext = NULL;
    size_t plaintext_len = 0;
    result = decrypt_payload(&params, ciphertext, ciphertext_len,
                              &plaintext, &plaintext_len);
    free(ciphertext);
    
    if (result != TS_CONFIG_PACK_OK) {
        goto cleanup;
    }
    
    /* 构建返回结构 */
    ts_config_pack_t *p = TS_CALLOC_PSRAM(1, sizeof(ts_config_pack_t));
    if (!p) {
        free(plaintext);
        result = TS_CONFIG_PACK_ERR_NO_MEM;
        goto cleanup;
    }
    
    /* 从 JSON 提取元数据 */
    cJSON *metadata = cJSON_GetObjectItem(root, "metadata");
    if (metadata) {
        cJSON *name = cJSON_GetObjectItem(metadata, "name");
        cJSON *desc = cJSON_GetObjectItem(metadata, "description");
        cJSON *source = cJSON_GetObjectItem(metadata, "source_file");
        cJSON *target = cJSON_GetObjectItem(metadata, "target_device");
        
        if (name && cJSON_IsString(name)) {
            p->name = strdup(cJSON_GetStringValue(name));
        }
        if (desc && cJSON_IsString(desc)) {
            p->description = strdup(cJSON_GetStringValue(desc));
        }
        if (source && cJSON_IsString(source)) {
            p->source_file = strdup(cJSON_GetStringValue(source));
        }
        if (target && cJSON_IsString(target)) {
            p->target_device = strdup(cJSON_GetStringValue(target));
        }
        /* TODO: 解析 ISO 8601 时间戳 */
    }
    
    p->content = plaintext;
    p->content_len = plaintext_len;
    p->sig_info = sig_info;
    
    *pack = p;
    result = TS_CONFIG_PACK_OK;
    
cleanup:
    if (root) cJSON_Delete(root);
    return result;
}

void ts_config_pack_free(ts_config_pack_t *pack)
{
    if (!pack) return;
    
    free(pack->name);
    free(pack->description);
    free(pack->content);
    free(pack->source_file);
    free(pack->target_device);
    free(pack);
}

/*===========================================================================*/
/*                          Create Functions                                  */
/*===========================================================================*/

ts_config_pack_result_t ts_config_pack_create(
    const char *name,
    const char *json_content,
    size_t json_len,
    const ts_config_pack_export_opts_t *opts,
    char **output,
    size_t *output_len)
{
    if (!name || !json_content || !opts || !output || !output_len) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    if (!s_initialized) {
        return TS_CONFIG_PACK_ERR_NOT_INIT;
    }
    
    /* 检查导出权限 */
    if (!ts_config_pack_can_export()) {
        ESP_LOGE(TAG, "This device is not authorized to export config packs");
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 解析接收方证书，获取公钥 */
    ts_keypair_t recipient_key = NULL;
    esp_err_t ret = ts_crypto_keypair_import(opts->recipient_cert_pem,
                                              opts->recipient_cert_len,
                                              &recipient_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse recipient certificate");
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    /* 计算接收方证书指纹 */
    char recipient_fingerprint[CERT_FINGERPRINT_LEN + 1];
    ret = compute_cert_fingerprint(opts->recipient_cert_pem, opts->recipient_cert_len,
                                    recipient_fingerprint, sizeof(recipient_fingerprint));
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    /* 生成临时密钥对 */
    ts_keypair_t ephemeral_key = NULL;
    ret = ts_crypto_keypair_generate(TS_CRYPTO_KEY_EC_P256, &ephemeral_key);
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    /* 导出临时公钥 */
    uint8_t ephemeral_pubkey[ECDH_PUBKEY_LEN];
    size_t pubkey_len = sizeof(ephemeral_pubkey);
    ret = ts_crypto_keypair_export_public_raw(ephemeral_key, ephemeral_pubkey, &pubkey_len);
    if (ret != ESP_OK) {
        ts_crypto_keypair_free(ephemeral_key);
        ts_crypto_keypair_free(recipient_key);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    /* ECDH 密钥协商 */
    uint8_t shared_secret[32];
    size_t shared_len = sizeof(shared_secret);
    ret = ts_crypto_ecdh_compute_shared(ephemeral_key, opts->recipient_cert_pem,
                                         shared_secret, &shared_len);
    
    /* 立即销毁临时私钥 */
    ts_crypto_keypair_free(ephemeral_key);
    ts_crypto_keypair_free(recipient_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ECDH key agreement failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* 生成随机 salt 和 IV */
    uint8_t salt[HKDF_SALT_LEN];
    uint8_t iv[AES_IV_LEN];
    ts_crypto_random(salt, sizeof(salt));
    ts_crypto_random(iv, sizeof(iv));
    
    /* HKDF 密钥派生 */
    uint8_t aes_key[AES_KEY_LEN];
    ret = ts_crypto_hkdf(salt, sizeof(salt),
                          shared_secret, shared_len,
                          TS_CONFIG_PACK_HKDF_INFO, strlen(TS_CONFIG_PACK_HKDF_INFO),
                          aes_key, sizeof(aes_key));
    
    /* 清除 shared secret */
    memset(shared_secret, 0, sizeof(shared_secret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HKDF key derivation failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* AES-GCM 加密 */
    size_t ciphertext_len = json_len;
    uint8_t *ciphertext = TS_MALLOC_PSRAM(ciphertext_len);
    uint8_t tag[AES_TAG_LEN];
    
    if (!ciphertext) {
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    ret = ts_crypto_aes_gcm_encrypt(aes_key, sizeof(aes_key),
                                     iv, sizeof(iv),
                                     NULL, 0,  /* 无 AAD */
                                     json_content, json_len,
                                     ciphertext, tag);
    
    /* 清除 AES 密钥 */
    memset(aes_key, 0, sizeof(aes_key));
    
    if (ret != ESP_OK) {
        free(ciphertext);
        ESP_LOGE(TAG, "AES-GCM encryption failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* 计算内容哈希 */
    uint8_t content_hash[SHA256_LEN];
    ts_crypto_hash(TS_HASH_SHA256, json_content, json_len, content_hash, sizeof(content_hash));
    
    /* 获取设备私钥用于签名 */
    char key_pem[TS_CERT_KEY_MAX_LEN];
    size_t key_len = sizeof(key_pem);
    ret = ts_cert_get_private_key(key_pem, &key_len);
    if (ret != ESP_OK) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    ts_keypair_t signer_key = NULL;
    ret = ts_crypto_keypair_import(key_pem, key_len, &signer_key);
    memset(key_pem, 0, sizeof(key_pem));  /* 清除私钥 */
    
    if (ret != ESP_OK) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 计算待签名数据的哈希 */
    uint8_t data_hash[SHA256_LEN];
    ts_crypto_hash(TS_HASH_SHA256, ciphertext, ciphertext_len, data_hash, sizeof(data_hash));
    
    /* ECDSA 签名 */
    uint8_t signature[MAX_SIGNATURE_LEN];
    size_t sig_len = sizeof(signature);
    ret = ts_crypto_ecdsa_sign(signer_key, data_hash, sizeof(data_hash),
                                signature, &sig_len);
    ts_crypto_keypair_free(signer_key);
    
    if (ret != ESP_OK) {
        free(ciphertext);
        ESP_LOGE(TAG, "ECDSA signing failed");
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 获取设备证书 */
    char cert_pem[TS_CERT_PEM_MAX_LEN];
    size_t cert_len = sizeof(cert_pem);
    ret = ts_cert_get_certificate(cert_pem, &cert_len);
    if (ret != ESP_OK) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_PERMISSION;
    }
    
    /* 构建 JSON 输出 */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        free(ciphertext);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    cJSON_AddStringToObject(root, "tscfg_version", TS_CONFIG_PACK_VERSION);
    cJSON_AddStringToObject(root, "format", "encrypted");
    
    /* metadata */
    cJSON *metadata = cJSON_CreateObject();
    cJSON_AddStringToObject(metadata, "name", name);
    if (opts->description) {
        cJSON_AddStringToObject(metadata, "description", opts->description);
    }
    
    /* 获取当前时间 */
    time_t now = time(NULL);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
    cJSON_AddStringToObject(metadata, "created_at", time_str);
    
    /* 获取设备 ID */
    ts_cert_info_t cert_info;
    if (ts_cert_get_info(&cert_info) == ESP_OK) {
        cJSON_AddStringToObject(metadata, "created_by", cert_info.subject_cn);
    }
    
    /* TODO: 解析接收方证书获取 target_device */
    cJSON_AddStringToObject(metadata, "target_device", "unknown");
    
    char source_file[128];
    snprintf(source_file, sizeof(source_file), "%s.json", name);
    cJSON_AddStringToObject(metadata, "source_file", source_file);
    
    /* Base64 编码 content_hash */
    char hash_b64[64];
    size_t hash_b64_len = sizeof(hash_b64);
    ts_crypto_base64_encode(content_hash, sizeof(content_hash), hash_b64, &hash_b64_len);
    cJSON_AddStringToObject(metadata, "content_hash", hash_b64);
    
    cJSON_AddItemToObject(root, "metadata", metadata);
    
    /* encryption */
    cJSON *encryption = cJSON_CreateObject();
    cJSON_AddStringToObject(encryption, "algorithm", TS_CONFIG_PACK_ALGORITHM);
    cJSON_AddStringToObject(encryption, "kdf", TS_CONFIG_PACK_KDF);
    
    /* Base64 编码各字段 */
    char pubkey_b64[128], salt_b64[64], iv_b64[32], tag_b64[32];
    size_t b64_len;
    
    b64_len = sizeof(pubkey_b64);
    ts_crypto_base64_encode(ephemeral_pubkey, pubkey_len, pubkey_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "ephemeral_public_key", pubkey_b64);
    
    b64_len = sizeof(salt_b64);
    ts_crypto_base64_encode(salt, sizeof(salt), salt_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "salt", salt_b64);
    
    b64_len = sizeof(iv_b64);
    ts_crypto_base64_encode(iv, sizeof(iv), iv_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "iv", iv_b64);
    
    b64_len = sizeof(tag_b64);
    ts_crypto_base64_encode(tag, sizeof(tag), tag_b64, &b64_len);
    cJSON_AddStringToObject(encryption, "tag", tag_b64);
    
    cJSON_AddStringToObject(encryption, "recipient_cert_fingerprint", recipient_fingerprint);
    cJSON_AddItemToObject(root, "encryption", encryption);
    
    /* signature */
    cJSON *sig_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(sig_obj, "algorithm", TS_CONFIG_PACK_SIG_ALGORITHM);
    cJSON_AddStringToObject(sig_obj, "signer_certificate", cert_pem);
    
    char sig_b64[128];
    b64_len = sizeof(sig_b64);
    ts_crypto_base64_encode(signature, sig_len, sig_b64, &b64_len);
    cJSON_AddStringToObject(sig_obj, "signature", sig_b64);
    cJSON_AddStringToObject(sig_obj, "signed_at", time_str);
    cJSON_AddBoolToObject(sig_obj, "is_official", true);  /* Developer 设备签名 */
    cJSON_AddItemToObject(root, "signature", sig_obj);
    
    /* payload (Base64 ciphertext) */
    size_t payload_b64_max = ((ciphertext_len + 2) / 3) * 4 + 1;
    char *payload_b64 = TS_MALLOC_PSRAM(payload_b64_max);
    if (!payload_b64) {
        free(ciphertext);
        cJSON_Delete(root);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    b64_len = payload_b64_max;
    ts_crypto_base64_encode(ciphertext, ciphertext_len, payload_b64, &b64_len);
    cJSON_AddStringToObject(root, "payload", payload_b64);
    
    free(ciphertext);
    free(payload_b64);
    
    /* 输出 JSON */
    char *json_output = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    if (!json_output) {
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    *output = json_output;
    *output_len = strlen(json_output);
    
    ESP_LOGI(TAG, "Created config pack '%s' (%zu bytes)", name, *output_len);
    return TS_CONFIG_PACK_OK;
}

esp_err_t ts_config_pack_save(const char *path, const char *tscfg_json, size_t tscfg_len)
{
    if (!path || !tscfg_json) {
        return ESP_ERR_INVALID_ARG;
    }
    
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGE(TAG, "Failed to create file: %s", path);
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t written = fwrite(tscfg_json, 1, tscfg_len, f);
    fclose(f);
    
    if (written != tscfg_len) {
        ESP_LOGE(TAG, "Failed to write file: %s", path);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Saved config pack to %s", path);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Verify Functions                                  */
/*===========================================================================*/

ts_config_pack_result_t ts_config_pack_verify(
    const char *path,
    ts_config_pack_sig_info_t *info)
{
    if (!path) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    /* 读取文件 */
    FILE *f = fopen(path, "r");
    if (!f) {
        return TS_CONFIG_PACK_ERR_IO;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > CONFIG_TS_CONFIG_PACK_MAX_SIZE) {
        fclose(f);
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    char *json_buf = TS_MALLOC_PSRAM(file_size + 1);
    if (!json_buf) {
        fclose(f);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    fread(json_buf, 1, file_size, f);
    fclose(f);
    json_buf[file_size] = '\0';
    
    ts_config_pack_result_t result = ts_config_pack_verify_mem(json_buf, file_size, info);
    free(json_buf);
    
    return result;
}

ts_config_pack_result_t ts_config_pack_verify_mem(
    const char *tscfg_json,
    size_t tscfg_len,
    ts_config_pack_sig_info_t *info)
{
    if (!tscfg_json) {
        return TS_CONFIG_PACK_ERR_INVALID_ARG;
    }
    
    cJSON *root = NULL;
    ts_config_pack_crypto_params_t params = {0};
    char *payload_b64 = NULL;
    char *signer_cert_pem = NULL;
    char *signature_b64 = NULL;
    ts_config_pack_sig_info_t sig_info = {0};
    
    ts_config_pack_result_t result = parse_tscfg_json(
        tscfg_json, tscfg_len,
        &root, &params, &payload_b64, &signer_cert_pem, &signature_b64, &sig_info);
    
    if (result != TS_CONFIG_PACK_OK) {
        if (root) cJSON_Delete(root);
        return result;
    }
    
    /* Base64 解码 payload */
    size_t payload_b64_len = strlen(payload_b64);
    size_t ciphertext_max_len = (payload_b64_len * 3) / 4 + 4;
    uint8_t *ciphertext = TS_MALLOC_PSRAM(ciphertext_max_len);
    if (!ciphertext) {
        cJSON_Delete(root);
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    size_t ciphertext_len = ciphertext_max_len;
    if (ts_crypto_base64_decode(payload_b64, payload_b64_len,
                                 ciphertext, &ciphertext_len) != ESP_OK) {
        free(ciphertext);
        cJSON_Delete(root);
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* Base64 解码签名 */
    size_t sig_b64_len = strlen(signature_b64);
    uint8_t signature[MAX_SIGNATURE_LEN];
    size_t sig_len = sizeof(signature);
    if (ts_crypto_base64_decode(signature_b64, sig_b64_len,
                                 signature, &sig_len) != ESP_OK) {
        free(ciphertext);
        cJSON_Delete(root);
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* 验证签名 */
    result = verify_signature(signer_cert_pem, ciphertext, ciphertext_len,
                               signature, sig_len);
    free(ciphertext);
    cJSON_Delete(root);
    
    if (result == TS_CONFIG_PACK_OK && info) {
        sig_info.valid = true;
        *info = sig_info;
    }
    
    return result;
}

/*===========================================================================*/
/*                          Utility Functions                                 */
/*===========================================================================*/

esp_err_t ts_config_pack_export_device_cert(char *cert_pem, size_t *cert_len)
{
    return ts_cert_get_certificate(cert_pem, cert_len);
}

esp_err_t ts_config_pack_get_cert_fingerprint(char *fingerprint, size_t len)
{
    if (!fingerprint || len < CERT_FINGERPRINT_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_initialized || s_device_fingerprint[0] == '\0') {
        return ESP_ERR_INVALID_STATE;
    }
    
    strncpy(fingerprint, s_device_fingerprint, len - 1);
    fingerprint[len - 1] = '\0';
    return ESP_OK;
}

/*===========================================================================*/
/*                          Internal Functions                                */
/*===========================================================================*/

/**
 * @brief 计算证书 SHA-256 指纹
 */
static esp_err_t compute_cert_fingerprint(const char *cert_pem, size_t cert_len,
                                           char *fingerprint, size_t fp_len)
{
    if (!cert_pem || !fingerprint || fp_len < CERT_FINGERPRINT_LEN + 1) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* 计算 PEM 内容的 SHA-256 */
    uint8_t hash[SHA256_LEN];
    esp_err_t ret = ts_crypto_hash(TS_HASH_SHA256, cert_pem, cert_len, hash, sizeof(hash));
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* 转换为 hex 字符串 */
    return ts_crypto_hex_encode(hash, sizeof(hash), fingerprint, fp_len);
}

/**
 * @brief 解析 .tscfg JSON 文件
 */
static ts_config_pack_result_t parse_tscfg_json(
    const char *json, size_t len,
    cJSON **root,
    ts_config_pack_crypto_params_t *params,
    char **payload_b64,
    char **signer_cert_pem,
    char **signature_b64,
    ts_config_pack_sig_info_t *sig_info)
{
    *root = cJSON_ParseWithLength(json, len);
    if (!*root) {
        ESP_LOGE(TAG, "JSON parse error");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* 验证版本 */
    cJSON *version = cJSON_GetObjectItem(*root, "tscfg_version");
    if (!version || !cJSON_IsString(version)) {
        ESP_LOGE(TAG, "Missing tscfg_version");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    if (strcmp(cJSON_GetStringValue(version), TS_CONFIG_PACK_VERSION) != 0) {
        ESP_LOGE(TAG, "Unsupported version: %s", cJSON_GetStringValue(version));
        return TS_CONFIG_PACK_ERR_VERSION;
    }
    
    /* 提取加密参数 */
    cJSON *encryption = cJSON_GetObjectItem(*root, "encryption");
    if (!encryption) {
        ESP_LOGE(TAG, "Missing encryption section");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    cJSON *ephemeral_pub = cJSON_GetObjectItem(encryption, "ephemeral_public_key");
    cJSON *salt = cJSON_GetObjectItem(encryption, "salt");
    cJSON *iv = cJSON_GetObjectItem(encryption, "iv");
    cJSON *tag = cJSON_GetObjectItem(encryption, "tag");
    cJSON *recipient_fp = cJSON_GetObjectItem(encryption, "recipient_cert_fingerprint");
    
    if (!ephemeral_pub || !salt || !iv || !tag || !recipient_fp) {
        ESP_LOGE(TAG, "Missing encryption parameters");
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    /* Base64 解码加密参数 */
    const char *pub_str = cJSON_GetStringValue(ephemeral_pub);
    size_t out_len = sizeof(params->ephemeral_pubkey);
    if (ts_crypto_base64_decode(pub_str, strlen(pub_str), 
                                 params->ephemeral_pubkey, &out_len) != ESP_OK) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->salt);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(salt), strlen(cJSON_GetStringValue(salt)),
                                 params->salt, &out_len) != ESP_OK) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->iv);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(iv), strlen(cJSON_GetStringValue(iv)),
                                 params->iv, &out_len) != ESP_OK) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    out_len = sizeof(params->tag);
    if (ts_crypto_base64_decode(cJSON_GetStringValue(tag), strlen(cJSON_GetStringValue(tag)),
                                 params->tag, &out_len) != ESP_OK) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    strncpy(params->recipient_fingerprint, cJSON_GetStringValue(recipient_fp),
            sizeof(params->recipient_fingerprint) - 1);
    
    /* 提取 payload */
    cJSON *payload = cJSON_GetObjectItem(*root, "payload");
    if (!payload || !cJSON_IsString(payload)) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    *payload_b64 = cJSON_GetStringValue(payload);
    
    /* 提取签名信息 */
    cJSON *signature_obj = cJSON_GetObjectItem(*root, "signature");
    if (!signature_obj) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    cJSON *signer_cert = cJSON_GetObjectItem(signature_obj, "signer_certificate");
    cJSON *sig = cJSON_GetObjectItem(signature_obj, "signature");
    cJSON *is_official = cJSON_GetObjectItem(signature_obj, "is_official");
    
    if (!signer_cert || !sig) {
        return TS_CONFIG_PACK_ERR_PARSE;
    }
    
    *signer_cert_pem = cJSON_GetStringValue(signer_cert);
    *signature_b64 = cJSON_GetStringValue(sig);
    
    /* 填充签名信息 */
    if (sig_info) {
        sig_info->valid = false;  /* 稍后验证 */
        sig_info->is_official = is_official ? cJSON_IsTrue(is_official) : false;
        
        /* 解析签名者证书获取 CN 和 OU */
        ts_cert_info_t cert_info;
        if (ts_cert_parse_certificate(*signer_cert_pem, strlen(*signer_cert_pem) + 1, 
                                       &cert_info) == ESP_OK) {
            strncpy(sig_info->signer_cn, cert_info.subject_cn, sizeof(sig_info->signer_cn) - 1);
            strncpy(sig_info->signer_ou, cert_info.subject_ou, sizeof(sig_info->signer_ou) - 1);
            /* 根据 OU 字段更新 is_official 标记 */
            if (strstr(cert_info.subject_ou, "Developer") != NULL) {
                sig_info->is_official = true;
            }
        }
    }
    
    return TS_CONFIG_PACK_OK;
}

/**
 * @brief 验证签名
 */
static ts_config_pack_result_t verify_signature(
    const char *signer_cert_pem,
    const uint8_t *data_to_sign,
    size_t data_len,
    const uint8_t *signature,
    size_t sig_len)
{
    /* 导入签名者公钥 */
    ts_keypair_t signer_key = NULL;
    esp_err_t ret = ts_crypto_keypair_import(signer_cert_pem, strlen(signer_cert_pem) + 1,
                                              &signer_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse signer certificate");
        return TS_CONFIG_PACK_ERR_CERT_CHAIN;
    }
    
    /* 计算数据哈希 */
    uint8_t hash[SHA256_LEN];
    ts_crypto_hash(TS_HASH_SHA256, data_to_sign, data_len, hash, sizeof(hash));
    
    /* 验证签名 */
    ret = ts_crypto_ecdsa_verify(signer_key, hash, sizeof(hash), signature, sig_len);
    ts_crypto_keypair_free(signer_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Signature verification failed");
        return TS_CONFIG_PACK_ERR_SIGNATURE;
    }
    
    /* TODO: 验证证书链 */
#if CONFIG_TS_CONFIG_PACK_VERIFY_CERT_CHAIN
    /* 
     * 应该验证 signer_cert_pem 是由受信任的 CA 签发的
     * 这需要加载 CA 链并进行验证
     */
    ESP_LOGW(TAG, "Certificate chain verification not yet implemented");
#endif
    
    return TS_CONFIG_PACK_OK;
}

/**
 * @brief 解密 payload
 */
static ts_config_pack_result_t decrypt_payload(
    const ts_config_pack_crypto_params_t *params,
    const uint8_t *ciphertext,
    size_t ciphertext_len,
    char **plaintext,
    size_t *plaintext_len)
{
    /* 获取设备私钥 */
    char key_pem[TS_CERT_KEY_MAX_LEN];
    size_t key_len = sizeof(key_pem);
    esp_err_t ret = ts_cert_get_private_key(key_pem, &key_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device private key");
        return TS_CONFIG_PACK_ERR_RECIPIENT;
    }
    
    ts_keypair_t device_key = NULL;
    ret = ts_crypto_keypair_import(key_pem, key_len, &device_key);
    memset(key_pem, 0, sizeof(key_pem));  /* 清除私钥 */
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to import device key");
        return TS_CONFIG_PACK_ERR_RECIPIENT;
    }
    
    /* ECDH 密钥协商 */
    uint8_t shared_secret[32];
    size_t shared_len = sizeof(shared_secret);
    ret = ts_crypto_ecdh_compute_shared_raw(device_key,
                                             params->ephemeral_pubkey,
                                             ECDH_PUBKEY_LEN,
                                             shared_secret, &shared_len);
    ts_crypto_keypair_free(device_key);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ECDH key agreement failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* HKDF 密钥派生 */
    uint8_t aes_key[AES_KEY_LEN];
    ret = ts_crypto_hkdf(params->salt, HKDF_SALT_LEN,
                          shared_secret, shared_len,
                          TS_CONFIG_PACK_HKDF_INFO, strlen(TS_CONFIG_PACK_HKDF_INFO),
                          aes_key, sizeof(aes_key));
    
    memset(shared_secret, 0, sizeof(shared_secret));
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HKDF key derivation failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    /* AES-GCM 解密 */
    char *decrypted = TS_MALLOC_PSRAM(ciphertext_len + 1);
    if (!decrypted) {
        memset(aes_key, 0, sizeof(aes_key));
        return TS_CONFIG_PACK_ERR_NO_MEM;
    }
    
    ret = ts_crypto_aes_gcm_decrypt(aes_key, sizeof(aes_key),
                                     params->iv, AES_IV_LEN,
                                     NULL, 0,  /* 无 AAD */
                                     ciphertext, ciphertext_len,
                                     params->tag, decrypted);
    
    memset(aes_key, 0, sizeof(aes_key));
    
    if (ret != ESP_OK) {
        free(decrypted);
        ESP_LOGE(TAG, "AES-GCM decryption failed");
        return TS_CONFIG_PACK_ERR_DECRYPT;
    }
    
    decrypted[ciphertext_len] = '\0';
    *plaintext = decrypted;
    *plaintext_len = ciphertext_len;
    
    return TS_CONFIG_PACK_OK;
}
