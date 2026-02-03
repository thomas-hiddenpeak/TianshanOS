"""
TianShanOS PKI Server - 配置模块
"""
from pydantic_settings import BaseSettings
from functools import lru_cache
import os


class Settings(BaseSettings):
    """应用配置"""
    
    # CA 路径
    ca_cert_path: str = os.path.expanduser("~/tianshan-pki/step-ca/certs/intermediate_ca.crt")
    ca_key_path: str = os.path.expanduser("~/tianshan-pki/step-ca/secrets/intermediate_ca_key")
    ca_chain_path: str = os.path.expanduser("~/tianshan-pki/step-ca/certs/ca_chain.crt")
    
    # CA 密钥密码
    ca_key_password: str = "tianshan-intermediate-2026"
    
    # 服务器配置
    host: str = "0.0.0.0"
    port: int = 8443
    
    # 签发策略
    default_validity_days: int = 365
    auto_sign_enabled: bool = False
    require_device_token: bool = True
    
    # 数据库
    database_path: str = os.path.expanduser("~/tianshan-pki/pki-server/pki.db")
    
    # 管理员密码
    admin_password: str = "tianshan-pki-admin"
    
    # 日志级别
    log_level: str = "INFO"
    
    class Config:
        env_file = ".env"
        env_file_encoding = "utf-8"


@lru_cache()
def get_settings() -> Settings:
    return Settings()
