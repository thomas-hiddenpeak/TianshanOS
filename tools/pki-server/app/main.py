"""
TianShanOS PKI Server - 主应用
"""
from fastapi import FastAPI, HTTPException, Request, Depends, Header
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse, JSONResponse
from fastapi.middleware.cors import CORSMiddleware
from datetime import datetime, timedelta
from typing import Optional
import os
import secrets
import hashlib

from .config import get_settings
from .database import (
    init_db, create_csr_request, get_pending_requests, get_request_by_id,
    update_request_status, RequestStatus, create_certificate, get_all_certificates,
    get_certificate_by_id, revoke_certificate, delete_certificate, add_device_to_whitelist,
    get_device_by_token, get_all_whitelist_devices, update_device_last_used,
    delete_device_from_whitelist, add_audit_log, get_audit_logs
)
from .ca import get_ca
from .models import (
    ClientCertGenerateRequest, ClientCertGenerateResponse,
    CSRSubmitRequest, CSRSubmitResponse, CSRApproveRequest, CSRRejectRequest,
    CSRInfo, CertificateInfo, CertificateRevokeRequest, DeviceWhitelistAdd,
    DeviceWhitelistInfo, SigningConfig, SigningConfigUpdate, DashboardStats,
    LoginRequest, LoginResponse
)

settings = get_settings()

app = FastAPI(
    title="TianShanOS PKI Server",
    description="PKI 证书管理服务",
    version="1.0.0"
)

# CORS
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 静态文件和模板
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
app.mount("/static", StaticFiles(directory=os.path.join(BASE_DIR, "static")), name="static")
templates = Jinja2Templates(directory=os.path.join(BASE_DIR, "templates"))

# 配置 Jinja2 不解析 Vue 模板语法
templates.env.variable_start_string = '[['
templates.env.variable_end_string = ']]'

# 简单的 Token 存储
active_tokens = {}


# ============== 认证 ==============

def create_token() -> str:
    """创建认证 Token"""
    token = secrets.token_urlsafe(32)
    expires = datetime.now() + timedelta(hours=24)
    active_tokens[token] = expires
    return token


def verify_token(authorization: Optional[str] = Header(None)) -> bool:
    """验证 Token"""
    if not authorization:
        raise HTTPException(status_code=401, detail="Missing authorization")
    
    token = authorization.replace("Bearer ", "")
    if token not in active_tokens:
        raise HTTPException(status_code=401, detail="Invalid token")
    
    if datetime.now() > active_tokens[token]:
        del active_tokens[token]
        raise HTTPException(status_code=401, detail="Token expired")
    
    return True


# ============== 启动事件 ==============

@app.on_event("startup")
async def startup():
    """应用启动"""
    await init_db()
    # 预加载 CA
    try:
        ca = get_ca()
        print(f"✅ CA loaded: {ca.get_ca_info()['subject']}")
    except Exception as e:
        print(f"❌ Failed to load CA: {e}")



# ============== 健康检查 ==============

@app.get("/api/health")
async def health_check():
    """健康检查端点 - 供 ESP32 设备检测服务器可达性"""
    return {"status": "ok", "timestamp": datetime.now().isoformat()}

# ============== 页面路由 ==============

@app.get("/", response_class=HTMLResponse)
async def index():
    """主页"""
    html_path = os.path.join(BASE_DIR, "templates", "index.html")
    with open(html_path, "r", encoding="utf-8") as f:
        return HTMLResponse(content=f.read())


@app.get("/login", response_class=HTMLResponse)
async def login_page():
    """登录页"""
    html_path = os.path.join(BASE_DIR, "templates", "login.html")
    with open(html_path, "r", encoding="utf-8") as f:
        return HTMLResponse(content=f.read())


# ============== 认证 API ==============

@app.post("/api/auth/login", response_model=LoginResponse)
async def login(req: LoginRequest):
    """登录"""
    if req.password != settings.admin_password:
        raise HTTPException(status_code=401, detail="Invalid password")
    
    token = create_token()
    expires = datetime.now() + timedelta(hours=24)
    
    await add_audit_log("LOGIN", "admin", None, "admin", "Admin logged in")
    
    return LoginResponse(token=token, expires_at=expires.isoformat())


@app.post("/api/auth/logout")
async def logout(authorization: Optional[str] = Header(None)):
    """登出"""
    if authorization:
        token = authorization.replace("Bearer ", "")
        if token in active_tokens:
            del active_tokens[token]
    return {"message": "Logged out"}


# ============== CSR API (设备端调用) ==============

@app.post("/api/csr/submit", response_model=CSRSubmitResponse)
async def submit_csr(req: CSRSubmitRequest, request: Request):
    """
    设备提交 CSR
    
    这是设备调用的接口，用于提交证书签发请求。
    如果设备 Token 在白名单且设置为自动审批，会直接返回证书。
    """
    ca = get_ca()
    
    # 解析 CSR
    try:
        csr_info = ca.parse_csr(req.csr_pem)
    except Exception as e:
        raise HTTPException(status_code=400, detail=f"Invalid CSR: {e}")
    
    # 检查设备 Token
    auto_approve = False
    validity_days = req.validity_days or settings.default_validity_days
    
    if req.device_token:
        device = await get_device_by_token(req.device_token)
        if device:
            await update_device_last_used(req.device_token)
            if device["auto_approve"]:
                auto_approve = True
                validity_days = device["validity_days"]
        elif settings.require_device_token:
            raise HTTPException(status_code=403, detail="Device token not in whitelist")
    elif settings.require_device_token:
        raise HTTPException(status_code=403, detail="Device token required")
    
    # 全局自动签发
    if settings.auto_sign_enabled:
        auto_approve = True
    
    # 创建请求记录
    request_id = await create_csr_request(
        device_id=req.device_id,
        common_name=csr_info["common_name"],
        csr_pem=req.csr_pem,
        device_ip=req.device_ip or request.client.host,
        device_token=req.device_token,
        san_ips=",".join(csr_info["san_ips"]) if csr_info["san_ips"] else None,
        san_dns=",".join(csr_info["san_dns"]) if csr_info["san_dns"] else None,
        validity_days=validity_days,
        cert_type=req.cert_type or "server"
    )
    
    await add_audit_log(
        "CSR_SUBMIT", "csr_request", str(request_id),
        req.device_id, f"CSR submitted: CN={csr_info['common_name']}",
        request.client.host
    )
    
    # 自动签发
    if auto_approve:
        try:
            # 合并 CSR 中的 SAN 和请求建议的 SAN
            additional_ips = list(set((csr_info.get("san_ips") or []) + (req.suggested_san_ips or [])))
            additional_dns = list(set((csr_info.get("san_dns") or []) + (req.suggested_san_dns or [])))
            
            cert_pem, serial_number, not_before, not_after = ca.sign_csr(
                req.csr_pem,
                validity_days=validity_days,
                cert_type=req.cert_type or "server",
                additional_ips=additional_ips if additional_ips else None,
                additional_dns=additional_dns if additional_dns else None
            )
            
            await update_request_status(request_id, RequestStatus.APPROVED, "auto")
            
            await create_certificate(
                request_id=request_id,
                device_id=req.device_id,
                common_name=csr_info["common_name"],
                serial_number=serial_number,
                cert_pem=cert_pem,
                not_before=not_before,
                not_after=not_after,
                issued_by="auto"
            )
            
            await add_audit_log(
                "CERT_ISSUED", "certificate", serial_number,
                "auto", f"Auto-issued: CN={csr_info['common_name']}",
                request.client.host
            )
            
            return CSRSubmitResponse(
                request_id=request_id,
                status="approved",
                message="Certificate issued automatically",
                certificate=cert_pem,
                ca_chain=ca.get_ca_chain()
            )
        except Exception as e:
            await update_request_status(request_id, RequestStatus.REJECTED, "auto", str(e))
            raise HTTPException(status_code=500, detail=f"Auto-sign failed: {e}")
    
    return CSRSubmitResponse(
        request_id=request_id,
        status="pending",
        message="CSR submitted, waiting for approval"
    )


@app.get("/api/csr/status/{request_id}")
async def get_csr_status(request_id: int):
    """
    查询 CSR 请求状态
    
    设备可以轮询此接口等待审批结果。
    """
    ca = get_ca()
    req = await get_request_by_id(request_id)
    
    if not req:
        raise HTTPException(status_code=404, detail="Request not found")
    
    result = {
        "request_id": request_id,
        "status": req["status"],
    }
    
    if req["status"] == RequestStatus.APPROVED:
        # 获取对应的证书
        from .database import aiosqlite
        async with aiosqlite.connect(settings.database_path) as db:
            db.row_factory = aiosqlite.Row
            cursor = await db.execute(
                "SELECT cert_pem FROM certificates WHERE request_id = ?",
                (request_id,)
            )
            row = await cursor.fetchone()
            if row:
                result["certificate"] = row["cert_pem"]
                result["ca_chain"] = ca.get_ca_chain()
    elif req["status"] == RequestStatus.REJECTED:
        result["reject_reason"] = req["reject_reason"]
    
    return result


# ============== 管理 API (需要认证) ==============

@app.get("/api/dashboard", response_model=DashboardStats)
async def get_dashboard(auth: bool = Depends(verify_token)):
    """获取仪表盘统计"""
    ca = get_ca()
    certs = await get_all_certificates()
    pending = await get_pending_requests()
    whitelist = await get_all_whitelist_devices()
    
    now = datetime.now()
    valid = 0
    expiring = 0
    revoked = 0
    
    for cert in certs:
        if cert["revoked_at"]:
            revoked += 1
        else:
            not_after = datetime.fromisoformat(cert["not_after"])
            if not_after > now:
                valid += 1
                if (not_after - now).days <= 30:
                    expiring += 1
    
    return DashboardStats(
        pending_requests=len(pending),
        total_certificates=len(certs),
        valid_certificates=valid,
        expiring_soon=expiring,
        revoked_certificates=revoked,
        whitelist_devices=len(whitelist),
        ca_days_until_expiry=ca.get_ca_info()["days_until_expiry"]
    )


@app.get("/api/requests")
async def list_requests(auth: bool = Depends(verify_token)):
    """获取所有待审批请求"""
    requests = await get_pending_requests()
    return {"requests": requests}


@app.get("/api/requests/{request_id}")
async def get_request(request_id: int, auth: bool = Depends(verify_token)):
    """获取请求详情"""
    req = await get_request_by_id(request_id)
    if not req:
        raise HTTPException(status_code=404, detail="Request not found")
    
    # 解析 CSR 获取更多信息
    ca = get_ca()
    try:
        csr_info = ca.parse_csr(req["csr_pem"])
        req["csr_info"] = csr_info
    except:
        pass
    
    return req


@app.post("/api/requests/{request_id}/approve")
async def approve_request(
    request_id: int,
    req: CSRApproveRequest,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """审批通过 CSR 请求"""
    ca = get_ca()
    csr_req = await get_request_by_id(request_id)
    
    if not csr_req:
        raise HTTPException(status_code=404, detail="Request not found")
    
    if csr_req["status"] != RequestStatus.PENDING:
        raise HTTPException(status_code=400, detail="Request already processed")
    
    validity_days = req.validity_days or csr_req["validity_days"]
    cert_type = req.cert_type or csr_req["cert_type"]
    
    # 合并 SAN：优先使用审批时指定的，否则使用原始请求中的 + 设备 IP
    additional_ips = req.additional_ips or []
    if not additional_ips:
        # 从原始请求获取 san_ips
        if csr_req.get("san_ips"):
            additional_ips = csr_req["san_ips"].split(",")
        # 如果还是没有，使用设备 IP
        if not additional_ips and csr_req.get("device_ip"):
            additional_ips = [csr_req["device_ip"]]
    
    try:
        # 获取设备类型（默认为 Device）
        device_type = req.device_type or "Device"
        
        cert_pem, serial_number, not_before, not_after = ca.sign_csr(
            csr_req["csr_pem"],
            validity_days=validity_days,
            cert_type=cert_type,
            device_type=device_type,
            additional_ips=additional_ips if additional_ips else None,
            additional_dns=req.additional_dns
        )
        
        await update_request_status(request_id, RequestStatus.APPROVED, "admin")
        
        cert_id = await create_certificate(
            request_id=request_id,
            device_id=csr_req["device_id"],
            common_name=csr_req["common_name"],
            serial_number=serial_number,
            cert_pem=cert_pem,
            not_before=not_before,
            not_after=not_after,
            issued_by="admin"
        )
        
        await add_audit_log(
            "CERT_ISSUED", "certificate", serial_number,
            "admin", f"Approved: CN={csr_req['common_name']}, validity={validity_days}d",
            request.client.host
        )
        
        return {
            "message": "Certificate issued",
            "certificate_id": cert_id,
            "serial_number": serial_number
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Signing failed: {e}")


@app.post("/api/requests/{request_id}/reject")
async def reject_request(
    request_id: int,
    req: CSRRejectRequest,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """拒绝 CSR 请求"""
    csr_req = await get_request_by_id(request_id)
    
    if not csr_req:
        raise HTTPException(status_code=404, detail="Request not found")
    
    if csr_req["status"] != RequestStatus.PENDING:
        raise HTTPException(status_code=400, detail="Request already processed")
    
    await update_request_status(request_id, RequestStatus.REJECTED, "admin", req.reason)
    
    await add_audit_log(
        "CSR_REJECTED", "csr_request", str(request_id),
        "admin", f"Rejected: {req.reason}",
        request.client.host
    )
    
    return {"message": "Request rejected"}


# ============== 证书管理 API ==============

@app.get("/api/certificates")
async def list_certificates(auth: bool = Depends(verify_token)):
    """获取所有证书"""
    certs = await get_all_certificates()
    
    now = datetime.now()
    result = []
    for cert in certs:
        not_after = datetime.fromisoformat(cert["not_after"])
        days_until_expiry = (not_after - now).days
        
        result.append({
            **cert,
            "is_valid": cert["revoked_at"] is None and not_after > now,
            "days_until_expiry": days_until_expiry
        })
    
    return {"certificates": result}


@app.get("/api/certificates/{cert_id}")
async def get_certificate(cert_id: int, auth: bool = Depends(verify_token)):
    """获取证书详情"""
    cert = await get_certificate_by_id(cert_id)
    if not cert:
        raise HTTPException(status_code=404, detail="Certificate not found")
    return cert


@app.get("/api/certificates/{cert_id}/download")
async def download_certificate(cert_id: int, format: str = "pem", auth: bool = Depends(verify_token)):
    """
    下载证书
    
    支持格式：
    - pem: PEM 格式（默认）
    - der: DER 二进制格式
    - pkcs12: PKCS#12 格式（需要私钥，仅适用于客户端证书）
    """
    from cryptography import x509
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.serialization import Encoding, pkcs12
    from cryptography.hazmat.backends import default_backend
    import base64
    import secrets
    
    cert = await get_certificate_by_id(cert_id)
    if not cert:
        raise HTTPException(status_code=404, detail="Certificate not found")
    
    ca = get_ca()
    
    result = {
        "certificate": cert["cert_pem"],
        "ca_chain": ca.get_ca_chain(),
        "has_private_key": cert.get("private_key_pem") is not None
    }
    
    # 如果请求 DER 格式，转换证书
    if format == "der":
        try:
            cert_obj = x509.load_pem_x509_certificate(cert["cert_pem"].encode())
            der_bytes = cert_obj.public_bytes(Encoding.DER)
            result["der_base64"] = base64.b64encode(der_bytes).decode()
        except Exception as e:
            # 转换失败时只返回 PEM
            pass
    
    # PKCS12 格式需要私钥
    if format == "pkcs12":
        private_key_pem = cert.get("private_key_pem")
        if not private_key_pem:
            raise HTTPException(
                status_code=400, 
                detail="PKCS12 格式需要私钥。此证书没有存储私钥（设备证书的私钥永远不会离开设备）。"
            )
        
        try:
            # 加载证书和私钥
            cert_obj = x509.load_pem_x509_certificate(cert["cert_pem"].encode())
            private_key = serialization.load_pem_private_key(
                private_key_pem.encode(),
                password=None,
                backend=default_backend()
            )
            
            # 生成 PKCS12
            pkcs12_password = secrets.token_urlsafe(12)
            pkcs12_data = pkcs12.serialize_key_and_certificates(
                name=cert["common_name"].encode(),
                key=private_key,
                cert=cert_obj,
                cas=[ca.ca_cert],
                encryption_algorithm=serialization.BestAvailableEncryption(pkcs12_password.encode())
            )
            
            result["pkcs12_base64"] = base64.b64encode(pkcs12_data).decode()
            result["pkcs12_password"] = pkcs12_password
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"生成 PKCS12 失败: {e}")
    
    return result


@app.delete("/api/certificates/{cert_id}")
async def delete_cert(
    cert_id: int,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """
    删除证书（仅限已吊销或已过期的证书）
    """
    cert = await get_certificate_by_id(cert_id)
    if not cert:
        raise HTTPException(status_code=404, detail="Certificate not found")
    
    # 检查是否可以删除
    now = datetime.now()
    not_after = datetime.fromisoformat(cert["not_after"])
    is_expired = not_after < now
    is_revoked = cert["revoked_at"] is not None
    
    if not is_expired and not is_revoked:
        raise HTTPException(
            status_code=400, 
            detail="只能删除已过期或已吊销的证书"
        )
    
    await delete_certificate(cert_id)
    
    await add_audit_log(
        "CERT_DELETED", "certificate", cert["serial_number"],
        "admin", f"Deleted: CN={cert['common_name']}",
        request.client.host
    )
    
    return {"message": "Certificate deleted"}


@app.post("/api/certificates/{cert_id}/revoke")
async def revoke_cert(
    cert_id: int,
    req: CertificateRevokeRequest,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """吊销证书"""
    cert = await get_certificate_by_id(cert_id)
    if not cert:
        raise HTTPException(status_code=404, detail="Certificate not found")
    
    if cert["revoked_at"]:
        raise HTTPException(status_code=400, detail="Certificate already revoked")
    
    await revoke_certificate(cert_id, req.reason, "admin")
    
    await add_audit_log(
        "CERT_REVOKED", "certificate", cert["serial_number"],
        "admin", f"Revoked: {req.reason}",
        request.client.host
    )
    
    return {"message": "Certificate revoked"}


# ============== 设备白名单 API ==============

@app.get("/api/whitelist")
async def list_whitelist(auth: bool = Depends(verify_token)):
    """获取设备白名单"""
    devices = await get_all_whitelist_devices()
    return {"devices": devices}


@app.post("/api/whitelist")
async def add_to_whitelist(
    req: DeviceWhitelistAdd,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """添加设备到白名单"""
    existing = await get_device_by_token(req.device_token)
    if existing:
        raise HTTPException(status_code=400, detail="Token already exists")
    
    device_id = await add_device_to_whitelist(
        device_token=req.device_token,
        device_name=req.device_name,
        description=req.description,
        auto_approve=req.auto_approve,
        validity_days=req.validity_days
    )
    
    await add_audit_log(
        "WHITELIST_ADD", "device", req.device_token,
        "admin", f"Added: {req.device_name or req.device_token}",
        request.client.host
    )
    
    return {"message": "Device added", "id": device_id}


@app.delete("/api/whitelist/{device_id}")
async def remove_from_whitelist(
    device_id: int,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """从白名单移除设备"""
    await delete_device_from_whitelist(device_id)
    
    await add_audit_log(
        "WHITELIST_REMOVE", "device", str(device_id),
        "admin", "Device removed",
        request.client.host
    )
    
    return {"message": "Device removed"}


# ============== CA 信息 API ==============

@app.get("/api/ca/info")
async def get_ca_info(auth: bool = Depends(verify_token)):
    """获取 CA 信息"""
    ca = get_ca()
    return ca.get_ca_info()


@app.get("/api/ca/chain")
async def get_ca_chain():
    """获取 CA 链（公开）"""
    ca = get_ca()
    return {"ca_chain": ca.get_ca_chain()}


# ============== 审计日志 API ==============

@app.get("/api/audit-logs")
async def get_logs(limit: int = 100, auth: bool = Depends(verify_token)):
    """获取审计日志"""
    logs = await get_audit_logs(limit)
    return {"logs": logs}


# ============== 配置 API ==============

@app.get("/api/config")
async def get_config(auth: bool = Depends(verify_token)):
    """获取配置"""
    return SigningConfig(
        default_validity_days=settings.default_validity_days,
        auto_sign_enabled=settings.auto_sign_enabled,
        require_device_token=settings.require_device_token
    )


# ============== 客户端证书 API ==============

@app.get("/api/client-certs")
async def list_client_certs(auth: bool = Depends(verify_token)):
    """
    获取所有客户端证书列表
    
    客户端证书和设备证书都存储在同一个表中，通过 device_id 区分：
    - 设备证书: device_id 以 TIANSHAN- 开头
    - 客户端证书: device_id 为 client:xxx 格式
    """
    certs = await get_all_certificates()
    
    now = datetime.now()
    client_certs = []
    for cert in certs:
        # 筛选客户端证书（device_id 以 client: 开头）
        if cert["device_id"].startswith("client:"):
            not_after = datetime.fromisoformat(cert["not_after"])
            days_until_expiry = (not_after - now).days
            
            client_certs.append({
                **cert,
                "name": cert["device_id"].replace("client:", ""),
                "is_valid": cert["revoked_at"] is None and not_after > now,
                "days_until_expiry": days_until_expiry
            })
    
    return {"client_certs": client_certs}


@app.post("/api/client-certs/generate")
async def generate_client_cert(
    req: ClientCertGenerateRequest,
    request: Request,
    auth: bool = Depends(verify_token)
):
    """
    生成客户端证书
    
    在服务器端生成密钥对并签发客户端证书。
    返回 PEM 格式的证书和私钥，以及 PKCS12 格式文件。
    
    ⚠️ 注意：私钥仅在此次响应中返回，请妥善保存！
    """
    ca = get_ca()
    
    # 生成 CN
    common_name = req.name
    if req.email:
        common_name = f"{req.name} <{req.email}>"
    
    try:
        cert_pem, key_pem, serial_number, pkcs12_base64, pkcs12_password, not_before, not_after = \
            ca.generate_client_cert(
                common_name=common_name,
                validity_days=req.validity_days,
                email=req.email,
                role=req.role
            )
        
        # 保存到数据库（device_id 使用特殊前缀，存储私钥以便后续生成 P12）
        device_id = f"client:{req.name}"
        
        cert_id = await create_certificate(
            request_id=None,  # 没有 CSR 请求
            device_id=device_id,
            common_name=common_name,
            serial_number=serial_number,
            cert_pem=cert_pem,
            not_before=not_before,
            not_after=not_after,
            issued_by="admin",
            private_key_pem=key_pem  # 存储私钥以便后续下载 P12
        )
        
        await add_audit_log(
            "CLIENT_CERT_ISSUED", "certificate", serial_number,
            "admin", f"Client cert issued: {req.name}, validity={req.validity_days}d",
            request.client.host
        )
        
        return {
            "cert_id": cert_id,
            "name": req.name,
            "serial_number": serial_number,
            "not_before": not_before.isoformat(),
            "not_after": not_after.isoformat(),
            "certificate": cert_pem,
            "private_key": key_pem,
            "ca_chain": ca.get_ca_chain(),
            "pkcs12_base64": pkcs12_base64,
            "pkcs12_password": pkcs12_password
        }
        
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Failed to generate client cert: {e}")
