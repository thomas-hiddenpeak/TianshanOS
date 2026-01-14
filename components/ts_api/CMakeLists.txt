# TianShanOS API Component

set(TS_API_SRCS
    "src/ts_api.c"
    "src/ts_api_system.c"
    "src/ts_api_config.c"
    "src/ts_api_device.c"
    "src/ts_api_led.c"
    "src/ts_api_network.c"
)

idf_component_register(
    SRCS ${TS_API_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_drivers ts_led ts_net json
    PRIV_REQUIRES ts_hal esp_timer esp_app_format spi_flash bootloader_support esp_wifi
)
