/**
 * @file ts_device_ctrl.c
 * @brief Device Power Control (AGX/LPMU) Implementation
 */

#include "ts_device_ctrl.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_device"

typedef struct {
    ts_gpio_handle_t power_en;
    ts_gpio_handle_t reset;
    ts_gpio_handle_t force_off;
    ts_gpio_handle_t sys_rst;
    ts_gpio_handle_t power_good;
    ts_gpio_handle_t carrier_pwr_on;
    ts_gpio_handle_t shutdown_req;
    ts_gpio_handle_t sleep_wake;
} device_gpio_handles_t;

typedef struct {
    bool configured;
    ts_agx_pins_t pins;
    device_gpio_handles_t gpio;
    ts_device_state_t state;
    uint32_t power_on_time;
    uint32_t boot_count;
    int32_t last_error;
} device_instance_t;

static device_instance_t s_devices[TS_DEVICE_MAX];
static bool s_initialized = false;

static void shutdown_req_callback(ts_gpio_handle_t handle, void *arg)
{
    ts_device_id_t *dev = (ts_device_id_t *)arg;
    // Note: Event would be posted here in full implementation
    (void)dev;
}

static ts_gpio_handle_t create_output_gpio(int gpio_num, const char *name)
{
    if (gpio_num < 0) return NULL;
    
    ts_gpio_handle_t handle = ts_gpio_create_raw(gpio_num, name);
    if (!handle) return NULL;
    
    ts_gpio_config_t cfg = {
        .direction = TS_GPIO_DIR_OUTPUT,
        .pull_mode = TS_GPIO_PULL_NONE,
        .intr_type = TS_GPIO_INTR_DISABLE,
        .drive = TS_GPIO_DRIVE_2,
        .invert = false,
        .initial_level = 0
    };
    ts_gpio_configure(handle, &cfg);
    return handle;
}

static ts_gpio_handle_t create_input_gpio(int gpio_num, const char *name, bool with_pullup)
{
    if (gpio_num < 0) return NULL;
    
    ts_gpio_handle_t handle = ts_gpio_create_raw(gpio_num, name);
    if (!handle) return NULL;
    
    ts_gpio_config_t cfg = {
        .direction = TS_GPIO_DIR_INPUT,
        .pull_mode = with_pullup ? TS_GPIO_PULL_UP : TS_GPIO_PULL_NONE,
        .intr_type = TS_GPIO_INTR_DISABLE,
        .drive = TS_GPIO_DRIVE_2,
        .invert = false,
        .initial_level = -1
    };
    ts_gpio_configure(handle, &cfg);
    return handle;
}

esp_err_t ts_device_ctrl_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_devices, 0, sizeof(s_devices));
    
    // Initialize all pins to -1
    for (int i = 0; i < TS_DEVICE_MAX; i++) {
        s_devices[i].pins = (ts_agx_pins_t){
            .gpio_power_en = -1,
            .gpio_reset = -1,
            .gpio_force_off = -1,
            .gpio_sys_rst = -1,
            .gpio_power_good = -1,
            .gpio_carrier_pwr_on = -1,
            .gpio_shutdown_req = -1,
            .gpio_sleep_wake = -1
        };
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "Device control initialized");
    return ESP_OK;
}

esp_err_t ts_device_ctrl_deinit(void)
{
    for (int i = 0; i < TS_DEVICE_MAX; i++) {
        device_instance_t *dev = &s_devices[i];
        if (dev->gpio.power_en) ts_gpio_destroy(dev->gpio.power_en);
        if (dev->gpio.reset) ts_gpio_destroy(dev->gpio.reset);
        if (dev->gpio.force_off) ts_gpio_destroy(dev->gpio.force_off);
        if (dev->gpio.sys_rst) ts_gpio_destroy(dev->gpio.sys_rst);
        if (dev->gpio.power_good) ts_gpio_destroy(dev->gpio.power_good);
        if (dev->gpio.carrier_pwr_on) ts_gpio_destroy(dev->gpio.carrier_pwr_on);
        if (dev->gpio.shutdown_req) ts_gpio_destroy(dev->gpio.shutdown_req);
        if (dev->gpio.sleep_wake) ts_gpio_destroy(dev->gpio.sleep_wake);
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_device_configure_agx(const ts_agx_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[TS_DEVICE_AGX];
    dev->pins = *pins;
    
    // Configure output pins
    dev->gpio.power_en = create_output_gpio(pins->gpio_power_en, "agx_pwr_en");
    dev->gpio.reset = create_output_gpio(pins->gpio_reset, "agx_reset");
    dev->gpio.force_off = create_output_gpio(pins->gpio_force_off, "agx_foff");
    dev->gpio.carrier_pwr_on = create_output_gpio(pins->gpio_carrier_pwr_on, "agx_carrier");
    dev->gpio.sleep_wake = create_output_gpio(pins->gpio_sleep_wake, "agx_sw");
    
    // Configure input pins
    dev->gpio.power_good = create_input_gpio(pins->gpio_power_good, "agx_pg", true);
    dev->gpio.sys_rst = create_input_gpio(pins->gpio_sys_rst, "agx_rst_in", true);
    
    // Shutdown request with interrupt
    if (pins->gpio_shutdown_req >= 0) {
        dev->gpio.shutdown_req = ts_gpio_create_raw(pins->gpio_shutdown_req, "agx_shutdown");
        if (dev->gpio.shutdown_req) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_INPUT,
                .pull_mode = TS_GPIO_PULL_UP,
                .intr_type = TS_GPIO_INTR_NEGEDGE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = -1
            };
            ts_gpio_configure(dev->gpio.shutdown_req, &cfg);
            
            static ts_device_id_t agx_id = TS_DEVICE_AGX;
            ts_gpio_set_isr_callback(dev->gpio.shutdown_req, shutdown_req_callback, &agx_id);
            ts_gpio_intr_enable(dev->gpio.shutdown_req);
        }
    }
    
    dev->configured = true;
    dev->state = TS_DEVICE_STATE_OFF;
    
    TS_LOGI(TAG, "AGX configured");
    return ESP_OK;
}

esp_err_t ts_device_power_on(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    if (dev->state == TS_DEVICE_STATE_ON) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "Powering on device %d", device);
    dev->state = TS_DEVICE_STATE_BOOTING;
    
    if (device == TS_DEVICE_AGX) {
        // Carrier power on first
        if (dev->gpio.carrier_pwr_on) {
            ts_gpio_set_level(dev->gpio.carrier_pwr_on, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        
        // Main power enable
        if (dev->gpio.power_en) {
            ts_gpio_set_level(dev->gpio.power_en, 1);
        }
        
        // Wait for power good
#ifdef CONFIG_TS_DRIVERS_AGX_POWER_ON_DELAY_MS
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_DRIVERS_AGX_POWER_ON_DELAY_MS));
#else
        vTaskDelay(pdMS_TO_TICKS(100));
#endif
        
        // Release reset if held
        if (dev->gpio.reset) {
            ts_gpio_set_level(dev->gpio.reset, 1);
        }
    }
    
    dev->power_on_time = esp_timer_get_time() / 1000;
    dev->boot_count++;
    dev->state = TS_DEVICE_STATE_ON;
    
    TS_LOGI(TAG, "Device %d powered on", device);
    
    return ESP_OK;
}

esp_err_t ts_device_power_off(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGI(TAG, "Powering off device %d", device);
    
    if (device == TS_DEVICE_AGX) {
        // Assert reset
        if (dev->gpio.reset) {
            ts_gpio_set_level(dev->gpio.reset, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Power off
        if (dev->gpio.power_en) {
            ts_gpio_set_level(dev->gpio.power_en, 0);
        }
        
        // Carrier power off
        if (dev->gpio.carrier_pwr_on) {
            ts_gpio_set_level(dev->gpio.carrier_pwr_on, 0);
        }
    }
    
    dev->state = TS_DEVICE_STATE_OFF;
    TS_LOGI(TAG, "Device %d powered off", device);
    
    return ESP_OK;
}

esp_err_t ts_device_force_off(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGW(TAG, "Force power off device %d", device);
    
    if (device == TS_DEVICE_AGX && dev->gpio.force_off) {
        ts_gpio_set_level(dev->gpio.force_off, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ts_gpio_set_level(dev->gpio.force_off, 0);
    }
    
    return ts_device_power_off(device);
}

esp_err_t ts_device_reset(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGI(TAG, "Resetting device %d", device);
    
    if (device == TS_DEVICE_AGX && dev->gpio.reset) {
        ts_gpio_set_level(dev->gpio.reset, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
        ts_gpio_set_level(dev->gpio.reset, 1);
    }
    
    dev->boot_count++;
    return ESP_OK;
}

esp_err_t ts_device_get_status(ts_device_id_t device, ts_device_status_t *status)
{
    if (device >= TS_DEVICE_MAX || !status) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    status->state = dev->state;
    status->boot_count = dev->boot_count;
    status->last_error = dev->last_error;
    
    if (dev->gpio.power_good) {
        status->power_good = ts_gpio_get_level(dev->gpio.power_good) == 1;
    } else {
        status->power_good = dev->state == TS_DEVICE_STATE_ON;
    }
    
    if (dev->state == TS_DEVICE_STATE_ON && dev->power_on_time > 0) {
        status->uptime_ms = (esp_timer_get_time() / 1000) - dev->power_on_time;
    } else {
        status->uptime_ms = 0;
    }
    
    return ESP_OK;
}

bool ts_device_is_powered(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return false;
    return s_devices[device].state == TS_DEVICE_STATE_ON;
}

esp_err_t ts_device_request_shutdown(ts_device_id_t device)
{
    if (device >= TS_DEVICE_MAX) return ESP_ERR_INVALID_ARG;
    
    device_instance_t *dev = &s_devices[device];
    if (!dev->configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGI(TAG, "Requesting shutdown for device %d", device);
    
    if (device == TS_DEVICE_AGX && dev->gpio.sleep_wake) {
        // Pulse sleep/wake to signal shutdown request
        ts_gpio_set_level(dev->gpio.sleep_wake, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ts_gpio_set_level(dev->gpio.sleep_wake, 0);
    }
    
    return ESP_OK;
}

esp_err_t ts_device_handle_shutdown_request(ts_device_id_t device)
{
    TS_LOGI(TAG, "Handling shutdown request from device %d", device);
    return ts_device_power_off(device);
}
