/**
 * @file ts_usb_mux.h
 * @brief USB MUX Control Driver
 * 
 * 支持 3 目标切换：ESP32 / AGX / LPMU
 * 使用 2 个 GPIO 控制 4 路选择（实际使用 3 路）
 * 
 * Truth Table:
 *   MUX1=0, MUX2=0 -> ESP32 (default)
 *   MUX1=1, MUX2=0 -> AGX
 *   MUX1=1, MUX2=1 -> LPMU
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** USB MUX target */
typedef enum {
    TS_USB_MUX_ESP32 = 0,       /**< Route to ESP32 (default) */
    TS_USB_MUX_AGX,             /**< Route to AGX */
    TS_USB_MUX_LPMU,            /**< Route to LPMU */
    TS_USB_MUX_DISCONNECT,      /**< Disconnect all */
} ts_usb_mux_target_t;

/** USB MUX pins configuration */
typedef struct {
    int gpio_sel0;              /**< MUX select pin 0 */
    int gpio_sel1;              /**< MUX select pin 1 */
} ts_usb_mux_pins_t;

/**
 * @brief Initialize USB MUX subsystem
 * @return ESP_OK on success
 */
esp_err_t ts_usb_mux_init(void);

/**
 * @brief Deinitialize USB MUX subsystem
 * @return ESP_OK on success
 */
esp_err_t ts_usb_mux_deinit(void);

/**
 * @brief Configure USB MUX pins
 * @param pins Pin configuration
 * @return ESP_OK on success
 */
esp_err_t ts_usb_mux_configure(const ts_usb_mux_pins_t *pins);

/**
 * @brief Set USB MUX target
 * @param target Target device
 * @return ESP_OK on success
 */
esp_err_t ts_usb_mux_set_target(ts_usb_mux_target_t target);

/**
 * @brief Get current USB MUX target
 * @return Current target
 */
ts_usb_mux_target_t ts_usb_mux_get_target(void);

/**
 * @brief Check if USB MUX is configured
 * @return true if configured
 */
bool ts_usb_mux_is_configured(void);

#ifdef __cplusplus
}
#endif
