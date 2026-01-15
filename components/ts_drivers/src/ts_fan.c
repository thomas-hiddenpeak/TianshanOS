/**
 * @file ts_fan.c
 * @brief Fan Control Driver Implementation
 */

#include "ts_fan.h"
#include "ts_hal_pwm.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_fan"
#define FAN_NVS_NAMESPACE "fan_config"

typedef struct {
    bool initialized;
    ts_fan_config_t config;
    ts_pwm_handle_t pwm;
    ts_gpio_handle_t tach_gpio;
    ts_fan_mode_t mode;
    uint8_t current_duty;
    uint16_t rpm;
    int16_t temperature;
    volatile uint32_t tach_count;
    int64_t last_tach_time;
} fan_instance_t;

static fan_instance_t s_fans[TS_FAN_MAX];
static esp_timer_handle_t s_update_timer = NULL;
static bool s_initialized = false;

static void tach_isr_callback(ts_gpio_handle_t handle, void *arg)
{
    fan_instance_t *fan = (fan_instance_t *)arg;
    fan->tach_count++;
}

static uint8_t calc_duty_from_curve(fan_instance_t *fan, int16_t temp)
{
    if (fan->config.curve_points == 0) {
        return fan->config.max_duty;
    }
    
    const ts_fan_curve_point_t *curve = fan->config.curve;
    uint8_t points = fan->config.curve_points;
    
    if (temp <= curve[0].temp) return curve[0].duty;
    if (temp >= curve[points - 1].temp) return curve[points - 1].duty;
    
    for (int i = 0; i < points - 1; i++) {
        if (temp >= curve[i].temp && temp < curve[i + 1].temp) {
            int32_t t_range = curve[i + 1].temp - curve[i].temp;
            int32_t d_range = curve[i + 1].duty - curve[i].duty;
            return curve[i].duty + (temp - curve[i].temp) * d_range / t_range;
        }
    }
    return fan->config.max_duty;
}

static void fan_update_callback(void *arg)
{
    int64_t now = esp_timer_get_time();
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        fan_instance_t *fan = &s_fans[i];
        if (!fan->initialized) continue;
        
        // Calculate RPM from tachometer
        if (fan->tach_gpio) {
            int64_t dt_us = now - fan->last_tach_time;
            if (dt_us > 0) {
                fan->rpm = (fan->tach_count * 60 * 1000000) / (dt_us * 2);
                fan->tach_count = 0;
                fan->last_tach_time = now;
            }
        }
        
        // Auto mode: adjust duty based on temperature curve
        if (fan->mode == TS_FAN_MODE_AUTO) {
            uint8_t target = calc_duty_from_curve(fan, fan->temperature);
            if (target < fan->config.min_duty && target > 0) {
                target = fan->config.min_duty;
            }
            if (target > fan->config.max_duty) {
                target = fan->config.max_duty;
            }
            if (target != fan->current_duty) {
                fan->current_duty = target;
                ts_pwm_set_duty(fan->pwm, (float)target);
            }
        }
    }
}

esp_err_t ts_fan_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(s_fans, 0, sizeof(s_fans));
    
    // Default curves for all fans
    ts_fan_curve_point_t default_curve[] = {
        {300, 20}, {400, 40}, {500, 60}, {600, 80}, {700, 100}
    };
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        s_fans[i].config.min_duty = 20;
        s_fans[i].config.max_duty = 100;
        memcpy(s_fans[i].config.curve, default_curve, sizeof(default_curve));
        s_fans[i].config.curve_points = 5;
    }
    
    // Start update timer
    esp_timer_create_args_t timer_args = {
        .callback = fan_update_callback,
        .name = "fan_update"
    };
    esp_timer_create(&timer_args, &s_update_timer);
    
#ifdef CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS
    esp_timer_start_periodic(s_update_timer, CONFIG_TS_DRIVERS_FAN_TEMP_UPDATE_MS * 1000);
#else
    esp_timer_start_periodic(s_update_timer, 1000000);
#endif
    
    s_initialized = true;
    TS_LOGI(TAG, "Fan driver initialized");
    return ESP_OK;
}

esp_err_t ts_fan_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    if (s_update_timer) {
        esp_timer_stop(s_update_timer);
        esp_timer_delete(s_update_timer);
        s_update_timer = NULL;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].pwm) {
            ts_pwm_destroy(s_fans[i].pwm);
            s_fans[i].pwm = NULL;
        }
        if (s_fans[i].tach_gpio) {
            ts_gpio_destroy(s_fans[i].tach_gpio);
            s_fans[i].tach_gpio = NULL;
        }
    }
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_fan_configure(ts_fan_id_t fan, const ts_fan_config_t *config)
{
    if (fan >= TS_FAN_MAX || !config) return ESP_ERR_INVALID_ARG;
    
    fan_instance_t *f = &s_fans[fan];
    f->config = *config;
    
    // Create PWM handle for the fan's GPIO
    f->pwm = ts_pwm_create_raw(config->gpio_pwm, "fan");
    if (!f->pwm) {
        TS_LOGE(TAG, "Failed to create PWM for fan %d", fan);
        return ESP_FAIL;
    }
    
    // Configure PWM
    ts_pwm_config_t pwm_cfg = {
        .frequency = 25000,  // 25kHz for PC fans
        .resolution_bits = 8,
        .timer = TS_PWM_TIMER_AUTO,
        .invert = false,
        .initial_duty = 0.0f
    };
    esp_err_t ret = ts_pwm_configure(f->pwm, &pwm_cfg);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to configure PWM for fan %d", fan);
        ts_pwm_destroy(f->pwm);
        f->pwm = NULL;
        return ret;
    }
    
    // Initialize tachometer if configured
    if (config->gpio_tach >= 0) {
        f->tach_gpio = ts_gpio_create_raw(config->gpio_tach, "fan_tach");
        if (f->tach_gpio) {
            ts_gpio_config_t gpio_cfg = {
                .direction = TS_GPIO_DIR_INPUT,
                .pull_mode = TS_GPIO_PULL_UP,
                .intr_type = TS_GPIO_INTR_NEGEDGE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = -1
            };
            ts_gpio_configure(f->tach_gpio, &gpio_cfg);
            ts_gpio_set_isr_callback(f->tach_gpio, tach_isr_callback, f);
            ts_gpio_intr_enable(f->tach_gpio);
            f->last_tach_time = esp_timer_get_time();
        }
    }
    
    f->initialized = true;
    f->mode = TS_FAN_MODE_OFF;
    
    TS_LOGI(TAG, "Fan %d configured: PWM=%d, TACH=%d", fan, config->gpio_pwm, config->gpio_tach);
    return ESP_OK;
}

esp_err_t ts_fan_set_mode(ts_fan_id_t fan, ts_fan_mode_t mode)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    s_fans[fan].mode = mode;
    
    if (mode == TS_FAN_MODE_OFF) {
        ts_pwm_set_duty(s_fans[fan].pwm, 0);
        s_fans[fan].current_duty = 0;
    }
    
    return ESP_OK;
}

esp_err_t ts_fan_set_duty(ts_fan_id_t fan, uint8_t duty_percent)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    if (duty_percent > 100) duty_percent = 100;
    
    s_fans[fan].mode = TS_FAN_MODE_MANUAL;
    s_fans[fan].current_duty = duty_percent;
    
    return ts_pwm_set_duty(s_fans[fan].pwm, (float)duty_percent);
}

esp_err_t ts_fan_set_temperature(ts_fan_id_t fan, int16_t temp_01c)
{
    if (fan >= TS_FAN_MAX) return ESP_ERR_INVALID_ARG;
    s_fans[fan].temperature = temp_01c;
    return ESP_OK;
}

esp_err_t ts_fan_get_status(ts_fan_id_t fan, ts_fan_status_t *status)
{
    if (fan >= TS_FAN_MAX || !status) return ESP_ERR_INVALID_ARG;
    if (!s_fans[fan].initialized) return ESP_ERR_INVALID_STATE;
    
    status->mode = s_fans[fan].mode;
    status->duty_percent = s_fans[fan].current_duty;
    status->rpm = s_fans[fan].rpm;
    status->temp = s_fans[fan].temperature;
    status->is_running = s_fans[fan].current_duty > 0;
    
    return ESP_OK;
}

esp_err_t ts_fan_set_curve(ts_fan_id_t fan, const ts_fan_curve_point_t *curve, uint8_t points)
{
    if (fan >= TS_FAN_MAX || !curve || points > 8) return ESP_ERR_INVALID_ARG;
    
    memcpy(s_fans[fan].config.curve, curve, points * sizeof(ts_fan_curve_point_t));
    s_fans[fan].config.curve_points = points;
    
    return ESP_OK;
}

esp_err_t ts_fan_emergency_full(void)
{
    TS_LOGW(TAG, "Emergency: All fans to 100%%");
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (s_fans[i].initialized) {
            s_fans[i].mode = TS_FAN_MODE_MANUAL;
            s_fans[i].current_duty = 100;
            ts_pwm_set_duty(s_fans[i].pwm, 100.0f);
        }
    }
    return ESP_OK;
}

esp_err_t ts_fan_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(FAN_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (!s_fans[i].initialized) continue;
        
        char key[16];
        
        // 保存模式
        snprintf(key, sizeof(key), "fan%d.mode", i);
        nvs_set_u8(nvs, key, (uint8_t)s_fans[i].mode);
        
        // 保存当前占空比
        snprintf(key, sizeof(key), "fan%d.duty", i);
        nvs_set_u8(nvs, key, s_fans[i].current_duty);
        
        // 保存启用标志
        snprintf(key, sizeof(key), "fan%d.en", i);
        nvs_set_u8(nvs, key, 1);
        
        TS_LOGI(TAG, "Saved fan %d: mode=%d, duty=%d%%", 
                i, s_fans[i].mode, s_fans[i].current_duty);
    }
    
    ret = nvs_commit(nvs);
    nvs_close(nvs);
    
    return ret;
}

esp_err_t ts_fan_load_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(FAN_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret != ESP_OK) {
        TS_LOGD(TAG, "No saved fan config found");
        return ESP_ERR_NOT_FOUND;
    }
    
    for (int i = 0; i < TS_FAN_MAX; i++) {
        if (!s_fans[i].initialized) continue;
        
        char key[16];
        
        // 检查是否有保存的配置
        snprintf(key, sizeof(key), "fan%d.en", i);
        uint8_t enabled = 0;
        if (nvs_get_u8(nvs, key, &enabled) != ESP_OK || !enabled) {
            continue;
        }
        
        // 加载模式
        snprintf(key, sizeof(key), "fan%d.mode", i);
        uint8_t mode = 0;
        if (nvs_get_u8(nvs, key, &mode) == ESP_OK) {
            s_fans[i].mode = (ts_fan_mode_t)mode;
        }
        
        // 加载占空比
        snprintf(key, sizeof(key), "fan%d.duty", i);
        uint8_t duty = 0;
        if (nvs_get_u8(nvs, key, &duty) == ESP_OK) {
            s_fans[i].current_duty = duty;
            if (s_fans[i].mode == TS_FAN_MODE_MANUAL && s_fans[i].pwm) {
                ts_pwm_set_duty(s_fans[i].pwm, (float)duty);
            }
        }
        
        TS_LOGI(TAG, "Restored fan %d: mode=%d, duty=%d%%", 
                i, s_fans[i].mode, s_fans[i].current_duty);
    }
    
    nvs_close(nvs);
    return ESP_OK;
}
