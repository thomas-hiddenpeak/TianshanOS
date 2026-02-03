"""
TianShanOS PKI Server - 证书签发模块
"""
from cryptography import x509
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.backends import default_backend
from datetime import datetime, timedelta
from typing import Optional, Tuple, List
import ipaddress
import os

from .config import get_settings

settings = get_settings()


class CertificateAuthority:
    """证书颁发机构"""
    
    def __init__(self):
        self.ca_cert = None
        self.ca_key = None
        self.ca_chain = None
        self._load_ca()
    
    def _load_ca(self):
        """加载 CA 证书和私钥"""
        # 加载 CA 证书
        with open(settings.ca_cert_path, "rb") as f:
            self.ca_cert = x509.load_pem_x509_certificate(f.read(), default_backend())
        
        # 加载 CA 私钥
        with open(settings.ca_key_path, "rb") as f:
            self.ca_key = serialization.load_pem_private_key(
                f.read(),
                password=settings.ca_key_password.encode(),
                backend=default_backend()
            )
        
        # 加载 CA 链
        with open(settings.ca_chain_path, "rb") as f:
            self.ca_chain = f.read().decode()
    
    def parse_csr(self, csr_pem: str) -> dict:
        """解析 CSR"""
        csr = x509.load_pem_x509_csr(csr_pem.encode(), default_backend())
        
        # 提取 CN
        common_name = None
        for attr in csr.subject:
            if attr.oid == NameOID.COMMON_NAME:
                common_name = attr.value
                break
        
        # 提取 SAN
        san_ips = []
        san_dns = []
        try:
            san_ext = csr.extensions.get_extension_for_class(x509.SubjectAlternativeName)
            for name in san_ext.value:
                if isinstance(name, x509.IPAddress):
                    san_ips.append(str(name.value))
                elif isinstance(name, x509.DNSName):
                    san_dns.append(name.value)
        except x509.ExtensionNotFound:
            pass
        
        return {
            "common_name": common_name,
            "san_ips": san_ips,
            "san_dns": san_dns,
            "public_key_type": type(csr.public_key()).__name__,
            "signature_valid": csr.is_signature_valid,
        }
    
    def sign_csr(
        self,
        csr_pem: str,
        validity_days: int = 365,
        cert_type: str = "server",
        device_type: str = "Device",
        additional_ips: Optional[List[str]] = None,
        additional_dns: Optional[List[str]] = None,
    ) -> Tuple[str, str, datetime, datetime]:
        """
        签发证书
        
        Args:
            csr_pem: CSR PEM 格式字符串
            validity_days: 有效天数
            cert_type: 证书类型 (server/client/both)
            device_type: 设备类型 (Device/Developer)，写入证书 OU 字段
            additional_ips: 额外的 IP SAN
            additional_dns: 额外的 DNS SAN
            
        Returns:
            (cert_pem, serial_number, not_before, not_after)
        """
        csr = x509.load_pem_x509_csr(csr_pem.encode(), default_backend())
        
        if not csr.is_signature_valid:
            raise ValueError("CSR signature is invalid")
        
        now = datetime.utcnow()
        not_before = now
        not_after = now + timedelta(days=validity_days)
        
        # 构建 Subject（添加 OU 字段标识设备类型）
        # 从 CSR 中提取原始 Subject 属性
        original_attrs = list(csr.subject)
        
        # 过滤掉已有的 OU 属性（如果有的话）
        filtered_attrs = [attr for attr in original_attrs if attr.oid != NameOID.ORGANIZATIONAL_UNIT_NAME]
        
        # 添加设备类型作为 OU
        filtered_attrs.append(x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, device_type))
        
        # 构建新的 Subject
        subject = x509.Name(filtered_attrs)
        
        # 构建 SAN
        san_names = []
        
        # 从 CSR 提取 SAN
        try:
            csr_san = csr.extensions.get_extension_for_class(x509.SubjectAlternativeName)
            san_names.extend(csr_san.value)
        except x509.ExtensionNotFound:
            pass
        
        # 添加额外的 IP
        if additional_ips:
            for ip in additional_ips:
                try:
                    san_names.append(x509.IPAddress(ipaddress.ip_address(ip)))
                except ValueError:
                    pass
        
        # 添加额外的 DNS
        if additional_dns:
            for dns in additional_dns:
                san_names.append(x509.DNSName(dns))
        
        # 确定密钥用途
        if cert_type == "server":
            key_usage = x509.KeyUsage(
                digital_signature=True,
                key_encipherment=True,
                content_commitment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            )
            extended_key_usage = x509.ExtendedKeyUsage([
                ExtendedKeyUsageOID.SERVER_AUTH,
                ExtendedKeyUsageOID.CLIENT_AUTH,
            ])
        elif cert_type == "client":
            key_usage = x509.KeyUsage(
                digital_signature=True,
                key_encipherment=False,
                content_commitment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            )
            extended_key_usage = x509.ExtendedKeyUsage([
                ExtendedKeyUsageOID.CLIENT_AUTH,
            ])
        else:  # both
            key_usage = x509.KeyUsage(
                digital_signature=True,
                key_encipherment=True,
                content_commitment=False,
                data_encipherment=False,
                key_agreement=False,
                key_cert_sign=False,
                crl_sign=False,
                encipher_only=False,
                decipher_only=False,
            )
            extended_key_usage = x509.ExtendedKeyUsage([
                ExtendedKeyUsageOID.SERVER_AUTH,
                ExtendedKeyUsageOID.CLIENT_AUTH,
            ])
        
        # 构建证书
        builder = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(self.ca_cert.subject)
            .public_key(csr.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(not_before)
            .not_valid_after(not_after)
            .add_extension(
                x509.BasicConstraints(ca=False, path_length=None),
                critical=True,
            )
            .add_extension(key_usage, critical=True)
            .add_extension(extended_key_usage, critical=False)
        )
        
        # 添加 SAN
        if san_names:
            builder = builder.add_extension(
                x509.SubjectAlternativeName(san_names),
                critical=False,
            )
        
        # 添加 Authority Key Identifier
        builder = builder.add_extension(
            x509.AuthorityKeyIdentifier.from_issuer_subject_key_identifier(
                self.ca_cert.extensions.get_extension_for_class(
                    x509.SubjectKeyIdentifier
                ).value
            ),
            critical=False,
        )
        
        # 签发
        cert = builder.sign(self.ca_key, hashes.SHA256(), default_backend())
        
        # 转换为 PEM
        cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode()
        serial_number = format(cert.serial_number, 'X')
        
        return cert_pem, serial_number, not_before, not_after
    
    def get_ca_chain(self) -> str:
        """获取 CA 链"""
        return self.ca_chain
    
    def get_ca_info(self) -> dict:
        """获取 CA 信息"""
        return {
            "subject": self.ca_cert.subject.rfc4514_string(),
            "issuer": self.ca_cert.issuer.rfc4514_string(),
            "serial_number": format(self.ca_cert.serial_number, 'X'),
            "not_before": self.ca_cert.not_valid_before_utc.isoformat(),
            "not_after": self.ca_cert.not_valid_after_utc.isoformat(),
            "days_until_expiry": (self.ca_cert.not_valid_after_utc - datetime.utcnow().replace(tzinfo=self.ca_cert.not_valid_after_utc.tzinfo)).days,
        }

    def generate_client_cert(
        self,
        common_name: str,
        validity_days: int = 365,
        email: Optional[str] = None,
        role: str = "operator",
    ) -> Tuple[str, str, str, str, str, datetime, datetime]:
        """
        生成客户端证书（包括密钥对）
        
        Args:
            common_name: 证书 CN（通常是用户名或标识）
            validity_days: 有效天数
            email: 可选的邮箱地址
            role: 角色/权限级别，写入 OU 字段（admin/operator/viewer）
            
        Returns:
            (cert_pem, key_pem, serial_number, pkcs12_base64, pkcs12_password, not_before, not_after)
        """
        import base64
        import secrets
        from cryptography.hazmat.primitives.serialization import pkcs12
        
        # 生成 EC 密钥对
        private_key = ec.generate_private_key(ec.SECP256R1(), default_backend())
        
        now = datetime.utcnow()
        not_before = now
        not_after = now + timedelta(days=validity_days)
        
        # 构建 Subject（包含 OU 字段用于角色）
        subject_attrs = [
            x509.NameAttribute(NameOID.COMMON_NAME, common_name),
            x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME, role),
        ]
        if email:
            subject_attrs.append(x509.NameAttribute(NameOID.EMAIL_ADDRESS, email))
        subject = x509.Name(subject_attrs)
        
        # Key Usage for client certificate
        key_usage = x509.KeyUsage(
            digital_signature=True,
            key_encipherment=False,
            content_commitment=False,
            data_encipherment=False,
            key_agreement=False,
            key_cert_sign=False,
            crl_sign=False,
            encipher_only=False,
            decipher_only=False,
        )
        
        extended_key_usage = x509.ExtendedKeyUsage([
            ExtendedKeyUsageOID.CLIENT_AUTH,
        ])
        
        # 构建证书
        builder = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(self.ca_cert.subject)
            .public_key(private_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(not_before)
            .not_valid_after(not_after)
            .add_extension(
                x509.BasicConstraints(ca=False, path_length=None),
                critical=True,
            )
            .add_extension(key_usage, critical=True)
            .add_extension(extended_key_usage, critical=False)
            .add_extension(
                x509.AuthorityKeyIdentifier.from_issuer_subject_key_identifier(
                    self.ca_cert.extensions.get_extension_for_class(
                        x509.SubjectKeyIdentifier
                    ).value
                ),
                critical=False,
            )
        )
        
        # 签发
        cert = builder.sign(self.ca_key, hashes.SHA256(), default_backend())
        
        # 转换为 PEM
        cert_pem = cert.public_bytes(serialization.Encoding.PEM).decode()
        key_pem = private_key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption()
        ).decode()
        
        serial_number = format(cert.serial_number, 'X')
        
        # 生成 PKCS12
        pkcs12_password = secrets.token_urlsafe(12)
        pkcs12_data = pkcs12.serialize_key_and_certificates(
            name=common_name.encode(),
            key=private_key,
            cert=cert,
            cas=[self.ca_cert],
            encryption_algorithm=serialization.BestAvailableEncryption(pkcs12_password.encode())
        )
        pkcs12_base64 = base64.b64encode(pkcs12_data).decode()
        
        return cert_pem, key_pem, serial_number, pkcs12_base64, pkcs12_password, not_before, not_after


# 全局 CA 实例
_ca_instance: Optional[CertificateAuthority] = None


def get_ca() -> CertificateAuthority:
    """获取 CA 实例"""
    global _ca_instance
    if _ca_instance is None:
        _ca_instance = CertificateAuthority()
    return _ca_instance

