# TianShanOS 统一配置系统设计

> 版本：1.0.0  
> 日期：2026-01-19  
> 状态：✅ **已实现**

---

## 1. 设计目标

- **统一入口**：所有模块配置通过 `ts_config` 管理
- **SD卡优先**：便于出厂预置和批量部署
- **双写同步**：持久化时同时写入 NVS 和 SD卡
- **模块隔离**：每个组件独立配置文件和 NVS 命名空间
- **版本迁移**：Schema 变更时自动迁移旧配置
- **显式持久化**：CLI 修改默认临时，需显式保存

---

## 2. 配置优先级

```
优先级（高→低）：
┌─────────────────────────────────────────────────────────────┐
│  1. 内存缓存（CLI/API 运行时修改，重启丢失）                   │
├─────────────────────────────────────────────────────────────┤
│  2. SD卡配置文件 /sdcard/config/{module}.json                │
│     - SD卡存在且配置文件存在时优先使用                         │
│     - 便于出厂预置、批量部署、故障恢复                         │
├─────────────────────────────────────────────────────────────┤
│  3. NVS 持久化存储（各模块独立命名空间）                       │
│     - SD卡不存在或配置文件不存在时使用                         │
│     - 热插拔期间写入时标记 "pending_sync"                     │
├─────────────────────────────────────────────────────────────┤
│  4. Schema 默认值（代码内定义）                               │
│     - 首次启动或配置重置时使用                                │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 存储布局

### 3.1 SD卡配置文件

```
/sdcard/config/
├── net.json          # 网络配置（以太网、IP设置）
├── dhcp.json         # DHCP 服务器配置
├── wifi.json         # WiFi AP/STA 配置
├── led.json          # LED 设备配置
├── fan.json          # 风扇配置
├── device.json       # 设备控制配置（AGX/LPMU/USB MUX）
└── system.json       # 系统配置（日志级别、时区等）
```

> **注意**：`ts_security`（密钥存储）不参与此配置系统，保持独立管理。

### 3.2 NVS 命名空间

| 命名空间 | 用途 | 导出到SD卡 |
|----------|------|-----------|
| `ts_meta` | 元配置（版本、同步状态） | ❌ 不导出 |
| `ts_net` | 网络配置 | ✅ |
| `ts_dhcp` | DHCP 配置 | ✅ |
| `ts_wifi` | WiFi 配置 | ✅ |
| `ts_led` | LED 配置 | ✅ |
| `ts_fan` | 风扇配置 | ✅ |
| `ts_device` | 设备控制配置 | ✅ |
| `ts_system` | 系统配置 | ✅ |
| `ts_keys` | 密钥存储 | ❌ 独立管理 |
| `ts_hosts` | SSH Known Hosts | ❌ 独立管理 |

### 3.3 元配置（ts_meta）

```c
// NVS 键值
"schema_version"    // uint16_t: 配置 Schema 版本号
"pending_sync"      // uint8_t:  位掩码，标记哪些模块需要同步到SD卡
"global_seq"        // uint32_t: 全局配置序列号（每次持久化时递增）
"sync_seq"          // uint32_t: 上次同步到SD卡时的序列号
```

> **无 RTC 设计**：系统不依赖绝对时间，使用单调递增的序列号判断配置新旧。

---

## 4. 配置文件格式

### 4.1 通用结构

```json
{
  "_meta": {
    "version": "1.0.0",
    "module": "net",
    "schema_version": 1,
    "seq": 42
  },
  "eth": {
    "mode": "static",
    "ip": "10.10.99.97",
    "netmask": "255.255.255.0",
    "gateway": "10.10.99.1",
    "dns": "8.8.8.8"
  }
}
```

> **seq 字段**：单调递增的序列号，用于判断配置版本新旧，不依赖 RTC。

### 4.2 各模块配置示例

<details>
<summary><b>net.json</b> - 网络配置</summary>

```json
{
  "_meta": { "version": "1.0.0", "module": "net", "schema_version": 1, "seq": 1 },
  "eth": {
    "mode": "static",
    "ip": "10.10.99.97",
    "netmask": "255.255.255.0",
    "gateway": "10.10.99.1",
    "dns": "8.8.8.8",
    "hostname": "TianShanOS"
  }
}
```
</details>

<details>
<summary><b>dhcp.json</b> - DHCP 服务器配置</summary>

```json
{
  "_meta": { "version": "1.0.0", "module": "dhcp", "schema_version": 1, "seq": 1 },
  "eth": {
    "enabled": true,
    "start_ip": "10.10.99.100",
    "end_ip": "10.10.99.200",
    "gateway": "10.10.99.97",
    "netmask": "255.255.255.0",
    "dns": "8.8.8.8",
    "lease_minutes": 60,
    "bindings": [
      { "mac": "aa:bb:cc:dd:ee:ff", "ip": "10.10.99.50", "hostname": "jetson-agx" }
    ]
  },
  "ap": {
    "enabled": true,
    "start_ip": "192.168.4.2",
    "end_ip": "192.168.4.10",
    "gateway": "192.168.4.1",
    "netmask": "255.255.255.0",
    "dns": "192.168.4.1",
    "lease_minutes": 60,
    "bindings": []
  }
}
```
</details>

<details>
<summary><b>led.json</b> - LED 配置</summary>

```json
{
  "_meta": { "version": "1.0.0", "module": "led", "schema_version": 1, "seq": 1 },
  "touch": {
    "brightness": 80,
    "boot_effect": "heartbeat",
    "boot_color": "#00FF00",
    "boot_speed": 50
  },
  "board": {
    "brightness": 60,
    "boot_effect": "rainbow",
    "boot_speed": 30
  },
  "matrix": {
    "brightness": 40,
    "boot_mode": "image",
    "boot_image": "/sdcard/images/logo.png",
    "boot_effect": null,
    "boot_filter": "breathing",
    "boot_filter_speed": 50
  }
}
```
</details>

<details>
<summary><b>wifi.json</b> - WiFi 配置</summary>

```json
{
  "_meta": { "version": "1.0.0", "module": "wifi", "schema_version": 1, "seq": 1 },
  "ap": {
    "enabled": true,
    "ssid": "TianShanOS",
    "password": "tianshan123",
    "channel": 1,
    "max_connections": 4
  },
  "sta": {
    "enabled": false,
    "ssid": "",
    "password": "",
    "auto_connect": true
  }
}
```
</details>

---

## 5. 核心流程

### 5.1 启动加载流程

```
系统启动
    │
    ▼
┌─────────────────────────────────────────┐
│ 1. 初始化 ts_config 核心                 │
│    - 读取 ts_meta:schema_version        │
│    - 注册各模块 Schema                   │
└─────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────┐
│ 2. 等待存储服务就绪                       │
│    - 监听 TS_EVT_STORAGE_SD_MOUNTED     │
└─────────────────────────────────────────┘
    │
    ├── SD卡挂载成功 ──────────────────────┐
    │                                      ▼
    │                    ┌─────────────────────────────────────┐
    │                    │ 3a. 检查 /sdcard/config/{module}.json │
    │                    └─────────────────────────────────────┘
    │                              │
    │               ┌──────────────┴──────────────┐
    │               ▼                             ▼
    │        配置文件存在                    配置文件不存在
    │               │                             │
    │               ▼                             ▼
    │        从 SD卡加载               检查 NVS 是否有配置
    │               │                             │
    │               │                    ┌────────┴────────┐
    │               │                    ▼                 ▼
    │               │              NVS 有配置         NVS 无配置
    │               │                    │                 │
    │               │                    ▼                 ▼
    │               │            从 NVS 加载         使用默认值
    │               │            并导出到 SD卡       并导出到 SD卡
    │               │                    │                 │
    │               └────────────────────┴─────────────────┘
    │                                    │
    │                                    ▼
    │                           应用配置到内存
    │
    └── SD卡未挂载 ────────────────────────┐
                                          ▼
                        ┌─────────────────────────────────────┐
                        │ 3b. 从 NVS 加载配置                  │
                        │     无 NVS 配置则使用默认值           │
                        └─────────────────────────────────────┘
                                          │
                                          ▼
                                  应用配置到内存
```

### 5.2 配置持久化流程

```
用户执行 --persist / --save
    │
    ▼
┌─────────────────────────────────────────┐
│ 1. 写入 NVS（组件命名空间）               │
└─────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────┐
│ 2. 检查 SD卡状态                         │
└─────────────────────────────────────────┘
    │
    ├── SD卡已挂载 ────────────────────────┐
    │                                      ▼
    │                    ┌─────────────────────────────────────┐
    │                    │ 3a. 递增 global_seq                  │
    │                    │     同时写入 SD卡配置文件（含新 seq）  │
    │                    └─────────────────────────────────────┘
    │                              │
    │                              ▼
    │                    ┌─────────────────────────────────────┐
    │                    │ 4a. 更新 sync_seq = global_seq      │
    │                    │     清除 pending_sync 标记           │
    │                    └─────────────────────────────────────┘
    │
    └── SD卡未挂载 ────────────────────────┐
                                          ▼
                        ┌─────────────────────────────────────┐
                        │ 3b. 设置 pending_sync 标记           │
                        │     ts_meta:pending_sync |= (1<<module) │
                        └─────────────────────────────────────┘
```

### 5.3 SD卡热插拔处理

```
SD卡插入事件 (TS_EVT_STORAGE_SD_MOUNTED)
    │
    ▼
┌─────────────────────────────────────────┐
│ 1. 读取 ts_meta:pending_sync            │
└─────────────────────────────────────────┘
    │
    ├── 有待同步模块 (pending_sync != 0) ──┐
    │                                      ▼
    │                    ┌─────────────────────────────────────┐
    │                    │ 2a. 遍历待同步模块                   │
    │                    │     - 从 NVS 读取配置               │
    │                    │     - 写入 SD卡配置文件              │
    │                    │     - 清除对应 pending_sync 位       │
    │                    └─────────────────────────────────────┘
    │
    └── 无待同步 ──────────────────────────┐
                                          ▼
                        ┌─────────────────────────────────────┐
                        │ 2b. 检查 SD卡配置是否比 NVS 新        │
                        │     - 比较 SD卡 _meta.seq 与 NVS seq │
                        │     - 如果 SD卡 seq 较大，重新加载    │
                        │       （说明 SD卡被外部修改过）        │
                        └─────────────────────────────────────┘
```

### 5.4 冲突处理逻辑

```
配置冲突判定（基于序列号）：
    │
    ▼
读取 NVS: global_seq, sync_seq, pending_sync
读取 SD卡: _meta.seq
    │
    ├── pending_sync 标记存在？
    │       │
    │       ├── YES ──► NVS 优先（SD卡拔出期间有新写入）
    │       │           - global_seq > sync_seq 说明有未同步的更新
    │       │           - 从 NVS 加载配置
    │       │           - 同步到 SD卡（更新 SD卡的 seq）
    │       │           - 更新 sync_seq = global_seq
    │       │           - 清除 pending_sync
    │       │
    │       └── NO ───► 比较序列号
    │                       │
    │                       ├── SD卡 seq > NVS global_seq
    │                       │       │
    │                       │       └──► SD卡被外部修改，SD卡优先
    │                       │           - 从 SD卡加载配置
    │                       │           - 更新 NVS（保持一致性）
    │                       │           - 更新 global_seq = SD卡 seq
    │                       │
    │                       └── SD卡 seq <= NVS global_seq
    │                               │
    │                               └──► 正常情况，SD卡优先
    │                                   - 从 SD卡加载配置
```

> **序列号设计**：
> - `global_seq`：每次持久化时递增，记录在 NVS
> - `sync_seq`：上次成功同步到 SD卡时的 seq 值
> - `_meta.seq`：写入 SD卡配置文件时的 seq 值
> - `pending_sync`：SD卡拔出期间写入 NVS 时设置的标记

---

## 6. Schema 版本迁移

### 6.1 迁移机制

每个模块的 Schema 有版本号，当版本升级时触发迁移：

```c
// 模块 Schema 定义
typedef struct {
    const char *key;
    ts_config_type_t type;
    union {
        bool        default_bool;
        int32_t     default_int;
        const char *default_str;
    };
    const char *description;
} ts_config_schema_entry_t;

typedef struct {
    uint16_t version;                           // Schema 版本
    const ts_config_schema_entry_t *entries;
    size_t entry_count;
    esp_err_t (*migrate)(uint16_t old_version); // 迁移函数
} ts_config_module_schema_t;
```

### 6.2 迁移示例

```c
// net 模块 Schema v1 → v2 迁移
static esp_err_t net_schema_migrate(uint16_t old_version)
{
    if (old_version < 2) {
        // v1 → v2: 新增 dns2 字段
        // 检查是否存在旧键 "eth.dns"
        char dns[16];
        if (ts_config_get_string(TS_CONFIG_MODULE_NET, "eth.dns", dns, sizeof(dns)) == ESP_OK) {
            // 保持原有 dns 为 dns1，新增 dns2 使用默认值
            ts_config_set_string(TS_CONFIG_MODULE_NET, "eth.dns1", dns);
            ts_config_set_string(TS_CONFIG_MODULE_NET, "eth.dns2", "8.8.4.4");
        }
    }
    return ESP_OK;
}

static const ts_config_module_schema_t s_net_schema = {
    .version = 2,
    .entries = (ts_config_schema_entry_t[]){
        { "eth.mode",    TS_CONFIG_TYPE_STRING, .default_str = "static" },
        { "eth.ip",      TS_CONFIG_TYPE_STRING, .default_str = "10.10.99.97" },
        { "eth.netmask", TS_CONFIG_TYPE_STRING, .default_str = "255.255.255.0" },
        { "eth.gateway", TS_CONFIG_TYPE_STRING, .default_str = "10.10.99.1" },
        { "eth.dns1",    TS_CONFIG_TYPE_STRING, .default_str = "8.8.8.8" },     // v2 新增
        { "eth.dns2",    TS_CONFIG_TYPE_STRING, .default_str = "8.8.4.4" },     // v2 新增
    },
    .entry_count = 6,
    .migrate = net_schema_migrate,
};
```

### 6.3 迁移流程

```
启动时检测版本
    │
    ▼
读取 NVS/SD卡中的 schema_version
    │
    ├── 版本匹配 ──► 正常加载
    │
    └── 版本不匹配 ──► 执行迁移
                          │
                          ▼
                    ┌─────────────────┐
                    │ 调用 migrate()  │
                    │ 逐版本升级      │
                    │ v1→v2→v3→...   │
                    └─────────────────┘
                          │
                          ▼
                    更新 schema_version
                    持久化新配置
```

---

## 7. API 设计

### 7.1 模块枚举

```c
typedef enum {
    TS_CONFIG_MODULE_NET = 0,   // 网络配置
    TS_CONFIG_MODULE_DHCP,      // DHCP 配置
    TS_CONFIG_MODULE_WIFI,      // WiFi 配置
    TS_CONFIG_MODULE_LED,       // LED 配置
    TS_CONFIG_MODULE_FAN,       // 风扇配置
    TS_CONFIG_MODULE_DEVICE,    // 设备控制配置
    TS_CONFIG_MODULE_SYSTEM,    // 系统配置
    TS_CONFIG_MODULE_MAX
} ts_config_module_t;
```

### 7.2 核心 API

```c
/* ============================================================================
 * 模块注册
 * ========================================================================== */

/**
 * @brief 注册配置模块
 */
esp_err_t ts_config_register_module(
    ts_config_module_t module,
    const char *nvs_namespace,
    const ts_config_module_schema_t *schema
);

/* ============================================================================
 * 配置读取（按优先级：内存 > SD卡 > NVS > 默认值）
 * ========================================================================== */

esp_err_t ts_config_get_bool(ts_config_module_t module, const char *key, bool *value);
esp_err_t ts_config_get_int(ts_config_module_t module, const char *key, int32_t *value);
esp_err_t ts_config_get_string(ts_config_module_t module, const char *key, char *buf, size_t len);

/* ============================================================================
 * 配置写入（仅写入内存缓存）
 * ========================================================================== */

esp_err_t ts_config_set_bool(ts_config_module_t module, const char *key, bool value);
esp_err_t ts_config_set_int(ts_config_module_t module, const char *key, int32_t value);
esp_err_t ts_config_set_string(ts_config_module_t module, const char *key, const char *value);

/* ============================================================================
 * 持久化（同时写入 NVS 和 SD卡）
 * ========================================================================== */

/**
 * @brief 持久化模块配置
 * @param module 模块ID，传入 TS_CONFIG_MODULE_MAX 表示全部模块
 */
esp_err_t ts_config_persist(ts_config_module_t module);

/* ============================================================================
 * 加载与同步
 * ========================================================================== */

/**
 * @brief 从存储加载配置（自动处理 SD/NVS 优先级）
 */
esp_err_t ts_config_load(ts_config_module_t module);

/**
 * @brief 强制从 SD卡加载（忽略 pending_sync）
 */
esp_err_t ts_config_load_from_sdcard(ts_config_module_t module);

/**
 * @brief 强制从 NVS 加载
 */
esp_err_t ts_config_load_from_nvs(ts_config_module_t module);

/**
 * @brief 导出配置到 SD卡
 */
esp_err_t ts_config_export_to_sdcard(ts_config_module_t module);

/**
 * @brief 同步待处理的配置（SD卡插入后调用）
 */
esp_err_t ts_config_sync_pending(void);

/* ============================================================================
 * 重置
 * ========================================================================== */

/**
 * @brief 重置配置到默认值
 * @param persist 是否同时清除 NVS 和 SD卡
 */
esp_err_t ts_config_reset(ts_config_module_t module, bool persist);

/* ============================================================================
 * 变更订阅
 * ========================================================================== */

typedef void (*ts_config_change_cb_t)(ts_config_module_t module, 
                                       const char *key, 
                                       void *user_data);

esp_err_t ts_config_subscribe(ts_config_module_t module, 
                               const char *key_pattern,
                               ts_config_change_cb_t callback, 
                               void *user_data);

/* ============================================================================
 * 查询
 * ========================================================================== */

/**
 * @brief 检查是否有待同步的配置
 */
bool ts_config_has_pending_sync(void);

/**
 * @brief 获取模块的 Schema 版本
 */
uint16_t ts_config_get_schema_version(ts_config_module_t module);

/**
 * @brief 获取当前全局序列号
 */
uint32_t ts_config_get_global_seq(void);

/**
 * @brief 获取模块在 SD卡中的序列号
 */
uint32_t ts_config_get_sdcard_seq(ts_config_module_t module);
```

---

## 8. CLI 命令设计

### 8.1 各模块命令保持不变

```bash
# 网络配置（临时）
net --set --ip 10.10.99.50 --netmask 255.255.255.0

# 网络配置（持久化 → NVS + SD卡）
net --set --ip 10.10.99.50 --persist
net --save                              # 等同于将当前配置持久化

# LED 配置
led --brightness --device matrix --value 80
led --save --device matrix              # 持久化

# DHCP 配置
dhcp --pool --iface eth --start-ip 10.10.99.100 --end-ip 10.10.99.200
dhcp --save

# 风扇配置
fan --set --id 0 --speed 75
fan --save
```

### 8.2 统一 config 命令增强

```bash
# 列出所有模块配置
config --list
config --list --module net

# 直接读写配置
config --get --module net --key eth.ip
config --set --module net --key eth.ip --value 10.10.99.50
config --set --module net --key eth.ip --value 10.10.99.50 --persist

# 导入导出
config --export                         # 导出所有模块到 SD卡
config --export --module net            # 导出单个模块
config --import                         # 从 SD卡导入所有
config --import --module led

# 同步状态
config --sync-status                    # 显示待同步模块
config --sync                           # 手动触发同步

# 重置
config --reset --module net             # 重置单个模块
config --reset --module net --persist   # 重置并清除存储
```

---

## 9. 事件定义

```c
// 已有事件（ts_event.h）
TS_EVENT_BASE_STORAGE
    TS_EVT_STORAGE_SD_MOUNTED
    TS_EVT_STORAGE_SD_UNMOUNTED
    TS_EVT_STORAGE_SPIFFS_MOUNTED
    TS_EVT_STORAGE_SPIFFS_UNMOUNTED

// 新增配置事件
TS_EVENT_BASE_CONFIG
    TS_EVT_CONFIG_LOADED          // 配置加载完成
    TS_EVT_CONFIG_CHANGED         // 配置变更（内存）
    TS_EVT_CONFIG_PERSISTED       // 配置持久化完成
    TS_EVT_CONFIG_SYNC_NEEDED     // 需要同步到 SD卡
    TS_EVT_CONFIG_SYNC_COMPLETE   // 同步完成
    TS_EVT_CONFIG_MIGRATED        // Schema 迁移完成
```

---

## 10. 组件迁移指南

### 10.1 迁移前（当前实现）

```c
// ts_net_manager.c - 直接操作 NVS
esp_err_t ts_net_manager_save_config(void) {
    nvs_handle_t handle;
    nvs_open("ts_net", NVS_READWRITE, &handle);
    nvs_set_str(handle, "eth_ip", s_state.eth_config.ip);
    nvs_set_str(handle, "eth_netmask", s_state.eth_config.netmask);
    // ... 逐个写入
    nvs_commit(handle);
    nvs_close(handle);
}

esp_err_t ts_net_manager_load_config(void) {
    nvs_handle_t handle;
    nvs_open("ts_net", NVS_READONLY, &handle);
    nvs_get_str(handle, "eth_ip", s_state.eth_config.ip, ...);
    // ... 逐个读取
    nvs_close(handle);
}
```

### 10.2 迁移后（使用统一配置）

```c
// ts_net_config.c - 配置 Schema 定义
static const ts_config_module_schema_t s_net_schema = {
    .version = 1,
    .entries = (ts_config_schema_entry_t[]){
        { "eth.mode",    TS_CONFIG_TYPE_STRING, .default_str = "static" },
        { "eth.ip",      TS_CONFIG_TYPE_STRING, .default_str = "10.10.99.97" },
        { "eth.netmask", TS_CONFIG_TYPE_STRING, .default_str = "255.255.255.0" },
        { "eth.gateway", TS_CONFIG_TYPE_STRING, .default_str = "10.10.99.1" },
        { "eth.dns",     TS_CONFIG_TYPE_STRING, .default_str = "8.8.8.8" },
        { "eth.hostname",TS_CONFIG_TYPE_STRING, .default_str = "TianShanOS" },
    },
    .entry_count = 6,
    .migrate = NULL,  // v1 无需迁移
};

// ts_net_manager.c - 使用统一配置 API
esp_err_t ts_net_manager_init(void) {
    // 注册配置模块
    ts_config_register_module(TS_CONFIG_MODULE_NET, "ts_net", &s_net_schema);
    
    // 加载配置（自动处理 SD/NVS 优先级）
    ts_config_load(TS_CONFIG_MODULE_NET);
    
    // 订阅变更
    ts_config_subscribe(TS_CONFIG_MODULE_NET, "eth.*", on_eth_config_changed, NULL);
    
    // 读取配置应用
    apply_eth_config();
    return ESP_OK;
}

static void apply_eth_config(void) {
    char ip[16], netmask[16], gateway[16];
    ts_config_get_string(TS_CONFIG_MODULE_NET, "eth.ip", ip, sizeof(ip));
    ts_config_get_string(TS_CONFIG_MODULE_NET, "eth.netmask", netmask, sizeof(netmask));
    ts_config_get_string(TS_CONFIG_MODULE_NET, "eth.gateway", gateway, sizeof(gateway));
    // 应用到网络栈...
}

esp_err_t ts_net_manager_save_config(void) {
    // 一行搞定，自动写入 NVS + SD卡
    return ts_config_persist(TS_CONFIG_MODULE_NET);
}
```

---

## 11. 实施计划

| 阶段 | 任务 | 预估 | 优先级 |
|------|------|------|--------|
| **Phase 1** | 重构 ts_config 核心框架 | 2-3 天 | P0 |
| | - 模块注册机制 | | |
| | - SD/NVS 优先级逻辑 | | |
| | - 双写同步机制 | | |
| | - pending_sync 标记 | | |
| | - Schema 版本迁移框架 | | |
| **Phase 2** | 迁移 ts_net 模块（示范） | 1 天 | P0 |
| **Phase 3** | 迁移其他模块 | 2-3 天 | P1 |
| | - ts_dhcp | | |
| | - ts_wifi | | |
| | - ts_led | | |
| | - ts_fan | | |
| **Phase 4** | 更新 CLI 命令 | 1 天 | P1 |
| **Phase 5** | 测试与文档 | 1 天 | P1 |

---

## 12. 排除范围

以下模块**不参与**统一配置系统：

| 模块 | 原因 |
|------|------|
| `ts_security` / `ts_keys` | 安全性要求完全不同，需要独立的加密存储和访问控制 |
| `ts_hosts` | SSH Known Hosts 指纹，属于安全凭据 |
| `ts_pin_manager` | 引脚配置来自 `pins.json`，是编译时/板级配置，非运行时配置 |

---

## 附录 A：JSON 序列化规范

```c
// 类型映射
TS_CONFIG_TYPE_BOOL   → JSON boolean
TS_CONFIG_TYPE_INT8   → JSON number
TS_CONFIG_TYPE_INT32  → JSON number
TS_CONFIG_TYPE_UINT32 → JSON number
TS_CONFIG_TYPE_STRING → JSON string
TS_CONFIG_TYPE_BLOB   → JSON string (Base64 编码)

// 数组处理
"bindings": [...]     → 特殊处理，需要模块自定义序列化
```

## 附录 B：错误码

```c
#define TS_CONFIG_ERR_BASE              0x10000

#define TS_CONFIG_ERR_NOT_FOUND         (TS_CONFIG_ERR_BASE + 1)   // 配置不存在
#define TS_CONFIG_ERR_TYPE_MISMATCH     (TS_CONFIG_ERR_BASE + 2)   // 类型不匹配
#define TS_CONFIG_ERR_BUFFER_TOO_SMALL  (TS_CONFIG_ERR_BASE + 3)   // 缓冲区太小
#define TS_CONFIG_ERR_SD_NOT_MOUNTED    (TS_CONFIG_ERR_BASE + 4)   // SD卡未挂载
#define TS_CONFIG_ERR_PARSE_FAILED      (TS_CONFIG_ERR_BASE + 5)   // JSON 解析失败
#define TS_CONFIG_ERR_SCHEMA_MISMATCH   (TS_CONFIG_ERR_BASE + 6)   // Schema 版本不兼容
#define TS_CONFIG_ERR_MIGRATE_FAILED    (TS_CONFIG_ERR_BASE + 7)   // 迁移失败
```
