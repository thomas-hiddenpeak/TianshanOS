# TianShanOS PKI Server

基于 FastAPI 的 PKI 证书管理 Web 服务。

## 功能

- 📋 **CSR 请求管理** - 接收设备 CSR，手动/自动审批
- 📜 **证书管理** - 签发、查看、下载、吊销证书
- 📱 **设备白名单** - 配置自动审批的设备
- 📊 **仪表盘** - 统计信息和 CA 状态
- 📝 **审计日志** - 所有操作记录

## 快速开始

```bash
# 1. 安装依赖
cd ~/tianshan-pki/pki-server
pip install -r requirements.txt

# 2. 启动服务
python run.py

# 3. 访问 Web 界面
# http://localhost:8443
# 默认密码: tianshan-pki-admin
```

## API 端点

### 设备端（ESP32 调用）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/csr/submit` | 提交 CSR |
| GET | `/api/csr/status/{id}` | 查询请求状态 |
| GET | `/api/ca/chain` | 获取 CA 链（公开） |

### 管理端（需认证）

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/auth/login` | 登录获取 Token |
| GET | `/api/dashboard` | 仪表盘统计 |
| GET | `/api/requests` | 待审批列表 |
| POST | `/api/requests/{id}/approve` | 审批通过 |
| POST | `/api/requests/{id}/reject` | 拒绝 |
| GET | `/api/certificates` | 证书列表 |
| POST | `/api/certificates/{id}/revoke` | 吊销证书 |
| GET/POST | `/api/whitelist` | 设备白名单管理 |

## ESP32 集成示例

```c
// 提交 CSR
esp_http_client_config_t config = {
    .url = "http://10.10.99.100:8443/api/csr/submit",
    .method = HTTP_METHOD_POST,
};

cJSON *body = cJSON_CreateObject();
cJSON_AddStringToObject(body, "device_id", "rm01");
cJSON_AddStringToObject(body, "csr_pem", csr_pem);
cJSON_AddStringToObject(body, "device_token", DEVICE_TOKEN);

// 发送请求...

// 响应示例（自动签发）
{
    "request_id": 1,
    "status": "approved",
    "certificate": "-----BEGIN CERTIFICATE-----...",
    "ca_chain": "-----BEGIN CERTIFICATE-----..."
}

// 响应示例（等待审批）
{
    "request_id": 2,
    "status": "pending",
    "message": "CSR submitted, waiting for approval"
}
```

## 配置

编辑 `.env` 文件：

```bash
# CA 路径
CA_CERT_PATH=/home/tom/tianshan-pki/step-ca/certs/intermediate_ca.crt
CA_KEY_PATH=/home/tom/tianshan-pki/step-ca/secrets/intermediate_ca_key
CA_KEY_PASSWORD=tianshan-intermediate-2026

# 服务器
HOST=0.0.0.0
PORT=8443

# 签发策略
DEFAULT_VALIDITY_DAYS=365
AUTO_SIGN_ENABLED=false       # 全局自动签发
REQUIRE_DEVICE_TOKEN=true     # 要求设备 Token

# 管理密码
ADMIN_PASSWORD=tianshan-pki-admin
```

## 签发模式

| 模式 | 设置 | 说明 |
|------|------|------|
| 手动审批 | `AUTO_SIGN_ENABLED=false` | 所有请求需管理员审批 |
| 白名单自动 | 设备 `auto_approve=true` | 白名单内设备自动签发 |
| 完全自动 | `AUTO_SIGN_ENABLED=true` | 所有请求自动签发（危险） |

## 目录结构

```
pki-server/
├── app/
│   ├── __init__.py
│   ├── main.py        # FastAPI 主应用
│   ├── config.py      # 配置
│   ├── database.py    # SQLite 数据库
│   ├── ca.py          # 证书签发逻辑
│   └── models.py      # Pydantic 模型
├── templates/
│   ├── index.html     # 管理界面
│   └── login.html     # 登录页
├── static/            # 静态资源
├── requirements.txt
├── run.py             # 启动脚本
├── .env               # 配置文件
└── pki.db             # SQLite 数据库（运行后生成）
```
