/**
 * @file ts_cmd_dhcp.c
 * @brief DHCP Server Console Commands
 * 
 * 实现 dhcp 命令族：
 * - dhcp --status          显示 DHCP 服务器状态
 * - dhcp --clients         列出已连接客户端
 * - dhcp --start/--stop    启动/停止服务器
 * - dhcp --config          显示/修改配置
 * - dhcp --pool            设置地址池
 * - dhcp --bind            管理静态绑定
 * - dhcp --save            保存配置
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-16
 */

#include "ts_console.h"
#include "ts_dhcp_server.h"
#include "ts_config_module.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#define TAG "cmd_dhcp"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *list;
    struct arg_lit *clients;
    struct arg_lit *start;
    struct arg_lit *stop;
    struct arg_lit *restart;
    struct arg_lit *config;
    struct arg_lit *pool;
    struct arg_lit *bind;
    struct arg_lit *bindings;
    struct arg_lit *unbind;
    struct arg_lit *save;
    struct arg_lit *reset;
    struct arg_str *iface;
    struct arg_str *start_ip;
    struct arg_str *end_ip;
    struct arg_str *gateway;
    struct arg_str *netmask;
    struct arg_str *dns;
    struct arg_int *lease;
    struct arg_str *mac;
    struct arg_str *ip;
    struct arg_str *hostname;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_dhcp_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

/* 特殊值表示所有接口 */
#define TS_DHCP_IF_ALL  ((ts_dhcp_if_t)-1)

static ts_dhcp_if_t parse_iface(const char *str)
{
    if (!str || str[0] == '\0') return TS_DHCP_IF_ALL;  /* 默认显示所有接口 */
    if (strcmp(str, "all") == 0) return TS_DHCP_IF_ALL;
    if (strcmp(str, "ap") == 0 || strcmp(str, "wifi") == 0 || strcmp(str, "wifi_ap") == 0) return TS_DHCP_IF_AP;
    if (strcmp(str, "eth") == 0 || strcmp(str, "ethernet") == 0) return TS_DHCP_IF_ETH;
    return TS_DHCP_IF_ALL;
}

static const char *iface_display_name(ts_dhcp_if_t iface)
{
    switch (iface) {
        case TS_DHCP_IF_AP:  return "WiFi AP";
        case TS_DHCP_IF_ETH: return "Ethernet";
        default:             return "Unknown";
    }
}

static const char *state_color(ts_dhcp_server_state_t state)
{
    switch (state) {
        case TS_DHCP_STATE_RUNNING:  return "\033[32m";  /* 绿色 */
        case TS_DHCP_STATE_STARTING: return "\033[33m";  /* 黄色 */
        case TS_DHCP_STATE_ERROR:    return "\033[31m";  /* 红色 */
        default:                     return "\033[90m";  /* 灰色 */
    }
}

static void format_uptime(uint32_t sec, char *buf, size_t len)
{
    uint32_t days = sec / 86400;
    uint32_t hours = (sec % 86400) / 3600;
    uint32_t mins = (sec % 3600) / 60;
    uint32_t secs = sec % 60;
    
    if (days > 0) {
        snprintf(buf, len, "%lud %02lu:%02lu:%02lu", 
                 (unsigned long)days, (unsigned long)hours, 
                 (unsigned long)mins, (unsigned long)secs);
    } else {
        snprintf(buf, len, "%02lu:%02lu:%02lu", 
                 (unsigned long)hours, (unsigned long)mins, (unsigned long)secs);
    }
}

/*===========================================================================*/
/*                          Command: dhcp --list (所有接口概览)               */
/*===========================================================================*/

static int do_dhcp_list_all(bool json_output)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    if (json_output) {
        printf("[\n");
        for (int i = 0; i < num_ifaces; i++) {
            ts_dhcp_status_t status;
            ts_dhcp_config_t config;
            
            esp_err_t ret = ts_dhcp_server_get_status(interfaces[i], &status);
            if (ret != ESP_OK) {
                memset(&status, 0, sizeof(status));
                status.state = TS_DHCP_STATE_STOPPED;
            }
            ts_dhcp_server_get_config(interfaces[i], &config);
            
            printf("  {\n");
            printf("    \"interface\": \"%s\",\n", ts_dhcp_if_to_str(interfaces[i]));
            printf("    \"display_name\": \"%s\",\n", iface_display_name(interfaces[i]));
            printf("    \"state\": \"%s\",\n", ts_dhcp_state_to_str(status.state));
            printf("    \"running\": %s,\n", status.state == TS_DHCP_STATE_RUNNING ? "true" : "false");
            printf("    \"active_leases\": %lu,\n", (unsigned long)status.active_leases);
            printf("    \"pool_start\": \"%s\",\n", config.pool.start_ip);
            printf("    \"pool_end\": \"%s\"\n", config.pool.end_ip);
            printf("  }%s\n", (i < num_ifaces - 1) ? "," : "");
        }
        printf("]\n");
        return 0;
    }
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                      DHCP Server - All Interfaces                         ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Interface      │ State       │ Leases │ Pool Range                       ║\n");
    printf("╠─────────────────┼─────────────┼────────┼───────────────────────────────────╣\n");
    
    for (int i = 0; i < num_ifaces; i++) {
        ts_dhcp_status_t status;
        ts_dhcp_config_t config;
        
        esp_err_t ret = ts_dhcp_server_get_status(interfaces[i], &status);
        if (ret != ESP_OK) {
            memset(&status, 0, sizeof(status));
            status.state = TS_DHCP_STATE_STOPPED;
        }
        ts_dhcp_server_get_config(interfaces[i], &config);
        
        char pool_range[48];
        snprintf(pool_range, sizeof(pool_range), "%s - %s", config.pool.start_ip, config.pool.end_ip);
        
        printf("║  %-14s │ %s%-11s\033[0m │ %3lu/%-3lu │ %-33s ║\n",
               iface_display_name(interfaces[i]),
               state_color(status.state),
               ts_dhcp_state_to_str(status.state),
               (unsigned long)status.active_leases,
               (unsigned long)status.total_pool_size,
               pool_range);
    }
    
    printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n  Use 'dhcp --status --iface <ap|eth>' for detailed interface status\n\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --status                            */
/*===========================================================================*/

static int do_dhcp_status(ts_dhcp_if_t iface, bool json_output)
{
    /* 如果指定 all 或未指定接口，显示所有接口概览 */
    if (iface == TS_DHCP_IF_ALL) {
        return do_dhcp_list_all(json_output);
    }
    
    ts_dhcp_status_t status;
    ts_dhcp_config_t config;
    
    esp_err_t ret = ts_dhcp_server_get_status(iface, &status);
    if (ret != ESP_OK) {
        printf("Error: Failed to get DHCP status for %s\n", iface_display_name(iface));
        return 1;
    }
    
    ts_dhcp_server_get_config(iface, &config);
    
    if (json_output) {
        printf("{\n");
        printf("  \"interface\": \"%s\",\n", ts_dhcp_if_to_str(iface));
        printf("  \"display_name\": \"%s\",\n", iface_display_name(iface));
        printf("  \"state\": \"%s\",\n", ts_dhcp_state_to_str(status.state));
        printf("  \"running\": %s,\n", status.state == TS_DHCP_STATE_RUNNING ? "true" : "false");
        printf("  \"active_leases\": %lu,\n", (unsigned long)status.active_leases);
        printf("  \"total_offers\": %lu,\n", (unsigned long)status.total_offers);
        printf("  \"pool_size\": %lu,\n", (unsigned long)status.total_pool_size);
        printf("  \"available\": %lu,\n", (unsigned long)status.available_count);
        printf("  \"uptime_sec\": %lu,\n", (unsigned long)status.uptime_sec);
        printf("  \"pool\": {\n");
        printf("    \"start\": \"%s\",\n", config.pool.start_ip);
        printf("    \"end\": \"%s\",\n", config.pool.end_ip);
        printf("    \"gateway\": \"%s\",\n", config.pool.gateway);
        printf("    \"netmask\": \"%s\",\n", config.pool.netmask);
        printf("    \"dns\": \"%s\",\n", config.pool.dns1);
        printf("    \"lease_min\": %lu\n", (unsigned long)config.lease_time_min);
        printf("  }\n");
        printf("}\n");
        return 0;
    }
    
    char uptime[32];
    format_uptime(status.uptime_sec, uptime, sizeof(uptime));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║              DHCP Server Status                           ║\n");
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Interface:    %-12s                                ║\n", iface_display_name(iface));
    printf("║  State:        %s%-12s\033[0m                             ║\n", 
           state_color(status.state), ts_dhcp_state_to_str(status.state));
    printf("║  Uptime:       %-16s                            ║\n", uptime);
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Address Pool                                             ║\n");
    printf("║    Start:      %-16s                            ║\n", config.pool.start_ip);
    printf("║    End:        %-16s                            ║\n", config.pool.end_ip);
    printf("║    Gateway:    %-16s                            ║\n", config.pool.gateway);
    printf("║    Netmask:    %-16s                            ║\n", config.pool.netmask);
    printf("║    DNS:        %-16s                            ║\n", config.pool.dns1);
    printf("║    Lease:      %-5lu minutes                              ║\n", 
           (unsigned long)config.lease_time_min);
    printf("╠═══════════════════════════════════════════════════════════╣\n");
    printf("║  Statistics                                               ║\n");
    printf("║    Pool Size:     %-5lu                                   ║\n", 
           (unsigned long)status.total_pool_size);
    printf("║    Active Leases: %-5lu                                   ║\n", 
           (unsigned long)status.active_leases);
    printf("║    Available:     %-5lu                                   ║\n", 
           (unsigned long)status.available_count);
    printf("║    Total Offers:  %-5lu                                   ║\n", 
           (unsigned long)status.total_offers);
    printf("╚═══════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --clients                           */
/*===========================================================================*/

/* 前向声明 - 单接口客户端显示 */
static int do_dhcp_clients_single(ts_dhcp_if_t iface, bool json_output);

static int do_dhcp_clients(ts_dhcp_if_t iface, bool json_output)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 如果指定 all，显示所有接口的客户端 */
    if (iface == TS_DHCP_IF_ALL) {
        if (json_output) {
            printf("[\n");
            for (int i = 0; i < num_ifaces; i++) {
                ts_dhcp_client_t clients[TS_DHCP_MAX_CLIENTS];
                size_t count = 0;
                ts_dhcp_server_get_clients(interfaces[i], clients, TS_DHCP_MAX_CLIENTS, &count);
                
                printf("  {\n");
                printf("    \"interface\": \"%s\",\n", ts_dhcp_if_to_str(interfaces[i]));
                printf("    \"display_name\": \"%s\",\n", iface_display_name(interfaces[i]));
                printf("    \"count\": %zu,\n", count);
                printf("    \"clients\": [");
                for (size_t j = 0; j < count; j++) {
                    char mac_str[18];
                    ts_dhcp_mac_array_to_str(clients[j].mac, mac_str, sizeof(mac_str));
                    printf("%s{\"mac\":\"%s\",\"ip\":\"%s\",\"hostname\":\"%s\"}",
                           j > 0 ? "," : "", mac_str, clients[j].ip, clients[j].hostname);
                }
                printf("]\n");
                printf("  }%s\n", (i < num_ifaces - 1) ? "," : "");
            }
            printf("]\n");
            return 0;
        }
        
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════════════╗\n");
        printf("║                      DHCP Clients - All Interfaces                        ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════════════╝\n");
        
        int total_clients = 0;
        for (int i = 0; i < num_ifaces; i++) {
            ts_dhcp_client_t clients[TS_DHCP_MAX_CLIENTS];
            size_t count = 0;
            ts_dhcp_server_get_clients(interfaces[i], clients, TS_DHCP_MAX_CLIENTS, &count);
            total_clients += count;
            
            printf("\n[%s] %zu clients:\n", iface_display_name(interfaces[i]), count);
            if (count == 0) {
                printf("  (no clients)\n");
            } else {
                printf("  %-18s  %-16s  %-16s\n", "MAC Address", "IP Address", "Hostname");
                printf("  ────────────────────────────────────────────────────────\n");
                for (size_t j = 0; j < count; j++) {
                    char mac_str[18];
                    ts_dhcp_mac_array_to_str(clients[j].mac, mac_str, sizeof(mac_str));
                    printf("  %-18s  %-16s  %-16s\n",
                           mac_str,
                           clients[j].ip[0] ? clients[j].ip : "(pending)",
                           clients[j].hostname[0] ? clients[j].hostname : "-");
                }
            }
        }
        printf("\nTotal: %d clients across all interfaces\n\n", total_clients);
        return 0;
    }
    
    return do_dhcp_clients_single(iface, json_output);
}

static int do_dhcp_clients_single(ts_dhcp_if_t iface, bool json_output)
{
    ts_dhcp_client_t clients[TS_DHCP_MAX_CLIENTS];
    size_t count = 0;
    
    esp_err_t ret = ts_dhcp_server_get_clients(iface, clients, TS_DHCP_MAX_CLIENTS, &count);
    if (ret != ESP_OK) {
        printf("Error: Failed to get DHCP clients\n");
        return 1;
    }
    
    if (json_output) {
        printf("{\n");
        printf("  \"interface\": \"%s\",\n", ts_dhcp_if_to_str(iface));
        printf("  \"count\": %zu,\n", count);
        printf("  \"clients\": [\n");
        for (size_t i = 0; i < count; i++) {
            char mac_str[18];
            ts_dhcp_mac_array_to_str(clients[i].mac, mac_str, sizeof(mac_str));
            printf("    {\n");
            printf("      \"mac\": \"%s\",\n", mac_str);
            printf("      \"ip\": \"%s\",\n", clients[i].ip);
            printf("      \"hostname\": \"%s\",\n", clients[i].hostname);
            printf("      \"lease_start\": %lu,\n", (unsigned long)clients[i].lease_start);
            printf("      \"lease_expire\": %lu,\n", (unsigned long)clients[i].lease_expire);
            printf("      \"is_static\": %s\n", clients[i].is_static ? "true" : "false");
            printf("    }%s\n", i < count - 1 ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
        return 0;
    }
    
    if (count == 0) {
        printf("No DHCP clients connected.\n");
        return 0;
    }
    
    printf("\n");
    printf("DHCP Clients (%s):\n", ts_dhcp_if_to_str(iface));
    printf("═══════════════════════════════════════════════════════════════════════════\n");
    printf("%-18s  %-16s  %-16s  %-8s\n", "MAC Address", "IP Address", "Hostname", "Type");
    printf("───────────────────────────────────────────────────────────────────────────\n");
    
    for (size_t i = 0; i < count; i++) {
        char mac_str[18];
        ts_dhcp_mac_array_to_str(clients[i].mac, mac_str, sizeof(mac_str));
        
        printf("%-18s  %-16s  %-16s  %-8s\n",
               mac_str,
               clients[i].ip[0] ? clients[i].ip : "(pending)",
               clients[i].hostname[0] ? clients[i].hostname : "-",
               clients[i].is_static ? "static" : "dynamic");
    }
    
    printf("───────────────────────────────────────────────────────────────────────────\n");
    printf("Total: %zu clients\n\n", count);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --start/--stop                      */
/*===========================================================================*/

static int do_dhcp_start(ts_dhcp_if_t iface)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 如果指定 all，对所有接口操作 */
    if (iface == TS_DHCP_IF_ALL) {
        int success = 0, failed = 0;
        printf("Starting DHCP server on all interfaces...\n");
        for (int i = 0; i < num_ifaces; i++) {
            esp_err_t ret = ts_dhcp_server_start(interfaces[i]);
            if (ret == ESP_OK) {
                printf("  %s: started\n", iface_display_name(interfaces[i]));
                success++;
            } else {
                printf("  %s: failed (%s)\n", iface_display_name(interfaces[i]), esp_err_to_name(ret));
                failed++;
            }
        }
        printf("Done. %d started, %d failed.\n", success, failed);
        return failed > 0 ? 1 : 0;
    }
    
    printf("Starting DHCP server on %s...\n", iface_display_name(iface));
    
    esp_err_t ret = ts_dhcp_server_start(iface);
    if (ret != ESP_OK) {
        printf("Error: Failed to start DHCP server: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("DHCP server started successfully.\n");
    return 0;
}

static int do_dhcp_stop(ts_dhcp_if_t iface)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 如果指定 all，对所有接口操作 */
    if (iface == TS_DHCP_IF_ALL) {
        int success = 0, failed = 0;
        printf("Stopping DHCP server on all interfaces...\n");
        for (int i = 0; i < num_ifaces; i++) {
            esp_err_t ret = ts_dhcp_server_stop(interfaces[i]);
            if (ret == ESP_OK) {
                printf("  %s: stopped\n", iface_display_name(interfaces[i]));
                success++;
            } else {
                printf("  %s: failed (%s)\n", iface_display_name(interfaces[i]), esp_err_to_name(ret));
                failed++;
            }
        }
        printf("Done. %d stopped, %d failed.\n", success, failed);
        return failed > 0 ? 1 : 0;
    }
    
    printf("Stopping DHCP server on %s...\n", iface_display_name(iface));
    
    esp_err_t ret = ts_dhcp_server_stop(iface);
    if (ret != ESP_OK) {
        printf("Error: Failed to stop DHCP server: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("DHCP server stopped.\n");
    return 0;
}

static int do_dhcp_restart(ts_dhcp_if_t iface)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 如果指定 all，对所有接口操作 */
    if (iface == TS_DHCP_IF_ALL) {
        int success = 0, failed = 0;
        printf("Restarting DHCP server on all interfaces...\n");
        for (int i = 0; i < num_ifaces; i++) {
            esp_err_t ret = ts_dhcp_server_restart(interfaces[i]);
            if (ret == ESP_OK) {
                printf("  %s: restarted\n", iface_display_name(interfaces[i]));
                success++;
            } else {
                printf("  %s: failed (%s)\n", iface_display_name(interfaces[i]), esp_err_to_name(ret));
                failed++;
            }
        }
        printf("Done. %d restarted, %d failed.\n", success, failed);
        return failed > 0 ? 1 : 0;
    }
    
    printf("Restarting DHCP server on %s...\n", iface_display_name(iface));
    
    esp_err_t ret = ts_dhcp_server_restart(iface);
    if (ret != ESP_OK) {
        printf("Error: Failed to restart DHCP server: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("DHCP server restarted successfully.\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --pool                              */
/*===========================================================================*/

static int do_dhcp_pool(ts_dhcp_if_t iface, 
                        const char *start_ip, const char *end_ip,
                        const char *gateway, const char *netmask,
                        const char *dns, int lease)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 检查是否有修改参数 */
    bool has_modify = (start_ip && start_ip[0]) || (end_ip && end_ip[0]) ||
                      (gateway && gateway[0]) || (netmask && netmask[0]) ||
                      (dns && dns[0]) || (lease > 0);
    
    /* 如果指定 all 且没有修改参数，显示所有接口配置 */
    if (iface == TS_DHCP_IF_ALL) {
        if (has_modify) {
            printf("Error: Cannot modify pool for all interfaces at once.\n");
            printf("       Please specify --iface <ap|eth> to modify a specific interface.\n");
            return 1;
        }
        
        printf("\nAddress Pool Configuration (All Interfaces):\n");
        printf("═══════════════════════════════════════════════════════════════════════════\n");
        for (int i = 0; i < num_ifaces; i++) {
            ts_dhcp_config_t config;
            ts_dhcp_server_get_config(interfaces[i], &config);
            printf("\n[%s]\n", iface_display_name(interfaces[i]));
            printf("  Start IP:  %s\n", config.pool.start_ip);
            printf("  End IP:    %s\n", config.pool.end_ip);
            printf("  Gateway:   %s\n", config.pool.gateway);
            printf("  Netmask:   %s\n", config.pool.netmask);
            printf("  DNS:       %s\n", config.pool.dns1);
            printf("  Lease:     %lu minutes\n", (unsigned long)config.lease_time_min);
        }
        printf("\n");
        return 0;
    }
    
    ts_dhcp_config_t config;
    ts_dhcp_server_get_config(iface, &config);
    
    bool modified = false;
    
    if (start_ip && start_ip[0]) {
        strncpy(config.pool.start_ip, start_ip, TS_DHCP_IP_STR_MAX_LEN - 1);
        modified = true;
    }
    if (end_ip && end_ip[0]) {
        strncpy(config.pool.end_ip, end_ip, TS_DHCP_IP_STR_MAX_LEN - 1);
        modified = true;
    }
    if (gateway && gateway[0]) {
        strncpy(config.pool.gateway, gateway, TS_DHCP_IP_STR_MAX_LEN - 1);
        modified = true;
    }
    if (netmask && netmask[0]) {
        strncpy(config.pool.netmask, netmask, TS_DHCP_IP_STR_MAX_LEN - 1);
        modified = true;
    }
    if (dns && dns[0]) {
        strncpy(config.pool.dns1, dns, TS_DHCP_IP_STR_MAX_LEN - 1);
        modified = true;
    }
    if (lease > 0) {
        config.lease_time_min = (uint32_t)lease;
        modified = true;
    }
    
    if (!modified) {
        /* 只显示当前配置 */
        printf("Current address pool configuration:\n");
        printf("  Start IP:  %s\n", config.pool.start_ip);
        printf("  End IP:    %s\n", config.pool.end_ip);
        printf("  Gateway:   %s\n", config.pool.gateway);
        printf("  Netmask:   %s\n", config.pool.netmask);
        printf("  DNS:       %s\n", config.pool.dns1);
        printf("  Lease:     %lu minutes\n", (unsigned long)config.lease_time_min);
        return 0;
    }
    
    esp_err_t ret = ts_dhcp_server_set_config(iface, &config);
    if (ret != ESP_OK) {
        printf("Error: Failed to set configuration: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Configuration updated. Use 'dhcp --restart' to apply.\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --bind                              */
/*===========================================================================*/

static int do_dhcp_bind(ts_dhcp_if_t iface, const char *mac, const char *ip, const char *hostname)
{
    /* 绑定操作必须指定接口 */
    if (iface == TS_DHCP_IF_ALL) {
        printf("Error: Must specify interface for binding.\n");
        printf("Usage: dhcp --bind --iface <ap|eth> --mac aa:bb:cc:dd:ee:ff --ip 10.10.99.50\n");
        return 1;
    }
    
    if (!mac || !mac[0] || !ip || !ip[0]) {
        printf("Error: MAC and IP are required for static binding.\n");
        printf("Usage: dhcp --bind --iface <ap|eth> --mac aa:bb:cc:dd:ee:ff --ip 10.10.99.50\n");
        return 1;
    }
    
    ts_dhcp_static_binding_t binding = {0};
    
    esp_err_t ret = ts_dhcp_mac_str_to_array(mac, binding.mac);
    if (ret != ESP_OK) {
        printf("Error: Invalid MAC address format.\n");
        return 1;
    }
    
    strncpy(binding.ip, ip, TS_DHCP_IP_STR_MAX_LEN - 1);
    binding.enabled = true;
    
    if (hostname && hostname[0]) {
        strncpy(binding.hostname, hostname, TS_DHCP_HOSTNAME_MAX_LEN - 1);
    }
    
    ret = ts_dhcp_server_add_static_binding(iface, &binding);
    if (ret != ESP_OK) {
        printf("Error: Failed to add binding: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("Static binding added: %s -> %s\n", mac, ip);
    return 0;
}

static int do_dhcp_unbind(ts_dhcp_if_t iface, const char *mac)
{
    /* 解绑操作必须指定接口 */
    if (iface == TS_DHCP_IF_ALL) {
        printf("Error: Must specify interface for unbinding.\n");
        printf("Usage: dhcp --unbind --iface <ap|eth> --mac aa:bb:cc:dd:ee:ff\n");
        return 1;
    }
    
    if (!mac || !mac[0]) {
        printf("Error: MAC address is required.\n");
        printf("Usage: dhcp --unbind --iface <ap|eth> --mac aa:bb:cc:dd:ee:ff\n");
        return 1;
    }
    
    uint8_t mac_arr[6];
    esp_err_t ret = ts_dhcp_mac_str_to_array(mac, mac_arr);
    if (ret != ESP_OK) {
        printf("Error: Invalid MAC address format.\n");
        return 1;
    }
    
    ret = ts_dhcp_server_remove_static_binding(iface, mac_arr);
    if (ret != ESP_OK) {
        printf("Error: Binding not found.\n");
        return 1;
    }
    
    printf("Static binding removed: %s\n", mac);
    return 0;
}

static int do_dhcp_bindings(ts_dhcp_if_t iface, bool json_output)
{
    const ts_dhcp_if_t interfaces[] = { TS_DHCP_IF_AP, TS_DHCP_IF_ETH };
    const int num_ifaces = sizeof(interfaces) / sizeof(interfaces[0]);
    
    /* 如果指定 all，显示所有接口的绑定 */
    if (iface == TS_DHCP_IF_ALL) {
        if (json_output) {
            printf("[\n");
            for (int i = 0; i < num_ifaces; i++) {
                ts_dhcp_static_binding_t bindings[TS_DHCP_MAX_STATIC_BINDINGS];
                size_t count = 0;
                ts_dhcp_server_get_static_bindings(interfaces[i], bindings, 
                                                   TS_DHCP_MAX_STATIC_BINDINGS, &count);
                printf("  {\n");
                printf("    \"interface\": \"%s\",\n", ts_dhcp_if_to_str(interfaces[i]));
                printf("    \"count\": %zu,\n", count);
                printf("    \"bindings\": [");
                for (size_t j = 0; j < count; j++) {
                    char mac_str[18];
                    ts_dhcp_mac_array_to_str(bindings[j].mac, mac_str, sizeof(mac_str));
                    printf("%s{\"mac\":\"%s\",\"ip\":\"%s\"}", j > 0 ? "," : "", mac_str, bindings[j].ip);
                }
                printf("]\n");
                printf("  }%s\n", (i < num_ifaces - 1) ? "," : "");
            }
            printf("]\n");
            return 0;
        }
        
        printf("\nStatic Bindings (All Interfaces):\n");
        printf("═══════════════════════════════════════════════════════════════════════════\n");
        int total = 0;
        for (int i = 0; i < num_ifaces; i++) {
            ts_dhcp_static_binding_t bindings[TS_DHCP_MAX_STATIC_BINDINGS];
            size_t count = 0;
            ts_dhcp_server_get_static_bindings(interfaces[i], bindings, 
                                               TS_DHCP_MAX_STATIC_BINDINGS, &count);
            total += count;
            printf("\n[%s] %zu bindings:\n", iface_display_name(interfaces[i]), count);
            if (count == 0) {
                printf("  (no bindings)\n");
            } else {
                printf("  %-18s  %-16s  %-16s\n", "MAC Address", "IP Address", "Hostname");
                printf("  ────────────────────────────────────────────────────────\n");
                for (size_t j = 0; j < count; j++) {
                    char mac_str[18];
                    ts_dhcp_mac_array_to_str(bindings[j].mac, mac_str, sizeof(mac_str));
                    printf("  %-18s  %-16s  %-16s\n", mac_str, bindings[j].ip,
                           bindings[j].hostname[0] ? bindings[j].hostname : "-");
                }
            }
        }
        printf("\nTotal: %d static bindings\n\n", total);
        return 0;
    }
    
    ts_dhcp_static_binding_t bindings[TS_DHCP_MAX_STATIC_BINDINGS];
    size_t count = 0;
    
    esp_err_t ret = ts_dhcp_server_get_static_bindings(iface, bindings, 
                                                        TS_DHCP_MAX_STATIC_BINDINGS, &count);
    if (ret != ESP_OK) {
        printf("Error: Failed to get static bindings.\n");
        return 1;
    }
    
    if (json_output) {
        printf("{\n");
        printf("  \"count\": %zu,\n", count);
        printf("  \"bindings\": [\n");
        for (size_t i = 0; i < count; i++) {
            char mac_str[18];
            ts_dhcp_mac_array_to_str(bindings[i].mac, mac_str, sizeof(mac_str));
            printf("    {\n");
            printf("      \"mac\": \"%s\",\n", mac_str);
            printf("      \"ip\": \"%s\",\n", bindings[i].ip);
            printf("      \"hostname\": \"%s\",\n", bindings[i].hostname);
            printf("      \"enabled\": %s\n", bindings[i].enabled ? "true" : "false");
            printf("    }%s\n", i < count - 1 ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
        return 0;
    }
    
    if (count == 0) {
        printf("No static bindings configured.\n");
        return 0;
    }
    
    printf("\nStatic DHCP Bindings:\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("%-18s  %-16s  %-16s  %-8s\n", "MAC Address", "IP Address", "Hostname", "Enabled");
    printf("───────────────────────────────────────────────────────────────\n");
    
    for (size_t i = 0; i < count; i++) {
        char mac_str[18];
        ts_dhcp_mac_array_to_str(bindings[i].mac, mac_str, sizeof(mac_str));
        printf("%-18s  %-16s  %-16s  %-8s\n",
               mac_str, bindings[i].ip,
               bindings[i].hostname[0] ? bindings[i].hostname : "-",
               bindings[i].enabled ? "yes" : "no");
    }
    
    printf("───────────────────────────────────────────────────────────────\n");
    printf("Total: %zu bindings\n\n", count);
    
    return 0;
}

/*===========================================================================*/
/*                          Command: dhcp --save/--reset                      */
/*===========================================================================*/

static int do_dhcp_save(void)
{
    ts_console_printf("Saving DHCP configuration...\n");
    
    /* 调用原有保存方法 */
    esp_err_t ret = ts_dhcp_server_save_config();
    if (ret != ESP_OK) {
        ts_console_error("Failed to save to NVS: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    /* 同时使用统一配置模块进行双写 */
    ret = ts_config_module_persist(TS_CONFIG_MODULE_DHCP);
    if (ret == ESP_OK) {
        ts_console_success("Configuration saved to NVS");
        if (ts_config_module_has_pending_sync()) {
            printf(" (SD card sync pending)\n");
        } else {
            printf(" and SD card\n");
        }
    } else {
        printf("Configuration saved to NVS\n");
    }
    
    return 0;
}

static int do_dhcp_reset(void)
{
    esp_err_t ret = ts_dhcp_server_reset_config();
    if (ret != ESP_OK) {
        printf("Error: Failed to reset configuration: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    printf("DHCP configuration reset to defaults.\n");
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int do_cmd_dhcp(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_dhcp_args);
    
    if (s_dhcp_args.help->count > 0) {
        printf("Usage: dhcp [OPTIONS]\n\n");
        printf("DHCP Server Management\n\n");
        printf("Options:\n");
        printf("  --status                 Show DHCP server status\n");
        printf("  --list                   List all interfaces status (same as --status without --iface)\n");
        printf("  --clients                List connected DHCP clients\n");
        printf("  --start                  Start DHCP server\n");
        printf("  --stop                   Stop DHCP server\n");
        printf("  --restart                Restart DHCP server\n");
        printf("  --pool                   Show/configure address pool\n");
        printf("  --bind                   Add static binding (requires --mac, --ip)\n");
        printf("  --unbind                 Remove static binding (requires --mac)\n");
        printf("  --bindings               List static bindings\n");
        printf("  --save                   Save configuration to NVS\n");
        printf("  --reset                  Reset to default configuration\n");
        printf("  --iface <ap|eth|all>     Select interface (default: all)\n");
        printf("                           ap/wifi    - WiFi AP interface\n");
        printf("                           eth        - Ethernet interface\n");
        printf("                           all        - All interfaces\n");
        printf("  --start-ip <ip>          Pool start IP\n");
        printf("  --end-ip <ip>            Pool end IP\n");
        printf("  --gateway <ip>           Gateway IP\n");
        printf("  --netmask <mask>         Subnet mask\n");
        printf("  --dns <ip>               DNS server IP\n");
        printf("  --lease <min>            Lease time in minutes\n");
        printf("  --mac <addr>             MAC address for binding\n");
        printf("  --ip <addr>              IP address for binding\n");
        printf("  --hostname <name>        Hostname for binding\n");
        printf("  --json                   Output in JSON format\n");
        printf("\nExamples:\n");
        printf("  dhcp --status                        Show all interfaces status\n");
        printf("  dhcp --status --iface ap             Show WiFi AP DHCP status\n");
        printf("  dhcp --status --iface eth            Show Ethernet DHCP status\n");
        printf("  dhcp --clients --iface ap            List WiFi AP clients\n");
        printf("  dhcp --start --iface eth             Start Ethernet DHCP server\n");
        printf("  dhcp --pool --iface ap --start-ip 10.10.99.100 --end-ip 10.10.99.200\n");
        printf("  dhcp --bind --iface ap --mac aa:bb:cc:dd:ee:ff --ip 10.10.99.50\n");
        printf("  dhcp --unbind --iface ap --mac aa:bb:cc:dd:ee:ff\n");
        return 0;
    }
    
    if (nerrors > 0) {
        arg_print_errors(stderr, s_dhcp_args.end, "dhcp");
        return 1;
    }
    
    /* 解析接口参数 - 默认显示所有接口 */
    ts_dhcp_if_t iface = TS_DHCP_IF_ALL;
    if (s_dhcp_args.iface->count > 0) {
        iface = parse_iface(s_dhcp_args.iface->sval[0]);
    }
    
    bool json_output = s_dhcp_args.json->count > 0;
    
    /* --list 显示所有接口状态 */
    if (s_dhcp_args.list->count > 0) {
        return do_dhcp_list_all(json_output);
    }
    
    /* 处理各种命令 */
    if (s_dhcp_args.status->count > 0) {
        return do_dhcp_status(iface, json_output);
    }
    
    if (s_dhcp_args.clients->count > 0) {
        return do_dhcp_clients(iface, json_output);
    }
    
    if (s_dhcp_args.start->count > 0) {
        return do_dhcp_start(iface);
    }
    
    if (s_dhcp_args.stop->count > 0) {
        return do_dhcp_stop(iface);
    }
    
    if (s_dhcp_args.restart->count > 0) {
        return do_dhcp_restart(iface);
    }
    
    if (s_dhcp_args.pool->count > 0) {
        return do_dhcp_pool(iface,
                           s_dhcp_args.start_ip->count > 0 ? s_dhcp_args.start_ip->sval[0] : NULL,
                           s_dhcp_args.end_ip->count > 0 ? s_dhcp_args.end_ip->sval[0] : NULL,
                           s_dhcp_args.gateway->count > 0 ? s_dhcp_args.gateway->sval[0] : NULL,
                           s_dhcp_args.netmask->count > 0 ? s_dhcp_args.netmask->sval[0] : NULL,
                           s_dhcp_args.dns->count > 0 ? s_dhcp_args.dns->sval[0] : NULL,
                           s_dhcp_args.lease->count > 0 ? s_dhcp_args.lease->ival[0] : 0);
    }
    
    if (s_dhcp_args.bind->count > 0) {
        return do_dhcp_bind(iface,
                           s_dhcp_args.mac->count > 0 ? s_dhcp_args.mac->sval[0] : NULL,
                           s_dhcp_args.ip->count > 0 ? s_dhcp_args.ip->sval[0] : NULL,
                           s_dhcp_args.hostname->count > 0 ? s_dhcp_args.hostname->sval[0] : NULL);
    }
    
    if (s_dhcp_args.unbind->count > 0) {
        return do_dhcp_unbind(iface,
                              s_dhcp_args.mac->count > 0 ? s_dhcp_args.mac->sval[0] : NULL);
    }
    
    if (s_dhcp_args.bindings->count > 0) {
        return do_dhcp_bindings(iface, json_output);
    }
    
    if (s_dhcp_args.save->count > 0) {
        return do_dhcp_save();
    }
    
    if (s_dhcp_args.reset->count > 0) {
        return do_dhcp_reset();
    }
    
    /* 默认显示状态 */
    return do_dhcp_status(iface, json_output);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_dhcp_register(void)
{
    /* 初始化参数表 */
    s_dhcp_args.status = arg_lit0(NULL, "status", "Show DHCP server status");
    s_dhcp_args.list = arg_lit0(NULL, "list", "List all interfaces status");
    s_dhcp_args.clients = arg_lit0(NULL, "clients", "List DHCP clients");
    s_dhcp_args.start = arg_lit0(NULL, "start", "Start DHCP server");
    s_dhcp_args.stop = arg_lit0(NULL, "stop", "Stop DHCP server");
    s_dhcp_args.restart = arg_lit0(NULL, "restart", "Restart DHCP server");
    s_dhcp_args.config = arg_lit0(NULL, "config", "Show configuration");
    s_dhcp_args.pool = arg_lit0(NULL, "pool", "Configure address pool");
    s_dhcp_args.bind = arg_lit0(NULL, "bind", "Add static binding");
    s_dhcp_args.bindings = arg_lit0(NULL, "bindings", "List static bindings");
    s_dhcp_args.unbind = arg_lit0(NULL, "unbind", "Remove static binding");
    s_dhcp_args.save = arg_lit0(NULL, "save", "Save configuration");
    s_dhcp_args.reset = arg_lit0(NULL, "reset", "Reset to defaults");
    s_dhcp_args.iface = arg_str0(NULL, "iface", "<ap|eth|all>", "Interface (default: all)");
    s_dhcp_args.start_ip = arg_str0(NULL, "start-ip", "<ip>", "Pool start IP");
    s_dhcp_args.end_ip = arg_str0(NULL, "end-ip", "<ip>", "Pool end IP");
    s_dhcp_args.gateway = arg_str0(NULL, "gateway", "<ip>", "Gateway IP");
    s_dhcp_args.netmask = arg_str0(NULL, "netmask", "<mask>", "Subnet mask");
    s_dhcp_args.dns = arg_str0(NULL, "dns", "<ip>", "DNS server");
    s_dhcp_args.lease = arg_int0(NULL, "lease", "<min>", "Lease time (minutes)");
    s_dhcp_args.mac = arg_str0(NULL, "mac", "<addr>", "MAC address");
    s_dhcp_args.ip = arg_str0(NULL, "ip", "<addr>", "IP address");
    s_dhcp_args.hostname = arg_str0(NULL, "hostname", "<name>", "Hostname");
    s_dhcp_args.json = arg_lit0("j", "json", "JSON output");
    s_dhcp_args.help = arg_lit0("h", "help", "Show help");
    s_dhcp_args.end = arg_end(5);
    
    const ts_console_cmd_t cmd = {
        .command = "dhcp",
        .help = "DHCP Server management",
        .func = do_cmd_dhcp,
    };
    
    return ts_console_register_cmd(&cmd);
}
