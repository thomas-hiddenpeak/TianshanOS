# TianShanOS PKI 设计文档

> **文档版本**: 1.2  
> **创建日期**: 2026-01-24  
> **更新日期**: 2026-01-25  
> **状态**: Phase 2 完成 - ESP32 CSR 模块已实现

本文档记录 TianShanOS 的 PKI（公钥基础设施）设计，包括 mTLS 双向认证、证书签发流程、设备激活机制等所有安全相关内容。

---

## 目录

1. [设计目标](#1-设计目标)
2. [整体架构](#2-整体架构)
3. [CA 层级结构](#3-ca-层级结构)
4. [证书类型与策略](#4-证书类型与策略)
5. [ESP32 能力评估](#5-esp32-能力评估)
6. [设备激活流程](#6-设备激活流程)
7. [证书更新机制](#7-证书更新机制)
8. [mTLS 权限模型](#8-mtls-权限模型)
9. [实施计划](#9-实施计划)
10. [技术细节](#10-技术细节)
11. [安全注意事项](#11-安全注意事项)

---

## 1. 设计目标

### 1.1 核心需求

- **设备身份认证**: 每个 ESP32 设备拥有唯一的证书身份
- **用户身份认证**: 管理员/操作员通过客户端证书访问设备
- **双向认证 (mTLS)**: 设备和用户互相验证身份
- **基于角色的访问控制**: 通过证书 OU 字段区分权限级别
- **离线友好**: 设备主要在离线环境运行，支持 TF 卡更新证书

### 1.2 安全原则

- **CA 私钥绝不存储在 ESP32 上**: 设备只能生成 CSR，不能自签证书
- **私钥不离开设备**: 设备私钥在本地生成，只导出 CSR（公钥部分）
- **最小权限原则**: 不同角色拥有不同的 API 访问权限

### 1.3 部署场景

| 场景 | 说明 |
|------|------|
| 初次激活 | 设备联网，自动生成 CSR 并获取证书 |
| 日常运行 | **离线运行**，使用本地证书进行 mTLS |
| 证书更新 | TF 卡导入 或 临时联网自动续期 |

---

## 2. 整体架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           TianShanOS PKI 架构                                │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│    ┌─────────────┐                          ┌─────────────────────────┐    │
│    │   Root CA   │ ◄── 离线存储（保险箱）     │      管理员电脑          │    │
│    │  20年有效期  │                          │  ┌─────────────────┐    │    │
│    └──────┬──────┘                          │  │ 用户证书 (P12)  │    │    │
│           │ 签发                             │  │ admin/operator  │    │    │
│           ▼                                  │  └────────┬────────┘    │    │
│    ┌─────────────┐                          └───────────┼─────────────┘    │
│    │Intermediate │ ◄── step-ca 服务器                   │                   │
│    │     CA      │     在线签发证书                      │ mTLS 连接         │
│    │  10年有效期  │                                      │                   │
│    └──────┬──────┘                                      │                   │
│           │                                              │                   │
│           │ 签发                                         ▼                   │
│           │                              ┌─────────────────────────────┐    │
│           ▼                              │        ESP32 设备            │    │
│    ┌─────────────┐                       │  ┌───────────────────────┐  │    │
│    │ 设备证书    │ ──────────────────────│─►│ HTTPS Server (mTLS)  │  │    │
│    │ 10年有效期  │                       │  │ 验证用户证书          │  │    │
│    └─────────────┘                       │  │ 检查 OU 权限          │  │    │
│                                          │  └───────────────────────┘  │    │
│                                          │                             │    │
│                                          │  私钥 + 设备证书 存储在 NVS  │    │
│                                          └─────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 核心流程

```
用户请求 → TLS 握手 → 双向证书验证 → 提取用户角色 (OU) → 检查 API 权限 → 执行/拒绝
```

---

## 3. CA 层级结构

### 3.1 两级 CA 设计

| CA 级别 | 作用 | 存储位置 | 有效期 | 说明 |
|---------|------|----------|--------|------|
| **Root CA** | 信任锚点 | 离线（硬件保险箱/HSM） | 20年 | 仅用于签发 Intermediate CA |
| **Intermediate CA** | 在线签发 | step-ca 服务器 | 10年 | 签发所有终端证书 |

### 3.2 为什么需要两级 CA

1. **Root CA 离线保护**: Root 私钥泄露 = 整个 PKI 失效，必须物理隔离
2. **Intermediate CA 可更换**: 如果 Intermediate 被攻破，可用 Root 签发新的
3. **限制爆炸半径**: Intermediate 证书可以设置路径长度约束

### 3.3 证书链

```
Root CA Certificate
    └── Intermediate CA Certificate
            ├── Device Certificate (ESP32)
            ├── User Certificate (Admin)
            ├── User Certificate (Operator)
            └── Service Certificate (API Server)
```

设备和用户都信任同一个 Intermediate CA，因此可以互相验证。

---

## 4. 证书类型与策略

### 4.1 证书类型总览

| 证书类型 | 用途 | 有效期 | 签发者 | 存储位置 |
|----------|------|--------|--------|----------|
| **设备证书** | ESP32 设备身份 | 10年 | Intermediate CA | ESP32 NVS |
| **用户证书** | 管理员/操作员身份 | 1-5年 | Intermediate CA | 用户电脑/手机 |
| **服务证书** | API 服务器等 | 1-2年 | Intermediate CA | 服务器 |

### 4.2 证书独立性

**关键结论**: 设备证书和用户证书**完全独立**，各自更新互不影响。

- 设备证书过期 → 只需更新设备证书，用户证书不变
- 用户离职/换人 → 只需吊销/签发用户证书，设备证书不变
- 唯一约束：都由同一个 Intermediate CA 签发

### 4.3 设备证书模板

```json
{
  "subject": {
    "commonName": "TIANSHAN-{{ .DeviceSerial }}",
    "organization": ["TianShanOS"],
    "organizationalUnit": ["device"]
  },
  "keyUsage": ["digitalSignature", "keyEncipherment"],
  "extKeyUsage": ["serverAuth", "clientAuth"],
  "validity": "87600h",
  "san": {
    "ips": ["{{ .DeviceIP }}"],
    "dns": ["{{ .DeviceSerial }}.local"]
  }
}
```

### 4.4 用户证书模板

```json
{
  "subject": {
    "commonName": "{{ .UserName }}",
    "organization": ["TianShanOS"],
    "organizationalUnit": ["{{ .Role }}"]
  },
  "keyUsage": ["digitalSignature"],
  "extKeyUsage": ["clientAuth"],
  "validity": "43800h"
}
```

### 4.5 角色定义 (OU 字段)

| OU 值 | 角色 | 权限 |
|-------|------|------|
| `admin` | 管理员 | 完全控制：配置、电源、证书管理 |
| `operator` | 操作员 | 日常操作：电源控制、状态查看 |
| `viewer` | 观察者 | 只读：状态查看 |
| `device` | 设备 | 设备身份（非人类用户） |

---

## 5. ESP32 能力评估

### 5.1 评估结论

**ESP32-S3 完全支持该 PKI 架构，无技术障碍。**

### 5.2 功能支持状态

| 功能 | 状态 | 说明 |
|------|------|------|
| CSR 生成 | ✅ 支持 | `MBEDTLS_X509_CSR_WRITE_C` 默认启用 |
| SAN 扩展 | ✅ 支持 | `mbedtls_x509write_csr_set_subject_alternative_name()` |
| PEM 格式 | ✅ 支持 | `CONFIG_MBEDTLS_PEM_WRITE_C=y` |
| ECDSA | ✅ 支持 | `CONFIG_MBEDTLS_ECDSA_C=y` |
| mTLS 服务器 | ✅ 支持 | `httpd_ssl_config.cacert_pem` |
| 硬件加速 | ✅ 支持 | SHA/AES/MPI 硬件加速 |

### 5.3 已知限制与对策

| 限制 | 影响 | 对策 |
|------|------|------|
| 每 SSL 连接 ~40KB | 并发受限 | `max_open_sockets=2-3`，使用 PSRAM |
| RSA-2048 生成 3-5秒 | 激活耗时 | **改用 ECDSA P-256（1-2秒）** |
| DRAM 碎片 | 大分配失败 | `MALLOC_CAP_SPIRAM` 策略 |

### 5.4 推荐密钥类型

| 类型 | 生成时间 | 证书大小 | 推荐 |
|------|---------|---------|------|
| **ECDSA P-256** | 1-2秒 | ~1KB | ✅ **首选** |
| RSA-2048 | 3-5秒 | ~2KB | ⚠️ 较慢 |
| RSA-4096 | 15-30秒 | ~4KB | ❌ 过慢 |

### 5.5 当前 sdkconfig 配置（已优化）

```kconfig
# 已启用的关键配置
CONFIG_MBEDTLS_PEM_WRITE_C=y
CONFIG_MBEDTLS_ECDSA_C=y
CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y

# mbedTLS 默认启用（无需配置）
# MBEDTLS_X509_CSR_WRITE_C
# MBEDTLS_X509_CREATE_C
# MBEDTLS_PK_WRITE_C
```

---

## 6. 设备激活流程

### 6.1 激活状态机

```
┌──────────────┐
│   FACTORY    │ ← 出厂状态，无证书
└──────┬───────┘
       │ 首次启动 + 联网
       ▼
┌──────────────┐
│  GENERATING  │ ← 生成 ECDSA 密钥对 + CSR
└──────┬───────┘
       │ 生成完成
       ▼
┌──────────────┐
│   PENDING    │ ← CSR 已提交，等待 CA 签发
└──────┬───────┘
       │ 收到证书
       ▼
┌──────────────┐
│  ACTIVATED   │ ← 正常运行
└──────────────┘
```

### 6.2 激活方式

| 方式 | 流程 | 适用场景 |
|------|------|---------|
| **自动激活** | ESP32 直连 CA 服务器 API | 生产环境 |
| **手动激活** | CLI 导出 CSR → 手动签名 → 导入 | 开发调试 |
| **工厂预置** | 烧录时注入证书 | 批量生产 |

### 6.3 自动激活流程（伪代码）

```c
void ts_pki_activation_task(void *arg) {
    if (ts_pki_is_activated()) {
        return;  // 已激活
    }
    
    // 1. 生成密钥对 + CSR
    ts_pki_csr_params_t params = {
        .device_id = get_device_serial(),
        .org_unit = "device",
        .ip_addr = get_current_ip(),
    };
    ts_pki_csr_result_t result;
    ts_pki_generate_csr(&params, &result);
    
    // 2. 立即存储私钥（避免丢失）
    ts_pki_store_private_key(result.private_key, result.key_len);
    
    // 3. 提交 CSR 到 CA 服务器
    char *cert = NULL;
    esp_err_t ret = ts_pki_submit_csr_to_ca(result.csr, &cert);
    
    // 4. 存储证书
    if (ret == ESP_OK) {
        ts_pki_store_certificate(cert, strlen(cert));
        ts_event_post(TS_EVENT_PKI, TS_EVENT_PKI_ACTIVATED, ...);
    }
}
```

---

## 7. 证书更新机制

### 7.1 部署场景约束

**关键约束**: 设备**主要离线运行**，但支持：
- TF 卡更新证书（主要方式）
- 临时联网自动续期（可选）

### 7.2 更新渠道

| 渠道 | 场景 | 优先级 |
|------|------|--------|
| **TF 卡** | 用户将证书文件放入 SD 卡 | ⭐ 主要 |
| **临时联网** | 设备短暂接入网络 | ⭐ 可选 |
| **CLI 串口** | 开发调试 / 紧急恢复 | 备用 |

### 7.3 TF 卡目录结构

```
/sdcard/
└── tianshan/
    └── certs/
        ├── update/              # 放入新证书，设备自动导入
        │   ├── device.crt       # 新设备证书
        │   └── ca_chain.crt     # CA 链（可选）
        │
        ├── export/              # 设备导出的 CSR
        │   └── device.csr
        │
        └── backup/              # 自动备份旧证书
            └── device_20260124.crt
```

### 7.4 更新流程

```
设备启动 / 定期检查
        │
        ▼
┌─────────────────┐
│ 检查证书状态     │
│ 剩余 < 365天?   │
└────────┬────────┘
         │ 是
         ▼
┌─────────────────┐     找到文件
│ 检测 TF 卡      │ ──────────────→ 验证并导入
│ /certs/update/  │
└────────┬────────┘
         │ 无文件
         ▼
┌─────────────────┐     有网络
│ 检测网络连接    │ ──────────────→ 自动续期
└────────┬────────┘
         │ 无网络
         ▼
┌─────────────────┐
│ LED 告警 + CLI  │ ← 提醒用户
└─────────────────┘
```

### 7.5 CLI 命令

```bash
# 证书状态
pki --status

# TF 卡操作
pki --export-csr           # 导出 CSR 到 TF 卡
pki --import-cert          # 从 TF 卡导入证书

# 临时联网续期
pki --renew-online         # 手动触发在线续期

# 高级操作
pki --rotate-key           # 密钥轮换（生成新密钥对）
pki --show-cert            # 显示证书详情
```

### 7.6 过期告警策略

| 剩余时间 | 告警级别 | 行为 |
|----------|---------|------|
| < 365天 | 警告 | LED 黄色闪烁 + CLI 提示 |
| 已过期 | 严重 | LED 红色常亮 + **允许继续运行**（宽限期） |

**宽限期设计**: 离线场景下，证书过期不应立即停止服务，给用户时间更新。

---

## 8. mTLS 权限模型

### 8.1 验证流程

```
Client        TLS Handshake       ESP32 Server
  │                                    │
  │──── ClientHello ──────────────────→│
  │←─── ServerHello + Server Cert ─────│
  │──── Client Certificate ───────────→│
  │                                    │
  │     ┌────────────────────────────┐ │
  │     │ 1. 验证证书链（CA 签发）    │ │
  │     │ 2. 检查证书有效期          │ │
  │     │ 3. 提取 OU 字段（角色）    │ │
  │     │ 4. 检查 API 权限          │ │
  │     └────────────────────────────┘ │
  │                                    │
  │←─── 200 OK / 403 Forbidden ────────│
```

### 8.2 角色权限矩阵

| API 路径 | 方法 | viewer | operator | admin |
|----------|------|--------|----------|-------|
| `/api/system/info` | GET | ✅ | ✅ | ✅ |
| `/api/system/memory` | GET | ✅ | ✅ | ✅ |
| `/api/device/power` | POST | ❌ | ✅ | ✅ |
| `/api/device/reset` | POST | ❌ | ✅ | ✅ |
| `/api/config/*` | GET | ✅ | ✅ | ✅ |
| `/api/config/*` | PUT | ❌ | ❌ | ✅ |
| `/api/pki/status` | GET | ✅ | ✅ | ✅ |
| `/api/pki/renew` | POST | ❌ | ❌ | ✅ |
| `/api/service/*` | POST | ❌ | ❌ | ✅ |

### 8.3 代码实现（伪代码）

```c
typedef enum {
    TS_ROLE_VIEWER   = 0,
    TS_ROLE_OPERATOR = 1,
    TS_ROLE_ADMIN    = 2,
} ts_role_t;

// 从证书提取角色
ts_role_t ts_pki_get_role_from_cert(mbedtls_x509_crt *cert) {
    char ou[32];
    // 提取 OU 字段
    mbedtls_x509_dn_gets(ou, sizeof(ou), &cert->subject);
    
    if (strstr(ou, "OU=admin")) return TS_ROLE_ADMIN;
    if (strstr(ou, "OU=operator")) return TS_ROLE_OPERATOR;
    return TS_ROLE_VIEWER;
}

// 权限检查
bool ts_api_check_permission(const char *path, http_method_t method, ts_role_t role) {
    // 查找权限表...
}
```

---

## 9. 实施计划

### 9.1 阶段总览

```
Phase 1          Phase 2          Phase 3          Phase 4          Phase 5
PKI基础设施 ────→ ESP32 CSR ────→ 设备激活 ────→ mTLS服务 ────→ 生命周期
(服务器端)        (固件端)         (首次启动)      (运行时)        (维护)

┌─────────┐      ┌─────────┐      ┌─────────┐    ┌─────────┐    ┌─────────┐
│ step-ca │      │ ts_pki  │      │ ts_pki  │    │ts_https │    │ ts_pki  │
│ 服务器  │      │ 密钥生成│      │ 激活流程│    │ mTLS   │    │ TF卡更新│
│ 证书策略│      │ CSR创建 │      │ 证书存储│    │ 权限验证│    │ 联网续期│
└─────────┘      └─────────┘      └─────────┘    └─────────┘    └─────────┘

预计: 1天         预计: 2天         预计: 1天      预计: 2天       预计: 1天
```

### 9.2 Phase 1: PKI 基础设施

| 任务 | 说明 | 输出 |
|------|------|------|
| 1.1 安装 step-ca | Docker 或本地 | CA 服务 |
| 1.2 创建 Root CA | 离线生成 | `root_ca.crt` |
| 1.3 创建 Intermediate CA | 在线服务 | `intermediate_ca.crt` |
| 1.4 配置证书模板 | 设备/用户模板 | `device.tpl`, `user.tpl` |
| 1.5 配置 API | CSR 提交接口 | HTTPS 端点 |

### 9.3 Phase 2: ESP32 CSR 模块

**组件结构**:
```
components/ts_pki/
├── CMakeLists.txt
├── Kconfig
├── include/
│   ├── ts_pki.h
│   └── ts_pki_types.h
└── src/
    ├── ts_pki_keygen.c      # 密钥生成
    ├── ts_pki_csr.c         # CSR 创建
    └── ts_pki_storage.c     # 证书存储
```

**核心 API**:
```c
// 生成密钥对 + CSR
esp_err_t ts_pki_generate_csr(const ts_pki_csr_params_t *params, 
                               ts_pki_csr_result_t *result);

// 存储私钥/证书
esp_err_t ts_pki_store_private_key(const uint8_t *key, size_t len);
esp_err_t ts_pki_store_certificate(const uint8_t *cert, size_t len);

// 加载凭证
esp_err_t ts_pki_load_credentials(ts_pki_credentials_t *creds);

// 状态检查
bool ts_pki_is_activated(void);
int ts_pki_get_days_until_expiry(void);
```

### 9.4 Phase 3: 设备激活流程

| 任务 | 说明 |
|------|------|
| 3.1 激活状态机 | FACTORY → GENERATING → PENDING → ACTIVATED |
| 3.2 首次启动检测 | 检查 NVS 中是否有证书 |
| 3.3 CSR 提交 | HTTPS POST 到 CA 服务器 |
| 3.4 证书存储 | 存入 NVS，加密保护 |

### 9.5 Phase 4: mTLS HTTPS 服务器

| 任务 | 说明 |
|------|------|
| 4.1 加载设备证书 | 从 NVS 加载私钥和证书 |
| 4.2 配置客户端验证 | `cacert_pem` = Intermediate CA |
| 4.3 角色提取 | 解析证书 OU 字段 |
| 4.4 权限检查中间件 | 每个请求检查权限 |

### 9.6 Phase 5: 证书生命周期

| 任务 | 说明 |
|------|------|
| 5.1 TF 卡检测 | 启动时扫描 `/sdcard/tianshan/certs/update/` |
| 5.2 证书导入 | 验证链 → 备份旧证书 → 导入新证书 |
| 5.3 临时联网续期 | 检测网络 → 生成 CSR → 获取新证书 |
| 5.4 过期告警 | LED + CLI + 事件 |
| 5.5 CLI 命令 | `pki --status/--export-csr/--import-cert/--renew-online` |

### 9.7 建议开发顺序

1. **Phase 2** (ESP32 CSR) - 固件端核心，可用自签名证书测试
2. **Phase 1** (step-ca) - 搭建 CA 服务器
3. **Phase 3** (激活流程) - 串联前两个阶段
4. **Phase 4** (mTLS) - 验证整体流程
5. **Phase 5** (生命周期) - 完善更新机制

---

## 10. 技术细节

### 10.1 密钥生成代码示例

```c
#include "mbedtls/pk.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"

esp_err_t generate_ecdsa_keypair(mbedtls_pk_context *key) {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_pk_init(key);
    
    // 初始化随机数生成器
    mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                          (const unsigned char *)"TianShanOS", 10);
    
    // 设置密钥类型为 ECDSA
    mbedtls_pk_setup(key, mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY));
    
    // 生成 P-256 密钥对
    mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, 
                        mbedtls_pk_ec(*key),
                        mbedtls_ctr_drbg_random, &ctr_drbg);
    
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    
    return ESP_OK;
}
```

### 10.2 CSR 生成代码示例

```c
#include "mbedtls/x509_csr.h"

esp_err_t generate_csr(mbedtls_pk_context *key, 
                       const char *subject,
                       uint32_t ip_addr,
                       unsigned char *csr_buf, 
                       size_t buf_size) {
    mbedtls_x509write_csr csr;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_x509write_csr_init(&csr);
    
    // 设置 Subject
    mbedtls_x509write_csr_set_subject_name(&csr, subject);
    // 例如: "CN=TIANSHAN-001,O=TianShanOS,OU=device"
    
    // 设置密钥
    mbedtls_x509write_csr_set_key(&csr, key);
    
    // 设置签名算法
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    
    // 添加 SAN 扩展（IP 地址）
    mbedtls_x509_san_list san_ip = {
        .node = {
            .type = MBEDTLS_X509_SAN_IP_ADDRESS,
            .san = { .unstructured_name = { .p = (unsigned char*)&ip_addr, .len = 4 } }
        },
        .next = NULL
    };
    mbedtls_x509write_csr_set_subject_alternative_name(&csr, &san_ip);
    
    // 导出 PEM 格式
    int ret = mbedtls_x509write_csr_pem(&csr, csr_buf, buf_size,
                                         mbedtls_ctr_drbg_random, &ctr_drbg);
    
    mbedtls_x509write_csr_free(&csr);
    
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}
```

### 10.3 mTLS 服务器配置

```c
#include "esp_https_server.h"

esp_err_t start_https_server(void) {
    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    
    // 服务器证书和私钥
    extern const uint8_t server_cert_pem[];
    extern const uint8_t server_key_pem[];
    config.servercert = server_cert_pem;
    config.servercert_len = strlen((char*)server_cert_pem) + 1;
    config.prvtkey_pem = server_key_pem;
    config.prvtkey_len = strlen((char*)server_key_pem) + 1;
    
    // 启用客户端证书验证 (mTLS)
    extern const uint8_t ca_cert_pem[];  // Intermediate CA
    config.cacert_pem = ca_cert_pem;
    config.cacert_len = strlen((char*)ca_cert_pem) + 1;
    
    httpd_handle_t server = NULL;
    return httpd_ssl_start(&server, &config);
}
```

### 10.4 NVS 存储键名

| 键名 | 内容 | 说明 |
|------|------|------|
| `pki_privkey` | 设备私钥 (PEM) | 加密存储 |
| `pki_cert` | 设备证书 (PEM) | 明文存储 |
| `pki_ca_chain` | CA 证书链 (PEM) | 用于验证用户证书 |
| `pki_status` | 激活状态 | 0=未激活, 1=已激活 |

---

## 11. 安全注意事项

### 11.1 必须遵守

| 规则 | 原因 |
|------|------|
| **Root CA 私钥离线存储** | 泄露 = 整个 PKI 失效 |
| **设备私钥不离开 ESP32** | 只导出 CSR（公钥） |
| **使用 ECDSA P-256** | 比 RSA 更快更安全 |
| **启用 Flash 加密** | 保护 NVS 中的私钥 |

### 11.2 建议实践

| 实践 | 说明 |
|------|------|
| 定期审计证书 | 检查签发记录 |
| 监控异常登录 | 记录失败的 mTLS 握手 |
| 备份 CA | Root CA 多份离线备份 |
| 证书吊销计划 | 即使离线也要有应急方案 |

### 11.3 离线场景特殊考虑

| 问题 | 解决方案 |
|------|----------|
| 无法检查吊销 | 本地吊销列表 或 信任证书直到过期 |
| 时间不准确 | RTC 电池 + 宽松时间窗口 |
| 证书过期 | 宽限期 + LED 告警 + TF 卡更新 |

---

## 附录

### A. 相关文件

| 文件 | 说明 |
|------|------|
| `components/ts_pki/` | PKI 组件（待创建） |
| `components/ts_https/` | HTTPS 服务器（待创建） |
| `boards/*/certs/` | 板级证书配置 |

### B. 参考资料

- [ESP-IDF mbedTLS 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/mbedtls.html)
- [step-ca 文档](https://smallstep.com/docs/step-ca)
- [X.509 CSR RFC 2986](https://tools.ietf.org/html/rfc2986)

### C. 修订历史

| 版本 | 日期 | 变更 |
|------|------|------|
| 1.0 | 2026-01-24 | 初始版本，完成设计确认 |
| 1.1 | 2026-01-24 | Phase 1 完成，PKI 基础设施搭建 |
| 1.2 | 2026-01-25 | Phase 2 完成，ESP32 CSR 模块实现并验证 |

---

## 12. 实施记录

### 12.1 Phase 1: PKI 基础设施（已完成 ✅）

**完成日期**: 2026-01-24

#### 已安装工具

| 工具 | 版本 | 用途 |
|------|------|------|
| step-cli | 0.25.2 | 证书操作命令行工具 |
| step-ca | 0.25.2 | 证书颁发机构服务器 |

#### CA 层级

```
TianShanOS Root CA (ECDSA P-256, 20年)
  ├── fingerprint: ae5d4fe5cb45f725f4e8e26178ce583dfe5506e2f2554baf42ae4027fd573140
  └── TianShanOS Intermediate CA (ECDSA P-256, 10年)
        └── 签发设备证书和用户证书
```

#### 服务器配置

| 配置项 | 值 |
|--------|-----|
| 监听端口 | :8443 |
| DNS 名称 | 10.10.99.100, 192.168.0.152, localhost |
| 默认证书有效期 | 1 年 |
| 最大证书有效期 | 10 年 |
| Provisioner | admin (JWK) |

#### 目录结构

```
~/tianshan-pki/
├── root-ca/
│   ├── root_ca.crt              # Root CA 证书
│   └── root_ca.key              # Root CA 私钥（密码: tianshan-root-ca-2026）
├── intermediate-ca/
│   ├── intermediate_ca.crt      # Intermediate CA 证书
│   └── intermediate_ca.key      # Intermediate CA 私钥
├── step-ca/
│   ├── config/ca.json           # CA 服务配置
│   ├── certs/                   # CA 证书副本
│   ├── secrets/                 # 密钥和密码
│   ├── templates/               # 证书模板
│   │   ├── device.tpl           # 设备证书模板
│   │   └── user.tpl             # 用户证书模板
│   └── db/                      # BadgerDB 数据库
└── certs/
    ├── ca_chain.crt             # CA 证书链（ESP32 使用）
    ├── device_test.csr          # 测试 CSR
    ├── device_test.key          # 测试私钥
    └── device_test.crt          # 测试证书 ✅
```

#### 验证结果

```bash
# 证书签发测试
$ step ca sign device_test.csr device_test.crt --not-after 87600h
✔ Certificate: device_test.crt

# 证书验证
$ step certificate verify device_test.crt --roots ca_chain.crt
✅ Certificate chain verification PASSED

# 证书信息
Subject: CN=TIANSHAN-TEST-001
Issuer: CN=TianShanOS Intermediate CA
Validity: 2026-01-24 ~ 2036-01-22 (10年)
SAN: IP Address:10.10.99.97
Key: ECDSA P-256
```

#### 密码清单（生产环境请更换）

| 用途 | 密码 |
|------|------|
| Root CA 私钥 | tianshan-root-ca-2026 |
| Intermediate CA 私钥 | tianshan-intermediate-2026 |
| step-ca 服务 | tianshan-ca-server-2026 |
| admin provisioner | tianshan-admin-2026 |

#### 启动 CA 服务命令

```bash
# 启动 step-ca（后台运行）
STEPPATH=~/tianshan-pki/step-ca nohup step-ca \
  --password-file ~/tianshan-pki/step-ca/secrets/password.txt \
  > ~/tianshan-pki/step-ca/ca.log 2>&1 &

# 检查健康状态
curl -k https://localhost:8443/health
# 输出: {"status":"ok"}
```

### 12.2 Phase 2: ESP32 CSR 模块（已完成 ✅）

**完成日期**: 2026-01-25

#### 组件结构

```
components/ts_cert/
├── CMakeLists.txt                # 组件构建配置
├── Kconfig                       # 可配置选项
├── include/
│   └── ts_cert.h                 # 公开 API（~200行）
└── src/
    └── ts_cert.c                 # 核心实现（~880行）
```

#### 实现的功能

| 功能 | API | 说明 |
|------|-----|------|
| 模块初始化 | `ts_cert_init()` | 初始化 NVS 命名空间 |
| 密钥对生成 | `ts_cert_generate_keypair()` | ECDSA P-256，~1秒完成 |
| CSR 生成 | `ts_cert_generate_csr()` | 带 SAN IP 扩展 |
| 证书安装 | `ts_cert_install_certificate()` | 验证后存入 NVS |
| CA 链安装 | `ts_cert_install_ca_chain()` | 支持多级 CA |
| 状态查询 | `ts_cert_get_status()` | 返回详细 PKI 状态 |
| 证书信息 | `ts_cert_get_info()` | 解析证书详情 |
| 导出 CSR | `ts_cert_export_csr()` | 从 NVS 读取最后生成的 CSR |
| 重置 PKI | `ts_cert_reset()` | 清除所有密钥和证书 |

#### CLI 命令

```bash
# 查看 PKI 状态
pki --status

# 生成 ECDSA P-256 密钥对
pki --generate

# 生成 CSR（带设备 ID）
pki --csr --device-id TIANSHAN-RM01-001

# 从文件安装证书
pki --install --file /sdcard/device.crt

# 从文件安装 CA 链
pki --install-ca --file /sdcard/ca_chain.crt

# 导出 CSR 到文件
pki --export-csr --file /sdcard/device.csr

# 显示证书信息
pki --info

# 重置 PKI（清除所有）
pki --reset
```

#### NVS 存储键名

| 命名空间 | 键名 | 内容 |
|----------|------|------|
| `ts_pki` | `privkey` | ECDSA 私钥 (PEM) |
| `ts_pki` | `cert` | 设备证书 (PEM) |
| `ts_pki` | `ca_chain` | CA 证书链 (PEM) |
| `ts_pki` | `status` | PKI 状态枚举 |
| `ts_pki` | `last_csr` | 最后生成的 CSR |

#### PKI 状态枚举

```c
typedef enum {
    TS_CERT_STATUS_NOT_INITIALIZED = 0,  // 未初始化
    TS_CERT_STATUS_HAS_KEYPAIR,          // 已生成密钥
    TS_CERT_STATUS_HAS_CSR,              // 已生成 CSR
    TS_CERT_STATUS_ACTIVATED,            // 证书已安装
    TS_CERT_STATUS_EXPIRED,              // 证书已过期
} ts_cert_status_t;
```

#### 依赖组件

- `mbedtls` - 密码学操作
- `nvs_flash` - 持久化存储
- `esp_event` - 事件系统
- `lwip` - 网络地址转换

#### 关键技术实现

**1. SAN IP 扩展构建**

CSR 中正确嵌入 Subject Alternative Name (IP Address) 扩展，遵循 RFC 5280：

```c
// 构建 SAN 扩展 (DER 格式)
// OID: 2.5.29.17 (subjectAltName)
// GeneralName ::= CHOICE { iPAddress [7] OCTET STRING }
static esp_err_t build_san_extension(uint32_t ip_addr, 
                                      uint8_t *buf, 
                                      size_t *len);
```

**2. 密钥对生成**

```c
// 使用 ESP32 硬件熵源
mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, 
                    mbedtls_pk_ec(*key),
                    mbedtls_ctr_drbg_random, &ctr_drbg);
```

**3. 安全服务集成**

在 `ts_services.c` 的 `security_service_init()` 中调用 `ts_cert_init()`。

#### 测试验证

**测试序列**:
```
TianShanOS> pki --status
Status: NOT INITIALIZED

TianShanOS> pki --generate
✓ Key pair generated successfully

TianShanOS> pki --csr --device-id TIANSHAN-RM01-001
✓ CSR generated successfully
-----BEGIN CERTIFICATE REQUEST-----
MIH9MIGkAgEAMEIxGjAYBgNVBAMMEVRJQU5TSEFOLVJNMDEtMDAxMRMwEQYDVQQK
...
-----END CERTIFICATE REQUEST-----

# CA 服务器签发证书后...

TianShanOS> pki --install --file /sdcard/esp32_device.crt
✓ Certificate installed successfully

TianShanOS> pki --install-ca --file /sdcard/ca_chain.crt
✓ CA chain installed successfully

TianShanOS> pki --status
╔══════════════════════════════════════════╗
║           PKI Certificate Status         ║
╠══════════════════════════════════════════╣
║ Status:      ACTIVATED               ║
║ Private Key: ✓ Present                  ║
║ Certificate: ✓ Installed                ║
║ CA Chain:    ✓ Installed                ║
╠══════════════════════════════════════════╣
║              Certificate Info            ║
╠══════════════════════════════════════════╣
║ Subject: TIANSHAN-RM01-001                ║
║ Issuer:  TianShanOS Intermediate CA       ║
║ Serial:  75A5308C70593E0EBD1A5155FDA40071 ║
║ Validity: Valid                          ║
║ Expires:  364 days                         ║
╚══════════════════════════════════════════╝
```

**签发的设备证书**:
```
Subject: CN=TIANSHAN-RM01-001, O=TianShanOS, OU=Device
Issuer: CN=TianShanOS Intermediate CA
Serial: 75A5308C70593E0EBD1A5155FDA40071B3BB5A37
Validity: 2026-01-24 ~ 2027-01-24 (1年)
Key Usage: Digital Signature, Key Encipherment
Extended Key Usage: TLS Web Client Auth, TLS Web Server Auth
SAN: IP Address:10.10.99.97
Key: ECDSA P-256
```

#### 构建编译

**固件大小**:
```
TianShanOS.bin: 2,047,984 bytes (~2MB)
Free space: 1,032,208 bytes (34%)
```

**编译警告**: 无新增警告

#### 文件变更清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `components/ts_cert/include/ts_cert.h` | 新建 | 公开 API 定义 |
| `components/ts_cert/src/ts_cert.c` | 新建 | 核心实现 |
| `components/ts_cert/CMakeLists.txt` | 新建 | 组件构建 |
| `components/ts_cert/Kconfig` | 新建 | 配置选项 |
| `components/ts_console/commands/ts_cmd_pki.c` | 新建 | CLI 命令 |
| `components/ts_console/commands/ts_cmd_register.c` | 修改 | 注册 pki 命令 |
| `components/ts_console/include/ts_cmd_all.h` | 修改 | 添加声明 |
| `components/ts_console/CMakeLists.txt` | 修改 | 添加源文件 |
| `main/ts_services.c` | 修改 | 初始化 ts_cert |
| `main/CMakeLists.txt` | 修改 | 添加 ts_cert 依赖 |

#### 遇到的问题与解决

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| `implicit declaration of 'ntohl'` | 缺少头文件 | 添加 `#include "lwip/inet.h"` |
| `implicit declaration of 'inet_pton'` | ESP-IDF 不支持 POSIX | 改用 `#include "lwip/sockets.h"` |
| `ESP_ERR_INVALID_STATE` 运行时错误 | `ts_cert_init()` 未被调用 | 在 `security_service_init()` 中添加初始化 |

---

### 12.3 下一步: Phase 3 - 设备激活服务（待开始）

**计划内容**:
1. 自动 CSR 提交（网络可用时）
2. 证书到期检测和提醒
3. LED 告警指示
4. TF 卡自动检测和导入

### 12.4 下一步: Phase 4 - mTLS HTTPS 服务器（待开始）

**计划内容**:
1. 使用设备证书启动 HTTPS 服务器
2. 配置客户端证书验证（双向认证）
3. 从证书 OU 字段提取用户角色
4. 实现权限检查中间件
