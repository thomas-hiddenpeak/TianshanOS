/**
 * @file ts_drivers.c
 * @brief TianShanOS Device Drivers - Main Init
 */

#include "ts_drivers.h"
#include "ts_fan.h"
#include "ts_power.h"
#include "ts_device_ctrl.h"
#include "ts_usb_mux.h"
#include "ts_log.h"
#include "sdkconfig.h"

#define TAG "ts_drivers"

// Default fan GPIO pin (from pins.json: FAN_PWM_0=41)
#ifndef CONFIG_TS_DRIVERS_FAN0_PWM_GPIO
#define CONFIG_TS_DRIVERS_FAN0_PWM_GPIO 41
#endif

esp_err_t ts_drivers_init(void)
{
    esp_err_t ret;
    
    TS_LOGI(TAG, "Initializing device drivers");
    
#ifdef CONFIG_TS_DRIVERS_FAN_ENABLE
    ret = ts_fan_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Fan driver init failed: %s", esp_err_to_name(ret));
    } else {
        // Configure fan 0 only (only one fan GPIO on this board)
        ts_fan_config_t fan0_cfg = {
            .gpio_pwm = CONFIG_TS_DRIVERS_FAN0_PWM_GPIO,
            .gpio_tach = -1,  // No tach for now
            .min_duty = 20,
            .max_duty = 100,
            .curve_points = 0
        };
        ret = ts_fan_configure(TS_FAN_1, &fan0_cfg);
        if (ret != ESP_OK) {
            TS_LOGW(TAG, "Fan 0 configure failed: %s", esp_err_to_name(ret));
        } else {
            TS_LOGI(TAG, "Fan 0 configured on GPIO %d", fan0_cfg.gpio_pwm);
        }
    }
#endif

#ifdef CONFIG_TS_DRIVERS_POWER_ENABLE
    ret = ts_power_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Power monitor init failed: %s", esp_err_to_name(ret));
    }
#endif

#ifdef CONFIG_TS_DRIVERS_DEVICE_ENABLE
    ret = ts_device_ctrl_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "Device control init failed: %s", esp_err_to_name(ret));
    }
#endif

#ifdef CONFIG_TS_DRIVERS_USB_MUX_ENABLE
    ret = ts_usb_mux_init();
    if (ret != ESP_OK) {
        TS_LOGW(TAG, "USB MUX init failed: %s", esp_err_to_name(ret));
    }
#endif

    TS_LOGI(TAG, "Device drivers initialized");
    return ESP_OK;
}

esp_err_t ts_drivers_deinit(void)
{
#ifdef CONFIG_TS_DRIVERS_USB_MUX_ENABLE
    ts_usb_mux_deinit();
#endif
#ifdef CONFIG_TS_DRIVERS_DEVICE_ENABLE
    ts_device_ctrl_deinit();
#endif
#ifdef CONFIG_TS_DRIVERS_POWER_ENABLE
    ts_power_deinit();
#endif
#ifdef CONFIG_TS_DRIVERS_FAN_ENABLE
    ts_fan_deinit();
#endif
    return ESP_OK;
}
