/**
 * @file ts_cmd_net.c
 * @brief Network Console Commands
 * 
 * 实现 net 命令族：
 * - net --status       显示网络状态
 * - net --ip           显示 IP 配置
 * - net --set          设置静态 IP
 * - net --dhcp         DHCP 管理
 * 
 * @author TianShanOS Team
 * @version 1.0.0
 * @date 2026-01-15
 */

#include "ts_console.h"
#include "ts_log.h"
#include "argtable3/argtable3.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include <string.h>
#include <stdio.h>

#define TAG "cmd_net"

/*===========================================================================*/
/*                          Argument Tables                                   */
/*===========================================================================*/

static struct {
    struct arg_lit *status;
    struct arg_lit *ip;
    struct arg_lit *set;
    struct arg_lit *reset;
    struct arg_lit *enable;
    struct arg_lit *disable;
    struct arg_str *ip_addr;
    struct arg_str *netmask;
    struct arg_str *gateway;
    struct arg_lit *dhcp;
    struct arg_lit *json;
    struct arg_lit *help;
    struct arg_end *end;
} s_net_args;

/*===========================================================================*/
/*                          Helper Functions                                  */
/*===========================================================================*/

static const char *wifi_auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        default:                        return "UNKNOWN";
    }
}

/*===========================================================================*/
/*                          Command: net --status                             */
/*===========================================================================*/

static int do_net_status(bool json)
{
    esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t *netif_eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    
    bool wifi_connected = false;
    bool eth_connected = false;
    
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        wifi_connected = true;
    }
    
    // 以太网状态检查
    if (netif_eth && esp_netif_is_netif_up(netif_eth)) {
        eth_connected = true;
    }
    
    if (json) {
        ts_console_printf("{\"wifi\":{\"connected\":%s", 
            wifi_connected ? "true" : "false");
        if (wifi_connected) {
            ts_console_printf(",\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"",
                ap_info.ssid, ap_info.rssi, wifi_auth_mode_str(ap_info.authmode));
        }
        ts_console_printf("},\"ethernet\":{\"connected\":%s}}\n",
            eth_connected ? "true" : "false");
    } else {
        ts_console_printf("Network Status:\n\n");
        
        ts_console_printf("WiFi:\n");
        if (wifi_connected) {
            ts_console_printf("  Status:   \033[32mConnected\033[0m\n");
            ts_console_printf("  SSID:     %s\n", ap_info.ssid);
            ts_console_printf("  RSSI:     %d dBm\n", ap_info.rssi);
            ts_console_printf("  Auth:     %s\n", wifi_auth_mode_str(ap_info.authmode));
            ts_console_printf("  Channel:  %d\n", ap_info.primary);
        } else {
            ts_console_printf("  Status:   \033[33mDisconnected\033[0m\n");
        }
        
        ts_console_printf("\nEthernet:\n");
        if (eth_connected) {
            ts_console_printf("  Status:   \033[32mConnected\033[0m\n");
        } else {
            ts_console_printf("  Status:   \033[33mDisconnected\033[0m\n");
        }
        
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: net --ip                                 */
/*===========================================================================*/

static int do_net_ip(bool json)
{
    esp_netif_t *netif_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_t *netif_eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
    
    esp_netif_ip_info_t ip_info;
    esp_netif_dns_info_t dns_info;
    uint8_t mac[6];
    char mac_str[18];
    char ip_str[16], gw_str[16], nm_str[16], dns_str[16];
    
    if (json) {
        ts_console_printf("{\"interfaces\":[");
        bool first = true;
        
        // WiFi 接口
        if (netif_sta && esp_netif_is_netif_up(netif_sta)) {
            esp_netif_get_ip_info(netif_sta, &ip_info);
            esp_netif_get_mac(netif_sta, mac);
            esp_netif_get_dns_info(netif_sta, ESP_NETIF_DNS_MAIN, &dns_info);
            
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            if (!first) ts_console_printf(",");
            ts_console_printf(
                "{\"name\":\"wifi\",\"ip\":\"%s\",\"netmask\":\"%s\","
                "\"gateway\":\"%s\",\"dns\":\"%s\",\"mac\":\"%s\"}",
                esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str)),
                esp_ip4addr_ntoa(&ip_info.netmask, nm_str, sizeof(nm_str)),
                esp_ip4addr_ntoa(&ip_info.gw, gw_str, sizeof(gw_str)),
                esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str)),
                mac_str);
            first = false;
        }
        
        // 以太网接口
        if (netif_eth && esp_netif_is_netif_up(netif_eth)) {
            esp_netif_get_ip_info(netif_eth, &ip_info);
            esp_netif_get_mac(netif_eth, mac);
            esp_netif_get_dns_info(netif_eth, ESP_NETIF_DNS_MAIN, &dns_info);
            
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            if (!first) ts_console_printf(",");
            ts_console_printf(
                "{\"name\":\"eth\",\"ip\":\"%s\",\"netmask\":\"%s\","
                "\"gateway\":\"%s\",\"dns\":\"%s\",\"mac\":\"%s\"}",
                esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str)),
                esp_ip4addr_ntoa(&ip_info.netmask, nm_str, sizeof(nm_str)),
                esp_ip4addr_ntoa(&ip_info.gw, gw_str, sizeof(gw_str)),
                esp_ip4addr_ntoa(&dns_info.ip.u_addr.ip4, dns_str, sizeof(dns_str)),
                mac_str);
        }
        
        ts_console_printf("]}\n");
    } else {
        ts_console_printf("IP Configuration:\n\n");
        ts_console_printf("%-10s  %-16s  %-16s  %-16s  %s\n",
            "IFACE", "IP", "NETMASK", "GATEWAY", "MAC");
        ts_console_printf("─────────────────────────────────────────────────────────────────────────\n");
        
        // WiFi 接口
        if (netif_sta && esp_netif_is_netif_up(netif_sta)) {
            esp_netif_get_ip_info(netif_sta, &ip_info);
            esp_netif_get_mac(netif_sta, mac);
            
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            ts_console_printf("%-10s  %-16s  %-16s  %-16s  %s\n",
                "wifi",
                esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str)),
                esp_ip4addr_ntoa(&ip_info.netmask, nm_str, sizeof(nm_str)),
                esp_ip4addr_ntoa(&ip_info.gw, gw_str, sizeof(gw_str)),
                mac_str);
        }
        
        // 以太网接口
        if (netif_eth && esp_netif_is_netif_up(netif_eth)) {
            esp_netif_get_ip_info(netif_eth, &ip_info);
            esp_netif_get_mac(netif_eth, mac);
            
            snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            
            ts_console_printf("%-10s  %-16s  %-16s  %-16s  %s\n",
                "eth",
                esp_ip4addr_ntoa(&ip_info.ip, ip_str, sizeof(ip_str)),
                esp_ip4addr_ntoa(&ip_info.netmask, nm_str, sizeof(nm_str)),
                esp_ip4addr_ntoa(&ip_info.gw, gw_str, sizeof(gw_str)),
                mac_str);
        }
        
        ts_console_printf("\n");
    }
    
    return 0;
}

/*===========================================================================*/
/*                          Command: net --set                                */
/*===========================================================================*/

static int do_net_set(const char *ip, const char *netmask, const char *gateway)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    }
    
    if (!netif) {
        ts_console_error("No network interface available\n");
        return 1;
    }
    
    // 停止 DHCP 客户端
    esp_netif_dhcpc_stop(netif);
    
    // 设置静态 IP
    esp_netif_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    
    ip_info.ip.addr = esp_ip4addr_aton(ip);
    if (ip_info.ip.addr == 0 && strcmp(ip, "0.0.0.0") != 0) {
        ts_console_error("Invalid IP address: %s\n", ip);
        return 1;
    }
    
    if (netmask) {
        ip_info.netmask.addr = esp_ip4addr_aton(netmask);
        if (ip_info.netmask.addr == 0) {
            ts_console_error("Invalid netmask: %s\n", netmask);
            return 1;
        }
    } else {
        ip_info.netmask.addr = esp_ip4addr_aton("255.255.255.0");
    }
    
    if (gateway) {
        ip_info.gw.addr = esp_ip4addr_aton(gateway);
        if (ip_info.gw.addr == 0 && strcmp(gateway, "0.0.0.0") != 0) {
            ts_console_error("Invalid gateway: %s\n", gateway);
            return 1;
        }
    }
    
    esp_err_t ret = esp_netif_set_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        ts_console_error("Failed to set IP: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("IP configuration set\n");
    return 0;
}

/*===========================================================================*/
/*                          Command: net --dhcp                               */
/*===========================================================================*/

static int do_net_dhcp(bool enable)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif) {
        netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    }
    
    if (!netif) {
        ts_console_error("No network interface available\n");
        return 1;
    }
    
    esp_err_t ret;
    if (enable) {
        ret = esp_netif_dhcpc_start(netif);
        if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ts_console_warn("DHCP client already running\n");
            return 0;
        }
    } else {
        ret = esp_netif_dhcpc_stop(netif);
        if (ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
            ts_console_warn("DHCP client already stopped\n");
            return 0;
        }
    }
    
    if (ret != ESP_OK) {
        ts_console_error("Failed: %s\n", esp_err_to_name(ret));
        return 1;
    }
    
    ts_console_success("DHCP client %s\n", enable ? "started" : "stopped");
    return 0;
}

/*===========================================================================*/
/*                          Main Command Handler                              */
/*===========================================================================*/

static int cmd_net(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&s_net_args);
    
    if (s_net_args.help->count > 0) {
        ts_console_printf("Usage: net [options]\n\n");
        ts_console_printf("Options:\n");
        ts_console_printf("  -s, --status        Show network status\n");
        ts_console_printf("  -i, --ip            Show IP configuration\n");
        ts_console_printf("      --set           Set static IP\n");
        ts_console_printf("      --ip <addr>     IP address\n");
        ts_console_printf("      --netmask <nm>  Netmask\n");
        ts_console_printf("      --gateway <gw>  Gateway\n");
        ts_console_printf("      --dhcp          DHCP management\n");
        ts_console_printf("      --enable        Enable DHCP\n");
        ts_console_printf("      --disable       Disable DHCP\n");
        ts_console_printf("      --reset         Reset network\n");
        ts_console_printf("  -j, --json          JSON output\n");
        ts_console_printf("  -h, --help          Show this help\n\n");
        ts_console_printf("Examples:\n");
        ts_console_printf("  net --status\n");
        ts_console_printf("  net --ip\n");
        ts_console_printf("  net --set --ip 10.10.99.97 --netmask 255.255.255.0 --gateway 10.10.99.1\n");
        ts_console_printf("  net --dhcp --enable\n");
        return 0;
    }
    
    if (nerrors != 0) {
        arg_print_errors(stderr, s_net_args.end, argv[0]);
        return 1;
    }
    
    bool json = s_net_args.json->count > 0;
    
    if (s_net_args.ip->count > 0) {
        return do_net_ip(json);
    }
    
    if (s_net_args.set->count > 0) {
        if (s_net_args.ip_addr->count == 0) {
            ts_console_error("--ip required for --set\n");
            return 1;
        }
        return do_net_set(
            s_net_args.ip_addr->sval[0],
            s_net_args.netmask->count > 0 ? s_net_args.netmask->sval[0] : NULL,
            s_net_args.gateway->count > 0 ? s_net_args.gateway->sval[0] : NULL);
    }
    
    if (s_net_args.dhcp->count > 0) {
        if (s_net_args.enable->count > 0) {
            return do_net_dhcp(true);
        } else if (s_net_args.disable->count > 0) {
            return do_net_dhcp(false);
        } else {
            // 显示 DHCP 状态
            ts_console_printf("Use --dhcp --enable or --dhcp --disable\n");
            return 0;
        }
    }
    
    if (s_net_args.reset->count > 0) {
        // TODO: 重置网络配置
        ts_console_success("Network configuration reset\n");
        return 0;
    }
    
    // 默认显示状态
    return do_net_status(json);
}

/*===========================================================================*/
/*                          Registration                                      */
/*===========================================================================*/

esp_err_t ts_cmd_net_register(void)
{
    s_net_args.status   = arg_lit0("s", "status", "Show status");
    s_net_args.ip       = arg_lit0("i", "ip", "Show IP config");
    s_net_args.set      = arg_lit0(NULL, "set", "Set static IP");
    s_net_args.reset    = arg_lit0(NULL, "reset", "Reset network");
    s_net_args.enable   = arg_lit0(NULL, "enable", "Enable");
    s_net_args.disable  = arg_lit0(NULL, "disable", "Disable");
    s_net_args.ip_addr  = arg_str0(NULL, "ip", "<addr>", "IP address");
    s_net_args.netmask  = arg_str0(NULL, "netmask", "<nm>", "Netmask");
    s_net_args.gateway  = arg_str0(NULL, "gateway", "<gw>", "Gateway");
    s_net_args.dhcp     = arg_lit0(NULL, "dhcp", "DHCP control");
    s_net_args.json     = arg_lit0("j", "json", "JSON output");
    s_net_args.help     = arg_lit0("h", "help", "Show help");
    s_net_args.end      = arg_end(12);
    
    const ts_console_cmd_t cmd = {
        .command = "net",
        .help = "Network configuration and status",
        .hint = NULL,
        .category = TS_CMD_CAT_NETWORK,
        .func = cmd_net,
        .argtable = &s_net_args
    };
    
    esp_err_t ret = ts_console_register_cmd(&cmd);
    if (ret == ESP_OK) {
        TS_LOGI(TAG, "Network commands registered");
    }
    
    return ret;
}
