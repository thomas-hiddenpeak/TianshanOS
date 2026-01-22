# TianShanOS - Main Project CMakeLists.txt
# ESP32 机架管理操作系统
#
# 作者: TianShanOS Team
# 版本: 从 version.txt 读取

cmake_minimum_required(VERSION 3.16)

# ============================================================================
# 版本管理 - 从 version.txt 读取语义化版本号
# 格式: MAJOR.MINOR.PATCH[-PRERELEASE][+BUILD]
# 例如: 0.2.0, 1.0.0-alpha, 1.2.3-rc1+build123
# ============================================================================
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" VERSION_CONTENT)
string(STRIP "${VERSION_CONTENT}" TIANSHAN_VERSION_FULL)

# 解析版本号组件
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" VERSION_MATCH "${TIANSHAN_VERSION_FULL}")
if(NOT VERSION_MATCH)
    message(FATAL_ERROR "Invalid version format in version.txt: ${TIANSHAN_VERSION_FULL}")
endif()
set(TIANSHAN_VERSION_MAJOR ${CMAKE_MATCH_1})
set(TIANSHAN_VERSION_MINOR ${CMAKE_MATCH_2})
set(TIANSHAN_VERSION_PATCH ${CMAKE_MATCH_3})

# 解析预发布标识 (如 -alpha, -rc1)
string(REGEX MATCH "-([a-zA-Z0-9.]+)" PRERELEASE_MATCH "${TIANSHAN_VERSION_FULL}")
if(PRERELEASE_MATCH)
    set(TIANSHAN_VERSION_PRERELEASE ${CMAKE_MATCH_1})
else()
    set(TIANSHAN_VERSION_PRERELEASE "")
endif()

# 解析构建元数据 (如 +build123)
string(REGEX MATCH "\\+([a-zA-Z0-9.]+)" BUILD_MATCH "${TIANSHAN_VERSION_FULL}")
if(BUILD_MATCH)
    set(TIANSHAN_VERSION_BUILD ${CMAKE_MATCH_1})
else()
    # 使用 git commit hash 作为构建标识
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    
    # 检查是否有未提交的修改 (dirty)
    execute_process(
        COMMAND git status --porcelain
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_STATUS
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(GIT_STATUS)
        set(GIT_DIRTY "d")  # 缩短 dirty 标记
    else()
        set(GIT_DIRTY "")
    endif()
    
    # 获取构建时间戳 (MMDDHHmm 缩短格式，足够区分构建)
    string(TIMESTAMP BUILD_TIMESTAMP "%m%d%H%M")
    
    if(GIT_HASH)
        # 格式: hash.d.timestamp 或 hash.timestamp (确保总长度 < 32)
        # 例如: 0.2.0+cc54a17.d.01230242 (25字符)
        set(TIANSHAN_VERSION_BUILD "${GIT_HASH}${GIT_DIRTY}.${BUILD_TIMESTAMP}")
    else()
        set(TIANSHAN_VERSION_BUILD "${BUILD_TIMESTAMP}")
    endif()
endif()

# 构建完整版本字符串
set(TIANSHAN_VERSION "${TIANSHAN_VERSION_MAJOR}.${TIANSHAN_VERSION_MINOR}.${TIANSHAN_VERSION_PATCH}")
if(TIANSHAN_VERSION_PRERELEASE)
    set(TIANSHAN_VERSION "${TIANSHAN_VERSION}-${TIANSHAN_VERSION_PRERELEASE}")
endif()
if(TIANSHAN_VERSION_BUILD)
    set(TIANSHAN_VERSION_DISPLAY "${TIANSHAN_VERSION}+${TIANSHAN_VERSION_BUILD}")
else()
    set(TIANSHAN_VERSION_DISPLAY "${TIANSHAN_VERSION}")
endif()

# 设置 ESP-IDF 项目版本（会嵌入到 esp_app_desc）
set(PROJECT_VER "${TIANSHAN_VERSION_DISPLAY}")

# 设置组件目录
set(EXTRA_COMPONENT_DIRS 
    "${CMAKE_CURRENT_SOURCE_DIR}/components"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_config"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_log"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_event"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/ts_core/ts_service"
)

# 使用 OTA 分区表（16MB Flash, 双 OTA 分区）
set(PARTITION_TABLE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/partitions_ota.csv" CACHE STRING "" FORCE)

# 包含 ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 项目定义
project(TianShanOS VERSION ${TIANSHAN_VERSION_MAJOR}.${TIANSHAN_VERSION_MINOR}.${TIANSHAN_VERSION_PATCH})

# 添加编译定义 - 使用 idf_build_set_property 确保所有组件都能访问
idf_build_set_property(COMPILE_DEFINITIONS
    "TIANSHAN_OS_VERSION_MAJOR=${TIANSHAN_VERSION_MAJOR}"
    APPEND
)
idf_build_set_property(COMPILE_DEFINITIONS
    "TIANSHAN_OS_VERSION_MINOR=${TIANSHAN_VERSION_MINOR}"
    APPEND
)
idf_build_set_property(COMPILE_DEFINITIONS
    "TIANSHAN_OS_VERSION_PATCH=${TIANSHAN_VERSION_PATCH}"
    APPEND
)
idf_build_set_property(COMPILE_DEFINITIONS
    "TIANSHAN_OS_VERSION=\"${TIANSHAN_VERSION}\""
    APPEND
)
idf_build_set_property(COMPILE_DEFINITIONS
    "TIANSHAN_OS_VERSION_FULL=\"${TIANSHAN_VERSION_DISPLAY}\""
    APPEND
)

# 打印项目信息
message(STATUS "")
message(STATUS "╔═══════════════════════════════════════════════════════════╗")
message(STATUS "║              TianShanOS v${TIANSHAN_VERSION_DISPLAY}                      ║")
message(STATUS "║           ESP32 Rack Management Operating System          ║")
message(STATUS "╚═══════════════════════════════════════════════════════════╝")
message(STATUS "")
message(STATUS "Version: ${TIANSHAN_VERSION_DISPLAY}")
message(STATUS "  Major: ${TIANSHAN_VERSION_MAJOR}")
message(STATUS "  Minor: ${TIANSHAN_VERSION_MINOR}")
message(STATUS "  Patch: ${TIANSHAN_VERSION_PATCH}")
if(TIANSHAN_VERSION_PRERELEASE)
    message(STATUS "  Prerelease: ${TIANSHAN_VERSION_PRERELEASE}")
endif()
if(TIANSHAN_VERSION_BUILD)
    message(STATUS "  Build: ${TIANSHAN_VERSION_BUILD}")
endif()
message(STATUS "")
message(STATUS "Target: ${IDF_TARGET}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")
