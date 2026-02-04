#!/usr/bin/env python3
"""
TianShanOS PKI Server - 启动脚本
"""
import uvicorn
import os
import sys

# 添加项目路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from app.config import get_settings

if __name__ == "__main__":
    settings = get_settings()
    
    print(f"""
╔══════════════════════════════════════════════════════════════╗
║           TianShanOS PKI Server                              ║
╠══════════════════════════════════════════════════════════════╣
║  地址: http://{settings.host}:{settings.port}                          ║
║  API 文档: http://{settings.host}:{settings.port}/docs                  ║
║  管理密码: {settings.admin_password}                  ║
╚══════════════════════════════════════════════════════════════╝
    """)
    
    uvicorn.run(
        "app.main:app",
        host=settings.host,
        port=settings.port,
        reload=True,
        log_level=settings.log_level.lower()
    )
