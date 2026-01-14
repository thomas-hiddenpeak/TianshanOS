/**
 * @file ts_i18n.c
 * @brief TianShanOS Internationalization Implementation
 */

#include "ts_i18n.h"
#include "ts_log.h"
#include "ts_config.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TAG "ts_i18n"

/*===========================================================================*/
/*                              String Tables                                 */
/*===========================================================================*/

/* English strings (default) */
static const char *s_strings_en[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "Welcome to TianShanOS",
    [TS_STR_VERSION]           = "Version",
    [TS_STR_READY]             = "Ready",
    [TS_STR_ERROR]             = "Error",
    [TS_STR_SUCCESS]           = "Success",
    [TS_STR_FAILED]            = "Failed",
    [TS_STR_UNKNOWN_CMD]       = "Unknown command",
    [TS_STR_HELP_HEADER]       = "Available commands:",
    [TS_STR_USAGE]             = "Usage",
    
    /* Common prompts */
    [TS_STR_YES]               = "Yes",
    [TS_STR_NO]                = "No",
    [TS_STR_OK]                = "OK",
    [TS_STR_CANCEL]            = "Cancel",
    [TS_STR_CONFIRM]           = "Confirm",
    [TS_STR_LOADING]           = "Loading...",
    [TS_STR_PLEASE_WAIT]       = "Please wait...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "Device Information",
    [TS_STR_UPTIME]            = "Uptime",
    [TS_STR_FREE_HEAP]         = "Free Heap",
    [TS_STR_CHIP_MODEL]        = "Chip Model",
    [TS_STR_FIRMWARE_VER]      = "Firmware Version",
    [TS_STR_TEMPERATURE]       = "Temperature",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi connected",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi disconnected",
    [TS_STR_WIFI_SCANNING]     = "Scanning WiFi networks...",
    [TS_STR_WIFI_CONNECTING]   = "Connecting to WiFi...",
    [TS_STR_IP_ADDRESS]        = "IP Address",
    [TS_STR_MAC_ADDRESS]       = "MAC Address",
    [TS_STR_SIGNAL_STRENGTH]   = "Signal Strength",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED Controller",
    [TS_STR_LED_COUNT]         = "LED Count",
    [TS_STR_BRIGHTNESS]        = "Brightness",
    [TS_STR_EFFECT]            = "Effect",
    [TS_STR_COLOR]             = "Color",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "Voltage",
    [TS_STR_CURRENT]           = "Current",
    [TS_STR_POWER]             = "Power",
    [TS_STR_POWER_GOOD]        = "Power Good",
    [TS_STR_POWER_OFF]         = "Power Off",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "Invalid argument",
    [TS_STR_ERR_NOT_FOUND]     = "Not found",
    [TS_STR_ERR_NO_MEM]        = "Out of memory",
    [TS_STR_ERR_TIMEOUT]       = "Timeout",
    [TS_STR_ERR_NOT_SUPPORTED] = "Not supported",
    [TS_STR_ERR_INVALID_STATE] = "Invalid state",
    [TS_STR_ERR_IO]            = "I/O error",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "Rebooting...",
    [TS_STR_SHUTTING_DOWN]     = "Shutting down...",
    [TS_STR_REBOOT_IN]         = "Rebooting in %d seconds",
};

/* Simplified Chinese strings */
static const char *s_strings_zh_cn[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "欢迎使用天山操作系统",
    [TS_STR_VERSION]           = "版本",
    [TS_STR_READY]             = "就绪",
    [TS_STR_ERROR]             = "错误",
    [TS_STR_SUCCESS]           = "成功",
    [TS_STR_FAILED]            = "失败",
    [TS_STR_UNKNOWN_CMD]       = "未知命令",
    [TS_STR_HELP_HEADER]       = "可用命令:",
    [TS_STR_USAGE]             = "用法",
    
    /* Common prompts */
    [TS_STR_YES]               = "是",
    [TS_STR_NO]                = "否",
    [TS_STR_OK]                = "确定",
    [TS_STR_CANCEL]            = "取消",
    [TS_STR_CONFIRM]           = "确认",
    [TS_STR_LOADING]           = "加载中...",
    [TS_STR_PLEASE_WAIT]       = "请稍候...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "设备信息",
    [TS_STR_UPTIME]            = "运行时间",
    [TS_STR_FREE_HEAP]         = "可用内存",
    [TS_STR_CHIP_MODEL]        = "芯片型号",
    [TS_STR_FIRMWARE_VER]      = "固件版本",
    [TS_STR_TEMPERATURE]       = "温度",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi 已连接",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi 已断开",
    [TS_STR_WIFI_SCANNING]     = "正在扫描 WiFi 网络...",
    [TS_STR_WIFI_CONNECTING]   = "正在连接 WiFi...",
    [TS_STR_IP_ADDRESS]        = "IP 地址",
    [TS_STR_MAC_ADDRESS]       = "MAC 地址",
    [TS_STR_SIGNAL_STRENGTH]   = "信号强度",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED 控制器",
    [TS_STR_LED_COUNT]         = "LED 数量",
    [TS_STR_BRIGHTNESS]        = "亮度",
    [TS_STR_EFFECT]            = "特效",
    [TS_STR_COLOR]             = "颜色",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "电压",
    [TS_STR_CURRENT]           = "电流",
    [TS_STR_POWER]             = "功率",
    [TS_STR_POWER_GOOD]        = "电源正常",
    [TS_STR_POWER_OFF]         = "电源关闭",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "无效参数",
    [TS_STR_ERR_NOT_FOUND]     = "未找到",
    [TS_STR_ERR_NO_MEM]        = "内存不足",
    [TS_STR_ERR_TIMEOUT]       = "超时",
    [TS_STR_ERR_NOT_SUPPORTED] = "不支持",
    [TS_STR_ERR_INVALID_STATE] = "状态无效",
    [TS_STR_ERR_IO]            = "I/O 错误",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "正在重启...",
    [TS_STR_SHUTTING_DOWN]     = "正在关机...",
    [TS_STR_REBOOT_IN]         = "%d 秒后重启",
};

/* Traditional Chinese strings */
static const char *s_strings_zh_tw[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "歡迎使用天山作業系統",
    [TS_STR_VERSION]           = "版本",
    [TS_STR_READY]             = "就緒",
    [TS_STR_ERROR]             = "錯誤",
    [TS_STR_SUCCESS]           = "成功",
    [TS_STR_FAILED]            = "失敗",
    [TS_STR_UNKNOWN_CMD]       = "未知命令",
    [TS_STR_HELP_HEADER]       = "可用命令:",
    [TS_STR_USAGE]             = "用法",
    
    /* Common prompts */
    [TS_STR_YES]               = "是",
    [TS_STR_NO]                = "否",
    [TS_STR_OK]                = "確定",
    [TS_STR_CANCEL]            = "取消",
    [TS_STR_CONFIRM]           = "確認",
    [TS_STR_LOADING]           = "載入中...",
    [TS_STR_PLEASE_WAIT]       = "請稍候...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "裝置資訊",
    [TS_STR_UPTIME]            = "運行時間",
    [TS_STR_FREE_HEAP]         = "可用記憶體",
    [TS_STR_CHIP_MODEL]        = "晶片型號",
    [TS_STR_FIRMWARE_VER]      = "韌體版本",
    [TS_STR_TEMPERATURE]       = "溫度",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi 已連接",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi 已斷開",
    [TS_STR_WIFI_SCANNING]     = "正在掃描 WiFi 網路...",
    [TS_STR_WIFI_CONNECTING]   = "正在連接 WiFi...",
    [TS_STR_IP_ADDRESS]        = "IP 位址",
    [TS_STR_MAC_ADDRESS]       = "MAC 位址",
    [TS_STR_SIGNAL_STRENGTH]   = "訊號強度",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED 控制器",
    [TS_STR_LED_COUNT]         = "LED 數量",
    [TS_STR_BRIGHTNESS]        = "亮度",
    [TS_STR_EFFECT]            = "特效",
    [TS_STR_COLOR]             = "顏色",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "電壓",
    [TS_STR_CURRENT]           = "電流",
    [TS_STR_POWER]             = "功率",
    [TS_STR_POWER_GOOD]        = "電源正常",
    [TS_STR_POWER_OFF]         = "電源關閉",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "無效參數",
    [TS_STR_ERR_NOT_FOUND]     = "未找到",
    [TS_STR_ERR_NO_MEM]        = "記憶體不足",
    [TS_STR_ERR_TIMEOUT]       = "逾時",
    [TS_STR_ERR_NOT_SUPPORTED] = "不支援",
    [TS_STR_ERR_INVALID_STATE] = "狀態無效",
    [TS_STR_ERR_IO]            = "I/O 錯誤",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "正在重新啟動...",
    [TS_STR_SHUTTING_DOWN]     = "正在關機...",
    [TS_STR_REBOOT_IN]         = "%d 秒後重新啟動",
};

/* Japanese strings */
static const char *s_strings_ja[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "TianShanOS へようこそ",
    [TS_STR_VERSION]           = "バージョン",
    [TS_STR_READY]             = "準備完了",
    [TS_STR_ERROR]             = "エラー",
    [TS_STR_SUCCESS]           = "成功",
    [TS_STR_FAILED]            = "失敗",
    [TS_STR_UNKNOWN_CMD]       = "不明なコマンド",
    [TS_STR_HELP_HEADER]       = "使用可能なコマンド:",
    [TS_STR_USAGE]             = "使用法",
    
    /* Common prompts */
    [TS_STR_YES]               = "はい",
    [TS_STR_NO]                = "いいえ",
    [TS_STR_OK]                = "OK",
    [TS_STR_CANCEL]            = "キャンセル",
    [TS_STR_CONFIRM]           = "確認",
    [TS_STR_LOADING]           = "読み込み中...",
    [TS_STR_PLEASE_WAIT]       = "お待ちください...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "デバイス情報",
    [TS_STR_UPTIME]            = "稼働時間",
    [TS_STR_FREE_HEAP]         = "空きメモリ",
    [TS_STR_CHIP_MODEL]        = "チップモデル",
    [TS_STR_FIRMWARE_VER]      = "ファームウェア",
    [TS_STR_TEMPERATURE]       = "温度",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi 接続済み",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi 切断",
    [TS_STR_WIFI_SCANNING]     = "WiFi スキャン中...",
    [TS_STR_WIFI_CONNECTING]   = "WiFi に接続中...",
    [TS_STR_IP_ADDRESS]        = "IPアドレス",
    [TS_STR_MAC_ADDRESS]       = "MACアドレス",
    [TS_STR_SIGNAL_STRENGTH]   = "信号強度",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LEDコントローラ",
    [TS_STR_LED_COUNT]         = "LED数",
    [TS_STR_BRIGHTNESS]        = "明るさ",
    [TS_STR_EFFECT]            = "エフェクト",
    [TS_STR_COLOR]             = "色",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "電圧",
    [TS_STR_CURRENT]           = "電流",
    [TS_STR_POWER]             = "電力",
    [TS_STR_POWER_GOOD]        = "電源正常",
    [TS_STR_POWER_OFF]         = "電源オフ",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "無効な引数",
    [TS_STR_ERR_NOT_FOUND]     = "見つかりません",
    [TS_STR_ERR_NO_MEM]        = "メモリ不足",
    [TS_STR_ERR_TIMEOUT]       = "タイムアウト",
    [TS_STR_ERR_NOT_SUPPORTED] = "非対応",
    [TS_STR_ERR_INVALID_STATE] = "無効な状態",
    [TS_STR_ERR_IO]            = "I/O エラー",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "再起動中...",
    [TS_STR_SHUTTING_DOWN]     = "シャットダウン中...",
    [TS_STR_REBOOT_IN]         = "%d秒後に再起動",
};

/* Korean strings */
static const char *s_strings_ko[TS_STR_MAX] = {
    /* System messages */
    [TS_STR_WELCOME]           = "TianShanOS에 오신 것을 환영합니다",
    [TS_STR_VERSION]           = "버전",
    [TS_STR_READY]             = "준비됨",
    [TS_STR_ERROR]             = "오류",
    [TS_STR_SUCCESS]           = "성공",
    [TS_STR_FAILED]            = "실패",
    [TS_STR_UNKNOWN_CMD]       = "알 수 없는 명령",
    [TS_STR_HELP_HEADER]       = "사용 가능한 명령:",
    [TS_STR_USAGE]             = "사용법",
    
    /* Common prompts */
    [TS_STR_YES]               = "예",
    [TS_STR_NO]                = "아니오",
    [TS_STR_OK]                = "확인",
    [TS_STR_CANCEL]            = "취소",
    [TS_STR_CONFIRM]           = "확인",
    [TS_STR_LOADING]           = "로딩 중...",
    [TS_STR_PLEASE_WAIT]       = "잠시만 기다려 주세요...",
    
    /* Device status */
    [TS_STR_DEVICE_INFO]       = "장치 정보",
    [TS_STR_UPTIME]            = "가동 시간",
    [TS_STR_FREE_HEAP]         = "여유 메모리",
    [TS_STR_CHIP_MODEL]        = "칩 모델",
    [TS_STR_FIRMWARE_VER]      = "펌웨어 버전",
    [TS_STR_TEMPERATURE]       = "온도",
    
    /* Network messages */
    [TS_STR_WIFI_CONNECTED]    = "WiFi 연결됨",
    [TS_STR_WIFI_DISCONNECTED] = "WiFi 연결 끊김",
    [TS_STR_WIFI_SCANNING]     = "WiFi 네트워크 검색 중...",
    [TS_STR_WIFI_CONNECTING]   = "WiFi 연결 중...",
    [TS_STR_IP_ADDRESS]        = "IP 주소",
    [TS_STR_MAC_ADDRESS]       = "MAC 주소",
    [TS_STR_SIGNAL_STRENGTH]   = "신호 강도",
    
    /* LED messages */
    [TS_STR_LED_CONTROLLER]    = "LED 컨트롤러",
    [TS_STR_LED_COUNT]         = "LED 개수",
    [TS_STR_BRIGHTNESS]        = "밝기",
    [TS_STR_EFFECT]            = "효과",
    [TS_STR_COLOR]             = "색상",
    
    /* Power messages */
    [TS_STR_VOLTAGE]           = "전압",
    [TS_STR_CURRENT]           = "전류",
    [TS_STR_POWER]             = "전력",
    [TS_STR_POWER_GOOD]        = "전원 정상",
    [TS_STR_POWER_OFF]         = "전원 꺼짐",
    
    /* Error messages */
    [TS_STR_ERR_INVALID_ARG]   = "잘못된 인수",
    [TS_STR_ERR_NOT_FOUND]     = "찾을 수 없음",
    [TS_STR_ERR_NO_MEM]        = "메모리 부족",
    [TS_STR_ERR_TIMEOUT]       = "시간 초과",
    [TS_STR_ERR_NOT_SUPPORTED] = "지원되지 않음",
    [TS_STR_ERR_INVALID_STATE] = "잘못된 상태",
    [TS_STR_ERR_IO]            = "I/O 오류",
    
    /* Reboot/shutdown */
    [TS_STR_REBOOTING]         = "재시작 중...",
    [TS_STR_SHUTTING_DOWN]     = "종료 중...",
    [TS_STR_REBOOT_IN]         = "%d초 후 재시작",
};

/* Language names */
static const char *s_language_names[TS_LANG_MAX] = {
    [TS_LANG_EN]    = "English",
    [TS_LANG_ZH_CN] = "简体中文",
    [TS_LANG_ZH_TW] = "繁體中文",
    [TS_LANG_JA]    = "日本語",
    [TS_LANG_KO]    = "한국어",
};

/* All string tables */
static const char **s_string_tables[TS_LANG_MAX] = {
    [TS_LANG_EN]    = s_strings_en,
    [TS_LANG_ZH_CN] = s_strings_zh_cn,
    [TS_LANG_ZH_TW] = s_strings_zh_tw,
    [TS_LANG_JA]    = s_strings_ja,
    [TS_LANG_KO]    = s_strings_ko,
};

/*===========================================================================*/
/*                              State                                         */
/*===========================================================================*/

static ts_language_t s_current_lang = TS_LANG_EN;
static bool s_initialized = false;

/*===========================================================================*/
/*                              Implementation                                */
/*===========================================================================*/

esp_err_t ts_i18n_init(void)
{
    if (s_initialized) return ESP_OK;
    
    /* Try to load saved language from config */
    int32_t saved_lang = 0;
    if (ts_config_get_int32("system.language", &saved_lang, 0) == ESP_OK) {
        if (saved_lang >= 0 && saved_lang < TS_LANG_MAX) {
            s_current_lang = (ts_language_t)saved_lang;
        }
    }
    
    s_initialized = true;
    TS_LOGI(TAG, "I18n initialized, language: %s", s_language_names[s_current_lang]);
    return ESP_OK;
}

esp_err_t ts_i18n_deinit(void)
{
    if (!s_initialized) return ESP_OK;
    
    s_initialized = false;
    return ESP_OK;
}

esp_err_t ts_i18n_set_language(ts_language_t lang)
{
    if (lang >= TS_LANG_MAX) return ESP_ERR_INVALID_ARG;
    
    s_current_lang = lang;
    
    /* Save to config */
    ts_config_set_int32("system.language", (int32_t)lang);
    
    TS_LOGI(TAG, "Language set to: %s", s_language_names[lang]);
    return ESP_OK;
}

ts_language_t ts_i18n_get_language(void)
{
    return s_current_lang;
}

const char *ts_i18n_get_language_name(ts_language_t lang)
{
    if (lang >= TS_LANG_MAX) return "Unknown";
    return s_language_names[lang];
}

const char *ts_i18n_get(ts_string_id_t id)
{
    return ts_i18n_get_lang(s_current_lang, id);
}

const char *ts_i18n_get_lang(ts_language_t lang, ts_string_id_t id)
{
    if (id >= TS_STR_MAX) return "???";
    if (lang >= TS_LANG_MAX) lang = TS_LANG_EN;
    
    const char *str = s_string_tables[lang][id];
    
    /* Fallback to English if not available */
    if (str == NULL) {
        str = s_strings_en[id];
    }
    
    /* Final fallback */
    if (str == NULL) {
        str = "???";
    }
    
    return str;
}

int ts_i18n_sprintf(char *buf, size_t size, ts_string_id_t id, ...)
{
    const char *fmt = ts_i18n_get(id);
    
    va_list args;
    va_start(args, id);
    int ret = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    return ret;
}
