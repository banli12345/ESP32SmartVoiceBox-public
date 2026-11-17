#include "BrowserLocalPage.h"
#include "browser_local_url.h"
#include "setwifi.h"
#include "esp_netif.h"
#include <cstdio>

extern "C" bool browser_is_own_music_page(const char *url)
{
    /* Reserved homepage and loopback aliases. Match before any WiFi snapshot so the page opens offline. */
    if (browser_is_local_music_url(url, "172.31.42.1", nullptr)) return true;
    char ap_ip[16] = {}, sta_ip[16] = {};
    wifi_hotspot_copy_info(nullptr, 0, nullptr, 0, ap_ip, sizeof(ap_ip), nullptr, nullptr);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {};
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr) {
        snprintf(sta_ip, sizeof(sta_ip), IPSTR, IP2STR(&info.ip));
    }
    return browser_is_local_music_url(url, ap_ip, sta_ip);
}
