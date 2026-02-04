# TianShanOS 配置包系统设计 (Config Pack System)

## ⚠️ 实现状态

> **最后更新**：2026年2月4日  
> **状态**：代码开发完成，**尚未进行端到端集成测试**

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | 加密原语（ECDH/HKDF/Random） | ✅ 完成 |
| Phase 2 | ts_config_pack 组件 | ✅ 完成 |
| Phase 3 | CLI 命令 | ✅ 完成 |
| Phase 3.5 | WebUI API 和前端界面 | ✅ 完成 |
| Phase 4 | 端到端集成测试 | ⏳ 待测试 |

**待测试项**：
- [ ] Developer 设备导出加密配置包
- [ ] Device 设备导入并解密
- [ ] 签名验证和证书链校验
- [ ] WebUI 完整流程

---

## 1. 概述

### 1.1 目的

配置包系统（Config Pack）提供一种安全的机制，用于：

1. **官方配置分发**：官方开发者加密配置文件并签名，分发给用户设备
2. **设备间共享**：用户之间安全共享非敏感配置（需用目标设备证书加密）
3. **配置备份**：加密导出配置用于备份和恢复
4. **配置同步**：多设备场景下的配置同步

### 1.2 核心设计原则

| 原则 | 说明 |
|------|------|
| **配置驱动** | 符合 TianShanOS "面向配置而非面向代码" 的核心理念 |
| **PKI 统一** | 复用现有 PKI 基础设施（ts_cert），使用同一信任链 |
| **敏感隔离** | 敏感信息（密钥、令牌、凭证）不参与导出/分享 |
| **源码开放安全** | 设计假设源码公开，仅依赖密钥保护，不依赖算法隐藏 |

### 1.3 主要场景

#### 场景 A：官方配置分发（PC 工具）

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      官方配置分发（PC 工具）                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   官方开发者 PC                              用户设备                    │
│   ┌─────────┐                              ┌─────────┐                  │
│   │ 官方证书 │                              │ 设备证书 │                  │
│   │(签名用)  │                              │(解密用)  │                  │
│   └────┬────┘                              └────┬────┘                  │
│        │                                        │                       │
│        ▼                                        ▼                       │
│   ┌─────────────────┐                   ┌─────────────────┐            │
│   │ 1. 准备配置JSON  │                   │ 4. 验证官方签名  │            │
│   │ 2. 用设备公钥加密│    .tscfg 文件    │ 5. 用私钥解密    │            │
│   │ 3. 用官方私钥签名│ ───────────────▶ │ 6. 应用配置     │            │
│   └─────────────────┘                   └─────────────────┘            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 场景 B：开发机导出（同固件，root 权限）

开发机使用与用户设备**相同的固件**，通过 root 登录解锁导出功能。

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      开发机导出（root 权限）                             │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   开发机（同固件）                           用户设备                    │
│   ┌─────────────────┐                      ┌─────────┐                  │
│   │ 设备证书(解密用) │                      │ 设备证书 │                  │
│   │ 官方签名密钥*   │ ◄── NVS 预置         │(解密用)  │                  │
│   └────────┬────────┘                      └────┬────┘                  │
│            │                                    │                       │
│   root 登录解锁                                  │                       │
│            │                                    │                       │
│            ▼                                    ▼                       │
│   ┌─────────────────┐                   ┌─────────────────┐            │
│   │ WebUI 配置编辑   │                   │ 验证官方签名    │            │
│   │   ↓              │    .tscfg 文件    │ 用私钥解密      │            │
│   │ 一键导出加密包   │ ───────────────▶ │ 应用配置        │            │
│   └─────────────────┘                   └─────────────────┘            │
│                                                                         │
│   * 官方签名密钥通过首次配置流程安全注入到 NVS                           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**开发机配置流程**：

开发机与普通设备使用**完全相同的固件**，区别仅在于证书的 OU 字段。在 PKI 服务器审批 CSR 时，管理员选择设备类型：

```
PKI 服务器审批界面：
┌────────────────────────────────────┐
│ 设备类型:                           │
│   ○ 普通设备 (OU=Device)           │
│   ● 开发机   (OU=Developer) ← 选择 │
└────────────────────────────────────┘
```

签发后的证书包含：
```
Subject: CN=TIANSHAN-RM01-DEV-001, OU=Developer
```

**权限控制**：

| 功能 | 普通设备 (OU=Device) | 开发机 (OU=Developer) |
|------|---------------------|----------------------|
| 查看配置 | ✅ | ✅ |
| 编辑本地配置 | ✅ | ✅ |
| 导入 .tscfg | ✅ | ✅ |
| 导出设备证书 | ✅ | ✅ |
| **导出 .tscfg** | ❌ | ✅ |

**优势**：
- 无需额外密钥导入步骤
- 每台开发机有独立私钥，泄露影响范围小
- 可追溯签名来源（证书 CN 唯一标识设备）
- 可通过吊销证书撤销开发机权限

## 2. 加密方案

### 2.1 混合加密设计 (ECDH + AES-256-GCM)

采用混合加密方案，结合非对称密钥交换和对称加密的优势：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                            加密流程                                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  发送方                                                                  │
│  ┌───────────────┐                                                      │
│  │ 1. 生成临时 EC 密钥对（ephemeral）                                    │
│  │    ephemeral_priv, ephemeral_pub                                     │
│  └───────┬───────┘                                                      │
│          │                                                              │
│          ▼                                                              │
│  ┌───────────────┐     ┌───────────────┐                               │
│  │ 2. ECDH 密钥协商   │     │ 接收方设备证书  │                               │
│  │    shared_secret = ECDH(ephemeral_priv, recipient_pub)              │
│  └───────┬───────┘     └───────────────┘                               │
│          │                                                              │
│          ▼                                                              │
│  ┌───────────────┐                                                      │
│  │ 3. HKDF 密钥派生                                                      │
│  │    aes_key = HKDF-SHA256(shared_secret, salt, "tscfg-aes-key")      │
│  └───────┬───────┘                                                      │
│          │                                                              │
│          ▼                                                              │
│  ┌───────────────┐                                                      │
│  │ 4. AES-256-GCM 加密                                                   │
│  │    ciphertext, tag = AES-GCM(aes_key, iv, plaintext, aad)           │
│  └───────┬───────┘                                                      │
│          │                                                              │
│          ▼                                                              │
│  ┌───────────────────────────────────────────────────────────┐         │
│  │ 输出: ephemeral_pub || salt || iv || ciphertext || tag    │         │
│  └───────────────────────────────────────────────────────────┘         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 2.2 密钥派生参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 曲线 | NIST P-256 (secp256r1) | 与 ts_cert 保持一致 |
| KDF | HKDF-SHA256 | RFC 5869 |
| Salt | 32 字节随机 | 每次加密重新生成 |
| Info | `"tscfg-aes-key-v1"` | 上下文绑定 |
| AES Key | 256 位 | AES-256-GCM |
| IV/Nonce | 12 字节随机 | GCM 标准 |
| Tag | 16 字节 | 完整性验证 |

### 2.3 解密流程

```
接收方：
1. 提取 ephemeral_pub, salt, iv, ciphertext, tag
2. 使用设备私钥计算 shared_secret = ECDH(device_priv, ephemeral_pub)
3. 派生 aes_key = HKDF-SHA256(shared_secret, salt, "tscfg-aes-key")
4. 解密 plaintext = AES-GCM-Decrypt(aes_key, iv, ciphertext, aad, tag)
```

## 3. 数字签名

### 3.1 签名目的

| 验证项 | 说明 |
|--------|------|
| **来源真实性** | 确认配置确实由声称的发送方创建 |
| **完整性** | 确认配置未被篡改 |
| **官方认证** | 区分官方签名配置和用户自定义配置 |

### 3.2 签名算法

```
签名算法: ECDSA-SHA256 over P-256

签名对象（按顺序连接）:
┌────────────────────────────────────────────┐
│ 1. ciphertext（密文）                        │
│ 2. ephemeral_pub（临时公钥）                 │
│ 3. recipient_cert_fingerprint（接收方指纹）  │
│ 4. timestamp（时间戳）                       │
│ 5. version（版本号）                         │
└────────────────────────────────────────────┘

signature = ECDSA-Sign(SHA256(data_to_sign), signer_private_key)
```

### 3.3 PKI 信任模型

```
                    ┌─────────────────────┐
                    │     Root CA         │
                    │  (TianShan Root)    │
                    │  嵌入固件，不可变    │
                    └──────────┬──────────┘
                               │
              ┌────────────────┼────────────────┐
              │                │                │
              ▼                ▼                ▼
       ┌────────────┐  ┌────────────┐   ┌────────────┐
       │ 开发机证书   │  │ 普通设备证书 │   │ 用户证书   │
       │ OU=Developer│  │ OU=Device   │   │(mTLS用)   │
       └────────────┘  └────────────┘   └────────────┘
             │                │
             │                │
             ▼                ▼
       签名配置包         解密配置包
```

**信任链验证逻辑**：

```c
// 验证签名者证书的证书链
bool is_valid = verify_certificate_chain(signer_cert, ca_chain, root_ca);

// 检查是否为官方签名（开发机）
bool is_official = false;
if (is_valid) {
    // 检查签名者证书的 OU 字段是否为 "Developer"
    is_official = (strstr(signer_cert.ou, "Developer") != NULL);
}
```

### 3.4 开发机证书签发

通过 PKI 服务器审批时选择"开发机"类型，证书 Subject 中会包含 `OU=Developer`：

```
签发的设备证书示例：
Subject: CN=TIANSHAN-RM01-DEV-001, OU=Developer
Issuer: CN=TianShan Intermediate CA
Validity: 10 years
Key Usage: Digital Signature, Key Encipherment
Extended Key Usage: Server Auth, Client Auth
SAN: IP:10.10.99.97
```

普通设备证书：
```
Subject: CN=TIANSHAN-RM01-0042, OU=Device
...
```

## 4. 文件格式

### 4.1 文件扩展名与位置

| 类型 | 扩展名 | 位置 | 说明 |
|------|--------|------|------|
| 加密配置 | `.tscfg` | 与源 `.json` 同目录 | 加密后的配置包 |
| 明文配置 | `.json` | `/sdcard/config/` | 原始配置文件 |

**优先级规则**：
- 同名 `.tscfg` 和 `.json` 文件共存时，`.tscfg` 优先
- 系统加载配置时自动解密 `.tscfg` 到内存
- 明文 `.json` 文件**不会写入存储**，仅存在于内存

### 4.2 .tscfg 文件结构

```json
{
    "tscfg_version": "1.0",
    "format": "encrypted",
    
    "metadata": {
        "name": "led_effects",
        "description": "LED 特效配置",
        "created_at": "2025-01-20T10:30:00Z",
        "created_by": "TianShanOS Official",
        "target_device": "TIANSHAN-RM01-0042",
        "source_file": "led_effects.json",
        "content_hash": "base64(SHA256(plaintext))"
    },
    
    "encryption": {
        "algorithm": "ECDH-P256+AES-256-GCM",
        "kdf": "HKDF-SHA256",
        "ephemeral_public_key": "base64(65 bytes uncompressed point)",
        "salt": "base64(32 bytes)",
        "iv": "base64(12 bytes)",
        "tag": "base64(16 bytes)",
        "recipient_cert_fingerprint": "hex(SHA256(recipient_cert_der))"
    },
    
    "signature": {
        "algorithm": "ECDSA-SHA256",
        "signer_certificate": "-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----",
        "signature": "base64(DER encoded ECDSA signature)",
        "signed_at": "2025-01-20T10:30:00Z",
        "is_official": true
    },
    
    "payload": "base64(ciphertext)"
}
```

### 4.3 字段说明

| 字段路径 | 类型 | 必需 | 说明 |
|----------|------|------|------|
| `tscfg_version` | string | ✓ | 格式版本，当前 "1.0" |
| `format` | string | ✓ | 固定为 "encrypted" |
| `metadata.name` | string | ✓ | 配置名称（无扩展名） |
| `metadata.description` | string | - | 配置描述 |
| `metadata.created_at` | string | ✓ | ISO 8601 时间戳 |
| `metadata.created_by` | string | ✓ | 创建者标识 |
| `metadata.target_device` | string | ✓ | 目标设备 ID |
| `metadata.source_file` | string | ✓ | 原始文件名 |
| `metadata.content_hash` | string | ✓ | 原始内容的 SHA256 |
| `encryption.algorithm` | string | ✓ | 加密算法标识 |
| `encryption.ephemeral_public_key` | string | ✓ | Base64 临时公钥 |
| `encryption.salt` | string | ✓ | Base64 HKDF salt |
| `encryption.iv` | string | ✓ | Base64 GCM nonce |
| `encryption.tag` | string | ✓ | Base64 GCM auth tag |
| `encryption.recipient_cert_fingerprint` | string | ✓ | 接收方证书指纹 |
| `signature.algorithm` | string | ✓ | 签名算法 |
| `signature.signer_certificate` | string | ✓ | PEM 格式签名者证书 |
| `signature.signature` | string | ✓ | Base64 签名值 |
| `signature.signed_at` | string | ✓ | 签名时间 |
| `signature.is_official` | bool | ✓ | 官方签名标记 |
| `payload` | string | ✓ | Base64 加密内容 |

## 5. 配置加载流程

### 5.1 系统启动配置加载

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      配置加载流程（系统启动时）                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│   /sdcard/config/                                                       │
│   ├── led_effects.tscfg     ◄── 优先加载（加密）                        │
│   ├── led_effects.json      ◄── 备选（如果 .tscfg 不存在）              │
│   ├── automation/                                                       │
│   │   ├── rules.tscfg       ◄── 子目录同理                              │
│   │   └── rules.json                                                    │
│   └── network.json          ◄── 无对应 .tscfg，直接加载                 │
│                                                                         │
│   加载逻辑:                                                              │
│   1. 扫描目录，建立 {name -> [.tscfg, .json]} 映射                      │
│   2. 对每个配置名:                                                       │
│      - 存在 .tscfg? 解密并验证签名 → 成功则使用，失败则记录错误         │
│      - 不存在 .tscfg? 直接加载 .json                                    │
│   3. 配置仅存在于内存，不回写明文到存储                                  │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 解密与验证流程

```c
ts_config_pack_result_t ts_config_pack_load(const char *tscfg_path) {
    // 1. 解析 JSON 结构
    // 2. 验证版本兼容性
    // 3. 验证接收方证书指纹匹配本设备
    // 4. 验证签名者证书链
    // 5. 验证签名
    // 6. ECDH 密钥协商
    // 7. HKDF 密钥派生
    // 8. AES-GCM 解密
    // 9. 验证内容哈希
    // 10. 返回解密后的配置
}
```

## 6. 安全性分析

### 6.1 威胁模型

| 威胁 | 缓解措施 |
|------|---------|
| **配置文件被窃取** | ECDH 加密，攻击者无法解密（缺少设备私钥） |
| **配置文件被篡改** | ECDSA 签名 + AES-GCM tag 双重验证 |
| **重放攻击** | 时间戳 + 接收方指纹绑定 |
| **中间人攻击** | 端到端加密，仅持有设备私钥者可解密 |
| **伪造官方配置** | 官方私钥不公开，证书链验证 |
| **源码泄露** | 设计不依赖算法隐藏，仅依赖密钥安全 |

### 6.2 源码公开安全性

**问：源码公开是否影响安全性？**

答：**不影响**。根据 Kerckhoffs 原则，系统安全性仅依赖密钥保密：

| 公开内容 | 安全影响 |
|----------|---------|
| 加密算法 (ECDH + AES-GCM) | 无 - 公开标准算法 |
| 密钥派生参数 (HKDF info) | 无 - 防止跨协议攻击，非秘密 |
| 文件格式 | 无 - 格式解析不等于密钥获取 |
| 验证逻辑 | 无 - 验证逻辑公开不影响签名伪造难度 |
| Root CA 公钥 | 无 - 公钥本就用于公开分发 |

**保密内容**（不在代码中）：

| 保密内容 | 存储位置 |
|----------|---------|
| 设备私钥 | NVS（每设备唯一） |
| 官方签名私钥 | 官方 HSM / 安全存储 |
| Root CA 私钥 | 离线 CA 服务器 |

### 6.3 前向安全性

ECDH 使用临时密钥对（ephemeral key pair）：
- 每次加密生成新的临时私钥
- 加密完成后立即销毁临时私钥
- 即使设备私钥未来泄露，历史配置包仍安全

### 6.4 密钥管理

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           密钥生命周期                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  设备私钥:                                                               │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                          │
│  │  生成    │───▶│  存储NVS │───▶│  使用    │                          │
│  │(ts_cert) │    │ (加密)   │    │ (解密)   │                          │
│  └──────────┘    └──────────┘    └──────────┘                          │
│       │                               │                                 │
│       └───────── 永不导出 ────────────┘                                 │
│                                                                         │
│  临时私钥:                                                               │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                          │
│  │  生成    │───▶│ ECDH计算 │───▶│  销毁    │                          │
│  │(内存中)  │    │ (内存中) │    │ (清零)   │                          │
│  └──────────┘    └──────────┘    └──────────┘                          │
│       │                               │                                 │
│       └─────── 存在时间 < 1秒 ────────┘                                 │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.5 开发机签名机制

开发机通过证书 OU 字段标识身份，使用设备私钥签名配置包。

**签名验证逻辑**：

```c
// 验证签名者证书
bool is_official = false;
ts_cert_info_t signer_info;
ts_cert_parse_certificate(signer_cert_pem, len, &signer_info);

// 1. 验证证书链（由受信任 CA 签发）
if (verify_cert_chain(signer_cert, ca_chain)) {
    // 2. 检查 OU 字段是否为 "Developer"
    if (strstr(signer_info.subject_ou, "Developer") != NULL) {
        is_official = true;
    }
}
```

**设备端检查导出权限**：

```c
bool ts_config_pack_can_export(void) {
    ts_cert_info_t info;
    if (ts_cert_get_info(&info) != ESP_OK) {
        return false;
    }
    // 检查本设备证书 OU 是否为 Developer
    return (strstr(info.subject_ou, "Developer") != NULL);
}
```

**安全措施**：

| 措施 | 说明 |
|------|------|
| **CA 控制** | 只有 CA 管理员能决定哪台设备是开发机 |
| **证书链验证** | 必须由受信任 CA 签发的证书才能签名 |
| **可追溯** | 证书 CN 唯一标识签名设备 |
| **可撤销** | 吊销证书即可取消签名权限 |
| **审计日志** | PKI 服务器记录所有签发历史 |

## 7. API 设计

### 7.1 组件结构

```
components/ts_config_pack/
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── ts_config_pack.h
└── src/
    ├── ts_config_pack.c         # 主逻辑
    ├── ts_config_pack_crypto.c  # ECDH/HKDF 实现
    └── ts_config_pack_json.c    # JSON 序列化
```

### 7.2 公开 API

```c
/**
 * @file ts_config_pack.h
 * @brief TianShanOS Encrypted Configuration Package System
 */

#ifndef TS_CONFIG_PACK_H
#define TS_CONFIG_PACK_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/*===========================================================================*/
/*                              Types                                         */
/*===========================================================================*/

/**
 * @brief Configuration pack result codes
 */
typedef enum {
    TS_CONFIG_PACK_OK = 0,
    TS_CONFIG_PACK_ERR_PARSE,           /**< JSON parse error */
    TS_CONFIG_PACK_ERR_VERSION,         /**< Unsupported version */
    TS_CONFIG_PACK_ERR_RECIPIENT,       /**< Not intended for this device */
    TS_CONFIG_PACK_ERR_CERT_CHAIN,      /**< Certificate chain validation failed */
    TS_CONFIG_PACK_ERR_SIGNATURE,       /**< Signature verification failed */
    TS_CONFIG_PACK_ERR_DECRYPT,         /**< Decryption failed */
    TS_CONFIG_PACK_ERR_INTEGRITY,       /**< Content hash mismatch */
    TS_CONFIG_PACK_ERR_EXPIRED,         /**< Package expired */
    TS_CONFIG_PACK_ERR_NO_MEM,          /**< Memory allocation failed */
    TS_CONFIG_PACK_ERR_IO               /**< File I/O error */
} ts_config_pack_result_t;

/**
 * @brief Signature verification result
 */
typedef struct {
    bool valid;                          /**< Signature is valid */
    bool is_official;                    /**< Signed by official signer */
    char signer_cn[64];                  /**< Signer Common Name */
    int64_t signed_at;                   /**< Signature timestamp */
} ts_config_pack_sig_info_t;

/**
 * @brief Loaded configuration pack
 */
typedef struct {
    char *name;                          /**< Config name (no extension) */
    char *description;                   /**< Description */
    char *content;                       /**< Decrypted JSON content */
    size_t content_len;                  /**< Content length */
    ts_config_pack_sig_info_t sig_info;  /**< Signature info */
    int64_t created_at;                  /**< Creation timestamp */
} ts_config_pack_t;

/**
 * @brief Export options
 */
typedef struct {
    const char *recipient_cert_pem;      /**< Target device certificate (PEM) */
    const char *signer_key_pem;          /**< Signer private key (PEM) */
    const char *signer_cert_pem;         /**< Signer certificate (PEM) */
    const char *description;             /**< Optional description */
} ts_config_pack_export_opts_t;

/*===========================================================================*/
/*                         Core Functions                                     */
/*===========================================================================*/

/**
 * @brief Initialize configuration pack system
 * 
 * Must be called after ts_cert_init().
 * 
 * @return ESP_OK on success
 */
esp_err_t ts_config_pack_init(void);

/**
 * @brief Load and decrypt a .tscfg file
 * 
 * Verifies signature and decrypts content using device private key.
 * 
 * @param path Path to .tscfg file
 * @param pack Output: loaded configuration (caller must free with ts_config_pack_free)
 * @return TS_CONFIG_PACK_OK on success, error code otherwise
 */
ts_config_pack_result_t ts_config_pack_load(const char *path, ts_config_pack_t **pack);

/**
 * @brief Create an encrypted .tscfg package
 * 
 * Encrypts JSON content for a specific recipient and signs with provided key.
 * 
 * @param name Config name (without extension)
 * @param json_content JSON content to encrypt
 * @param json_len Content length
 * @param opts Export options (recipient cert, signer key/cert)
 * @param output Output buffer for .tscfg JSON (caller must free)
 * @param output_len Output length
 * @return TS_CONFIG_PACK_OK on success
 */
ts_config_pack_result_t ts_config_pack_create(
    const char *name,
    const char *json_content,
    size_t json_len,
    const ts_config_pack_export_opts_t *opts,
    char **output,
    size_t *output_len
);

/**
 * @brief Free a loaded configuration pack
 */
void ts_config_pack_free(ts_config_pack_t *pack);

/*===========================================================================*/
/*                      Batch Operations                                      */
/*===========================================================================*/

/**
 * @brief Scan directory and load all .tscfg files
 * 
 * @param dir_path Directory path
 * @param recursive Scan subdirectories
 * @param packs Output array of loaded packs (caller must free)
 * @param count Output count
 * @return ESP_OK on success (partial failures logged but not fatal)
 */
esp_err_t ts_config_pack_load_dir(
    const char *dir_path,
    bool recursive,
    ts_config_pack_t ***packs,
    size_t *count
);

/*===========================================================================*/
/*                      Utility Functions                                     */
/*===========================================================================*/

/**
 * @brief Check if a file is a valid .tscfg without fully decrypting
 * 
 * Validates structure and signature, but does not decrypt content.
 * 
 * @param path File path
 * @param info Output signature info (optional)
 * @return TS_CONFIG_PACK_OK if valid
 */
ts_config_pack_result_t ts_config_pack_verify(
    const char *path,
    ts_config_pack_sig_info_t *info
);

/**
 * @brief Get human-readable error message
 */
const char *ts_config_pack_strerror(ts_config_pack_result_t result);

/**
 * @brief Export device certificate for config encryption
 * 
 * Outputs the device certificate that others can use to encrypt configs
 * intended for this device.
 * 
 * @param cert_pem Output buffer for PEM certificate
 * @param cert_len Input: buffer size; Output: actual length
 * @return ESP_OK on success
 */
esp_err_t ts_config_pack_export_device_cert(char *cert_pem, size_t *cert_len);

#endif /* TS_CONFIG_PACK_H */
```

### 7.3 内部 Crypto API（新增）

需要在 `ts_crypto.h` 中添加以下函数：

```c
/*===========================================================================*/
/*                          ECDH Key Exchange                                 */
/*===========================================================================*/

/**
 * @brief Perform ECDH key agreement
 * 
 * @param local_keypair Local EC key pair (private key required)
 * @param peer_pubkey_der Peer's public key in DER or uncompressed format
 * @param peer_pubkey_len Peer public key length
 * @param shared_secret Output shared secret (32 bytes for P-256)
 * @param secret_len Output shared secret length
 * @return ESP_OK on success
 */
esp_err_t ts_crypto_ecdh_compute_shared(
    ts_keypair_t local_keypair,
    const uint8_t *peer_pubkey_der,
    size_t peer_pubkey_len,
    uint8_t *shared_secret,
    size_t *secret_len
);

/*===========================================================================*/
/*                          HKDF Key Derivation                               */
/*===========================================================================*/

/**
 * @brief HKDF key derivation (RFC 5869)
 * 
 * @param hash_algo Hash algorithm (SHA256 recommended)
 * @param ikm Input keying material
 * @param ikm_len IKM length
 * @param salt Salt (can be NULL)
 * @param salt_len Salt length
 * @param info Context info
 * @param info_len Info length
 * @param okm Output keying material
 * @param okm_len Desired output length
 * @return ESP_OK on success
 */
esp_err_t ts_crypto_hkdf(
    ts_hash_algo_t hash_algo,
    const uint8_t *ikm, size_t ikm_len,
    const uint8_t *salt, size_t salt_len,
    const uint8_t *info, size_t info_len,
    uint8_t *okm, size_t okm_len
);

/**
 * @brief Generate cryptographically secure random bytes
 */
esp_err_t ts_crypto_random(uint8_t *buf, size_t len);
```

## 8. CLI 命令设计

### 8.1 config 命令扩展

```bash
# 导出设备证书（用于让他人加密配置给本设备）
config --export-cert --output /sdcard/my_device.crt

# 验证 .tscfg 文件（不解密）
config --verify /sdcard/config/led_effects.tscfg

# 查看 .tscfg 元数据
config --info /sdcard/config/led_effects.tscfg
# 输出:
# Name: led_effects
# Created: 2025-01-20 10:30:00
# Signer: TianShanOS Official (official: yes)
# Target: TIANSHAN-RM01-0042
# Status: Valid

# 解密并查看内容（调试用）
config --decrypt /sdcard/config/led_effects.tscfg --json

# 批量验证目录
config --verify-dir /sdcard/config/ --recursive
```

### 8.2 命令参数定义

```c
// ts_cmd_config.c 中的参数结构
static struct {
    struct arg_lit *export_cert;     // --export-cert
    struct arg_str *output;          // --output <path>
    struct arg_lit *verify;          // --verify
    struct arg_str *verify_path;     // <tscfg_path>
    struct arg_lit *info;            // --info
    struct arg_lit *decrypt;         // --decrypt
    struct arg_lit *json;            // --json
    struct arg_lit *verify_dir;      // --verify-dir
    struct arg_lit *recursive;       // --recursive
    struct arg_end *end;
} s_config_args;
```

## 9. 实施计划

### 9.1 阶段划分

#### Phase 1: 基础设施（预计 2 天）

| 任务 | 文件 | 优先级 |
|------|------|--------|
| 添加 ECDH 函数 | `ts_crypto.c/h` | P0 |
| 添加 HKDF 函数 | `ts_crypto.c/h` | P0 |
| 添加 `ts_crypto_random()` | `ts_crypto.c/h` | P0 |
| 单元测试 | `test/test_crypto.c` | P1 |

```c
// 实现参考 - ECDH (mbedtls)
esp_err_t ts_crypto_ecdh_compute_shared(...) {
    mbedtls_ecdh_context ctx;
    mbedtls_ecdh_init(&ctx);
    
    // 1. 设置曲线参数
    mbedtls_ecp_group_load(&ctx.grp, MBEDTLS_ECP_DP_SECP256R1);
    
    // 2. 导入本地私钥
    mbedtls_mpi_copy(&ctx.d, &ec_keypair->d);
    
    // 3. 导入对方公钥
    mbedtls_ecp_point_read_binary(&ctx.grp, &ctx.Qp, peer_pubkey, len);
    
    // 4. 计算共享密钥
    mbedtls_ecdh_calc_secret(&ctx, &olen, shared_secret, secret_len, 
                              mbedtls_ctr_drbg_random, &ctr_drbg);
    
    mbedtls_ecdh_free(&ctx);
}
```

#### Phase 2: 核心组件（预计 3 天）

| 任务 | 文件 | 优先级 |
|------|------|--------|
| 创建组件目录结构 | `ts_config_pack/` | P0 |
| 实现 JSON 序列化/解析 | `ts_config_pack_json.c` | P0 |
| 实现加密/解密流程 | `ts_config_pack_crypto.c` | P0 |
| 实现主 API | `ts_config_pack.c` | P0 |
| Kconfig 配置 | `Kconfig` | P1 |

#### Phase 3: 集成（预计 2 天）

| 任务 | 文件 | 优先级 |
|------|------|--------|
| CLI 命令实现 | `ts_cmd_config.c` | P0 |
| 配置加载器集成 | `ts_storage.c` | P1 |
| 系统启动集成 | `ts_services.c` | P1 |
| 错误处理和日志 | 全组件 | P1 |

#### Phase 4: 测试与文档（预计 2 天）

| 任务 | 优先级 |
|------|--------|
| 端到端测试脚本 | P0 |
| Python 工具（官方加密工具） | P1 |
| 用户文档 | P1 |
| 安全审计 | P2 |

### 9.2 依赖关系

```
ts_crypto (ECDH/HKDF)
        │
        ▼
ts_config_pack ◄─── ts_cert (证书验证)
        │
        ├──▶ ts_console (CLI)
        │
        └──▶ ts_storage (配置加载)
```

### 9.3 测试用例

| 测试场景 | 验证点 |
|---------|--------|
| 正常加密解密 | 内容完整性 |
| 错误接收方 | 拒绝解密 |
| 签名篡改 | 验证失败 |
| 内容篡改 | GCM tag 失败 |
| 过期包 | 时间验证 |
| 证书链断裂 | 链验证失败 |
| 大文件 | 内存使用 |

### 9.4 官方加密工具（Python）

```python
#!/usr/bin/env python3
"""
TianShanOS Official Config Pack Tool
Usage: ts_config_pack.py --encrypt config.json --cert device.crt --key official.key --out config.tscfg
"""

import json
import argparse
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.kdf.hkdf import HKDF
# ... 完整实现
```

## 10. 注意事项

### 10.1 内存管理

- 所有大于 128 字节的分配使用 PSRAM
- 解密后的明文在使用完毕后清零（`mbedtls_platform_zeroize`）
- 临时密钥和共享密钥在使用后立即清零

### 10.2 错误处理

- 所有加密操作失败时不泄露错误细节（防止 oracle 攻击）
- 签名验证使用常量时间比较
- 解密失败统一返回 `TS_CONFIG_PACK_ERR_DECRYPT`

### 10.3 版本兼容

- `tscfg_version` 字段用于未来升级
- 向后兼容：新版本软件可读取旧版本格式
- 向前兼容：通过版本检查拒绝无法处理的新格式

## 11. 参考资料

- [RFC 5869 - HKDF](https://datatracker.ietf.org/doc/html/rfc5869)
- [NIST SP 800-56A - Key Agreement](https://csrc.nist.gov/publications/detail/sp/800-56a/rev-3/final)
- [mbedtls ECDH API](https://mbed-tls.readthedocs.io/projects/api/en/development/api/file/ecdh_8h/)
- TianShanOS PKI 设计：[PKI_DESIGN.md](PKI_DESIGN.md)
