"""
TianShanOS PKI Server - 数据库模块
"""
import aiosqlite
import os
from datetime import datetime
from typing import Optional, List
from .config import get_settings

settings = get_settings()

# 请求状态
class RequestStatus:
    PENDING = "pending"       # 待审批
    APPROVED = "approved"     # 已批准（已签发）
    REJECTED = "rejected"     # 已拒绝
    EXPIRED = "expired"       # 已过期（未审批）


async def init_db():
    """初始化数据库"""
    os.makedirs(os.path.dirname(settings.database_path), exist_ok=True)
    
    async with aiosqlite.connect(settings.database_path) as db:
        # CSR 请求表
        await db.execute("""
            CREATE TABLE IF NOT EXISTS csr_requests (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_id TEXT NOT NULL,
                device_ip TEXT,
                device_token TEXT,
                common_name TEXT NOT NULL,
                san_ips TEXT,
                san_dns TEXT,
                csr_pem TEXT NOT NULL,
                status TEXT DEFAULT 'pending',
                validity_days INTEGER DEFAULT 365,
                cert_type TEXT DEFAULT 'server',
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                processed_at TIMESTAMP,
                processed_by TEXT,
                reject_reason TEXT
            )
        """)
        
        # 已签发证书表
        await db.execute("""
            CREATE TABLE IF NOT EXISTS certificates (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                request_id INTEGER,
                device_id TEXT NOT NULL,
                common_name TEXT NOT NULL,
                serial_number TEXT UNIQUE,
                cert_pem TEXT NOT NULL,
                private_key_pem TEXT,
                not_before TIMESTAMP,
                not_after TIMESTAMP,
                issued_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                issued_by TEXT,
                revoked_at TIMESTAMP,
                revoke_reason TEXT,
                FOREIGN KEY (request_id) REFERENCES csr_requests(id)
            )
        """)
        
        # 检查是否需要添加 private_key_pem 字段（数据库升级）
        try:
            cursor = await db.execute("PRAGMA table_info(certificates)")
            columns = [row[1] for row in await cursor.fetchall()]
            if 'private_key_pem' not in columns:
                await db.execute("ALTER TABLE certificates ADD COLUMN private_key_pem TEXT")
        except:
            pass
        
        # 设备白名单表
        await db.execute("""
            CREATE TABLE IF NOT EXISTS device_whitelist (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                device_token TEXT UNIQUE NOT NULL,
                device_name TEXT,
                description TEXT,
                auto_approve BOOLEAN DEFAULT FALSE,
                validity_days INTEGER DEFAULT 365,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                last_used_at TIMESTAMP
            )
        """)
        
        # 审计日志表
        await db.execute("""
            CREATE TABLE IF NOT EXISTS audit_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                action TEXT NOT NULL,
                target_type TEXT,
                target_id TEXT,
                operator TEXT,
                details TEXT,
                ip_address TEXT,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        
        await db.commit()


async def get_db():
    """获取数据库连接"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        yield db


# ============== CSR 请求操作 ==============

async def create_csr_request(
    device_id: str,
    common_name: str,
    csr_pem: str,
    device_ip: Optional[str] = None,
    device_token: Optional[str] = None,
    san_ips: Optional[str] = None,
    san_dns: Optional[str] = None,
    validity_days: int = 365,
    cert_type: str = "server"
) -> int:
    """创建 CSR 请求"""
    async with aiosqlite.connect(settings.database_path) as db:
        cursor = await db.execute("""
            INSERT INTO csr_requests 
            (device_id, device_ip, device_token, common_name, san_ips, san_dns, 
             csr_pem, validity_days, cert_type)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (device_id, device_ip, device_token, common_name, san_ips, san_dns,
              csr_pem, validity_days, cert_type))
        await db.commit()
        return cursor.lastrowid


async def get_pending_requests() -> List[dict]:
    """获取待审批的请求"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("""
            SELECT * FROM csr_requests 
            WHERE status = 'pending'
            ORDER BY created_at DESC
        """)
        rows = await cursor.fetchall()
        return [dict(row) for row in rows]


async def get_request_by_id(request_id: int) -> Optional[dict]:
    """根据 ID 获取请求"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM csr_requests WHERE id = ?", (request_id,)
        )
        row = await cursor.fetchone()
        return dict(row) if row else None


async def update_request_status(
    request_id: int, 
    status: str, 
    processed_by: str = "system",
    reject_reason: Optional[str] = None
):
    """更新请求状态"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("""
            UPDATE csr_requests 
            SET status = ?, processed_at = ?, processed_by = ?, reject_reason = ?
            WHERE id = ?
        """, (status, datetime.now().isoformat(), processed_by, reject_reason, request_id))
        await db.commit()


# ============== 证书操作 ==============

async def create_certificate(
    request_id: int,
    device_id: str,
    common_name: str,
    serial_number: str,
    cert_pem: str,
    not_before: datetime,
    not_after: datetime,
    issued_by: str = "system",
    private_key_pem: Optional[str] = None
) -> int:
    """创建证书记录（可选存储私钥，仅用于客户端证书）"""
    async with aiosqlite.connect(settings.database_path) as db:
        cursor = await db.execute("""
            INSERT INTO certificates
            (request_id, device_id, common_name, serial_number, cert_pem,
             private_key_pem, not_before, not_after, issued_by)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (request_id, device_id, common_name, serial_number, cert_pem,
              private_key_pem, not_before.isoformat(), not_after.isoformat(), issued_by))
        await db.commit()
        return cursor.lastrowid


async def get_all_certificates() -> List[dict]:
    """获取所有证书"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("""
            SELECT * FROM certificates 
            ORDER BY issued_at DESC
        """)
        rows = await cursor.fetchall()
        return [dict(row) for row in rows]


async def get_certificate_by_id(cert_id: int) -> Optional[dict]:
    """根据 ID 获取证书"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM certificates WHERE id = ?", (cert_id,)
        )
        row = await cursor.fetchone()
        return dict(row) if row else None


async def revoke_certificate(cert_id: int, reason: str, operator: str = "admin"):
    """吊销证书"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("""
            UPDATE certificates 
            SET revoked_at = ?, revoke_reason = ?
            WHERE id = ?
        """, (datetime.now().isoformat(), reason, cert_id))
        await db.commit()


async def delete_certificate(cert_id: int):
    """删除证书记录（仅限已吊销或已过期的证书）"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("DELETE FROM certificates WHERE id = ?", (cert_id,))
        await db.commit()


# ============== 设备白名单操作 ==============

async def add_device_to_whitelist(
    device_token: str,
    device_name: str = None,
    description: str = None,
    auto_approve: bool = False,
    validity_days: int = 365
) -> int:
    """添加设备到白名单"""
    async with aiosqlite.connect(settings.database_path) as db:
        cursor = await db.execute("""
            INSERT INTO device_whitelist
            (device_token, device_name, description, auto_approve, validity_days)
            VALUES (?, ?, ?, ?, ?)
        """, (device_token, device_name, description, auto_approve, validity_days))
        await db.commit()
        return cursor.lastrowid


async def get_device_by_token(token: str) -> Optional[dict]:
    """根据 Token 获取设备"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute(
            "SELECT * FROM device_whitelist WHERE device_token = ?", (token,)
        )
        row = await cursor.fetchone()
        return dict(row) if row else None


async def get_all_whitelist_devices() -> List[dict]:
    """获取所有白名单设备"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("SELECT * FROM device_whitelist ORDER BY created_at DESC")
        rows = await cursor.fetchall()
        return [dict(row) for row in rows]


async def update_device_last_used(device_token: str):
    """更新设备最后使用时间"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("""
            UPDATE device_whitelist SET last_used_at = ? WHERE device_token = ?
        """, (datetime.now().isoformat(), device_token))
        await db.commit()


async def delete_device_from_whitelist(device_id: int):
    """从白名单删除设备"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("DELETE FROM device_whitelist WHERE id = ?", (device_id,))
        await db.commit()


# ============== 审计日志操作 ==============

async def add_audit_log(
    action: str,
    target_type: str = None,
    target_id: str = None,
    operator: str = None,
    details: str = None,
    ip_address: str = None
):
    """添加审计日志"""
    async with aiosqlite.connect(settings.database_path) as db:
        await db.execute("""
            INSERT INTO audit_logs
            (action, target_type, target_id, operator, details, ip_address)
            VALUES (?, ?, ?, ?, ?, ?)
        """, (action, target_type, target_id, operator, details, ip_address))
        await db.commit()


async def get_audit_logs(limit: int = 100) -> List[dict]:
    """获取审计日志"""
    async with aiosqlite.connect(settings.database_path) as db:
        db.row_factory = aiosqlite.Row
        cursor = await db.execute("""
            SELECT * FROM audit_logs ORDER BY created_at DESC LIMIT ?
        """, (limit,))
        rows = await cursor.fetchall()
        return [dict(row) for row in rows]
