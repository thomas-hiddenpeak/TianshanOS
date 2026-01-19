# TianShanOS Core API 设计规范

## 1. 架构概述

```
┌─────────────────────────────────────────────────────────────┐
│                      用户界面层                              │
├─────────────┬─────────────┬─────────────┬──────────────────┤
│    CLI      │   WebUI     │  REST API   │   WebSocket      │
│  (argtable) │  (HTTP)     │  (JSON)     │   (实时)         │
└──────┬──────┴──────┬──────┴──────┬──────┴───────┬──────────┘
       │             │             │              │
       └─────────────┴─────────────┴──────────────┘
                           │
                           ▼
       ┌───────────────────────────────────────────┐
       │           ts_api (Core API Layer)         │
       │  - 统一的参数验证                          │
       │  - 统一的权限检查                          │
       │  - 统一的日志记录                          │
       │  - 统一的错误处理                          │
       │  - 统一的 JSON 输入/输出                   │
       └─────────────────────┬─────────────────────┘
                             │
                             ▼
       ┌───────────────────────────────────────────┐
       │            服务/驱动层                     │
       │  ts_led, ts_fan, ts_device, ts_net, ...  │
       └───────────────────────────────────────────┘
```

## 2. API 命名规范

### 格式: `<模块>.<对象>.<动作>` 或 `<模块>.<动作>`

| 模块 | API 示例 | 说明 |
|------|----------|------|
| system | system.info, system.reboot | 系统级操作 |
| config | config.get, config.set | 配置管理 |
| device | device.agx.power, device.lpmu.reset | 设备控制 |
| led | led.effect.start, led.brightness | LED 控制 |
| fan | fan.speed, fan.mode | 风扇控制 |
| power | power.voltage, power.protection | 电源管理 |
| network | network.eth.status, network.wifi.scan | 网络 |
| temp | temp.sources, temp.read | 温度监控 |
| storage | storage.list, storage.format | 存储管理 |
| gpio | gpio.set, gpio.get | GPIO 操作 |
| service | service.list, service.start | 服务管理 |
| ssh | ssh.connect, ssh.exec | SSH 客户端 |

## 3. 参数规范

### 3.1 输入参数 (JSON)

```json
{
  "device": "matrix",       // 字符串参数
  "brightness": 128,        // 数值参数
  "enable": true,           // 布尔参数
  "colors": [255, 0, 0]     // 数组参数
}
```

### 3.2 返回结果 (ts_api_result_t)

```c
typedef struct {
    ts_api_result_code_t code;   // 结果码
    char *message;               // 人类可读消息
    cJSON *data;                 // 返回数据 (JSON)
} ts_api_result_t;
```

### 3.3 结果码

| 码 | 名称 | 说明 |
|----|------|------|
| 0 | TS_API_OK | 成功 |
| 1 | TS_API_ERR_INVALID_ARG | 参数错误 |
| 2 | TS_API_ERR_NOT_FOUND | 资源不存在 |
| 3 | TS_API_ERR_NO_PERMISSION | 权限不足 |
| 4 | TS_API_ERR_BUSY | 资源忙 |
| 5 | TS_API_ERR_TIMEOUT | 超时 |
| 6 | TS_API_ERR_NO_MEM | 内存不足 |
| 7 | TS_API_ERR_INTERNAL | 内部错误 |
| 8 | TS_API_ERR_NOT_SUPPORTED | 不支持 |
| 9 | TS_API_ERR_HARDWARE | 硬件错误 |

## 4. CLI 命令实现规范

### 4.1 标准模板

```c
static int cmd_xxx_handler(int argc, char **argv)
{
    // 1. 解析参数
    int nerrors = arg_parse(argc, argv, (void **)&s_xxx_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_xxx_args.end, argv[0]);
        return 1;
    }
    
    // 2. 构建 API 参数
    cJSON *params = cJSON_CreateObject();
    if (s_xxx_args.device->count > 0) {
        cJSON_AddStringToObject(params, "device", s_xxx_args.device->sval[0]);
    }
    
    // 3. 调用 API
    ts_api_result_t result;
    ts_api_result_init(&result);
    
    esp_err_t ret = ts_api_call("xxx.action", params, &result);
    cJSON_Delete(params);
    
    // 4. 处理结果
    if (ret == ESP_OK && result.code == TS_API_OK) {
        // 格式化输出 result.data
        if (s_xxx_args.json->count > 0) {
            char *json_str = cJSON_Print(result.data);
            printf("%s\n", json_str);
            free(json_str);
        } else {
            // 人类可读格式
            print_xxx_result(result.data);
        }
    } else {
        printf("Error: %s\n", result.message);
    }
    
    ts_api_result_free(&result);
    return (result.code == TS_API_OK) ? 0 : 1;
}
```

### 4.2 关键原则

1. **CLI 只做参数解析和结果格式化**，不包含业务逻辑
2. **所有业务逻辑在 API 层实现**
3. **CLI 通过 `ts_api_call()` 调用 API**
4. **支持 `--json` 参数输出原始 JSON**

## 5. API 实现规范

### 5.1 标准模板

```c
static esp_err_t api_xxx_action(const cJSON *params, ts_api_result_t *result)
{
    // 1. 参数验证
    const cJSON *device = cJSON_GetObjectItem(params, "device");
    if (!cJSON_IsString(device)) {
        ts_api_result_error(result, TS_API_ERR_INVALID_ARG, 
                           "Missing required parameter: device");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 2. 执行业务逻辑
    esp_err_t ret = ts_xxx_do_action(device->valuestring);
    if (ret != ESP_OK) {
        ts_api_result_error(result, TS_API_ERR_HARDWARE,
                           "Operation failed");
        return ret;
    }
    
    // 3. 构建返回数据
    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "status", "success");
    cJSON_AddStringToObject(data, "device", device->valuestring);
    
    ts_api_result_ok(result, data);
    return ESP_OK;
}
```

### 5.2 注册 API

```c
static const ts_api_endpoint_t s_xxx_endpoints[] = {
    {
        .name = "xxx.action",
        .description = "Perform action on xxx",
        .category = TS_API_CAT_XXX,
        .handler = api_xxx_action,
        .requires_auth = false,
    },
    // ... more endpoints
};

esp_err_t ts_api_xxx_register(void)
{
    return ts_api_register_multiple(s_xxx_endpoints, 
                                    sizeof(s_xxx_endpoints) / sizeof(s_xxx_endpoints[0]));
}
```

## 6. API 模块列表

| 文件 | 模块 | API 数量 | 状态 |
|------|------|----------|------|
| ts_api_system.c | system | 5 | ✅ 已有 |
| ts_api_config.c | config | 5 | ✅ 已有 |
| ts_api_device.c | device | 8 | ✅ 已有 |
| ts_api_led.c | led | 10 | ✅ 已有 |
| ts_api_network.c | network | 10 | ✅ 已有 |
| ts_api_fan.c | fan | 5 | 🆕 待创建 |
| ts_api_power.c | power | 5 | 🆕 待创建 |
| ts_api_temp.c | temp | 4 | 🆕 待创建 |
| ts_api_storage.c | storage | 6 | 🆕 待创建 |
| ts_api_gpio.c | gpio | 4 | 🆕 待创建 |
| ts_api_service.c | service | 5 | 🆕 待创建 |
| ts_api_ssh.c | ssh | 6 | 🆕 待创建 |

## 7. WebUI 集成

WebUI 通过 HTTP/WebSocket 调用相同的 API：

```
HTTP POST /api/v1/call
Content-Type: application/json

{
  "api": "led.effect.start",
  "params": {
    "device": "matrix",
    "effect": "fire",
    "speed": 50
  }
}
```

响应：
```json
{
  "code": 0,
  "message": "OK",
  "data": {
    "device": "matrix",
    "effect": "fire",
    "running": true
  }
}
```
