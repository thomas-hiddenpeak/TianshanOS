"""
TianShanOS PKI Server - API 模型
"""
from pydantic import BaseModel, Field
from typing import Optional, List
from datetime import datetime


# ============== CSR 请求相关 ==============

class CSRSubmitRequest(BaseModel):
    """CSR 提交请求"""
    device_id: str = Field(..., description="设备唯一标识")
    csr_pem: str = Field(..., description="PEM 格式的 CSR")
    device_token: Optional[str] = Field(None, description="设备认证 Token")
    device_ip: Optional[str] = Field(None, description="设备 IP 地址")
    validity_days: Optional[int] = Field(365, description="请求的有效期天数")
    cert_type: Optional[str] = Field("server", description="证书类型: server/client/both")
    suggested_san_ips: Optional[List[str]] = Field(None, description="建议添加的 IP SAN")
    suggested_san_dns: Optional[List[str]] = Field(None, description="建议添加的 DNS SAN")


class CSRSubmitResponse(BaseModel):
    """CSR 提交响应"""
    request_id: int
    status: str
    message: str
    # 如果自动签发，返回证书
    certificate: Optional[str] = None
    ca_chain: Optional[str] = None


class CSRApproveRequest(BaseModel):
    """CSR 审批请求"""
    validity_days: Optional[int] = Field(365, description="有效期天数")
    cert_type: Optional[str] = Field(None, description="覆盖证书类型")
    device_type: Optional[str] = Field("Device", description="设备类型: Device(普通设备) 或 Developer(开发机)")
    additional_ips: Optional[List[str]] = Field(None, description="额外的 IP SAN")
    additional_dns: Optional[List[str]] = Field(None, description="额外的 DNS SAN")


class CSRRejectRequest(BaseModel):
    """CSR 拒绝请求"""
    reason: str = Field(..., description="拒绝原因")


class CSRInfo(BaseModel):
    """CSR 请求信息"""
    id: int
    device_id: str
    device_ip: Optional[str]
    common_name: str
    san_ips: Optional[str]
    san_dns: Optional[str]
    status: str
    validity_days: int
    cert_type: str
    created_at: str
    processed_at: Optional[str]
    processed_by: Optional[str]
    reject_reason: Optional[str]


# ============== 证书相关 ==============

class CertificateInfo(BaseModel):
    """证书信息"""
    id: int
    device_id: str
    common_name: str
    serial_number: str
    not_before: str
    not_after: str
    issued_at: str
    issued_by: Optional[str]
    revoked_at: Optional[str]
    revoke_reason: Optional[str]
    is_valid: bool
    days_until_expiry: int


class CertificateRevokeRequest(BaseModel):
    """证书吊销请求"""
    reason: str = Field(..., description="吊销原因")


# ============== 设备白名单相关 ==============

class DeviceWhitelistAdd(BaseModel):
    """添加设备到白名单"""
    device_token: str = Field(..., description="设备 Token")
    device_name: Optional[str] = Field(None, description="设备名称")
    description: Optional[str] = Field(None, description="描述")
    auto_approve: bool = Field(False, description="是否自动审批")
    validity_days: int = Field(365, description="自动签发时的有效期")


class DeviceWhitelistInfo(BaseModel):
    """设备白名单信息"""
    id: int
    device_token: str
    device_name: Optional[str]
    description: Optional[str]
    auto_approve: bool
    validity_days: int
    created_at: str
    last_used_at: Optional[str]


# ============== 配置相关 ==============

class SigningConfig(BaseModel):
    """签发配置"""
    default_validity_days: int
    auto_sign_enabled: bool
    require_device_token: bool


class SigningConfigUpdate(BaseModel):
    """更新签发配置"""
    default_validity_days: Optional[int] = None
    auto_sign_enabled: Optional[bool] = None
    require_device_token: Optional[bool] = None


# ============== 统计相关 ==============

class DashboardStats(BaseModel):
    """仪表盘统计"""
    pending_requests: int
    total_certificates: int
    valid_certificates: int
    expiring_soon: int  # 30 天内过期
    revoked_certificates: int
    whitelist_devices: int
    ca_days_until_expiry: int


# ============== 认证相关 ==============

class LoginRequest(BaseModel):
    """登录请求"""
    password: str


class LoginResponse(BaseModel):
    """登录响应"""
    token: str
    expires_at: str


# ============== 客户端证书相关 ==============

class ClientCertGenerateRequest(BaseModel):
    """客户端证书生成请求（在服务器端生成密钥对）"""
    name: str = Field(..., description="证书名称/标识（如：admin、operator）")
    email: Optional[str] = Field(None, description="邮箱地址（可选，用于 CN）")
    role: str = Field("operator", description="角色/权限级别（admin/operator/viewer），写入证书 OU 字段")
    validity_days: int = Field(365, description="有效期天数")
    description: Optional[str] = Field(None, description="用途描述")


class ClientCertGenerateResponse(BaseModel):
    """客户端证书生成响应"""
    cert_id: int
    name: str
    serial_number: str
    not_before: str
    not_after: str
    # PEM 格式证书和私钥
    certificate: str
    private_key: str
    ca_chain: str
    # PKCS12 格式（Base64 编码）
    pkcs12_base64: str
    pkcs12_password: str
