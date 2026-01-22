#!/usr/bin/env python3
"""
TianShanOS 版本管理工具

使用方法:
    ./tools/version.py                  # 显示当前版本
    ./tools/version.py --bump major     # 升级主版本号 (0.2.0 -> 1.0.0)
    ./tools/version.py --bump minor     # 升级次版本号 (0.2.0 -> 0.3.0)
    ./tools/version.py --bump patch     # 升级修订号   (0.2.0 -> 0.2.1)
    ./tools/version.py --set 1.0.0      # 直接设置版本
    ./tools/version.py --pre alpha      # 设置预发布标识 (0.2.0-alpha)
    ./tools/version.py --pre ""         # 清除预发布标识
    ./tools/version.py --release        # 发布版本 (创建 git tag)
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# 项目根目录
PROJECT_ROOT = Path(__file__).parent.parent
VERSION_FILE = PROJECT_ROOT / "version.txt"

# 语义化版本正则表达式
SEMVER_REGEX = re.compile(
    r'^(?P<major>0|[1-9]\d*)'
    r'\.(?P<minor>0|[1-9]\d*)'
    r'\.(?P<patch>0|[1-9]\d*)'
    r'(?:-(?P<prerelease>(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*)'
    r'(?:\.(?:0|[1-9]\d*|\d*[a-zA-Z-][0-9a-zA-Z-]*))*))?'
    r'(?:\+(?P<build>[0-9a-zA-Z-]+(?:\.[0-9a-zA-Z-]+)*))?$'
)


class Version:
    """语义化版本类"""
    
    def __init__(self, major=0, minor=0, patch=0, prerelease=None, build=None):
        self.major = major
        self.minor = minor
        self.patch = patch
        self.prerelease = prerelease
        self.build = build
    
    @classmethod
    def parse(cls, version_str):
        """从字符串解析版本"""
        version_str = version_str.strip()
        match = SEMVER_REGEX.match(version_str)
        if not match:
            raise ValueError(f"无效的语义化版本: {version_str}")
        
        return cls(
            major=int(match.group('major')),
            minor=int(match.group('minor')),
            patch=int(match.group('patch')),
            prerelease=match.group('prerelease'),
            build=match.group('build')
        )
    
    def __str__(self):
        """转换为字符串"""
        version = f"{self.major}.{self.minor}.{self.patch}"
        if self.prerelease:
            version += f"-{self.prerelease}"
        if self.build:
            version += f"+{self.build}"
        return version
    
    @property
    def core(self):
        """核心版本号 (不含预发布和构建信息)"""
        return f"{self.major}.{self.minor}.{self.patch}"
    
    def bump(self, part):
        """升级版本号"""
        if part == 'major':
            return Version(self.major + 1, 0, 0)
        elif part == 'minor':
            return Version(self.major, self.minor + 1, 0)
        elif part == 'patch':
            return Version(self.major, self.minor, self.patch + 1)
        else:
            raise ValueError(f"未知的版本部分: {part}")
    
    def with_prerelease(self, prerelease):
        """设置预发布标识"""
        return Version(self.major, self.minor, self.patch, prerelease or None, self.build)
    
    def __lt__(self, other):
        """比较版本 (用于排序)"""
        # 比较核心版本
        if (self.major, self.minor, self.patch) != (other.major, other.minor, other.patch):
            return (self.major, self.minor, self.patch) < (other.major, other.minor, other.patch)
        
        # 有预发布标识 < 无预发布标识
        if self.prerelease and not other.prerelease:
            return True
        if not self.prerelease and other.prerelease:
            return False
        
        # 比较预发布标识
        if self.prerelease and other.prerelease:
            return self.prerelease < other.prerelease
        
        return False


def read_version():
    """读取当前版本"""
    if not VERSION_FILE.exists():
        return Version(0, 1, 0)
    return Version.parse(VERSION_FILE.read_text())


def write_version(version):
    """写入版本"""
    VERSION_FILE.write_text(str(version) + '\n')


def get_git_hash():
    """获取 git commit hash"""
    try:
        result = subprocess.run(
            ['git', 'rev-parse', '--short', 'HEAD'],
            capture_output=True,
            text=True,
            cwd=PROJECT_ROOT
        )
        if result.returncode == 0:
            return result.stdout.strip()
    except FileNotFoundError:
        pass
    return None


def create_git_tag(version, message=None):
    """创建 git tag"""
    tag_name = f"v{version}"
    if message is None:
        message = f"Release {tag_name}"
    
    try:
        # 检查 tag 是否已存在
        result = subprocess.run(
            ['git', 'tag', '-l', tag_name],
            capture_output=True,
            text=True,
            cwd=PROJECT_ROOT
        )
        if tag_name in result.stdout:
            print(f"警告: Tag {tag_name} 已存在")
            return False
        
        # 创建 tag
        subprocess.run(
            ['git', 'tag', '-a', tag_name, '-m', message],
            check=True,
            cwd=PROJECT_ROOT
        )
        print(f"已创建 tag: {tag_name}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"创建 tag 失败: {e}")
        return False
    except FileNotFoundError:
        print("警告: git 不可用，跳过 tag 创建")
        return False


def main():
    parser = argparse.ArgumentParser(
        description='TianShanOS 语义化版本管理工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                      显示当前版本
  %(prog)s --bump patch         升级修订号 (0.2.0 -> 0.2.1)
  %(prog)s --bump minor         升级次版本号 (0.2.0 -> 0.3.0)
  %(prog)s --bump major         升级主版本号 (0.2.0 -> 1.0.0)
  %(prog)s --set 1.0.0          直接设置版本
  %(prog)s --pre rc1            设置预发布标识 (0.2.0-rc1)
  %(prog)s --pre ""             清除预发布标识
  %(prog)s --release            发布版本 (创建 git tag)
        """
    )
    parser.add_argument('--bump', choices=['major', 'minor', 'patch'],
                        help='升级版本号')
    parser.add_argument('--set', dest='set_version', metavar='VERSION',
                        help='直接设置版本号')
    parser.add_argument('--pre', dest='prerelease', metavar='TAG',
                        help='设置预发布标识 (如 alpha, beta, rc1)')
    parser.add_argument('--release', action='store_true',
                        help='发布版本 (创建 git tag)')
    parser.add_argument('--dry-run', action='store_true',
                        help='仅显示变更，不实际修改')
    
    args = parser.parse_args()
    
    # 读取当前版本
    current = read_version()
    print(f"当前版本: {current}")
    
    git_hash = get_git_hash()
    if git_hash:
        print(f"Git commit: {git_hash}")
    
    new_version = current
    
    # 处理版本变更
    if args.set_version:
        try:
            new_version = Version.parse(args.set_version)
        except ValueError as e:
            print(f"错误: {e}")
            return 1
    elif args.bump:
        new_version = current.bump(args.bump)
    
    if args.prerelease is not None:
        new_version = new_version.with_prerelease(args.prerelease if args.prerelease else None)
    
    # 显示变更
    if str(new_version) != str(current):
        print(f"新版本: {new_version}")
        
        if args.dry_run:
            print("(dry-run 模式，未实际修改)")
        else:
            write_version(new_version)
            print(f"已更新 {VERSION_FILE}")
    
    # 发布版本
    if args.release:
        if new_version.prerelease:
            print(f"警告: 预发布版本 ({new_version}) 通常不创建 tag")
            confirm = input("是否继续? [y/N]: ")
            if confirm.lower() != 'y':
                return 0
        
        if not args.dry_run:
            create_git_tag(new_version)
        else:
            print(f"(dry-run) 将创建 tag: v{new_version}")
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
