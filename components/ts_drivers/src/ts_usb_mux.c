/**
 * @file ts_usb_mux.c
 * @brief USB MUX Control Implementation
 */

#include "ts_usb_mux.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "ts_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_usb_mux"

typedef struct {
    bool configured;
    ts_usb_mux_config_t config;
    ts_gpio_handle_t gpio_sel;
    ts_gpio_handle_t gpio_oe;
    ts_usb_target_t current_target;
    bool enabled;
} usb_mux_instance_t;

static usb_mux_instance_t s_muxes[TS_USB_MUX_MAX];
static bool s_initialized = false;

esp_err_t ts_usb_mux_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_muxes, 0, sizeof(s_muxes));
    
    // Initialize with invalid pins
    for (int i = 0; i < TS_USB_MUX_MAX; i++) {
        s_muxes[i].config.gpio_sel = -1;
        s_muxes[i].config.gpio_oe = -1;
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "USB MUX driver initialized");
    return ESP_OK;
}

esp_err_t ts_usb_mux_deinit(void)
{
    for (int i = 0; i < TS_USB_MUX_MAX; i++) {
        if (s_muxes[i].gpio_sel) {
            ts_gpio_destroy(s_muxes[i].gpio_sel);
            s_muxes[i].gpio_sel = NULL;
        }
        if (s_muxes[i].gpio_oe) {
            ts_gpio_destroy(s_muxes[i].gpio_oe);
            s_muxes[i].gpio_oe = NULL;
        }
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_usb_mux_configure(ts_usb_mux_id_t mux, const ts_usb_mux_config_t *config)
{
    if (mux >= TS_USB_MUX_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    m->config = *config;
    
    // Configure select pin
    if (config->gpio_sel >= 0) {
        m->gpio_sel = ts_gpio_create_raw(config->gpio_sel, "usb_sel");
        if (m->gpio_sel) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = config->sel_active_low ? 1 : 0
            };
            ts_gpio_configure(m->gpio_sel, &cfg);
        }
    }
    
    // Configure OE pin
    if (config->gpio_oe >= 0) {
        m->gpio_oe = ts_gpio_create_raw(config->gpio_oe, "usb_oe");
        if (m->gpio_oe) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_OUTPUT,
                .pull_mode = TS_GPIO_PULL_NONE,
                .intr_type = TS_GPIO_INTR_DISABLE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = config->oe_active_low ? 1 : 0  // Disabled initially
            };
            ts_gpio_configure(m->gpio_oe, &cfg);
        }
    }
    
    m->configured = true;
    m->current_target = TS_USB_TARGET_DISCONNECT;
    m->enabled = false;
    
    TS_LOGI(TAG, "USB MUX %d configured: SEL=%d, OE=%d", mux, config->gpio_sel, config->gpio_oe);
    return ESP_OK;
}

esp_err_t ts_usb_mux_set_target(ts_usb_mux_id_t mux, ts_usb_target_t target)
{
    if (mux >= TS_USB_MUX_MAX) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    if (!m->configured) return ESP_ERR_INVALID_STATE;
    
    int sel_level = 0;
    
    switch (target) {
        case TS_USB_TARGET_HOST:
            sel_level = m->config.sel_active_low ? 1 : 0;
            break;
        case TS_USB_TARGET_DEVICE:
            sel_level = m->config.sel_active_low ? 0 : 1;
            break;
        case TS_USB_TARGET_DISCONNECT:
            // Just disable the MUX
            return ts_usb_mux_enable(mux, false);
    }
    
    if (m->gpio_sel) {
        ts_gpio_set_level(m->gpio_sel, sel_level);
    }
    
    m->current_target = target;
    
    TS_LOGI(TAG, "USB MUX %d target: %s", mux, 
            target == TS_USB_TARGET_HOST ? "HOST" : "DEVICE");
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_get_status(ts_usb_mux_id_t mux, ts_usb_mux_status_t *status)
{
    if (mux >= TS_USB_MUX_MAX || !status) return ESP_ERR_INVALID_ARG;
    if (!s_muxes[mux].configured) return ESP_ERR_INVALID_STATE;
    
    status->target = s_muxes[mux].current_target;
    status->enabled = s_muxes[mux].enabled;
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_enable(ts_usb_mux_id_t mux, bool enable)
{
    if (mux >= TS_USB_MUX_MAX) return ESP_ERR_INVALID_ARG;
    
    usb_mux_instance_t *m = &s_muxes[mux];
    if (!m->configured) return ESP_ERR_INVALID_STATE;
    
    if (m->gpio_oe) {
        int oe_level = enable ? (m->config.oe_active_low ? 0 : 1) 
                              : (m->config.oe_active_low ? 1 : 0);
        ts_gpio_set_level(m->gpio_oe, oe_level);
    }
    
    m->enabled = enable;
    
    if (!enable) {
        m->current_target = TS_USB_TARGET_DISCONNECT;
    }
    
    return ESP_OK;
}

esp_err_t ts_usb_mux_switch_to_agx(ts_usb_mux_id_t mux)
{
    esp_err_t ret = ts_usb_mux_set_target(mux, TS_USB_TARGET_DEVICE);
    if (ret == ESP_OK) {
        ret = ts_usb_mux_enable(mux, true);
    }
    return ret;
}

esp_err_t ts_usb_mux_switch_to_host(ts_usb_mux_id_t mux, uint32_t timeout_ms)
{
    (void)timeout_ms;  // Not used in basic implementation
    esp_err_t ret = ts_usb_mux_set_target(mux, TS_USB_TARGET_HOST);
    if (ret == ESP_OK) {
        ret = ts_usb_mux_enable(mux, true);
    }
    return ret;
}

esp_err_t ts_usb_mux_switch_to_device(ts_usb_mux_id_t mux)
{
    return ts_usb_mux_switch_to_agx(mux);
}
