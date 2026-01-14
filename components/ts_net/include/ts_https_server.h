/**
 * @file ts_https_server.h
 * @brief HTTPS Server with TLS and mTLS support
 */

#pragma once

#include "esp_err.h"
#include "esp_https_server.h"
#include "ts_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** TLS authentication mode */
typedef enum {
    TS_TLS_AUTH_NONE = 0,       /**< No client authentication (standard HTTPS) */
    TS_TLS_AUTH_OPTIONAL,       /**< Optional client certificate */
    TS_TLS_AUTH_REQUIRED        /**< Required client certificate (mTLS) */
} ts_tls_auth_mode_t;

/** HTTPS server configuration */
typedef struct {
    uint16_t port;                      /**< HTTPS port (default: 443) */
    const uint8_t *server_cert;         /**< Server certificate (PEM format) */
    size_t server_cert_len;             /**< Server certificate length */
    const uint8_t *server_key;          /**< Server private key (PEM format) */
    size_t server_key_len;              /**< Server private key length */
    const uint8_t *ca_cert;             /**< CA certificate for client verification (mTLS) */
    size_t ca_cert_len;                 /**< CA certificate length */
    ts_tls_auth_mode_t auth_mode;       /**< Client authentication mode */
    size_t max_connections;             /**< Maximum concurrent connections */
} ts_https_config_t;

/** Default HTTPS configuration */
#define TS_HTTPS_DEFAULT_CONFIG() { \
    .port = 443, \
    .server_cert = NULL, \
    .server_cert_len = 0, \
    .server_key = NULL, \
    .server_key_len = 0, \
    .ca_cert = NULL, \
    .ca_cert_len = 0, \
    .auth_mode = TS_TLS_AUTH_NONE, \
    .max_connections = 4 \
}

/**
 * @brief Initialize HTTPS server
 * 
 * @param config HTTPS configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_init(const ts_https_config_t *config);

/**
 * @brief Deinitialize HTTPS server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_deinit(void);

/**
 * @brief Start HTTPS server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_start(void);

/**
 * @brief Stop HTTPS server
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_stop(void);

/**
 * @brief Check if HTTPS server is running
 * 
 * @return true if running
 */
bool ts_https_server_is_running(void);

/**
 * @brief Register route on HTTPS server
 * 
 * @param route Route configuration
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_register_route(const ts_http_route_t *route);

/**
 * @brief Unregister route from HTTPS server
 * 
 * @param uri URI path
 * @param method HTTP method
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_server_unregister_route(const char *uri, ts_http_method_t method);

/**
 * @brief Load certificates from files
 * 
 * @param config Configuration to update
 * @param cert_path Server certificate file path
 * @param key_path Server private key file path
 * @param ca_path CA certificate file path (optional, for mTLS)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_load_certs_from_files(ts_https_config_t *config,
                                          const char *cert_path,
                                          const char *key_path,
                                          const char *ca_path);

/**
 * @brief Free loaded certificates
 * 
 * @param config Configuration with certificates to free
 */
void ts_https_free_certs(ts_https_config_t *config);

/**
 * @brief Generate self-signed certificate
 * 
 * @param common_name Common name for certificate
 * @param validity_days Certificate validity in days
 * @param cert_out Output buffer for certificate (PEM)
 * @param cert_size Certificate buffer size
 * @param key_out Output buffer for private key (PEM)
 * @param key_size Private key buffer size
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_generate_self_signed(const char *common_name,
                                         int validity_days,
                                         uint8_t *cert_out, size_t cert_size,
                                         uint8_t *key_out, size_t key_size);

/**
 * @brief Get client certificate info (for mTLS)
 * 
 * @param req HTTP request
 * @param cn_out Common name output buffer
 * @param cn_size Common name buffer size
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ts_https_get_client_cert_cn(ts_http_request_t *req, 
                                       char *cn_out, size_t cn_size);

#ifdef __cplusplus
}
#endif
