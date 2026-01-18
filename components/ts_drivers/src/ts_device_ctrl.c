/**
 * @file ts_device_ctrl.c
 * @brief Device Power Control (AGX/LPMU) Implementation
 * 
 * AGX 使用电平控制（保持 HIGH/LOW）
 * LPMU 使用脉冲控制（按按钮方式切换）
 */

#include "ts_device_ctrl.h"
#include "ts_hal_gpio.h"
#include "ts_log.h"
#include "ts_event.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define TAG "ts_device"

/*===========================================================================*/
/*                          AGX Instance                                      */
/*===========================================================================*/

typedef struct {
    ts_gpio_handle_t power_en;
    ts_gpio_handle_t reset;
    ts_gpio_handle_t force_recovery;
    ts_gpio_handle_t sys_rst;
    ts_gpio_handle_t power_good;
    ts_gpio_handle_t carrier_pwr_on;
    ts_gpio_handle_t shutdown_req;
    ts_gpio_handle_t sleep_wake;
} agx_gpio_handles_t;

typedef struct {
    bool configured;
    ts_agx_pins_t pins;
    agx_gpio_handles_t gpio;
    ts_device_state_t state;
    uint32_t power_on_time;
    uint32_t boot_count;
    int32_t last_error;
} agx_instance_t;

/*===========================================================================*/
/*                          LPMU Instance                                     */
/*===========================================================================*/

typedef struct {
    ts_gpio_handle_t power_btn;
    ts_gpio_handle_t reset;
} lpmu_gpio_handles_t;

typedef struct {
    bool configured;
    ts_lpmu_pins_t pins;
    lpmu_gpio_handles_t gpio;
    ts_device_state_t state;
    uint32_t power_on_time;
    uint32_t boot_count;
    int32_t last_error;
} lpmu_instance_t;

/*===========================================================================*/
/*                          Static Instances                                  */
/*===========================================================================*/

static agx_instance_t s_agx = {0};
static lpmu_instance_t s_lpmu = {0};
static bool s_initialized = false;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/**
 * @brief Create output GPIO with specified initial level
 * @param gpio_num GPIO number
 * @param name GPIO name for debugging
 * @param initial_level Initial output level (0=LOW, 1=HIGH)
 * @return GPIO handle or NULL on failure
 */
static ts_gpio_handle_t create_output_gpio_with_level(int gpio_num, const char *name, int initial_level)
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
        .initial_level = initial_level
    };
    ts_gpio_configure(handle, &cfg);
    return handle;
}

/**
 * @brief Create output GPIO with default LOW level
 * @note Use create_output_gpio_with_level() for pins requiring HIGH initial state
 */
static ts_gpio_handle_t create_output_gpio(int gpio_num, const char *name)
{
    return create_output_gpio_with_level(gpio_num, name, 0);
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

static void shutdown_req_callback(ts_gpio_handle_t handle, void *arg)
{
    (void)handle;
    (void)arg;
    // AGX 发送了 shutdown 请求，可以发布事件
    // ts_event_post(TS_EVENT_BASE_DEVICE, TS_EVENT_DEVICE_SHUTDOWN_REQ, ...);
}

/*===========================================================================*/
/*                          AGX Control Functions                             */
/*===========================================================================*/

/**
 * AGX 电源控制使用反相逻辑（与 robOS 一致）：
 *   LOW  = 开机
 *   HIGH = 关机
 */

static esp_err_t agx_power_on(void)
{
    TS_LOGI(TAG, "AGX powering on (pin=%d)...", s_agx.pins.gpio_power_en);
    
    if (s_agx.state == TS_DEVICE_STATE_ON) {
        TS_LOGI(TAG, "AGX already ON");
        return ESP_OK;
    }
    
    s_agx.state = TS_DEVICE_STATE_BOOTING;
    
    int pin = s_agx.pins.gpio_power_en;
    
    if (pin <= 0) {
        TS_LOGE(TAG, "Invalid GPIO pin: %d (AGX not configured?)", pin);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 重置 GPIO 到默认状态
    gpio_reset_pin(pin);
    
    // 使用 INPUT_OUTPUT 模式，这样可以验证输出电平
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT,  // 同时启用输入和输出
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 设置 GPIO 电平（LOW = ON，反相逻辑）
    ret = gpio_set_level(pin, 0);
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "gpio_set_level failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 验证电平已正确设置
    int level = gpio_get_level(pin);
    TS_LOGI(TAG, "AGX powered on (GPIO%d=%d, expected 0)", pin, level);
    
    // Wait for power stabilization
    vTaskDelay(pdMS_TO_TICKS(TS_AGX_POWER_ON_DELAY_MS));
    
    s_agx.power_on_time = esp_timer_get_time() / 1000;
    s_agx.boot_count++;
    s_agx.state = TS_DEVICE_STATE_ON;
    
    return ESP_OK;
}

static esp_err_t agx_power_off(void)
{
    TS_LOGI(TAG, "AGX powering off (pin=%d)...", s_agx.pins.gpio_power_en);
    
    int pin = s_agx.pins.gpio_power_en;
    
    // 重置 GPIO 到默认状态
    gpio_reset_pin(pin);
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    esp_err_t ret = gpio_set_level(pin, 1);  // HIGH = OFF
    if (ret != ESP_OK) {
        TS_LOGE(TAG, "gpio_set_level failed: %s", esp_err_to_name(ret));
    }
    
    int level = gpio_get_level(pin);
    TS_LOGI(TAG, "AGX powered off (GPIO%d=%d, expected 1)", pin, level);
    
    s_agx.state = TS_DEVICE_STATE_OFF;
    return ESP_OK;
}

static esp_err_t agx_reset(void)
{
    TS_LOGI(TAG, "AGX resetting...");
    
    if (s_agx.gpio.reset) {
        // Pulse reset HIGH for TS_AGX_RESET_PULSE_MS
        ts_gpio_set_level(s_agx.gpio.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RESET_PULSE_MS));
        ts_gpio_set_level(s_agx.gpio.reset, 0);
    }
    
    s_agx.boot_count++;
    s_agx.state = TS_DEVICE_STATE_BOOTING;
    
    TS_LOGI(TAG, "AGX reset complete (boot #%lu)", s_agx.boot_count);
    return ESP_OK;
}

static esp_err_t agx_force_off(void)
{
    TS_LOGW(TAG, "AGX force power off...");
    
    // Pulse force_off signal (like holding power button)
    if (s_agx.gpio.force_recovery) {  // Use force_recovery pin as force_off
        ts_gpio_set_level(s_agx.gpio.force_recovery, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_POWER_PULSE_MS));
        ts_gpio_set_level(s_agx.gpio.force_recovery, 0);
    }
    
    return agx_power_off();
}

static esp_err_t agx_enter_recovery(void)
{
    TS_LOGI(TAG, "AGX entering recovery mode...");
    
    // robOS recovery sequence:
    // 1. Assert force recovery HIGH, hold 1000ms
    if (s_agx.gpio.force_recovery) {
        ts_gpio_set_level(s_agx.gpio.force_recovery, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RECOVERY_DELAY_MS));
    }
    
    // 2. Execute reset pulse
    if (s_agx.gpio.reset) {
        ts_gpio_set_level(s_agx.gpio.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RESET_PULSE_MS));
        ts_gpio_set_level(s_agx.gpio.reset, 0);
        vTaskDelay(pdMS_TO_TICKS(TS_AGX_RECOVERY_DELAY_MS));
    }
    
    // 3. Release force recovery
    if (s_agx.gpio.force_recovery) {
        ts_gpio_set_level(s_agx.gpio.force_recovery, 0);
    }
    
    s_agx.state = TS_DEVICE_STATE_RECOVERY;
    s_agx.boot_count++;
    
    TS_LOGI(TAG, "AGX in recovery mode");
    return ESP_OK;
}

/*===========================================================================*/
/*                          LPMU Control Functions                            */
/*===========================================================================*/

static esp_err_t lpmu_power_toggle(void)
{
    TS_LOGI(TAG, "LPMU power toggle (pulse)");
    
    // LPMU uses pulse to toggle power (like pressing a button)
    if (s_lpmu.gpio.power_btn) {
        ts_gpio_set_level(s_lpmu.gpio.power_btn, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_LPMU_POWER_PULSE_MS));
        ts_gpio_set_level(s_lpmu.gpio.power_btn, 0);
    }
    
    return ESP_OK;
}

static esp_err_t lpmu_power_on(void)
{
    if (s_lpmu.state == TS_DEVICE_STATE_ON) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "LPMU powering on...");
    s_lpmu.state = TS_DEVICE_STATE_BOOTING;
    
    lpmu_power_toggle();
    
    s_lpmu.power_on_time = esp_timer_get_time() / 1000;
    s_lpmu.boot_count++;
    s_lpmu.state = TS_DEVICE_STATE_ON;
    
    TS_LOGI(TAG, "LPMU powered on (boot #%lu)", s_lpmu.boot_count);
    return ESP_OK;
}

static esp_err_t lpmu_power_off(void)
{
    if (s_lpmu.state == TS_DEVICE_STATE_OFF) {
        return ESP_OK;
    }
    
    TS_LOGI(TAG, "LPMU powering off...");
    
    lpmu_power_toggle();
    
    s_lpmu.state = TS_DEVICE_STATE_OFF;
    TS_LOGI(TAG, "LPMU powered off");
    return ESP_OK;
}

static esp_err_t lpmu_reset(void)
{
    TS_LOGI(TAG, "LPMU resetting...");
    
    if (s_lpmu.gpio.reset) {
        // Pulse reset HIGH
        ts_gpio_set_level(s_lpmu.gpio.reset, 1);
        vTaskDelay(pdMS_TO_TICKS(TS_LPMU_RESET_PULSE_MS));
        ts_gpio_set_level(s_lpmu.gpio.reset, 0);
    }
    
    s_lpmu.boot_count++;
    s_lpmu.state = TS_DEVICE_STATE_BOOTING;
    
    TS_LOGI(TAG, "LPMU reset complete (boot #%lu)", s_lpmu.boot_count);
    return ESP_OK;
}

/*===========================================================================*/
/*                          Public API                                        */
/*===========================================================================*/

esp_err_t ts_device_ctrl_init(void)
{
    if (s_initialized) return ESP_OK;
    
    memset(&s_agx, 0, sizeof(s_agx));
    memset(&s_lpmu, 0, sizeof(s_lpmu));
    
    // Initialize AGX pins to -1
    s_agx.pins = (ts_agx_pins_t){
        .gpio_power_en = -1,
        .gpio_reset = -1,
        .gpio_force_recovery = -1,
        .gpio_sys_rst = -1,
        .gpio_power_good = -1,
        .gpio_carrier_pwr_on = -1,
        .gpio_shutdown_req = -1,
        .gpio_sleep_wake = -1
    };
    
    // Initialize LPMU pins to -1
    s_lpmu.pins = (ts_lpmu_pins_t){
        .gpio_power_btn = -1,
        .gpio_reset = -1
    };
    
    s_initialized = true;
    TS_LOGI(TAG, "Device control initialized");
    return ESP_OK;
}

esp_err_t ts_device_ctrl_deinit(void)
{
    // Cleanup AGX GPIO
    if (s_agx.gpio.power_en) ts_gpio_destroy(s_agx.gpio.power_en);
    if (s_agx.gpio.reset) ts_gpio_destroy(s_agx.gpio.reset);
    if (s_agx.gpio.force_recovery) ts_gpio_destroy(s_agx.gpio.force_recovery);
    if (s_agx.gpio.sys_rst) ts_gpio_destroy(s_agx.gpio.sys_rst);
    if (s_agx.gpio.power_good) ts_gpio_destroy(s_agx.gpio.power_good);
    if (s_agx.gpio.carrier_pwr_on) ts_gpio_destroy(s_agx.gpio.carrier_pwr_on);
    if (s_agx.gpio.shutdown_req) ts_gpio_destroy(s_agx.gpio.shutdown_req);
    if (s_agx.gpio.sleep_wake) ts_gpio_destroy(s_agx.gpio.sleep_wake);
    
    // Cleanup LPMU GPIO
    if (s_lpmu.gpio.power_btn) ts_gpio_destroy(s_lpmu.gpio.power_btn);
    if (s_lpmu.gpio.reset) ts_gpio_destroy(s_lpmu.gpio.reset);
    
    memset(&s_agx, 0, sizeof(s_agx));
    memset(&s_lpmu, 0, sizeof(s_lpmu));
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_device_configure_agx(const ts_agx_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    s_agx.pins = *pins;
    
    /*
     * AGX GPIO 初始状态（与 robOS 一致）：
     * - AGX_POWER:  HIGH=关机, LOW=开机 → 初始化为 HIGH（关机状态）
     * - AGX_RESET:  HIGH=复位, LOW=正常 → 初始化为 LOW（正常运行）
     * - AGX_FORCE_RECOVERY: HIGH=恢复模式 → 初始化为 LOW（正常模式）
     */
    
    // AGX_POWER: 初始化为 HIGH（关机状态），用户需显式开机
    s_agx.gpio.power_en = create_output_gpio_with_level(pins->gpio_power_en, "agx_pwr", 1);
    
    // AGX_RESET: 初始化为 LOW（正常运行）
    s_agx.gpio.reset = create_output_gpio_with_level(pins->gpio_reset, "agx_rst", 0);
    
    // AGX_FORCE_RECOVERY: 初始化为 LOW（正常模式）
    s_agx.gpio.force_recovery = create_output_gpio_with_level(pins->gpio_force_recovery, "agx_rcv", 0);
    
    // 其他输出引脚（如果配置）
    s_agx.gpio.carrier_pwr_on = create_output_gpio(pins->gpio_carrier_pwr_on, "agx_carrier");
    s_agx.gpio.sleep_wake = create_output_gpio(pins->gpio_sleep_wake, "agx_sw");
    
    // Configure input pins
    s_agx.gpio.power_good = create_input_gpio(pins->gpio_power_good, "agx_pg", true);
    s_agx.gpio.sys_rst = create_input_gpio(pins->gpio_sys_rst, "agx_rst_in", true);
    
    // Shutdown request with interrupt
    if (pins->gpio_shutdown_req >= 0) {
        s_agx.gpio.shutdown_req = ts_gpio_create_raw(pins->gpio_shutdown_req, "agx_shutdown");
        if (s_agx.gpio.shutdown_req) {
            ts_gpio_config_t cfg = {
                .direction = TS_GPIO_DIR_INPUT,
                .pull_mode = TS_GPIO_PULL_UP,
                .intr_type = TS_GPIO_INTR_NEGEDGE,
                .drive = TS_GPIO_DRIVE_2,
                .invert = false,
                .initial_level = -1
            };
            ts_gpio_configure(s_agx.gpio.shutdown_req, &cfg);
            ts_gpio_set_isr_callback(s_agx.gpio.shutdown_req, shutdown_req_callback, NULL);
            ts_gpio_intr_enable(s_agx.gpio.shutdown_req);
        }
    }
    
    s_agx.configured = true;
    s_agx.state = TS_DEVICE_STATE_OFF;
    
    TS_LOGI(TAG, "AGX configured (power=%d[HIGH=off], reset=%d[LOW=normal], recovery=%d[LOW=normal])",
            pins->gpio_power_en, pins->gpio_reset, pins->gpio_force_recovery);
    return ESP_OK;
}

esp_err_t ts_device_configure_lpmu(const ts_lpmu_pins_t *pins)
{
    if (!pins) return ESP_ERR_INVALID_ARG;
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    s_lpmu.pins = *pins;
    
    /*
     * LPMU GPIO 初始状态：
     * - LPMU_POWER_BTN: 脉冲控制（HIGH 300ms 切换电源）→ 初始化为 LOW
     * - LPMU_RESET: 脉冲控制（HIGH 300ms 复位）→ 初始化为 LOW
     */
    s_lpmu.gpio.power_btn = create_output_gpio_with_level(pins->gpio_power_btn, "lpmu_pwr", 0);
    s_lpmu.gpio.reset = create_output_gpio_with_level(pins->gpio_reset, "lpmu_rst", 0);
    
    s_lpmu.configured = true;
    s_lpmu.state = TS_DEVICE_STATE_OFF;
    
    TS_LOGI(TAG, "LPMU configured (power=%d[pulse HIGH], reset=%d[pulse HIGH])",
            pins->gpio_power_btn, pins->gpio_reset);
    return ESP_OK;
}

esp_err_t ts_device_power_on(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_power_on();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_on();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_power_off(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_power_off();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_off();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_force_off(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_force_off();
        case TS_DEVICE_LPMU:
            // LPMU 没有 force_off，直接 power_off
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_power_off();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_reset(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            return agx_reset();
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            return lpmu_reset();
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t ts_device_enter_recovery(ts_device_id_t device)
{
    if (device != TS_DEVICE_AGX) {
        TS_LOGW(TAG, "Recovery mode only supported for AGX");
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
    return agx_enter_recovery();
}

esp_err_t ts_device_get_status(ts_device_id_t device, ts_device_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    
    switch (device) {
        case TS_DEVICE_AGX:
            if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
            status->state = s_agx.state;
            status->boot_count = s_agx.boot_count;
            status->last_error = s_agx.last_error;
            if (s_agx.gpio.power_good) {
                status->power_good = ts_gpio_get_level(s_agx.gpio.power_good) == 1;
            } else {
                status->power_good = s_agx.state == TS_DEVICE_STATE_ON;
            }
            if (s_agx.state == TS_DEVICE_STATE_ON && s_agx.power_on_time > 0) {
                status->uptime_ms = (esp_timer_get_time() / 1000) - s_agx.power_on_time;
            } else {
                status->uptime_ms = 0;
            }
            break;
            
        case TS_DEVICE_LPMU:
            if (!s_lpmu.configured) return ESP_ERR_INVALID_STATE;
            status->state = s_lpmu.state;
            status->boot_count = s_lpmu.boot_count;
            status->last_error = s_lpmu.last_error;
            status->power_good = s_lpmu.state == TS_DEVICE_STATE_ON;
            if (s_lpmu.state == TS_DEVICE_STATE_ON && s_lpmu.power_on_time > 0) {
                status->uptime_ms = (esp_timer_get_time() / 1000) - s_lpmu.power_on_time;
            } else {
                status->uptime_ms = 0;
            }
            break;
            
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

bool ts_device_is_powered(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            return s_agx.state == TS_DEVICE_STATE_ON || s_agx.state == TS_DEVICE_STATE_RECOVERY;
        case TS_DEVICE_LPMU:
            return s_lpmu.state == TS_DEVICE_STATE_ON;
        default:
            return false;
    }
}

bool ts_device_is_configured(ts_device_id_t device)
{
    switch (device) {
        case TS_DEVICE_AGX:
            return s_agx.configured;
        case TS_DEVICE_LPMU:
            return s_lpmu.configured;
        default:
            return false;
    }
}

esp_err_t ts_device_request_shutdown(ts_device_id_t device)
{
    if (device != TS_DEVICE_AGX) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    if (!s_agx.configured) return ESP_ERR_INVALID_STATE;
    
    TS_LOGI(TAG, "Requesting AGX shutdown...");
    
    if (s_agx.gpio.sleep_wake) {
        // Pulse sleep/wake to signal shutdown request
        ts_gpio_set_level(s_agx.gpio.sleep_wake, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        ts_gpio_set_level(s_agx.gpio.sleep_wake, 0);
    }
    
    return ESP_OK;
}

esp_err_t ts_device_handle_shutdown_request(ts_device_id_t device)
{
    TS_LOGI(TAG, "Handling shutdown request from device %d", device);
    return ts_device_power_off(device);
}

const char *ts_device_state_to_str(ts_device_state_t state)
{
    switch (state) {
        case TS_DEVICE_STATE_OFF:      return "off";
        case TS_DEVICE_STATE_STANDBY:  return "standby";
        case TS_DEVICE_STATE_ON:       return "on";
        case TS_DEVICE_STATE_BOOTING:  return "booting";
        case TS_DEVICE_STATE_RECOVERY: return "recovery";
        case TS_DEVICE_STATE_ERROR:    return "error";
        default:                       return "unknown";
    }
}
