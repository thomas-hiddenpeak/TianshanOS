# TianShanOS 安全增强方案

> **版本**：v1.0  
> **日期**：2026-01-24  
> **作者**：TianShanOS 开发团队  

本文档规划 TianShanOS 的安全增强功能，包括 HTTPS、权限管理和双向认证。

---

## 📋 目录

1. [现状评估](#现状评估)
2. [HTTPS 启用方案](#https-启用方案)
3. [权限管理系统](#权限管理系统)
4. [双向认证（mTLS）](#双向认证mtls)
5. [实施路线图](#实施路线图)
6. [技术选型](#技术选型)

---

## 现状评估

### 已有安全功能 ✅

| 功能 | 状态 | 组件 |
|------|------|------|
| SSH 密钥管理 | ✅ 完整实现 | `ts_security` |
| 已知主机验证 | ✅ 完整实现 | `ts_security` |
| 密钥生成/导出 | ✅ 完整实现 | `ts_security` |
| SSH 公钥部署/撤销 | ✅ 完整实现 | `ts_security` |
| WebUI 登录认证 | ⚠️ 基础实现 | `ts_webui` |
| HTTP API 认证 | ⚠️ Token 机制 | `ts_webui_api` |
| WebSocket 认证 | ❌ 未实现 | - |

### 安全缺口 ⚠️

1. **传输安全**
   - ❌ HTTP 明文传输（端口 80）
   - ❌ WebSocket 未加密（ws://）
   - ❌ API Token 明文传输

2. **权限管理**
   - ❌ 无用户角色系统
   - ❌ 无细粒度权限控制
   - ❌ 无审计日志

3. **客户端认证**
   - ❌ 无客户端证书验证
   - ❌ 无设备白名单

---

## HTTPS 启用方案

### 方案 A：自签名证书（推荐用于内网）

#### 技术栈
- **ESP-IDF HTTPS Server**：`esp_https_server.h`
- **mbedTLS**：ESP-IDF 内置 TLS 库
- **证书格式**：PEM（X.509）

#### 实施步骤

**1. 证书生成工具**

```c
// components/ts_security/include/ts_cert.h
typedef struct {
    char *cert_pem;         // 证书内容（PEM 格式）
    size_t cert_len;
    char *privkey_pem;      // 私钥内容
    size_t privkey_len;
    char *ca_cert_pem;      // CA 证书（可选，用于 mTLS）
    size_t ca_cert_len;
    uint32_t expiry_days;   // 有效期（天）
    char *common_name;      // CN（如 tianshanos.local）
} ts_cert_config_t;

// 生成自签名证书
esp_err_t ts_cert_generate_self_signed(ts_cert_config_t *config);

// 保存证书到 NVS/SPIFFS
esp_err_t ts_cert_save(const char *cert_id, const ts_cert_config_t *config);

// 加载证书
esp_err_t ts_cert_load(const char *cert_id, ts_cert_config_t *config);
```

**2. HTTPS Server 配置**

```c
// components/ts_net/src/ts_https_server.c
httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

// 加载证书
ts_cert_config_t cert;
ts_cert_load("default", &cert);

conf.cacert_pem = cert.cert_pem;
conf.cacert_len = cert.cert_len;
conf.prvtkey_pem = cert.privkey_pem;
conf.prvtkey_len = cert.privkey_len;

// 启动 HTTPS
conf.httpd.server_port = 443;
conf.httpd.ctrl_port = 8443;
httpd_ssl_start(&server, &conf);
```

**3. WebUI 适配**

```javascript
// 自动检测协议
const protocol = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
const wsUrl = `${protocol}${window.location.host}/ws`;
```

#### Kconfig 配置

```kconfig
menu "TianShanOS HTTPS Configuration"
    config TS_HTTPS_ENABLED
        bool "Enable HTTPS"
        default n
        help
            Enable HTTPS server (port 443)
    
    config TS_HTTPS_PORT
        int "HTTPS Port"
        depends on TS_HTTPS_ENABLED
        default 443
        range 1 65535
    
    config TS_HTTP_REDIRECT
        bool "Redirect HTTP to HTTPS"
        depends on TS_HTTPS_ENABLED
        default y
        help
            Automatically redirect HTTP (port 80) to HTTPS (port 443)
    
    config TS_CERT_AUTO_GENERATE
        bool "Auto-generate self-signed certificate"
        depends on TS_HTTPS_ENABLED
        default y
        help
            Generate self-signed certificate on first boot if not exists
    
    config TS_CERT_EXPIRY_DAYS
        int "Certificate expiry (days)"
        depends on TS_CERT_AUTO_GENERATE
        default 3650
        help
            Certificate validity period (default: 10 years)
endmenu
```

#### CLI 命令

```bash
# 生成新证书
cert --generate --cn tianshanos.local --days 3650

# 查看证书信息
cert --info

# 导出证书（用于浏览器信任）
cert --export --type pem

# 删除证书
cert --delete

# 导入证书
cert --import --file /sdcard/cert.pem --key /sdcard/key.pem
```

#### WebUI 管理界面

在安全页面添加"证书管理" Tab：
- 查看当前证书信息（CN、有效期、指纹）
- 生成新证书
- 导入/导出证书
- 下载证书（浏览器信任）
- 强制 HTTPS 开关

---

### 方案 B：Let's Encrypt（公网场景）

#### 前提条件
- 有公网域名（如 `device.example.com`）
- 设备可访问公网
- 开放端口 80（ACME HTTP-01 验证）

#### 实施方案

**1. ACME 客户端集成**

使用 `esp-acme` 组件（需要移植或自研）：
```c
// 请求证书
esp_err_t ts_acme_request_cert(const char *domain, 
                                 const char *email,
                                 ts_cert_config_t *out_cert);

// 自动续期（定时任务）
void ts_acme_renew_task(void *param);
```

**2. 挑战**
- ESP32-S3 存储空间有限（ACME 证书 + 私钥 ~4KB）
- Let's Encrypt 证书 90 天有效期，需要自动续期
- 需要 NTP 同步（证书验证依赖时间）

**推荐**：内网设备使用方案 A（自签名），公网设备考虑使用反向代理（Nginx）处理 HTTPS。

---

## 权限管理系统

### 角色设计（RBAC）

#### 角色定义

```c
typedef enum {
    TS_ROLE_GUEST = 0,      // 访客（只读，无敏感信息）
    TS_ROLE_OPERATOR = 1,   // 操作员（监控、基础操作）
    TS_ROLE_ADMIN = 2,      // 管理员（完全控制）
    TS_ROLE_ROOT = 3,       // 超级管理员（系统配置）
} ts_user_role_t;
```

#### 权限矩阵

| 功能 | Guest | Operator | Admin | Root |
|------|-------|----------|-------|------|
| 查看仪表盘 | ✅ | ✅ | ✅ | ✅ |
| 查看日志 | ❌ | ✅ | ✅ | ✅ |
| LED 控制 | ❌ | ✅ | ✅ | ✅ |
| 设备监控 | ✅ | ✅ | ✅ | ✅ |
| 网络配置 | ❌ | ❌ | ✅ | ✅ |
| SSH 密钥管理 | ❌ | ❌ | ✅ | ✅ |
| OTA 升级 | ❌ | ❌ | ✅ | ✅ |
| 用户管理 | ❌ | ❌ | ❌ | ✅ |
| 系统重启 | ❌ | ❌ | ✅ | ✅ |
| 清空配置 | ❌ | ❌ | ❌ | ✅ |

### 用户数据结构

```c
// components/ts_security/include/ts_auth.h
typedef struct {
    char username[32];
    char password_hash[64];    // SHA256(salt + password)
    char salt[16];             // 随机盐值
    ts_user_role_t role;
    uint32_t created_at;       // Unix timestamp
    uint32_t last_login;
    bool enabled;
    uint32_t login_attempts;   // 登录失败次数（防暴力破解）
} ts_user_t;

// 用户管理 API
esp_err_t ts_auth_create_user(const char *username, const char *password, ts_user_role_t role);
esp_err_t ts_auth_delete_user(const char *username);
esp_err_t ts_auth_change_password(const char *username, const char *old_pw, const char *new_pw);
esp_err_t ts_auth_set_role(const char *username, ts_user_role_t role);
bool ts_auth_verify_password(const char *username, const char *password);
ts_user_role_t ts_auth_get_role(const char *username);
```

### 权限检查机制

```c
// API 层权限验证
typedef struct {
    const char *endpoint;           // 如 "system.reboot"
    ts_user_role_t min_role;        // 最低所需角色
    bool requires_auth;             // 是否需要认证
} ts_api_permission_t;

// 注册 API 时指定权限
ts_api_endpoint_t ep = {
    .name = "system.reboot",
    .handler = api_system_reboot,
    .min_role = TS_ROLE_ADMIN,      // ← 最低需要 Admin
    .requires_auth = true,
};

// API 调用前自动检查
static esp_err_t api_handler(ts_http_request_t *req, void *user_data) {
    // 1. 从 Token/Session 获取用户
    const char *username = get_username_from_token(req);
    
    // 2. 获取用户角色
    ts_user_role_t role = ts_auth_get_role(username);
    
    // 3. 检查权限
    if (role < endpoint->min_role) {
        return send_error(req, 403, "Permission denied");
    }
    
    // 4. 调用实际处理器
    return endpoint->handler(req, user_data);
}
```

### WebUI Session 管理

```javascript
// 登录时存储用户信息
async function login(username, password) {
    const result = await api.call('auth.login', {username, password});
    if (result.success) {
        // 存储 Token 和角色信息
        localStorage.setItem('auth_token', result.data.token);
        localStorage.setItem('user_role', result.data.role);
        localStorage.setItem('username', username);
        
        // 刷新页面，应用权限
        location.reload();
    }
}

// 根据角色显示/隐藏功能
function applyPermissions() {
    const role = parseInt(localStorage.getItem('user_role') || '0');
    
    // Guest: 隐藏敏感功能
    if (role < 2) { // ROLE_ADMIN
        document.querySelectorAll('[data-min-role]').forEach(el => {
            const minRole = parseInt(el.dataset.minRole);
            if (role < minRole) {
                el.style.display = 'none'; // 隐藏按钮/链接
            }
        });
    }
}

// HTML 标记权限
<button class="btn" data-min-role="2" onclick="rebootSystem()">重启系统</button>
```

### 审计日志

```c
// 记录敏感操作
void ts_audit_log(const char *username, const char *action, const char *details) {
    ts_log(TS_LOG_INFO, "audit", "[%s] %s: %s", username, action, details);
    // 可选：存储到专用 NVS 分区或 SD 卡
}

// 使用示例
ts_audit_log(req->username, "system.reboot", "User initiated system reboot");
```

---

## 双向认证（mTLS）

### 应用场景

1. **设备间通信**：TianShanOS 设备互联
2. **API 客户端认证**：仅允许可信设备访问 API
3. **自动化工具**：CI/CD 脚本、监控系统

### 实施方案

#### 1. CA 证书生成（Root 设备）

```c
// 在 Root 设备上生成 CA
esp_err_t ts_ca_generate(ts_cert_config_t *ca_config);

// 签发客户端证书
esp_err_t ts_ca_sign_client_cert(const ts_cert_config_t *ca, 
                                   const char *client_cn,
                                   ts_cert_config_t *out_client_cert);
```

#### 2. HTTPS Server 配置

```c
httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();

// 启用客户端证书验证
conf.client_verify_cert_pem = ca_cert_pem;
conf.client_verify_cert_len = ca_cert_len;

httpd_ssl_start(&server, &conf);
```

#### 3. 客户端证书管理

**WebUI 流程**：
1. 管理员在"安全 → 客户端证书"页面生成证书
2. 下载 `.p12` 文件（包含证书和私钥）
3. 浏览器导入证书（Chrome/Firefox 设置）
4. 访问设备时自动使用证书认证

**API 客户端示例**：
```python
import requests

# 使用客户端证书访问 API
response = requests.get(
    'https://192.168.1.100/api/v1/system/info',
    cert=('client.crt', 'client.key'),
    verify='ca.crt'  # 验证服务器证书
)
```

#### 4. 白名单机制

```c
// 存储允许的客户端证书指纹
typedef struct {
    char fingerprint[64];    // SHA256 指纹
    char description[64];    // 如 "CI/CD Bot"
    uint32_t added_at;
} ts_client_cert_whitelist_t;

// 验证客户端证书
bool ts_mtls_verify_client(const char *cert_der, size_t cert_len) {
    // 1. 计算证书指纹
    char fingerprint[64];
    mbedtls_sha256(cert_der, cert_len, fingerprint, 0);
    
    // 2. 检查白名单
    return ts_whitelist_contains(fingerprint);
}
```

---

## 实施路线图

### Phase 1：基础 HTTPS（优先级：高）

**时间**：1-2 周  
**工作量**：中等

- [ ] 实现 `ts_cert` 证书管理模块
- [ ] 集成 `esp_https_server`
- [ ] 添加 Kconfig 配置选项
- [ ] 实现 CLI `cert` 命令
- [ ] WebUI 证书管理界面
- [ ] HTTP → HTTPS 重定向
- [ ] 测试和文档

**交付物**：
- HTTPS 服务器运行（端口 443）
- 自签名证书自动生成
- 浏览器可访问（需信任证书）

---

### Phase 2：权限管理（优先级：中）

**时间**：2-3 周  
**工作量**：较大

- [ ] 实现 `ts_auth` 用户管理模块
- [ ] 定义角色和权限矩阵
- [ ] API 层权限检查机制
- [ ] WebUI Session 管理
- [ ] 用户管理界面（Root 专用）
- [ ] 审计日志功能
- [ ] 防暴力破解（登录限制）
- [ ] 测试和文档

**交付物**：
- 4 级角色系统（Guest/Operator/Admin/Root）
- WebUI 权限控制
- API 权限验证
- 审计日志

---

### Phase 3：双向认证（优先级：低）

**时间**：2-3 周  
**工作量**：较大

- [ ] 实现 CA 证书管理
- [ ] 客户端证书签发
- [ ] mbedTLS 客户端证书验证
- [ ] 白名单机制
- [ ] WebUI 客户端证书管理
- [ ] API 客户端库示例（Python/Node.js）
- [ ] 测试和文档

**交付物**：
- 完整的 mTLS 支持
- 客户端证书生成和管理
- API 客户端认证

---

## 技术选型

### TLS 库：mbedTLS

**优点**：
- ✅ ESP-IDF 内置，无需额外依赖
- ✅ 内存占用小（适合嵌入式）
- ✅ 支持 TLS 1.2/1.3
- ✅ 硬件加速（ESP32-S3 支持）

**缺点**：
- ⚠️ API 较底层，需要封装
- ⚠️ 证书操作需要手动处理

### 证书存储：NVS + SPIFFS

**方案**：
- **NVS**：存储小型证书（< 4KB）和私钥
- **SPIFFS/SD**：存储 CA 证书链和客户端证书集合

```c
// NVS 键名
#define NVS_CERT_NS "certs"
#define NVS_KEY_CERT_PEM "cert"
#define NVS_KEY_PRIVKEY_PEM "key"
#define NVS_KEY_CA_PEM "ca"
```

### 会话管理：JWT Token

**方案**：
```c
// 登录时生成 Token
char* ts_auth_generate_token(const char *username, ts_user_role_t role) {
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "username", username);
    cJSON_AddNumberToObject(payload, "role", role);
    cJSON_AddNumberToObject(payload, "exp", time(NULL) + 3600); // 1 hour
    
    char *jwt = jwt_encode(payload, secret_key);
    cJSON_Delete(payload);
    return jwt;
}
```

**优点**：
- 无需服务器端 Session 存储
- 可嵌入用户角色信息
- 支持过期时间

---

## 配置示例

### menuconfig 配置

```
TianShanOS Configuration → Security
    [*] Enable HTTPS
        (443) HTTPS Port
        [*] Redirect HTTP to HTTPS
        [*] Auto-generate self-signed certificate
        (3650) Certificate expiry (days)
    
    [*] Enable User Authentication
        (4) Maximum users
        [*] Enable audit logging
        (5) Max login attempts before lock
        (300) Login attempt reset time (seconds)
    
    [ ] Enable mTLS (mutual TLS authentication)
        [ ] Require client certificate for API access
```

### 默认用户

```c
// 首次启动创建默认用户
#define DEFAULT_ADMIN_USER "admin"
#define DEFAULT_ADMIN_PASS "tianshan"  // 首次登录后强制修改

void ts_auth_init_default_users(void) {
    if (!ts_auth_user_exists(DEFAULT_ADMIN_USER)) {
        ts_auth_create_user(DEFAULT_ADMIN_USER, DEFAULT_ADMIN_PASS, TS_ROLE_ROOT);
        ESP_LOGW(TAG, "Created default admin user. Please change password!");
    }
}
```

---

## 安全最佳实践

### 1. 密码策略
- ✅ 强制最小长度（8 字符）
- ✅ 密码强度检查（包含大小写+数字+符号）
- ✅ 禁止常见弱密码（`password`、`123456`）
- ✅ 密码历史（不能重复最近 3 个密码）

### 2. Token 安全
- ✅ 短期有效（1 小时）
- ✅ HttpOnly Cookie（防 XSS）
- ✅ CSRF Token 验证
- ✅ 刷新 Token 机制

### 3. 证书管理
- ✅ 证书到期前 30 天警告
- ✅ 自动续期（Let's Encrypt）
- ✅ 私钥加密存储（可选）
- ✅ 证书吊销列表（CRL）

### 4. 审计和监控
- ✅ 记录所有登录（成功/失败）
- ✅ 记录敏感操作（重启、OTA、配置修改）
- ✅ 异常登录告警（多次失败、新 IP）
- ✅ 导出审计日志

---

## 兼容性考虑

### HTTP 并存模式

**方案**：同时监听 HTTP（80）和 HTTPS（443）
- HTTP 仅允许查看仪表盘（只读）
- 敏感操作强制 HTTPS
- 配置选项：`TS_HTTP_ALLOWED_PAGES`

```c
// HTTP Server 中间件
static esp_err_t http_enforce_https(httpd_req_t *req) {
    const char *uri = req->uri;
    
    // 允许无 HTTPS 访问的路径
    const char *allowed[] = {"/", "/static/*", "/api/v1/system/info"};
    
    if (!is_uri_allowed(uri, allowed)) {
        // 重定向到 HTTPS
        httpd_resp_set_hdr(req, "Location", https_url);
        httpd_resp_set_status(req, "301 Moved Permanently");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    
    return ESP_OK;
}
```

### 向后兼容

- 默认配置：HTTPS 关闭，所有用户均为 Admin
- 升级后首次启动：显示安全配置向导
- 旧 API Token 自动迁移
- 配置导入/导出兼容性

---

## 总结

### 推荐实施顺序

1. **Phase 1: HTTPS**（必需，2 周）
   - 保护传输安全
   - 防止中间人攻击
   - 符合现代安全标准

2. **Phase 2: 权限管理**（推荐，3 周）
   - 多用户环境必需
   - 防止误操作
   - 审计追踪

3. **Phase 3: mTLS**（可选，3 周）
   - 高安全要求场景
   - 设备间互信
   - API 自动化工具

### 资源需求

| 项目 | Flash | RAM | 开发时间 |
|------|-------|-----|----------|
| HTTPS | +80KB | +20KB | 2 周 |
| 权限管理 | +40KB | +10KB | 3 周 |
| mTLS | +60KB | +15KB | 3 周 |
| **总计** | **+180KB** | **+45KB** | **8 周** |

**可行性**：
- ✅ Flash 空间充足（当前使用 36%，剩余 ~1.1MB）
- ✅ RAM 可接受（ESP32-S3 有 512KB）
- ✅ 开发时间合理

---

## 下一步行动

**立即可做**：
1. ✅ 评审本方案
2. ⏳ 确定 Phase 1 实施时间
3. ⏳ 创建 `components/ts_cert` 模块骨架
4. ⏳ 测试 ESP-IDF HTTPS Server 示例

**需要讨论**：
1. 证书 CN（Common Name）默认值？（建议：`tianshanos.local`）
2. 是否支持 Let's Encrypt？（公网设备）
3. 默认角色策略？（建议：首次启动创建 `admin` 用户）
4. 审计日志存储位置？（NVS vs SD 卡）

---

## 参考资料

- [ESP-IDF HTTPS Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_https_server.html)
- [mbedTLS Documentation](https://tls.mbed.org/api/)
- [OWASP Authentication Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Authentication_Cheat_Sheet.html)
- [RFC 8446 - TLS 1.3](https://tools.ietf.org/html/rfc8446)
