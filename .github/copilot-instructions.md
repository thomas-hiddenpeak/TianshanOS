# TianShanOS Copilot 指南

## 项目概述

TianShanOS 是**面向配置而非面向代码**的嵌入式 OS，基于 ESP-IDF v5.5+，用于 NVIDIA Jetson AGX 载板机架管理。

## 核心架构

```
CLI/WebUI → Core API → 服务管理(8阶段) → 事件总线 → 配置管理 → HAL → 平台适配层
```

- **所有组件通过事件总线解耦**，禁止直接调用其他组件内部函数
- 核心模块：`components/ts_core/` 下的 `ts_config`、`ts_event`、`ts_service`、`ts_log`
- 命名规范：组件前缀 `ts_`，宏/枚举前缀 `TS_`

## 事件系统模式

组件通信必须使用事件总线（参考 `ts_event.h`）：

```c
// 发布事件
ts_event_post(TS_EVENT_BASE_LED, TS_EVENT_LED_CHANGED, &data, sizeof(data));

// 订阅事件
ts_event_handler_register(TS_EVENT_BASE_LED, TS_EVENT_ANY_ID, handler_fn, user_data);
```

## Core API 层

CLI 和 WebUI 共享统一 API（参考 `ts_api.h`）：
- API 命名格式：`<category>.<action>`（如 `system.reboot`、`led.set_color`）
- 所有 API 返回 `ts_api_result_t` 结构，包含 code/message/JSON data
- 需要权限的 API 设置 `requires_auth` 和 `permission` 字段

## 服务生命周期（关键模式）

服务是系统基本单元，必须实现 4 个回调并在 `main/ts_services.c` 中注册：

```c
// 服务回调模板（参考 ts_services.c 中的 hal_service_* 实现）
static esp_err_t xxx_service_init(ts_service_handle_t h, void *d) { ... }
static esp_err_t xxx_service_start(ts_service_handle_t h, void *d) { ... }
static esp_err_t xxx_service_stop(ts_service_handle_t h, void *d) { ... }
static bool xxx_service_health(ts_service_handle_t h, void *d) { ... }
```

启动阶段顺序：`PLATFORM → CORE → HAL → DRIVER → NETWORK → SECURITY → SERVICE → UI`

## 配置驱动开发

**禁止硬编码 GPIO 或服务配置**，统一使用 JSON：

- `boards/rm01_esp32s3/pins.json` - 逻辑名 → GPIO（如 `"FAN_PWM_0": {"gpio": 41}`）
- `boards/rm01_esp32s3/services.json` - 服务启用、依赖、配置参数
- 优先级：CLI/API > NVS 持久化 > SD 卡文件 > 代码默认值

## CLI 命令实现模式

命令使用 **argtable3** 库，参数风格（非子命令）：

```bash
fan --status --id 0 --json   # 正确
fan --set --id 0 --speed 75  # 正确
```

新增命令步骤：
1. 在 `components/ts_console/commands/` 创建 `ts_cmd_xxx.c`
2. 使用 argtable3 定义参数，实现 `do_xxx_*` 处理函数
3. 在 `ts_cmd_register.c` 中调用 `ts_cmd_xxx_register()`
4. 在 `include/ts_cmd_all.h` 添加声明

## 开发工作流

使用 VS Code ESP-IDF 扩展（推荐）或命令行：

```bash
idf.py set-target esp32s3    # 或 esp32p4
idf.py menuconfig            # TianShanOS 配置在顶层菜单
idf.py build flash monitor   # 构建、烧录、监控
```

分区表 `partitions.csv`：factory(3MB) / storage(SPIFFS) / www(WebUI) / fatfs

## 代码约定

```c
static const char *TAG = "模块名";  // 每个 .c 文件必须定义

ESP_LOGI(TAG, "Info message");      // 日志 API
ESP_LOGE(TAG, "Error: %s", esp_err_to_name(ret));

// 错误处理 - 返回 esp_err_t，使用 ESP_OK/ESP_ERR_*
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(ret));
    return ret;
}
```

- 头文件：doxygen 注释（`@brief`, `@param`, `@return`），英文
- 源文件：中文注释用于架构说明

## 组件结构模板

```
components/ts_xxx/
├── CMakeLists.txt           # REQUIRES 声明依赖
├── Kconfig                  # CONFIG_TS_XXX_* 选项
├── include/ts_xxx.h         # 公开 API
└── src/ts_xxx.c             # 实现
```

CMakeLists.txt 示例（参考 `ts_led/CMakeLists.txt`）：
```cmake
idf_component_register(
    SRCS "src/ts_xxx.c"
    INCLUDE_DIRS "include"
    REQUIRES ts_core ts_hal   # 公开依赖
    PRIV_REQUIRES ts_storage  # 私有依赖
)
```

## 关键目录

- `components/ts_core/` - 核心框架（配置/事件/服务/日志）
- `components/ts_hal/platform/{esp32s3,esp32p4}/` - 平台特定 HAL 实现
- `components/ts_console/commands/` - CLI 命令实现
- `main/ts_services.c` - 服务注册入口
- `boards/rm01_esp32s3/` - 板级 JSON 配置
- `components/ts_api/` - 统一 API 层（CLI/WebUI 共享）- `sdcard/` - SD 卡内容模板（动画、脚本、图像）