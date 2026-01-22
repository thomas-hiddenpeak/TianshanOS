/**
 * @file ts_ota.h
 * @brief TianShanOS OTA (Over-The-Air) Update System
 *
 * This module provides firmware update functionality via:
 * - HTTPS download from remote server
 * - SD card local file
 * - WebUI upload
 *
 * Features:
 * - Dual OTA partition support (A/B update)
 * - Automatic rollback on boot failure
 * - Progress callback for UI updates
 * - Version checking
 * - Optional signature verification
 *
 * @author TianShanOS Team
 * @version 1.0.0
 */

#ifndef TS_OTA_H
#define TS_OTA_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_partition.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA update source type
 */
typedef enum {
    TS_OTA_SOURCE_HTTPS,    ///< Download from HTTPS URL
    TS_OTA_SOURCE_SDCARD,   ///< Load from SD card file
    TS_OTA_SOURCE_UPLOAD,   ///< Upload via WebUI/API
} ts_ota_source_t;

/**
 * @brief OTA update state
 */
typedef enum {
    TS_OTA_STATE_IDLE,              ///< No OTA in progress
    TS_OTA_STATE_CHECKING,          ///< Checking for updates
    TS_OTA_STATE_DOWNLOADING,       ///< Downloading firmware
    TS_OTA_STATE_VERIFYING,         ///< Verifying firmware
    TS_OTA_STATE_WRITING,           ///< Writing to flash
    TS_OTA_STATE_PENDING_REBOOT,    ///< Ready, waiting for reboot
    TS_OTA_STATE_ERROR,             ///< Error occurred
} ts_ota_state_t;

/**
 * @brief OTA error codes
 */
typedef enum {
    TS_OTA_ERR_NONE = 0,
    TS_OTA_ERR_INVALID_PARAM,
    TS_OTA_ERR_NO_PARTITION,
    TS_OTA_ERR_PARTITION_FULL,
    TS_OTA_ERR_CONNECTION_FAILED,
    TS_OTA_ERR_DOWNLOAD_FAILED,
    TS_OTA_ERR_VERIFY_FAILED,
    TS_OTA_ERR_WRITE_FAILED,
    TS_OTA_ERR_VERSION_MISMATCH,
    TS_OTA_ERR_SIGNATURE_INVALID,
    TS_OTA_ERR_FILE_NOT_FOUND,
    TS_OTA_ERR_ALREADY_RUNNING,
    TS_OTA_ERR_ABORTED,
    TS_OTA_ERR_TIMEOUT,
    TS_OTA_ERR_INTERNAL,
} ts_ota_error_t;

/**
 * @brief OTA progress information
 */
typedef struct {
    ts_ota_state_t state;       ///< Current state
    ts_ota_error_t error;       ///< Error code (if state == ERROR)
    size_t total_size;          ///< Total firmware size (0 if unknown)
    size_t received_size;       ///< Bytes received so far
    uint8_t progress_percent;   ///< Progress percentage (0-100)
    const char *status_msg;     ///< Human-readable status message
} ts_ota_progress_t;

/**
 * @brief OTA progress callback function type
 *
 * @param progress Progress information
 * @param user_data User-provided context
 */
typedef void (*ts_ota_progress_cb_t)(const ts_ota_progress_t *progress, void *user_data);

/**
 * @brief OTA update configuration
 */
typedef struct {
    ts_ota_source_t source;         ///< Update source type
    const char *url;                ///< HTTPS URL or file path
    const char *cert_pem;           ///< Server certificate (NULL for default)
    bool skip_cert_verify;          ///< Skip certificate verification (debug only)
    bool auto_reboot;               ///< Automatically reboot after update
    bool allow_downgrade;           ///< Allow downgrade to older version
    ts_ota_progress_cb_t progress_cb; ///< Progress callback (optional)
    void *user_data;                ///< User data for callback
} ts_ota_config_t;

/**
 * @brief Firmware version information
 */
typedef struct {
    char version[32];           ///< Version string (e.g., "1.0.0")
    char project_name[32];      ///< Project name
    char compile_time[32];      ///< Compile timestamp
    char compile_date[16];      ///< Compile date
    char idf_version[16];       ///< ESP-IDF version
    uint32_t secure_version;    ///< Secure version counter (anti-rollback)
} ts_ota_version_info_t;

/**
 * @brief Partition information
 */
typedef struct {
    char label[17];             ///< Partition label
    uint32_t address;           ///< Start address
    uint32_t size;              ///< Size in bytes
    bool is_running;            ///< Currently running from this partition
    bool is_bootable;           ///< Partition contains valid app
    ts_ota_version_info_t version; ///< Firmware version (if bootable)
} ts_ota_partition_info_t;

/**
 * @brief OTA status information
 */
typedef struct {
    ts_ota_state_t state;
    ts_ota_partition_info_t running;    ///< Currently running partition
    ts_ota_partition_info_t next;       ///< Next update partition
    bool pending_verify;                ///< New firmware pending verification
    uint32_t rollback_timeout;          ///< Seconds until auto-rollback
    uint32_t last_update_time;          ///< Timestamp of last successful update
} ts_ota_status_t;

// ============================================================================
//                              Core API
// ============================================================================

/**
 * @brief Initialize OTA subsystem
 *
 * Must be called before any other OTA functions.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_init(void);

/**
 * @brief Deinitialize OTA subsystem
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_deinit(void);

/**
 * @brief Start OTA update
 *
 * This function starts the OTA update process asynchronously.
 * Progress can be monitored via the callback function.
 *
 * @param config Update configuration
 * @return ESP_OK if update started successfully
 */
esp_err_t ts_ota_start(const ts_ota_config_t *config);

/**
 * @brief Abort ongoing OTA update
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_abort(void);

/**
 * @brief Get current OTA status
 *
 * @param status Output status structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_status(ts_ota_status_t *status);

/**
 * @brief Get current OTA progress
 *
 * @param progress Output progress structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_progress(ts_ota_progress_t *progress);

// ============================================================================
//                           Rollback API
// ============================================================================

/**
 * @brief Mark current firmware as valid
 *
 * Call this after successful boot to prevent automatic rollback.
 * Should be called after system verification (e.g., network connectivity).
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_mark_valid(void);

/**
 * @brief Check if current firmware is pending verification
 *
 * @return true if pending verification, false otherwise
 */
bool ts_ota_is_pending_verify(void);

/**
 * @brief Rollback to previous firmware
 *
 * This will set the boot partition to the previous firmware
 * and reboot the system.
 *
 * @return ESP_OK on success (will not return on success as system reboots)
 */
esp_err_t ts_ota_rollback(void);

/**
 * @brief Get rollback timeout remaining
 *
 * @return Seconds remaining until auto-rollback, 0 if not pending
 */
uint32_t ts_ota_get_rollback_timeout(void);

// ============================================================================
//                           Version API
// ============================================================================

/**
 * @brief Get running firmware version info
 *
 * @param info Output version info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_running_version(ts_ota_version_info_t *info);

/**
 * @brief Compare two version strings
 *
 * @param v1 First version string
 * @param v2 Second version string
 * @return -1 if v1 < v2, 0 if v1 == v2, 1 if v1 > v2
 */
int ts_ota_compare_versions(const char *v1, const char *v2);

/**
 * @brief Check if update is available from URL
 *
 * @param url URL to check for updates
 * @param available Output: true if newer version available
 * @param new_version Output: new version string (if available)
 * @param new_version_len Length of new_version buffer
 * @return ESP_OK on success
 */
esp_err_t ts_ota_check_update(const char *url, bool *available,
                               char *new_version, size_t new_version_len);

// ============================================================================
//                           Partition API
// ============================================================================

/**
 * @brief Get running partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_running_partition_info(ts_ota_partition_info_t *info);

/**
 * @brief Get next update partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_next_partition_info(ts_ota_partition_info_t *info);

/**
 * @brief Get boot partition information
 *
 * @param info Output partition info structure
 * @return ESP_OK on success
 */
esp_err_t ts_ota_get_boot_partition_info(ts_ota_partition_info_t *info);

// ============================================================================
//                           Upload API (for WebUI)
// ============================================================================

/**
 * @brief Begin firmware upload
 *
 * Call this before writing firmware data chunks.
 *
 * @param total_size Total firmware size (0 if unknown)
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_begin(size_t total_size);

/**
 * @brief Write firmware data chunk
 *
 * @param data Data buffer
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_write(const void *data, size_t len);

/**
 * @brief Finish firmware upload
 *
 * Finalizes the update and optionally reboots.
 *
 * @param auto_reboot Reboot after completion
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_end(bool auto_reboot);

/**
 * @brief Abort firmware upload
 *
 * @return ESP_OK on success
 */
esp_err_t ts_ota_upload_abort(void);

// ============================================================================
//                           Event Definitions
// ============================================================================

/**
 * @brief OTA event base - use ts_event.h definition for consistency
 */
// Note: TS_EVENT_BASE_OTA is defined in ts_event.h as "ts_ota"

/**
 * @brief OTA event IDs
 */
typedef enum {
    TS_EVENT_OTA_STARTED,           ///< OTA update started
    TS_EVENT_OTA_PROGRESS,          ///< Progress update
    TS_EVENT_OTA_COMPLETED,         ///< Update completed successfully
    TS_EVENT_OTA_FAILED,            ///< Update failed
    TS_EVENT_OTA_ABORTED,           ///< Update aborted by user
    TS_EVENT_OTA_PENDING_REBOOT,    ///< Waiting for reboot
    TS_EVENT_OTA_ROLLBACK_PENDING,  ///< Rollback is pending
    TS_EVENT_OTA_ROLLBACK_EXECUTED, ///< Rollback was executed
    TS_EVENT_OTA_VALIDATED,         ///< Firmware validated
} ts_event_ota_id_t;

#ifdef __cplusplus
}
#endif

#endif // TS_OTA_H
