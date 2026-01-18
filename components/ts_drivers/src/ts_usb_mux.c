/**
 * @file ts_usb_mux.c
 * @brief USB MUX Control Implementation
 * 
 * 3 目标切换：ESP32 / AGX / LPMU
 * 使用 2 个 GPIO 控制选择
 * 
 * Truth Table:
 *   SEL0=0, SEL1=0 -> ESP32 (default)
 *   SEL0=1, SEL1=0 -> AGX
 *   SEL0=1, SEL1=1 -> LPMU
 */

#include "ts_usb_mux.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include <string.h>

#define TAG "ts_usb_mux"

static struct {
    bool configured;
    bool initialized;
    ts_usb_mux_pins_t pins;
    ts_gpio_handle_t gpio_sel0;
    ts_gpio_handle_t gpio_sel1;
    ts_usb_mux_target_t current_target;
} s_mux = {0};

esp_err_t ts_usb_mux_init(void)
{
    if (s_mux.initialized) return ESP_OK;
    
    memset(&s_mux, 0, sizeof(s_mux));
    s_mux.pins.gpio_sel0 = -1;
    s_mux.pins.gpio_sel1 = -1;
    s_mux.current_target = TS_USB_MUX_ESP32;
    
    s_mux.initialized = true;
    TS_LOGI(TAG, "USB MUX driver initialized");
    return ESP_OK;
}

esp_err_t ts_usb_mux_deinit(void)
{
    if (s_mux.gpio_sel0) {
        ts_gpio_destroy(s_mux.gpio_sel0);
        s_mux.gpio_sel0 = NULL;
    }
    if (s_mux.gpio_sel1) {
        ts_gpio_destroy(s_mux.gpio_sel1);
        s_mux.gpio_sel1 = NULL;
    }
    
    s_mux.configured = false;
    s_mux.initialized = false;
    return ESP_OK;
}

esp_err_t ts_usb_mux_configure(const ts_usb_mux_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_mux.initialized) return ESP_ERR_INVALID_STATE;
    
    s_mux.pins = *pins;
    
    // Configure SEL0 pin
    if (pins->gpio_sel0 >= 0) {
        s_mux.gpio_sel0 = ts_gpio_create_raw(pins->gpio_sel0, "usb_sel0");
        if (s_mux.gpio_sel0) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = 0  // Default to ESP32 (LOW)
            };
            ts_gpio_configure(s_mux.gpio_sel0, &cfg);
        }
    }
    
    // Configure SEL1 pin
    if (pins->gpio_sel1 >= 0) {
        s_mux.gpio_sel1 = ts_gpio_create_raw(pins->gpio_sel1, "usb_sel1");
        if (s_mux.gpio_sel1) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = 0  // Default to ESP32 (LOW)
            };
            ts_gpio_configure(s_mux.gpio_sel1, &cfg);
        }
    }
    
    s_mux.configured = true;
    s_mux.current_target = TS_USB_MUX_ESP32;
    
    TS_LOGI(TAG, "USB MUX configured (sel0=%d, sel1=%d)",
            pins->gpio_sel0, pins->gpio_sel1);
    return ESP_OK;
}

esp_err_t ts_usb_mux_set_target(ts_usb_mux_target_t target)
{
    if (!s_mux.configured) {
        TS_LOGW(TAG, "USB MUX not configured");
        return ESP_ERR_INVALID_STATE;
    }
    
    int sel0 = 0, sel1 = 0;
    const char *target_str;
    
    switch (target) {
        case TS_USB_MUX_ESP32:
            // SEL0=0, SEL1=0
            sel0 = 0;
            sel1 = 0;
            target_str = "ESP32";
            break;
        case TS_USB_MUX_AGX:
            // SEL0=1, SEL1=0
            sel0 = 1;
            sel1 = 0;
            target_str = "AGX";
            break;
        case TS_USB_MUX_LPMU:
            // SEL0=1, SEL1=1
            sel0 = 1;
            sel1 = 1;
            target_str = "LPMU";
            break;
        case TS_USB_MUX_DISCONNECT:
            // SEL0=0, SEL1=1 (unused state, acts as disconnect)
            sel0 = 0;
            sel1 = 1;
            target_str = "DISCONNECT";
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    // Set GPIO levels
    if (s_mux.gpio_sel0) {
        ts_gpio_set_level(s_mux.gpio_sel0, sel0);
    }
    if (s_mux.gpio_sel1) {
        ts_gpio_set_level(s_mux.gpio_sel1, sel1);
    }
    
    s_mux.current_target = target;
    
    TS_LOGI(TAG, "USB MUX -> %s (sel0=%d, sel1=%d)", target_str, sel0, sel1);
    return ESP_OK;
}

ts_usb_mux_target_t ts_usb_mux_get_target(void)
{
    return s_mux.current_target;
}

bool ts_usb_mux_is_configured(void)
{
    return s_mux.configured;
}
