#include "setwifi.h"
#include "WeatherCity.h"
#include "WeatherConfig.h"
#include "WifiProfiles.h"
#include "DeviceDefaults.h"
#include "ui/custom/custom.h"
#include "IdfRuntime.h"
#include "LifeData.h"
#include "HotspotName.h"

#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "dhcpserver/dhcpserver.h"
#include "esp_netif_defaults.h"
#include "esp_netif_net_stack.h"
#include "lwip/netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "QuickSettingsService.h"

#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <sys/time.h>

static const char *TAG = "setwifi";

static char wifi_ssid_storage[33] = DEVICE_WIFI_SSID;
static char wifi_password_storage[64] = DEVICE_WIFI_PASSWORD;
const char *WIFI_SSID = wifi_ssid_storage;
const char *WIFI_PASSWORD = wifi_password_storage;
char WEATHER_CITY[32] = "nanning";
char WEATHER_CITY_DISPLAY[96] = "南宁";

extern "C" {
char WEATHER_FORECAST_DATE[WEATHER_FORECAST_DAYS][12] = {};
char WEATHER_FORECAST_DAY_TEXT[WEATHER_FORECAST_DAYS][32] = {};
char WEATHER_FORECAST_NIGHT_TEXT[WEATHER_FORECAST_DAYS][32] = {};
char WEATHER_FORECAST_HIGH[WEATHER_FORECAST_DAYS][8] = {};
char WEATHER_FORECAST_LOW[WEATHER_FORECAST_DAYS][8] = {};
uint8_t WEATHER_FORECAST_COUNT = 0;
}

enum {
    WIFI_REFRESH_IDLE = 0,
    WIFI_REFRESH_RUNNING = 1,
    WIFI_REFRESH_DONE = 2,
};

static const uint32_t WIFI_REFRESH_STACK_BYTES = 8192;
static const uint32_t WIFI_REFRESH_RETRY_MS = 30000;
static const uint32_t WIFI_REFRESH_PERIOD_MS = 3600000;

struct wifi_refresh_job {
    uint32_t generation;
    char city[32];
    char city_display[96];
};

struct wifi_refresh_result {
    uint32_t generation;
    char city[32];
    char city_display[96];
    char location[96];
    char weather[96];
    char temp[32];
    char update_time[48];
    char status_message[48];
    int weather_code;
    int http_status;
    char forecast_date[WEATHER_FORECAST_DAYS][12];
    char forecast_day_text[WEATHER_FORECAST_DAYS][32];
    char forecast_night_text[WEATHER_FORECAST_DAYS][32];
    char forecast_high[WEATHER_FORECAST_DAYS][8];
    char forecast_low[WEATHER_FORECAST_DAYS][8];
    uint8_t forecast_count;
    bool ntp_ok;
    bool weather_ok;
    bool forecast_ok;
};

static portMUX_TYPE refresh_mux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint8_t> refresh_state{WIFI_REFRESH_IDLE};
static bool refresh_pending = false;
static uint32_t refresh_generation = 1;
static wifi_refresh_job refresh_job = {};
static wifi_refresh_result refresh_done = {};
static TaskHandle_t refresh_task = nullptr;
static std::atomic<uint32_t> weather_retry_at{0};
static uint32_t refresh_task_retry_at = 0;

static void wifi_refresh_kick(bool allow_refresh);
static void wifi_refresh_on_gui_tick(bool allow_refresh);
static void wifi_refresh_worker(void *arg);

typedef struct {
    const char *pinyin;
    const char *chinese;
} CityEntry;

static const CityEntry city_table[] = {
    {"beijing", "北京"},
    {"shanghai", "上海"},
    {"tianjin", "天津"},
    {"chongqing", "重庆"},
    {"haerbin", "哈尔滨"},
    {"changchun", "长春"},
    {"shenyang", "沈阳"},
    {"hohhot", "呼和浩特"},
    {"shijiazhuang", "石家庄"},
    {"taiyuan", "太原"},
    {"jinan", "济南"},
    {"zhengzhou", "郑州"},
    {"nanjing", "南京"},
    {"hefei", "合肥"},
    {"wuhan", "武汉"},
    {"hangzhou", "杭州"},
    {"nanchang", "南昌"},
    {"fuzhou", "福州"},
    {"guangzhou", "广州"},
    {"nanning", "南宁"},
    {"haikou", "海口"},
    {"kunming", "昆明"},
    {"guiyang", "贵阳"},
    {"chengdu", "成都"},
    {"changsha", "长沙"},
    {"xian", "西安"},
    {"lanzhou", "兰州"},
    {"xining", "西宁"},
    {"yinchuan", "银川"},
    {"wulumuqi", "乌鲁木齐"},
    {"lasa", "拉萨"},
    {"shenzhen", "深圳"},
    {"xiamen", "厦门"},
    {"qingdao", "青岛"},
    {"dalian", "大连"},
    {"suzhou", "苏州"},
    {"wuxi", "无锡"},
    {"ningbo", "宁波"},
    {"dongguan", "东莞"},
    {"foshan", "佛山"},
    {"zhuhai", "珠海"},
    {"guilin", "桂林"},
    {"sanya", "三亚"},
    {"lhasa", "拉萨"},
    {"beijing", "北京"},
    {"harbin", "哈尔滨"},
    {"urumqi", "乌鲁木齐"},
    {"urumchi", "乌鲁木齐"},
    {nullptr, nullptr},
};

static const char *pinyin_to_chinese(const char *pinyin)
{
    if (!pinyin) return nullptr;
    for (int i = 0; city_table[i].pinyin != nullptr; ++i) {
        if (strcasecmp(pinyin, city_table[i].pinyin) == 0) return city_table[i].chinese;
    }
    return nullptr;
}

static const char *ntp_server_1 = "ntp.aliyun.com";
static const char *ntp_server_2 = "ntp.ntsc.ac.cn";
static const char *ntp_server_3 = "pool.ntp.org";

char *weather = nullptr;
char *temp = nullptr;
char *updateTime = nullptr;
static int s_weather_code = -1;
static char s_weather_status[48] = "";
static const char *s_city_error = "";

static const char *OFFLINE_CACHE_NAMESPACE = "offline";
static const uint64_t MIN_VALID_EPOCH = 1000000000ULL;
static const int WIFI_SCAN_LIMIT = 20;

static bool netif_ready = false;
static std::atomic<bool> wifi_started{false};
static bool wifi_driver_ready = false;
static bool sntp_started = false;
static bool timezone_ready = false;
static esp_netif_t *sta_netif = nullptr;
static esp_event_handler_instance_t wifi_any_instance = nullptr;
static esp_event_handler_instance_t ip_got_instance = nullptr;
static esp_event_handler_instance_t ip_lost_instance = nullptr;
static std::atomic<bool> sta_has_ip{false};
static std::atomic<bool> auto_reconnect{true};
static std::atomic<bool> have_sta_config{false};
static std::atomic<bool> scan_active{false};
static esp_ip4_addr_t sta_ip = {};
static char connected_ssid[33] = {};
static uint8_t connected_len = 0;
static portMUX_TYPE wifi_mux = portMUX_INITIALIZER_UNLOCKED;
static char wifi_ssid_list[WIFI_SCAN_LIMIT][33] = {};
static uint8_t wifi_ssid_len[WIFI_SCAN_LIMIT] = {};
static bool wifi_ssid_open[WIFI_SCAN_LIMIT] = {};
static int wifi_scan_results = 0;
static uint32_t wifi_scan_revision_value = 0;
static uint32_t link_epoch = 1;
static uint32_t scan_epoch = 1;
static uint32_t disc_event_epoch = 0;
static int disc_event_reason = 0;
static bool disc_event_pending = false;
static uint32_t scan_event_epoch = 0;
static bool scan_event_pending = false;
static uint32_t ip_event_epoch = 0;
static bool ip_event_pending = false;
static char ip_event_ssid[33] = {};
static uint8_t ip_event_len = 0;
static wifi_profile_store_t wifi_profiles = {};
static wifi_request_queue_t wifi_requests = {};
static wifi_attempt_t wifi_attempt = {};
static uint8_t wifi_fail_mask = 0;
static bool wifi_visible_fresh = false;
static bool wifi_auto_armed = false;
static uint32_t wifi_auto_due = 0;
static uint32_t wifi_attempt_deadline = 0;
static std::atomic<bool> wifi_attempt_wait_disc{false};
static std::atomic<bool> wifi_last_failed{false};
static char wifi_target[33] = {};
static bool wifi_profiles_ready = false;
// Do not start a new explicit scan until the canceled scan completion is drained.
static bool scan_draining = false;
static void retire_scan(void);
static std::atomic<bool> sta_desired{true};
static std::atomic<bool> sta_user_enabled{true};
static esp_netif_t *ap_netif = nullptr;
static std::atomic<bool> ap_active{false};
static std::atomic<bool> napt_on{false};
static std::atomic<uint8_t> ap_clients{0};
static std::atomic<bool> ap_dns_dirty{true};
static esp_ip4_addr_t ap_dns_applied = {};
static char hotspot_ssid[33] = {};
static char hotspot_password[64] = {};
static esp_netif_ip_info_t hotspot_network = {};
static bool hotspot_secret_ready = false;
static SemaphoreHandle_t wifi_api_lock = nullptr;
static StaticSemaphore_t wifi_api_lock_storage;

static int weather_parse_saved_code(const char *text);

static void copy_text(char *destination, size_t destination_size, const char *value)
{
    if (!destination || destination_size == 0) return;
    if (!value) value = "";
    size_t length = strnlen(value, destination_size - 1);
    /* Drop an incomplete UTF-8 sequence instead of publishing a split character. */
    while (length > 0) {
        size_t index = length - 1;
        while (index > 0 && ((unsigned char)value[index] & 0xC0) == 0x80) --index;
        const unsigned char lead = (unsigned char)value[index];
        size_t need = 1;
        if ((lead & 0xE0) == 0xC0) need = 2;
        else if ((lead & 0xF0) == 0xE0) need = 3;
        else if ((lead & 0xF8) == 0xF0) need = 4;
        else if (lead >= 0x80) need = length + 1;
        if (index + need <= length) break;
        length = index;
    }
    memmove(destination, value, length);
    destination[length] = '\0';
}

static void ensure_timezone(void)
{
    if (timezone_ready) return;
    setenv("TZ", "CST-8", 1);
    tzset();
    timezone_ready = true;
}

static bool clock_sample(struct tm *out)
{
    ensure_timezone();
    time_t now = 0;
    time(&now);
    struct tm local = {};
    if (!localtime_r(&now, &local)) return false;
    if (local.tm_year <= (2016 - 1900)) return false;
    if (out) *out = local;
    return true;
}

static bool wait_for_clock(struct tm *out, uint32_t timeout_ms)
{
    const uint32_t started = app_millis();
    do {
        if (clock_sample(out)) return true;
        if ((uint32_t)(app_millis() - started) >= timeout_ms) break;
        app_delay_ms(100);
    } while ((uint32_t)(app_millis() - started) < timeout_ms);
    return clock_sample(out);
}

static bool nvs_ready(void)
{
    const esp_err_t err = nvs_flash_init();
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) return true;
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(err));
    return false;
}

static bool nvs_copy(nvs_handle_t handle, const char *key, char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    size_t needed = 0;
    const esp_err_t query = nvs_get_str(handle, key, nullptr, &needed);
    if (query == ESP_ERR_NVS_NOT_FOUND) return true;
    if (query != ESP_OK || needed == 0) return false;
    if (needed <= out_size) return nvs_get_str(handle, key, out, &needed) == ESP_OK;
    char *temporary = static_cast<char *>(malloc(needed));
    if (!temporary) return false;
    const bool ok = nvs_get_str(handle, key, temporary, &needed) == ESP_OK;
    if (ok) copy_text(out, out_size, temporary);
    free(temporary);
    return ok;
}

static char *nvs_dup(nvs_handle_t handle, const char *key)
{
    size_t needed = 0;
    if (nvs_get_str(handle, key, nullptr, &needed) != ESP_OK || needed <= 1) return nullptr;
    char *value = static_cast<char *>(malloc(needed));
    if (!value) return nullptr;
    if (nvs_get_str(handle, key, value, &needed) != ESP_OK || value[0] == '\0') {
        free(value);
        return nullptr;
    }
    return value;
}

static void log_internal_heap(const char *action, esp_err_t err)
{
    ESP_LOGE(TAG, "%s: %s internal_free=%u internal_largest=%u", action, esp_err_to_name(err),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

static void unregister_wifi_handlers(void)
{
    if (wifi_any_instance) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_any_instance);
        wifi_any_instance = nullptr;
    }
    if (ip_got_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_got_instance);
        ip_got_instance = nullptr;
    }
    if (ip_lost_instance) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_LOST_IP, ip_lost_instance);
        ip_lost_instance = nullptr;
    }
}

static void release_wifi_attempt(void)
{
    unregister_wifi_handlers();
    if (wifi_driver_ready) {
        esp_wifi_stop();
        esp_wifi_deinit();
        wifi_driver_ready = false;
    }
    if (sta_netif) {
        esp_netif_destroy_default_wifi(sta_netif);
        sta_netif = nullptr;
    }
    wifi_started = false;
}

static bool ensure_netif(void)
{
    if (netif_ready) return true;
    if (!nvs_ready()) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_internal_heap("netif init failed", err);
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_internal_heap("event loop init failed", err);
        return false;
    }
    netif_ready = true;
    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        portENTER_CRITICAL(&wifi_mux);
        if (ap_clients < 255) ++ap_clients;
        portEXIT_CRITICAL(&wifi_mux);
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        portENTER_CRITICAL(&wifi_mux);
        if (ap_clients > 0) --ap_clients;
        portEXIT_CRITICAL(&wifi_mux);
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        ap_dns_dirty = true;
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const auto *event = static_cast<const wifi_event_sta_connected_t *>(data);
        size_t length = event ? event->ssid_len : 0;
        if (length > 32) length = 32;
        portENTER_CRITICAL(&wifi_mux);
        const bool requested = !wifi_attempt.active ||
            (!wifi_attempt_wait_disc && length == wifi_attempt.ssid_len &&
             event && std::memcmp(event->ssid, wifi_attempt.ssid, length) == 0);
        const bool established = !sta_has_ip || !connected_len ||
            (event && length == connected_len &&
             std::memcmp(event->ssid, connected_ssid, length) == 0);
        if (!sta_desired || !sta_user_enabled || !length || !requested || !established) {
            portEXIT_CRITICAL(&wifi_mux);
            return;
        }
        std::memset(connected_ssid, 0, sizeof(connected_ssid));
        if (event && length) std::memcpy(connected_ssid, event->ssid, length);
        connected_len = static_cast<uint8_t>(length);
        portEXIT_CRITICAL(&wifi_mux);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *disc = static_cast<const wifi_event_sta_disconnected_t *>(data);
        portENTER_CRITICAL(&wifi_mux);
        if (disc && disc->ssid_len && !wifi_attempt_wait_disc) {
            const size_t length = disc->ssid_len;
            const bool other_link = connected_len &&
                (length != connected_len || std::memcmp(disc->ssid, connected_ssid, length) != 0);
            const bool other_attempt = wifi_attempt.active &&
                (length != wifi_attempt.ssid_len || std::memcmp(disc->ssid, wifi_attempt.ssid, length) != 0);
            if (length > 32 || other_link || other_attempt) {
                portEXIT_CRITICAL(&wifi_mux);
                return;
            }
        }
        sta_has_ip = false;
        sta_ip.addr = 0;
        connected_ssid[0] = '\0';
        connected_len = 0;
        disc_event_epoch = link_epoch;
        disc_event_reason = disc ? static_cast<int>(disc->reason) : 0;
        disc_event_pending = true;
        portEXIT_CRITICAL(&wifi_mux);
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        portENTER_CRITICAL(&wifi_mux);
        scan_event_epoch = scan_epoch;
        scan_event_pending = true;
        portEXIT_CRITICAL(&wifi_mux);
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<const ip_event_got_ip_t *>(data);
        if (!event || !sta_desired || !sta_user_enabled || !sta_netif || !event->ip_info.ip.addr) return;
        esp_netif_ip_info_t live_ip = {};
        wifi_ap_record_t live_ap = {};
        if (esp_netif_get_ip_info(sta_netif, &live_ip) != ESP_OK ||
            live_ip.ip.addr != event->ip_info.ip.addr ||
            esp_wifi_sta_get_ap_info(&live_ap) != ESP_OK) return;
        const size_t live_len = strnlen(reinterpret_cast<const char *>(live_ap.ssid), 32);
        portENTER_CRITICAL(&wifi_mux);
        if (!sta_desired || !sta_user_enabled || !connected_len ||
            connected_len != live_len || std::memcmp(connected_ssid, live_ap.ssid, live_len) != 0 ||
            (wifi_attempt.active && (wifi_attempt_wait_disc ||
             connected_len != wifi_attempt.ssid_len ||
             std::memcmp(connected_ssid, wifi_attempt.ssid, connected_len) != 0))) {
            portEXIT_CRITICAL(&wifi_mux);
            return;
        }
        sta_ip = event->ip_info.ip;
        sta_has_ip = sta_ip.addr != 0;
        ip_event_epoch = link_epoch;
        ip_event_len = connected_len;
        std::memset(ip_event_ssid, 0, sizeof(ip_event_ssid));
        if (connected_len) std::memcpy(ip_event_ssid, connected_ssid, connected_len);
        ip_event_pending = true;
        portEXIT_CRITICAL(&wifi_mux);
        ap_dns_dirty = true;
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        // The old network's lost-IP timer can outlive a manual switch.
        esp_netif_ip_info_t live = {};
        wifi_ap_record_t associated = {};
        if (sta_desired && sta_user_enabled && sta_netif &&
            esp_netif_is_netif_up(sta_netif) &&
            esp_netif_get_ip_info(sta_netif, &live) == ESP_OK && live.ip.addr &&
            esp_wifi_sta_get_ap_info(&associated) == ESP_OK) return;
        portENTER_CRITICAL(&wifi_mux);
        sta_has_ip = false;
        sta_ip.addr = 0;
        portEXIT_CRITICAL(&wifi_mux);
        ap_dns_dirty = true;
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
    }
}

static bool register_wifi_handlers(void)
{
    if (wifi_any_instance && ip_got_instance && ip_lost_instance) return true;
    unregister_wifi_handlers();
    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler,
                                                       nullptr, &wifi_any_instance);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler,
                                                  nullptr, &ip_got_instance);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, wifi_event_handler,
                                                  nullptr, &ip_lost_instance);
    }
    if (err != ESP_OK) {
        log_internal_heap("wifi handler register failed", err);
        unregister_wifi_handlers();
        return false;
    }
    return true;
}

static bool ensure_wifi_started(void)
{
    if (wifi_started) return true;
    if (!ensure_netif()) return false;
    if (!sta_netif) sta_netif = esp_netif_create_default_wifi_sta();
    if (!sta_netif) {
        log_internal_heap("STA netif create failed", ESP_ERR_NO_MEM);
        return false;
    }
    if (!wifi_driver_ready) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        config.nvs_enable = 0;
        const esp_err_t err = esp_wifi_init(&config);
        if (err != ESP_OK) {
            log_internal_heap("wifi init failed", err);
            release_wifi_attempt();
            return false;
        }
        wifi_driver_ready = true;
        const esp_err_t stored = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (stored != ESP_OK) {
            log_internal_heap("wifi storage failed", stored);
            release_wifi_attempt();
            return false;
        }
    }
    if (!register_wifi_handlers()) {
        release_wifi_attempt();
        return false;
    }
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err != ESP_OK) {
        log_internal_heap("wifi start failed", err);
        release_wifi_attempt();
        return false;
    }
    wifi_started = true;
    return true;
}

static bool apply_sta_config(const char *ssid, const char *password)
{
    if (!ssid || !*ssid || !password || !ensure_wifi_started()) return false;
    const size_t ssid_len = strlen(ssid);
    const size_t password_len = strlen(password);
    if (ssid_len > 32 || password_len > 63) return false;

    auto_reconnect = false;
    esp_wifi_disconnect();
    wifi_config_t config = {};
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, password_len);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.failure_retry_cnt = 0;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.threshold.authmode = password_len ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
    const esp_err_t configured = esp_wifi_set_config(WIFI_IF_STA, &config);
    have_sta_config = configured == ESP_OK;
    auto_reconnect = false;
    if (configured != ESP_OK) return false;

    esp_err_t connected = esp_wifi_connect();
    if (connected == ESP_ERR_WIFI_NOT_STARTED) connected = esp_wifi_connect();
    return connected == ESP_OK;
}

static bool start_sntp(void)
{
    ensure_timezone();
    if (!ensure_netif()) return false;
    if (!sntp_started) {
        esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server_1);
        config.start = true;
        config.wait_for_sync = true;
        config.smooth_sync = false;
#if defined(CONFIG_LWIP_SNTP_MAX_SERVERS) && CONFIG_LWIP_SNTP_MAX_SERVERS > 1
        config.servers[1] = ntp_server_2;
        config.num_of_servers = 2;
#endif
#if defined(CONFIG_LWIP_SNTP_MAX_SERVERS) && CONFIG_LWIP_SNTP_MAX_SERVERS > 2
        config.servers[2] = ntp_server_3;
        config.num_of_servers = 3;
#endif
        if (esp_netif_sntp_init(&config) != ESP_OK) return false;
        sntp_started = true;
    } else if (esp_netif_sntp_start() != ESP_OK) {
        return false;
    }
    return true;
}

bool loadCachedTime()
{
    if (!nvs_ready()) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(OFFLINE_CACHE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;
    uint64_t saved_epoch = 0;
    const esp_err_t err = nvs_get_u64(handle, "epoch", &saved_epoch);
    nvs_close(handle);
    if (err != ESP_OK || saved_epoch < MIN_VALID_EPOCH) return false;
    ensure_timezone();
    struct timeval tv = {};
    tv.tv_sec = static_cast<time_t>(saved_epoch);
    settimeofday(&tv, nullptr);
    ESP_LOGI(TAG, "Loaded cached time: %llu", static_cast<unsigned long long>(saved_epoch));
    return true;
}

void saveCurrentTimeCache()
{
    const time_t now = time(nullptr);
    if (now < static_cast<time_t>(MIN_VALID_EPOCH) || !nvs_ready()) return;
    nvs_handle_t handle = 0;
    if (nvs_open(OFFLINE_CACHE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    if (nvs_set_u64(handle, "epoch", static_cast<uint64_t>(now)) == ESP_OK) nvs_commit(handle);
    nvs_close(handle);
}

static nvs_handle_t profile_nvs_handle = 0;

static int profile_nvs_open(void *, int writable)
{
    const esp_err_t err = nvs_open("wifi", writable ? NVS_READWRITE : NVS_READONLY, &profile_nvs_handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return WIFI_NVS_NOT_FOUND;
    return err == ESP_OK ? WIFI_NVS_OK : WIFI_NVS_ERR;
}

static void profile_nvs_close(void *)
{
    nvs_close(profile_nvs_handle);
    profile_nvs_handle = 0;
}

static int profile_nvs_get_blob(void *, const char *key, void *buffer, size_t *length)
{
    const esp_err_t err = nvs_get_blob(profile_nvs_handle, key, buffer, length);
    if (err == ESP_ERR_NVS_NOT_FOUND) return WIFI_NVS_NOT_FOUND;
    return err == ESP_OK ? WIFI_NVS_OK : WIFI_NVS_ERR;
}

static int profile_nvs_set_blob(void *, const char *key, const void *buffer, size_t length)
{
    return nvs_set_blob(profile_nvs_handle, key, buffer, length) == ESP_OK ? WIFI_NVS_OK : WIFI_NVS_ERR;
}

static int profile_nvs_get_str(void *, const char *key, char *buffer, size_t *length)
{
    const esp_err_t err = nvs_get_str(profile_nvs_handle, key, buffer, length);
    if (err == ESP_ERR_NVS_NOT_FOUND) return WIFI_NVS_NOT_FOUND;
    return err == ESP_OK ? WIFI_NVS_OK : WIFI_NVS_ERR;
}

static int profile_nvs_commit(void *)
{
    return nvs_commit(profile_nvs_handle) == ESP_OK ? WIFI_NVS_OK : WIFI_NVS_ERR;
}

static wifi_nvs_io_t profile_nvs_io(void)
{
    wifi_nvs_io_t io = {};
    io.open_ns = profile_nvs_open;
    io.close_ns = profile_nvs_close;
    io.get_blob = profile_nvs_get_blob;
    io.set_blob = profile_nvs_set_blob;
    io.get_str = profile_nvs_get_str;
    io.commit = profile_nvs_commit;
    return io;
}

static void publish_preferred_profile(void)
{
    char ssid[33] = {};
    char password[64] = {};
    if (wifi_profiles.count > 0) {
        const wifi_profile_t *item = &wifi_profiles.items[0];
        std::memcpy(ssid, item->ssid, item->ssid_len);
        std::memcpy(password, item->password, item->password_len);
    }
    portENTER_CRITICAL(&wifi_mux);
    std::memcpy(wifi_ssid_storage, ssid, sizeof(wifi_ssid_storage));
    std::memcpy(wifi_password_storage, password, sizeof(wifi_password_storage));
    portEXIT_CRITICAL(&wifi_mux);
    std::memset(password, 0, sizeof(password));
}

static void seed_bootstrap_profile(wifi_profile_store_t *store)
{
    if (!store || store->count) return;
    const char *ssid = DEVICE_WIFI_SSID;
    const char *password = DEVICE_WIFI_PASSWORD;
    if (!ssid || !password) return;
    bool changed = false;
    wifi_profile_note_success(store, ssid, std::strlen(ssid), password, std::strlen(password), &changed);
}

static void load_wifi_profiles(void)
{
    if (wifi_profiles_ready || !nvs_ready()) return;
    wifi_profile_store_t loaded = {};
    portENTER_CRITICAL(&wifi_mux);
    loaded = wifi_profiles;
    portEXIT_CRITICAL(&wifi_mux);
    const wifi_nvs_io_t io = profile_nvs_io();
    const wifi_profiles_status_t status = wifi_profiles_load(&loaded, &io);
    if (status == WIFI_PROFILES_IO_ERROR) return;
    if (status == WIFI_PROFILES_MIGRATED) {
        if (!wifi_profiles_store(&loaded, &io)) ESP_LOGW(TAG, "WiFi profile migration was not stored");
    } else if (status == WIFI_PROFILES_NOT_FOUND) {
        wifi_profile_store_clear(&loaded);
        seed_bootstrap_profile(&loaded);
    } else if (status == WIFI_PROFILES_REJECTED) {
        ESP_LOGW(TAG, "WiFi profile blob was not loaded");
    }
    portENTER_CRITICAL(&wifi_mux);
    wifi_profiles = loaded;
    portEXIT_CRITICAL(&wifi_mux);
    wifi_profiles_ready = true;
    publish_preferred_profile();
}

static bool persist_profiles_copy(const wifi_profile_store_t *next)
{
    if (!next || !wifi_profiles_ready || !nvs_ready()) return false;
    const wifi_nvs_io_t io = profile_nvs_io();
    if (!wifi_profiles_store(next, &io)) {
        ESP_LOGW(TAG, "WiFi profile store failed");
        return false;
    }
    portENTER_CRITICAL(&wifi_mux);
    wifi_profiles = *next;
    portEXIT_CRITICAL(&wifi_mux);
    publish_preferred_profile();
    return true;
}

void saveWiFiCredentials(const char *ssid, const char *password)
{
    if (!ssid || !password) return;
    const size_t ssid_len = std::strlen(ssid);
    const size_t password_len = std::strlen(password);
    wifi_profile_store_t next = wifi_profiles;
    bool changed = false;
    if (!wifi_profile_note_success(&next, ssid, ssid_len, password, password_len, &changed) || !changed) return;
    if (!persist_profiles_copy(&next)) return;
    ESP_LOGI(TAG, "Saved WiFi profile count=%u", static_cast<unsigned>(wifi_profiles.count));
}

bool connectWiFi()
{
    load_wifi_profiles();
    const bool loaded = wifi_profiles.count > 0;
    ESP_LOGI(TAG, "连接WiFi: %s", WIFI_SSID);
    ESP_LOGI(TAG, "%s", loaded ? "Using saved WiFi credentials" : "Using default WiFi credentials");
    if (!apply_sta_config(WIFI_SSID, WIFI_PASSWORD)) return false;

    const uint32_t started = app_millis();
    while (!sta_has_ip) {
        if ((uint32_t)(app_millis() - started) > 30000) {
            ESP_LOGW(TAG, "WiFi连接超时!");
            return false;
        }
        app_delay_ms(500);
    }
    char address[24] = {};
    portENTER_CRITICAL(&wifi_mux);
    const esp_ip4_addr_t ip = sta_ip;
    portEXIT_CRITICAL(&wifi_mux);
    esp_ip4addr_ntoa(&ip, address, sizeof(address));
    ESP_LOGI(TAG, "WiFi连接成功! IP地址: %s", address);
    saveWiFiCredentials(WIFI_SSID, WIFI_PASSWORD);

    struct tm time_info = {};
    ESP_LOGI(TAG, "Waiting for NTP");
    const bool ntp_ok = start_sntp() && wait_for_clock(&time_info, 5000);
    if (ntp_ok) {
        ESP_LOGI(TAG, "NTP synced: %d-%02d-%02d %02d:%02d:%02d",
                 time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
                 time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
        saveCurrentTimeCache();
    } else {
        ESP_LOGW(TAG, "NTP not ready yet; continuing startup");
    }
    return true;
}

extern "C" bool syncNTPTime(void)
{
    struct tm time_info = {};
    const bool synced = start_sntp() && wait_for_clock(&time_info, 5000);
    if (synced) saveCurrentTimeCache();
    return synced;
}

void cleanupWeatherData()
{
    free(weather);
    free(temp);
    free(updateTime);
    weather = nullptr;
    temp = nullptr;
    updateTime = nullptr;
}

bool loadCachedWeather()
{
    if (!nvs_ready()) return false;
    nvs_handle_t handle = 0;
    if (nvs_open(OFFLINE_CACHE_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return false;

    char saved_city[32] = {};
    char saved_city_display[96] = {};
    char saved_code[8] = {};
    nvs_copy(handle, "city", saved_city, sizeof(saved_city));
    nvs_copy(handle, "city_cn", saved_city_display, sizeof(saved_city_display));
    nvs_copy(handle, "wcode", saved_code, sizeof(saved_code));
    char *saved_weather = nvs_dup(handle, "weather");
    char *saved_temp = nvs_dup(handle, "temp");
    char *saved_update = nvs_dup(handle, "updated");
    uint8_t saved_count = 0;
    if (nvs_get_u8(handle, "fcount", &saved_count) != ESP_OK) saved_count = 0;
    char saved_dates[WEATHER_FORECAST_DAYS][12] = {};
    char saved_day[WEATHER_FORECAST_DAYS][32] = {};
    char saved_night[WEATHER_FORECAST_DAYS][32] = {};
    char saved_high[WEATHER_FORECAST_DAYS][8] = {};
    char saved_low[WEATHER_FORECAST_DAYS][8] = {};
    for (uint8_t i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        char key[12];
        snprintf(key, sizeof(key), "fdate%u", i);
        nvs_copy(handle, key, saved_dates[i], sizeof(saved_dates[i]));
        snprintf(key, sizeof(key), "fday%u", i);
        nvs_copy(handle, key, saved_day[i], sizeof(saved_day[i]));
        snprintf(key, sizeof(key), "fnight%u", i);
        nvs_copy(handle, key, saved_night[i], sizeof(saved_night[i]));
        snprintf(key, sizeof(key), "fhigh%u", i);
        nvs_copy(handle, key, saved_high[i], sizeof(saved_high[i]));
        snprintf(key, sizeof(key), "flow%u", i);
        nvs_copy(handle, key, saved_low[i], sizeof(saved_low[i]));
    }
    nvs_close(handle);

    if (saved_city[0]) copy_text(WEATHER_CITY, sizeof(WEATHER_CITY), saved_city);
    if (saved_city_display[0]) copy_text(WEATHER_CITY_DISPLAY, sizeof(WEATHER_CITY_DISPLAY), saved_city_display);
    s_weather_code = weather_parse_saved_code(saved_code);
    cleanupWeatherData();
    weather = saved_weather;
    temp = saved_temp;
    updateTime = saved_update;
    WEATHER_FORECAST_COUNT = saved_count > WEATHER_FORECAST_DAYS ? WEATHER_FORECAST_DAYS : saved_count;
    for (uint8_t i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        copy_text(WEATHER_FORECAST_DATE[i], sizeof(WEATHER_FORECAST_DATE[i]), saved_dates[i]);
        copy_text(WEATHER_FORECAST_DAY_TEXT[i], sizeof(WEATHER_FORECAST_DAY_TEXT[i]), saved_day[i]);
        copy_text(WEATHER_FORECAST_NIGHT_TEXT[i], sizeof(WEATHER_FORECAST_NIGHT_TEXT[i]), saved_night[i]);
        copy_text(WEATHER_FORECAST_HIGH[i], sizeof(WEATHER_FORECAST_HIGH[i]), saved_high[i]);
        copy_text(WEATHER_FORECAST_LOW[i], sizeof(WEATHER_FORECAST_LOW[i]), saved_low[i]);
    }
    const bool loaded = weather != nullptr || temp != nullptr || WEATHER_FORECAST_COUNT > 0;
    if (loaded) {
        ESP_LOGI(TAG, "Loaded cached weather: %s, %s C", weather ? weather : "--", temp ? temp : "--");
    }
    return loaded;
}

static int weather_parse_saved_code(const char *text)
{
    long value = 0;
    if (!text || !text[0]) return -1;
    for (const char *cursor = text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') return -1;
        value = value * 10 + (*cursor - '0');
        if (value > 99) return -1;
    }
    if ((value >= 0 && value <= 38) || value == 99) return static_cast<int>(value);
    return -1;
}

static void save_weather_nvs(const char *city, const char *city_cn,
                             const char *weather_text, const char *temp_text, const char *updated,
                             const char dates[][12], const char day[][32], const char night[][32],
                             const char high[][8], const char low[][8], uint8_t count,
                             int weather_code, bool include_forecast)
{
    if (!nvs_ready()) return;
    nvs_handle_t handle = 0;
    if (nvs_open(OFFLINE_CACHE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return;
    char code_text[8] = {};
    if ((weather_code >= 0 && weather_code <= 38) || weather_code == 99)
        snprintf(code_text, sizeof(code_text), "%d", weather_code);
    bool stored = nvs_set_str(handle, "city", city ? city : "") == ESP_OK &&
                  nvs_set_str(handle, "city_cn", city_cn ? city_cn : "") == ESP_OK &&
                  nvs_set_str(handle, "weather", weather_text ? weather_text : "") == ESP_OK &&
                  nvs_set_str(handle, "temp", temp_text ? temp_text : "") == ESP_OK &&
                  nvs_set_str(handle, "updated", updated ? updated : "") == ESP_OK &&
                  nvs_set_str(handle, "wcode", code_text) == ESP_OK;
    if (include_forecast) {
        stored = stored && nvs_set_u8(handle, "fcount", count) == ESP_OK;
        for (uint8_t i = 0; stored && i < WEATHER_FORECAST_DAYS; ++i) {
            char key[12];
            snprintf(key, sizeof(key), "fdate%u", i);
            stored = nvs_set_str(handle, key, dates[i]) == ESP_OK;
            snprintf(key, sizeof(key), "fday%u", i);
            stored = stored && nvs_set_str(handle, key, day[i]) == ESP_OK;
            snprintf(key, sizeof(key), "fnight%u", i);
            stored = stored && nvs_set_str(handle, key, night[i]) == ESP_OK;
            snprintf(key, sizeof(key), "fhigh%u", i);
            stored = stored && nvs_set_str(handle, key, high[i]) == ESP_OK;
            snprintf(key, sizeof(key), "flow%u", i);
            stored = stored && nvs_set_str(handle, key, low[i]) == ESP_OK;
        }
    }
    if (stored) nvs_commit(handle);
    nvs_close(handle);
    if (stored) ESP_LOGI(TAG, "Saved weather cache");
}

void saveCachedWeather()
{
    if (!weather && !temp && WEATHER_FORECAST_COUNT == 0) return;
    save_weather_nvs(WEATHER_CITY, WEATHER_CITY_DISPLAY,
                     weather ? weather : "", temp ? temp : "", updateTime ? updateTime : "",
                     WEATHER_FORECAST_DATE, WEATHER_FORECAST_DAY_TEXT, WEATHER_FORECAST_NIGHT_TEXT,
                     WEATHER_FORECAST_HIGH, WEATHER_FORECAST_LOW, WEATHER_FORECAST_COUNT,
                     s_weather_code, true);
}

static void save_weather_cache_from_result(const wifi_refresh_result *result)
{
    if (!result || (!result->weather[0] && !result->temp[0] && !(result->forecast_ok && result->forecast_count))) return;
    save_weather_nvs(result->city, result->city_display, result->weather, result->temp, result->update_time,
                     result->forecast_date, result->forecast_day_text, result->forecast_night_text,
                     result->forecast_high, result->forecast_low, result->forecast_count,
                     result->weather_code, true);
}

static char *http_get_bounded(const char *url, size_t limit, int *status_out)
{
    if (status_out) *status_out = -1;
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 8000;
    config.buffer_size = 1024;
    config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return nullptr;
    char *body = nullptr;
    const uint32_t deadline = app_millis() + 8000;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const int64_t headers = esp_http_client_fetch_headers(client);
        const int status = esp_http_client_get_status_code(client);
        if (status_out) *status_out = status;
        if (status == 200 && headers >= 0) {
            body = static_cast<char *>(heap_caps_malloc(limit + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!body) body = static_cast<char *>(malloc(limit + 1));
            if (body) {
                size_t used = 0;
                while (used < limit && (int32_t)(app_millis() - deadline) < 0) {
                    const int read = esp_http_client_read(client, body + used, static_cast<int>(limit - used));
                    if (read == -ESP_ERR_HTTP_EAGAIN) {
                        app_delay_ms(10);
                        continue;
                    }
                    if (read <= 0) break;
                    used += static_cast<size_t>(read);
                }
                body[used] = '\0';
            }
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return body;
}

static void json_copy(const cJSON *object, const char *key, char *out, size_t out_size, const char *fallback)
{
    if (!out || out_size == 0) return;
    copy_text(out, out_size, fallback ? fallback : "");
    const cJSON *item = object ? cJSON_GetObjectItem(object, key) : nullptr;
    if (cJSON_IsString(item) && item->valuestring) copy_text(out, out_size, item->valuestring);
    else if (cJSON_IsNumber(item)) snprintf(out, out_size, "%.0f", item->valuedouble);
}

static bool weather_append(char *destination, size_t capacity, size_t *used, const char *text)
{
    const size_t length = text ? strlen(text) : 0;
    if (!destination || !used || *used + length >= capacity) return false;
    if (length) memcpy(destination + *used, text, length);
    *used += length;
    destination[*used] = '\0';
    return true;
}

static bool weather_append_pct(char *destination, size_t capacity, size_t *used, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!destination || !used || !text) return false;
    for (const unsigned char *cursor = reinterpret_cast<const unsigned char *>(text); *cursor; ++cursor) {
        const unsigned char value = *cursor;
        const bool raw = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                         (value >= '0' && value <= '9') || value == '-' || value == '_' ||
                         value == '.' || value == '~';
        if (raw) {
            if (*used + 1 >= capacity) return false;
            destination[(*used)++] = static_cast<char>(value);
        } else {
            if (*used + 3 >= capacity) return false;
            destination[(*used)++] = '%';
            destination[(*used)++] = hex[value >> 4];
            destination[(*used)++] = hex[value & 0x0F];
        }
        destination[*used] = '\0';
    }
    return true;
}

static bool weather_build_url(char *url, size_t capacity, const char *prefix, const char *key,
                              const char *location, const char *suffix)
{
    size_t used = 0;
    if (!url || capacity < 8) return false;
    url[0] = '\0';
    const bool ok = weather_append(url, capacity, &used, prefix) &&
                    weather_append(url, capacity, &used, "?key=") &&
                    weather_append_pct(url, capacity, &used, key ? key : "") &&
                    weather_append(url, capacity, &used, "&location=") &&
                    weather_append_pct(url, capacity, &used, location ? location : "") &&
                    weather_append(url, capacity, &used, suffix ? suffix : "");
    if (!ok) url[0] = '\0';
    return ok;
}

static const char *weather_status_for(int http_status, bool parsed)
{
    if (parsed) return "";
    if (http_status == 403) return "天气接口无权限";
    if (http_status < 0) return "网络失败";
    if (http_status == 200) return "天气数据无效";
    return "天气请求失败";
}

static int weather_code_from_item(const cJSON *item)
{
    char digits[4];
    if (cJSON_IsString(item)) return weather_parse_saved_code(item->valuestring);
    if (!cJSON_IsNumber(item)) return -1;
    const double number = item->valuedouble;
    /* Check the double before converting: malformed huge/nonfinite JSON values
     * must not overflow the integer conversion. This also rejects NaN. */
    if (!(number >= 0 && number <= 99)) return -1;
    const int whole = static_cast<int>(number);
    if (number != static_cast<double>(whole) || whole < 0 || whole > 99) return -1;
    snprintf(digits, sizeof(digits), "%d", whole);
    return weather_parse_saved_code(digits);
}

/* The imported endpoint must implement the existing Seniverse JSON protocol.
 * Location permissions remain server-side; this client does not switch APIs. */
static bool fetch_weather_now_into(const char *city, wifi_refresh_result *out)
{
    if (!city || !out) return false;
    out->weather_code = -1;
    out->http_status = -1;
    copy_text(out->status_message, sizeof(out->status_message), "网络失败");
    weather_api_config_t api{};
    if (!weather_config_get(&api)) {
        copy_text(out->status_message, sizeof(out->status_message), "请导入天气配置");
        return false;
    }
    char url[768];
    if (!weather_build_url(url, sizeof(url),
                           api.now_url, api.api_key, city, "&language=zh-Hans&unit=c")) {
        memset(&api, 0, sizeof(api));
        copy_text(out->status_message, sizeof(out->status_message), "天气请求失败");
        return false;
    }
    memset(&api, 0, sizeof(api));
    int status = -1;
    char *payload = http_get_bounded(url, 8192, &status);
    memset(url, 0, sizeof(url));
    out->http_status = status;
    bool success = false;
    if (status == 200 && payload) {
        ESP_LOGI(TAG, "获取到天气数据");
        cJSON *root = cJSON_Parse(payload);
        if (!root) {
            ESP_LOGW(TAG, "JSON解析失败");
        } else {
            cJSON *results = cJSON_GetObjectItem(root, "results");
            cJSON *result = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
            cJSON *now_obj = cJSON_GetObjectItem(result, "now");
            cJSON *location = cJSON_GetObjectItem(result, "location");
            json_copy(location, "name", out->location, sizeof(out->location), "");
            json_copy(now_obj, "text", out->weather, sizeof(out->weather), "");
            json_copy(now_obj, "temperature", out->temp, sizeof(out->temp), "");
            json_copy(result, "last_update", out->update_time, sizeof(out->update_time), "");
            out->weather_code = weather_code_from_item(cJSON_GetObjectItem(now_obj, "code"));
            success = cJSON_IsObject(now_obj) && out->weather[0] && out->temp[0];
            ESP_LOGI(TAG, "当前天气: %s, 温度: %s, 代码: %d",
                     out->weather[0] ? out->weather : "N/A",
                     out->temp[0] ? out->temp : "N/A",
                     out->weather_code);
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGW(TAG, "HTTP请求失败，错误代码: %d", status);
    }
    copy_text(out->status_message, sizeof(out->status_message), weather_status_for(status, success));
    free(payload);
    return success;
}

static bool fetch_forecast_into(const char *city, wifi_refresh_result *out)
{
    if (!city || !out) return false;
    out->forecast_count = 0;
    memset(out->forecast_date, 0, sizeof(out->forecast_date));
    memset(out->forecast_day_text, 0, sizeof(out->forecast_day_text));
    memset(out->forecast_night_text, 0, sizeof(out->forecast_night_text));
    memset(out->forecast_high, 0, sizeof(out->forecast_high));
    memset(out->forecast_low, 0, sizeof(out->forecast_low));

    weather_api_config_t api{};
    if (!weather_config_get(&api)) return false;
    char url[768];
    if (!weather_build_url(url, sizeof(url),
                           api.daily_url, api.api_key, city,
                           "&language=zh-Hans&unit=c&start=0&days=3")) {
        memset(&api, 0, sizeof(api));
        return false;
    }
    memset(&api, 0, sizeof(api));
    int status = -1;
    char *payload = http_get_bounded(url, 8192, &status);
    memset(url, 0, sizeof(url));
    bool success = false;
    if (status == 200 && payload) {
        cJSON *root = cJSON_Parse(payload);
        cJSON *results = root ? cJSON_GetObjectItem(root, "results") : nullptr;
        cJSON *result = cJSON_IsArray(results) ? cJSON_GetArrayItem(results, 0) : nullptr;
        cJSON *daily = result ? cJSON_GetObjectItem(result, "daily") : nullptr;
        if (cJSON_IsArray(daily)) {
            cJSON *day = nullptr;
            cJSON_ArrayForEach(day, daily) {
                if (out->forecast_count >= WEATHER_FORECAST_DAYS) break;
                const uint8_t i = out->forecast_count;
                json_copy(day, "date", out->forecast_date[i], sizeof(out->forecast_date[i]), "");
                json_copy(day, "text_day", out->forecast_day_text[i], sizeof(out->forecast_day_text[i]), "--");
                json_copy(day, "text_night", out->forecast_night_text[i], sizeof(out->forecast_night_text[i]), "--");
                json_copy(day, "high", out->forecast_high[i], sizeof(out->forecast_high[i]), "--");
                json_copy(day, "low", out->forecast_low[i], sizeof(out->forecast_low[i]), "--");
                ++out->forecast_count;
            }
            success = out->forecast_count > 0;
            if (success) {
                ESP_LOGI(TAG, "Weather forecast: loaded %u day(s) for %s",
                         out->forecast_count, out->city_display[0] ? out->city_display : city);
            }
        } else {
            ESP_LOGW(TAG, "Weather forecast JSON parse failed");
        }
        cJSON_Delete(root);
    } else {
        ESP_LOGW(TAG, "Weather forecast HTTP request failed: %d", status);
    }
    free(payload);
    return success;
}

static void wifi_refresh_execute(const wifi_refresh_job *job, wifi_refresh_result *out)
{
    if (!job || !out) return;
    memset(out, 0, sizeof(*out));
    out->weather_code = -1;
    out->http_status = -1;
    out->generation = job->generation;
    copy_text(out->city, sizeof(out->city), job->city);
    copy_text(out->city_display, sizeof(out->city_display), job->city_display);
    out->ntp_ok = syncNTPTime();
    out->weather_ok = fetch_weather_now_into(job->city, out);
    if (out->weather_ok) {
        if (out->location[0] && strcmp(out->city_display, out->city) == 0) {
            copy_text(out->city_display, sizeof(out->city_display), out->location);
        }
        out->forecast_ok = fetch_forecast_into(job->city, out);
    }
}

static bool wifi_refresh_install_strings(const wifi_refresh_result *job)
{
    if (!job) return false;
    char *w = nullptr;
    char *t = nullptr;
    char *u = nullptr;
    if (job->weather[0]) {
        w = static_cast<char *>(malloc(strlen(job->weather) + 1));
        if (w) memcpy(w, job->weather, strlen(job->weather) + 1);
    }
    if (job->temp[0]) {
        t = static_cast<char *>(malloc(strlen(job->temp) + 1));
        if (t) memcpy(t, job->temp, strlen(job->temp) + 1);
    }
    if (job->update_time[0]) {
        u = static_cast<char *>(malloc(strlen(job->update_time) + 1));
        if (u) memcpy(u, job->update_time, strlen(job->update_time) + 1);
    }
    if ((job->weather[0] && !w) || (job->temp[0] && !t) || (job->update_time[0] && !u)) {
        free(w);
        free(t);
        free(u);
        return false;
    }
    cleanupWeatherData();
    weather = w;
    temp = t;
    updateTime = u;
    s_weather_code = job->weather_code;
    copy_text(s_weather_status, sizeof(s_weather_status), job->status_message);
    if (job->city_display[0]) copy_text(WEATHER_CITY_DISPLAY, sizeof(WEATHER_CITY_DISPLAY), job->city_display);
    WEATHER_FORECAST_COUNT = job->forecast_ok ? (job->forecast_count > WEATHER_FORECAST_DAYS ? WEATHER_FORECAST_DAYS : job->forecast_count) : 0;
    /* A successful new current-weather result must not retain another city's
     * old forecast if the forecast request failed. Failed current weather
     * never reaches this publication path and keeps the complete old cache. */
    memcpy(WEATHER_FORECAST_DATE, job->forecast_date, sizeof(WEATHER_FORECAST_DATE));
    memcpy(WEATHER_FORECAST_DAY_TEXT, job->forecast_day_text, sizeof(WEATHER_FORECAST_DAY_TEXT));
    memcpy(WEATHER_FORECAST_NIGHT_TEXT, job->forecast_night_text, sizeof(WEATHER_FORECAST_NIGHT_TEXT));
    memcpy(WEATHER_FORECAST_HIGH, job->forecast_high, sizeof(WEATHER_FORECAST_HIGH));
    memcpy(WEATHER_FORECAST_LOW, job->forecast_low, sizeof(WEATHER_FORECAST_LOW));
    return true;
}

static bool wifi_refresh_publish_job(const wifi_refresh_result *job)
{
    if (!wifi_refresh_install_strings(job)) return false;
    set_weather(weather, temp);
    return true;
}

static void wifi_refresh_worker_run_once(void)
{
    uint8_t state = WIFI_REFRESH_IDLE;
    wifi_refresh_job job = {};
    portENTER_CRITICAL(&refresh_mux);
    state = refresh_state;
    if (state == WIFI_REFRESH_RUNNING) job = refresh_job;
    portEXIT_CRITICAL(&refresh_mux);
    if (state != WIFI_REFRESH_RUNNING) return;

    wifi_refresh_result result = {};
    wifi_refresh_execute(&job, &result);

    uint32_t desired_gen = 0;
    portENTER_CRITICAL(&refresh_mux);
    desired_gen = refresh_generation;
    portEXIT_CRITICAL(&refresh_mux);
    if (result.weather_ok && result.generation == desired_gen) save_weather_cache_from_result(&result);

    portENTER_CRITICAL(&refresh_mux);
    refresh_done = result;
    refresh_state = WIFI_REFRESH_DONE;
    portEXIT_CRITICAL(&refresh_mux);
    ESP_LOGI(TAG, "refresh done city=%s ntp=%u weather=%u forecast=%u stack_free_min=%u",
             result.city, (unsigned)result.ntp_ok, (unsigned)result.weather_ok,
             (unsigned)result.forecast_ok, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

static void wifi_refresh_worker(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        wifi_refresh_worker_run_once();
    }
}

static void wifi_refresh_kick(bool allow_refresh)
{
    if (refresh_state != WIFI_REFRESH_IDLE) return;
    if (!sta_has_ip) return;
    const uint32_t now = app_millis();
    const bool due = refresh_pending ||
                     (allow_refresh && (int32_t)(now - weather_retry_at.load()) >= 0);
    if (!due) return;

    if (!refresh_task) {
        if (refresh_task_retry_at && (int32_t)(now - refresh_task_retry_at) < 0) return;
        /* NTP/cache persistence writes Flash. Keep this task's stack internal
         * so it remains accessible while the Flash cache is disabled. HTTP
         * response bodies retain their existing PSRAM-first allocation. */
        const BaseType_t created = xTaskCreatePinnedToCore(
            wifi_refresh_worker, "WifiRefresh", WIFI_REFRESH_STACK_BYTES, nullptr, 1, &refresh_task, 0);
        if (created != pdPASS) {
            refresh_task = nullptr;
            refresh_task_retry_at = now + 5000;
            ESP_LOGW(TAG, "WifiRefresh task create failed; retry in 5s");
            return;
        }
    }

    portENTER_CRITICAL(&refresh_mux);
    if (refresh_state != WIFI_REFRESH_IDLE) {
        portEXIT_CRITICAL(&refresh_mux);
        return;
    }
    refresh_job.generation = refresh_generation;
    copy_text(refresh_job.city, sizeof(refresh_job.city), WEATHER_CITY);
    copy_text(refresh_job.city_display, sizeof(refresh_job.city_display), WEATHER_CITY_DISPLAY);
    refresh_state = WIFI_REFRESH_RUNNING;
    refresh_pending = false;
    portEXIT_CRITICAL(&refresh_mux);
    xTaskNotifyGive(refresh_task);
}

static void wifi_refresh_on_gui_tick(bool allow_refresh)
{
    wifi_refresh_result job = {};
    bool have = false;
    uint32_t desired_gen = 0;
    portENTER_CRITICAL(&refresh_mux);
    if (refresh_state == WIFI_REFRESH_DONE) {
        job = refresh_done;
        refresh_state = WIFI_REFRESH_IDLE;
        have = true;
    }
    desired_gen = refresh_generation;
    portEXIT_CRITICAL(&refresh_mux);

    if (have) {
        if (job.generation == desired_gen) {
            bool published = true;
            if (job.weather_ok) published = wifi_refresh_publish_job(&job);
            else copy_text(s_weather_status, sizeof(s_weather_status), job.status_message);
            const bool complete = job.ntp_ok && job.weather_ok && job.forecast_ok && published;
            weather_retry_at = app_millis() + (complete ? WIFI_REFRESH_PERIOD_MS : WIFI_REFRESH_RETRY_MS);
        } else {
            refresh_pending = true;
            weather_retry_at = app_millis();
        }
    }
    wifi_refresh_kick(allow_refresh);
}

bool fetchWeatherData()
{
    wifi_refresh_result job = {};
    copy_text(job.city, sizeof(job.city), WEATHER_CITY);
    copy_text(job.city_display, sizeof(job.city_display), WEATHER_CITY_DISPLAY);
    if (!fetch_weather_now_into(WEATHER_CITY, &job)) return false;
    if (job.location[0] && strcmp(job.city_display, job.city) == 0) {
        copy_text(job.city_display, sizeof(job.city_display), job.location);
    }
    return wifi_refresh_install_strings(&job);
}

extern "C" bool fetchWeatherForecast(void)
{
    wifi_refresh_result job = {};
    copy_text(job.city, sizeof(job.city), WEATHER_CITY);
    copy_text(job.city_display, sizeof(job.city_display), WEATHER_CITY_DISPLAY);
    if (!fetch_forecast_into(WEATHER_CITY, &job)) {
        WEATHER_FORECAST_COUNT = 0;
        memset(WEATHER_FORECAST_DATE, 0, sizeof(WEATHER_FORECAST_DATE));
        memset(WEATHER_FORECAST_DAY_TEXT, 0, sizeof(WEATHER_FORECAST_DAY_TEXT));
        memset(WEATHER_FORECAST_NIGHT_TEXT, 0, sizeof(WEATHER_FORECAST_NIGHT_TEXT));
        memset(WEATHER_FORECAST_HIGH, 0, sizeof(WEATHER_FORECAST_HIGH));
        memset(WEATHER_FORECAST_LOW, 0, sizeof(WEATHER_FORECAST_LOW));
        return false;
    }
    WEATHER_FORECAST_COUNT = job.forecast_count;
    memcpy(WEATHER_FORECAST_DATE, job.forecast_date, sizeof(WEATHER_FORECAST_DATE));
    memcpy(WEATHER_FORECAST_DAY_TEXT, job.forecast_day_text, sizeof(WEATHER_FORECAST_DAY_TEXT));
    memcpy(WEATHER_FORECAST_NIGHT_TEXT, job.forecast_night_text, sizeof(WEATHER_FORECAST_NIGHT_TEXT));
    memcpy(WEATHER_FORECAST_HIGH, job.forecast_high, sizeof(WEATHER_FORECAST_HIGH));
    memcpy(WEATHER_FORECAST_LOW, job.forecast_low, sizeof(WEATHER_FORECAST_LOW));
    return true;
}

extern "C" const char *getWeatherCityError(void)
{
    return s_city_error ? s_city_error : "";
}

extern "C" int get_weather_code(void)
{
    return s_weather_code;
}

extern "C" const char *get_weather_status_message(void)
{
    return s_weather_status;
}

extern "C" bool updateWeatherCity(const char *newCity)
{
    char query[96];
    char id[32];
    char shown[96];
    if (weather_city_check_input(newCity, query, sizeof(query)) != 0) {
        s_city_error = "城市名称无效";
        return false;
    }
    if (weather_city_is_legacy_token(query)) {
        const char *chinese = pinyin_to_chinese(query);
        copy_text(id, sizeof(id), query);
        copy_text(shown, sizeof(shown), chinese ? chinese : query);
    } else {
        const int resolved = weather_city_lookup(query, id, sizeof(id), shown, sizeof(shown));
        if (resolved == 1) {
            s_city_error = "城市重名，请加省市";
            return false;
        }
        if (resolved != 0) {
            s_city_error = "未找到该城市";
            return false;
        }
    }
    s_city_error = "";
    copy_text(WEATHER_CITY, sizeof(WEATHER_CITY), id);
    copy_text(WEATHER_CITY_DISPLAY, sizeof(WEATHER_CITY_DISPLAY), shown);
    life_data_set_city(WEATHER_CITY);
    ESP_LOGI(TAG, "City changed to: %s (%s)", WEATHER_CITY, WEATHER_CITY_DISPLAY);
    portENTER_CRITICAL(&refresh_mux);
    if (++refresh_generation == 0) refresh_generation = 1;
    refresh_pending = true;
    portEXIT_CRITICAL(&refresh_mux);
    weather_retry_at = app_millis();
    wifi_refresh_kick(true);
    return true;
}

std::string getBeijingTime()
{
    struct tm time_info = {};
    if (!wait_for_clock(&time_info, 5000)) return "获取时间失败";
    char time_string[50];
    strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", &time_info);
    return std::string(time_string);
}

static void hotspot_format_ssid(char *ssid, size_t ssid_len, const uint8_t mac[6])
{
    if (!ssid || ssid_len == 0) return;
    const uint8_t b3 = mac ? mac[3] : 0;
    const uint8_t b4 = mac ? mac[4] : 0;
    const uint8_t b5 = mac ? mac[5] : 0;
    snprintf(ssid, ssid_len, "ESP32S3-%02X%02X%02X", b3, b4, b5);
}

static bool hotspot_generate_password(char *out, size_t out_len, uint32_t (*rng)(void))
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
    if (!out || out_len < 13 || !rng) return false;
    const unsigned span = static_cast<unsigned>(sizeof(alphabet) - 1U);
    for (int i = 0; i < 12; ++i) out[i] = alphabet[rng() % span];
    out[12] = '\0';
    if (strlen(out) != 12 || strcmp(out, "12345678") == 0) return false;
    return true;
}

extern "C" void wifi_driver_lock(void)
{
    portENTER_CRITICAL(&wifi_mux);
    if (!wifi_api_lock) wifi_api_lock = xSemaphoreCreateRecursiveMutexStatic(&wifi_api_lock_storage);
    portEXIT_CRITICAL(&wifi_mux);
    xSemaphoreTakeRecursive(wifi_api_lock, portMAX_DELAY);
}

extern "C" void wifi_driver_unlock(void)
{
    if (wifi_api_lock) xSemaphoreGiveRecursive(wifi_api_lock);
}

extern "C" void wifi_radio_note_desired(bool enabled)
{
    sta_desired = enabled;
    // The UI may request this during a driver operation. Only publish intent;
    // AppServices applies actual state under wifi_api_lock.
    if (!enabled) auto_reconnect = false;
}

static void clear_sta_link_state(void)
{
    portENTER_CRITICAL(&wifi_mux);
    sta_has_ip = false;
    sta_ip.addr = 0;
    connected_ssid[0] = '\0';
    connected_len = 0;
    portEXIT_CRITICAL(&wifi_mux);
}

static void remember_sta_restore(void)
{
    load_wifi_profiles();
    wifi_fail_mask = 0;
    wifi_visible_fresh = false;
    wifi_auto_armed = wifi_profiles.count > 0;
    wifi_auto_due = app_millis();
}

static bool hotspot_ensure_secret(void)
{
    if (hotspot_secret_ready && hotspot_password_valid(hotspot_password)) return true;
    if (!nvs_ready()) return false;
    char restored[64] = {};
    hotspot_password_restore(nullptr, restored);
    if (!hotspot_password_valid(restored)) {
        // Migrate the previously generated password without changing it.
        nvs_handle_t legacy = 0;
        char saved[64] = {};
        if (nvs_open("hotspot", NVS_READONLY, &legacy) == ESP_OK) {
            size_t length = sizeof(saved);
            if (nvs_get_str(legacy, "psk", saved, &length) != ESP_OK ||
                length == 0 || length > sizeof(saved) || saved[length - 1] != 0) saved[0] = 0;
            nvs_close(legacy);
        }
        if (hotspot_password_valid(saved)) hotspot_password_restore(saved, restored);
    }
    if (!hotspot_password_valid(restored)) {
        char generated[64] = {};
        if (!hotspot_generate_password(generated, sizeof(generated), esp_random)) return false;
        nvs_handle_t handle = 0;
        esp_err_t err = nvs_open("hotspot", NVS_READWRITE, &handle);
        if (err != ESP_OK) return false;
        err = nvs_set_str(handle, "psk", generated);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
        if (err != ESP_OK) {log_internal_heap("hotspot password save failed", err);return false;}
        hotspot_password_restore(generated, restored);
    }
    portENTER_CRITICAL(&wifi_mux);
    memcpy(hotspot_password, restored, sizeof(hotspot_password));
    portEXIT_CRITICAL(&wifi_mux);
    hotspot_secret_ready = true;
    return true;
}

static void hotspot_prepare_ssid(void)
{
    if (hotspot_ssid[0]) return;
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) memset(mac, 0, sizeof(mac));
    char name[sizeof(hotspot_ssid)] = {};
    hotspot_format_ssid(name, sizeof(name), mac);
    char restored[33] = {};
    hotspot_name_restore(name, restored);
    memcpy(name, restored, sizeof(name));
    portENTER_CRITICAL(&wifi_mux);
    memcpy(hotspot_ssid, name, sizeof(hotspot_ssid));
    portEXIT_CRITICAL(&wifi_mux);
}

static bool hotspot_program_ap(void)
{
    if (!hotspot_ssid[0]) hotspot_prepare_ssid();
    const size_t ssid_len = strlen(hotspot_ssid);
    const size_t pass_len = strlen(hotspot_password);
    if (ssid_len == 0 || ssid_len > 32 || !hotspot_password_valid(hotspot_password)) return false;
    wifi_config_t cfg = {};
    memcpy(cfg.ap.ssid, hotspot_ssid, ssid_len);
    cfg.ap.ssid_len = static_cast<uint8_t>(ssid_len);
    memcpy(cfg.ap.password, hotspot_password, pass_len);
    cfg.ap.channel = 1;
    wifi_ap_record_t joined = {};
    if (sta_has_ip && esp_wifi_sta_get_ap_info(&joined) == ESP_OK && joined.primary) cfg.ap.channel = joined.primary;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    cfg.ap.max_connection = 4;
    cfg.ap.beacon_interval = 100;
    cfg.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK) log_internal_heap("ap config failed", err);
    return err == ESP_OK;
}

static bool create_ap_netif(void)
{
    if (ap_netif) return true;
    if (!ensure_netif()) return false;
    const esp_netif_inherent_config_t *builtin = ESP_NETIF_BASE_DEFAULT_WIFI_AP;
    if (!builtin) return false;
    static esp_netif_inherent_config_t stored;
    stored = *builtin;
    stored.route_prio = 1;
    // Use a separate LAN, and avoid the upstream's subnet when it is known.
    esp_netif_ip_info_t upstream = {};
    if (sta_netif) esp_netif_get_ip_info(sta_netif, &upstream);
    const unsigned networks[][4] = {{172,31,42,1},{192,168,4,1},{192,168,5,1},{10,42,43,1}};
    esp_netif_ip_info_t selected = {};
    for (const auto &network : networks) {
        IP4_ADDR(&selected.ip, network[0], network[1], network[2], network[3]);
        IP4_ADDR(&selected.netmask, 255,255,255,0);
        selected.gw = selected.ip;
        const bool overlap = upstream.ip.addr && upstream.netmask.addr &&
            (((selected.ip.addr & upstream.netmask.addr) == (upstream.ip.addr & upstream.netmask.addr)) ||
             ((selected.ip.addr & selected.netmask.addr) == (upstream.ip.addr & selected.netmask.addr)));
        if (!overlap) break;
    }
    portENTER_CRITICAL(&wifi_mux);
    hotspot_network = selected;
    portEXIT_CRITICAL(&wifi_mux);
    stored.ip_info = &hotspot_network;
    esp_netif_config_t cfg = {};
    cfg.base = &stored;
    cfg.driver = nullptr;
    cfg.stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_AP;
    ap_netif = esp_netif_new(&cfg);
    if (!ap_netif) {
        log_internal_heap("AP netif create failed", ESP_ERR_NO_MEM);
        return false;
    }
    const esp_err_t attached = esp_netif_attach_wifi_ap(ap_netif);
    const esp_err_t handlers = attached == ESP_OK ? esp_wifi_set_default_wifi_ap_handlers() : attached;
    if (attached != ESP_OK || (handlers != ESP_OK && handlers != ESP_ERR_INVALID_STATE)) {
        log_internal_heap("AP netif attach failed", attached != ESP_OK ? attached : handlers);
        esp_netif_destroy_default_wifi(ap_netif);
        ap_netif = nullptr;
        return false;
    }
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
    return true;
}

static void hotspot_fail_cleanup(void);

static bool wifi_commit_mode(wifi_mode_t mode, bool with_ap)
{
    const bool reconnect = auto_reconnect;
    auto_reconnect = false;
    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
    }
    if (!wifi_driver_ready || !sta_netif) {
        if (!ensure_wifi_started()) return false;
        if (wifi_started) {
            esp_wifi_stop();
            wifi_started = false;
        }
    }
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err == ESP_OK && with_ap && !hotspot_program_ap()) err = ESP_FAIL;
    if (err == ESP_OK && mode != WIFI_MODE_NULL) err = esp_wifi_start();
    if (err != ESP_OK) {
        log_internal_heap("wifi mode failed", err);
        wifi_started = false;
        auto_reconnect = false;
        return false;
    }
    wifi_started = true;
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
    auto_reconnect = sta_user_enabled && reconnect;
    return true;
}

static void wifi_hotspot_refresh_dns_locked(void)
{
    if (!ap_active || !ap_netif) return;
    esp_netif_dns_info_t dns = {};
    bool have = false;
    if (sta_netif && sta_has_ip &&
        esp_netif_get_dns_info(sta_netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
        dns.ip.type == ESP_IPADDR_TYPE_V4 && dns.ip.u_addr.ip4.addr != 0) {
        have = true;
    }
    const uint32_t addr = have ? dns.ip.u_addr.ip4.addr : 0;
    if (!ap_dns_dirty && ap_dns_applied.addr == addr) return;
    uint8_t offer = have ? static_cast<uint8_t>(OFFER_DNS) : 0;
    esp_err_t err = esp_netif_dhcps_stop(ap_netif);
    if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        log_internal_heap("hotspot DHCP stop failed", err);return;
    }
    err = esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
    if (have) {
        if (err == ESP_OK) err = esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    }
    // ESP-NETIF rejects DNS 0.0.0.0. With no upstream, disable the DHCP
    // DNS offer above and leave the server's cached address untouched.
    const esp_err_t started = esp_netif_dhcps_start(ap_netif);
    if (err != ESP_OK || (started != ESP_OK && started != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)) {
        log_internal_heap("hotspot DHCP DNS failed", err != ESP_OK ? err : started);return;
    }
    const bool refresh_clients = have && ap_dns_applied.addr == 0 && ap_clients > 0;
    ap_dns_applied.addr = addr;
    ap_dns_dirty = false;
    if (refresh_clients) esp_wifi_deauth_sta(0); // Renew leases which had no upstream DNS.
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
}

static void hotspot_fail_cleanup(void)
{
    if (ap_netif && napt_on) esp_netif_napt_disable(ap_netif);
    napt_on = false;
    ap_active = false;
    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
    }
    if (ap_netif) {
        esp_netif_destroy_default_wifi(ap_netif);
        ap_netif = nullptr;
    }
    ap_clients = 0;
    if (sta_desired) {
        if (ensure_wifi_started()) {
            sta_user_enabled = true;
            auto_reconnect = true;
            remember_sta_restore();
        } else {
            sta_user_enabled = false;
        }
    }
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
}

extern "C" bool wifi_radio_apply(bool enabled)
{
    if (enabled) {
        sta_desired = true;
        if (ap_active) {
            if (!(sta_user_enabled && wifi_started)) {
                if (!wifi_commit_mode(WIFI_MODE_APSTA, true)) {
                    sta_user_enabled = false;
                    return false;
                }
            }
        } else if (!(sta_user_enabled && wifi_started)) {
            if (!ensure_wifi_started()) {
                sta_user_enabled = false;
                return false;
            }
        }
        sta_user_enabled = true;
        auto_reconnect = true;
        remember_sta_restore();
        return true;
    }

    sta_desired = false;
    sta_user_enabled = false;
    auto_reconnect = false;
    retire_scan();
    portENTER_CRITICAL(&wifi_mux);
    wifi_request_cancel(&wifi_requests);
    std::memset(&wifi_attempt, 0, sizeof(wifi_attempt));
    wifi_attempt_wait_disc = false;
    wifi_target[0] = '\0';
    wifi_last_failed = false;
    wifi_fail_mask = 0;
    wifi_visible_fresh = false;
    wifi_auto_armed = false;
    scan_active = false;
    ++scan_epoch;
    if (scan_epoch == 0) scan_epoch = 1;
    ++link_epoch;
    if (link_epoch == 0) link_epoch = 1;
    ip_event_pending = false;
    disc_event_pending = false;
    if (!scan_draining) scan_event_pending = false;
    if (wifi_scan_results != 0) {
        wifi_scan_results = 0;
        std::memset(wifi_ssid_list, 0, sizeof(wifi_ssid_list));
        ++wifi_scan_revision_value;
        if (wifi_scan_revision_value == 0) wifi_scan_revision_value = 1;
    }
    portEXIT_CRITICAL(&wifi_mux);
    if (wifi_driver_ready && wifi_started) {
        if (ap_active) {
            if (!wifi_commit_mode(WIFI_MODE_AP, true)) return false;
        } else {
            esp_wifi_disconnect();
            esp_wifi_stop();
            wifi_started = false;
        }
    }
    clear_sta_link_state();
    return true;
}

extern "C" bool wifi_radio_set_enabled(bool enabled)
{
    wifi_driver_lock();
    const bool ok = wifi_radio_apply(enabled);
    wifi_driver_unlock();
    return ok;
}

extern "C" bool wifi_radio_get_enabled(void)
{
    return sta_user_enabled && wifi_started;
}

extern "C" bool wifi_hotspot_apply(bool enabled)
{
    if (!enabled) {
        if (!(ap_active || ap_netif || napt_on)) return true;
        if (napt_on && ap_netif) esp_netif_napt_disable(ap_netif);
        napt_on = false;
        ap_active = false;
        if (wifi_started) {
            esp_wifi_stop();
            wifi_started = false;
        }
        if (ap_netif) {
            esp_netif_destroy_default_wifi(ap_netif);
            ap_netif = nullptr;
        }
        ap_clients = 0;
        if (sta_desired) {
            if (!ensure_wifi_started()) {
                sta_user_enabled = false;
                return false;
            }
            sta_user_enabled = true;
            auto_reconnect = true;
            remember_sta_restore();
        }
        if (sta_netif) esp_netif_set_default_netif(sta_netif);
        return true;
    }

    if (ap_active && napt_on && wifi_started) return true;
    if (!hotspot_ensure_secret()) return false;
    hotspot_prepare_ssid();
    if (!wifi_driver_ready && !ensure_wifi_started()) return false;
    if (!create_ap_netif()) {
        hotspot_fail_cleanup();
        return false;
    }
    const wifi_mode_t mode = (sta_desired || sta_user_enabled) ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    if (!wifi_commit_mode(mode, true)) {
        hotspot_fail_cleanup();
        return false;
    }
    // esp_wifi_start posts AP_START asynchronously. ESP-NETIF rejects NAPT
    // until that event has brought the AP's lwIP interface up.
    const uint32_t ap_wait_started = app_millis();
    while (!esp_netif_is_netif_up(ap_netif) && app_millis() - ap_wait_started < 2000) {
        app_delay_ms(10);
    }
    if (!esp_netif_is_netif_up(ap_netif)) {
        log_internal_heap("AP network start timeout", ESP_ERR_TIMEOUT);
        hotspot_fail_cleanup();
        return false;
    }
    const esp_err_t napt = esp_netif_napt_enable(ap_netif);
    if (napt != ESP_OK) {
        log_internal_heap("napt enable failed", napt);
        hotspot_fail_cleanup();
        return false;
    }
    napt_on = true;
    ap_active = true;
    ap_dns_dirty = true;
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
    wifi_hotspot_refresh_dns_locked();
    if (sta_desired || sta_user_enabled) {
        sta_user_enabled = true;
        sta_desired = true;
        auto_reconnect = true;
        remember_sta_restore();
    }
    ESP_LOGI(TAG, "Hotspot up ssid=%s", hotspot_ssid);
    return true;
}

extern "C" bool wifi_hotspot_apply_credentials(const char *value, const char *password)
{
    char normalized[33] = {};
    char next_password[64] = {};
    if (!hotspot_name_normalize(value, normalized) || !hotspot_password_valid(password)) return false;
    memcpy(next_password, password, strlen(password));
    wifi_driver_lock();
    hotspot_prepare_ssid();
    bool ok = true;
    if (ap_active && wifi_started) {
        wifi_config_t config = {};
        ok = esp_wifi_get_config(WIFI_IF_AP, &config) == ESP_OK;
        if (ok) {
            memset(config.ap.ssid, 0, sizeof(config.ap.ssid));
            const size_t length = strlen(normalized);
            memcpy(config.ap.ssid, normalized, length);
            config.ap.ssid_len = static_cast<uint8_t>(length);
            memset(config.ap.password, 0, sizeof(config.ap.password));
            memcpy(config.ap.password, next_password, strlen(next_password));
            ok = esp_wifi_set_config(WIFI_IF_AP, &config) == ESP_OK;
        }
    }
    if (ok) {
        portENTER_CRITICAL(&wifi_mux);
        memcpy(hotspot_ssid, normalized, sizeof(hotspot_ssid));
        memcpy(hotspot_password, next_password, sizeof(hotspot_password));
        portEXIT_CRITICAL(&wifi_mux);
        hotspot_secret_ready = true;
    }
    wifi_driver_unlock();
    return ok;
}

extern "C" bool wifi_hotspot_apply_name(const char *value)
{
    char password[64] = {};
    portENTER_CRITICAL(&wifi_mux);
    memcpy(password, hotspot_password, sizeof(password));
    portEXIT_CRITICAL(&wifi_mux);
    return wifi_hotspot_apply_credentials(value, password);
}

extern "C" bool wifi_hotspot_set_enabled(bool enabled)
{
    wifi_driver_lock();
    const bool ok = wifi_hotspot_apply(enabled);
    wifi_driver_unlock();
    return ok;
}

extern "C" bool wifi_hotspot_get_enabled(void)
{
    return ap_active && napt_on && wifi_started;
}

extern "C" bool wifi_hotspot_internet_available(void)
{
    return ap_active && napt_on && sta_has_ip;
}

struct hotspot_route_debug {bool napt, sta_default, ap_up;};
static esp_err_t hotspot_read_route(void *context)
{
    auto *result = static_cast<hotspot_route_debug *>(context);
    auto *ap = static_cast<struct netif *>(esp_netif_get_netif_impl(ap_netif));
    auto *sta = sta_netif ? static_cast<struct netif *>(esp_netif_get_netif_impl(sta_netif)) : nullptr;
    result->ap_up = ap && netif_is_up(ap);
    result->sta_default = sta && netif_default == sta;
#if IP_NAPT
    result->napt = ap && ap->napt;
#endif
    return ESP_OK;
}

extern "C" void wifi_hotspot_maintain_locked(void)
{
    static int last_status = -1;
    if (!ap_active || !wifi_started) {last_status = -1;return;}
    static uint32_t last_poll = 0;
    if (app_millis() - last_poll < 500) return;
    last_poll = app_millis();
    // STA reconnect/mode changes may arrive after AP_START. Reassert NAPT
    // once the upstream is ready, matching the IDF AP+STA router sequence.
    if (sta_has_ip && ap_dns_dirty && esp_netif_is_netif_up(ap_netif)) {
        const esp_err_t err = esp_netif_napt_enable(ap_netif);
        napt_on = err == ESP_OK;
        if (err != ESP_OK) log_internal_heap("hotspot NAPT refresh failed", err);
    }
    wifi_sta_list_t list = {};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK) {
        unsigned count = list.num;
        if (count > 255) count = 255;
        portENTER_CRITICAL(&wifi_mux);
        ap_clients = static_cast<uint8_t>(count);
        portEXIT_CRITICAL(&wifi_mux);
    }
    wifi_hotspot_refresh_dns_locked();
    const int status = (int)ap_clients.load() | (sta_has_ip ? 256 : 0) |
                       (ap_dns_applied.addr ? 512 : 0);
    if (status != last_status) {
        printf("[HOTSPOT] clients=%u upstream=%u dns_ready=%u napt=%u\n",
               (unsigned)ap_clients.load(), (unsigned)sta_has_ip.load(),
               (unsigned)(ap_dns_applied.addr != 0), (unsigned)napt_on.load());
        hotspot_route_debug route = {};
        esp_netif_tcpip_exec(hotspot_read_route, &route);
        esp_netif_ip_info_t wan = {};
        if (sta_netif) esp_netif_get_ip_info(sta_netif, &wan);
        printf("[ROUTE] lan=" IPSTR " wan=" IPSTR " gateway=" IPSTR " dns=" IPSTR
               " nat_actual=%u default_sta=%u ap_up=%u\n",
               IP2STR(&hotspot_network.ip), IP2STR(&wan.ip), IP2STR(&wan.gw), IP2STR(&ap_dns_applied),
               (unsigned)route.napt, (unsigned)route.sta_default, (unsigned)route.ap_up);
        last_status = status;
    }
    if (sta_netif) esp_netif_set_default_netif(sta_netif);
}

extern "C" void wifi_hotspot_copy_info(char *ssid, size_t ssid_len, char *password, size_t password_len,
                                       char *ip, size_t ip_len, uint8_t *clients, bool *internet_available)
{
    // Snapshot only: first-time NVS work belongs to AppServices on enable.
    portENTER_CRITICAL(&wifi_mux);
    if (ssid && ssid_len) copy_text(ssid, ssid_len, hotspot_ssid);
    if (password && password_len) copy_text(password, password_len, hotspot_password);
    const uint8_t client_count = ap_clients;
    const bool internet = ap_active && napt_on && sta_has_ip;
    const bool ap_up = ap_active;
    const esp_ip4_addr_t address = hotspot_network.ip;
    portEXIT_CRITICAL(&wifi_mux);
    if (clients) *clients = client_count;
    if (internet_available) *internet_available = internet;
    if (ip && ip_len) {
        if (ap_up) snprintf(ip, ip_len, IPSTR, IP2STR(&address));
        else copy_text(ip, ip_len, "0.0.0.0");
    }
}

static const uint32_t WIFI_JOIN_TIMEOUT_MS = 20000;
static const uint32_t WIFI_DISC_WAIT_MS = 1500;
static const uint32_t WIFI_AUTO_COOLDOWN_MS = 10000;

static void publish_join_target(const char *ssid, size_t length)
{
    portENTER_CRITICAL(&wifi_mux);
    std::memset(wifi_target, 0, sizeof(wifi_target));
    if (ssid && length && length <= WIFI_PROFILE_SSID_MAX) std::memcpy(wifi_target, ssid, length);
    portEXIT_CRITICAL(&wifi_mux);
}

static bool sta_radio_ready(void)
{
    if (!sta_user_enabled || !sta_desired) return false;
    if (wifi_started && wifi_driver_ready) return true;
    if (ap_active) return wifi_commit_mode(static_cast<wifi_mode_t>(wifi_radio_mode(true, true)), true);
    return ensure_wifi_started();
}

static void retire_scan(void)
{
    if (!scan_active) return;
    portENTER_CRITICAL(&wifi_mux);
    scan_active = false;
    scan_draining = true;
    ++scan_epoch;
    if (scan_epoch == 0) scan_epoch = 1;
    // Preserve an already delivered SCAN_DONE. If it has not arrived yet,
    // scan_stop will produce it; a newer scan stays queued until it is drained.
    portEXIT_CRITICAL(&wifi_mux);
    if (wifi_driver_ready) esp_wifi_scan_stop();
}

static void clear_attempt_secret(void)
{
    portENTER_CRITICAL(&wifi_mux);
    std::memset(wifi_attempt.password, 0, sizeof(wifi_attempt.password));
    wifi_attempt.password_len = 0;
    wifi_attempt.active = false;
    wifi_attempt_wait_disc = false;
    portEXIT_CRITICAL(&wifi_mux);
}

static bool issue_sta_connect(const char *ssid, size_t ssid_len, const char *password, size_t password_len, bool manual)
{
    if (!ssid || !password || !sta_radio_ready()) return false;
    retire_scan();
    char name[33] = {};
    char secret[64] = {};
    std::memcpy(name, ssid, ssid_len);
    if (password_len) std::memcpy(secret, password, password_len);
    const uint32_t now = app_millis();
    portENTER_CRITICAL(&wifi_mux);
    ++link_epoch;
    if (link_epoch == 0) link_epoch = 1;
    sta_has_ip = false;
    sta_ip.addr = 0;
    connected_ssid[0] = '\0';
    connected_len = 0;
    ip_event_pending = false;
    disc_event_pending = false;
    std::memset(&wifi_attempt, 0, sizeof(wifi_attempt));
    wifi_attempt.active = true;
    wifi_attempt.manual = manual;
    wifi_attempt.epoch = link_epoch;
    wifi_attempt.ssid_len = static_cast<uint8_t>(ssid_len);
    std::memcpy(wifi_attempt.ssid, name, ssid_len);
    wifi_attempt.password_len = static_cast<uint8_t>(password_len);
    if (password_len) std::memcpy(wifi_attempt.password, secret, password_len);
    wifi_attempt_wait_disc = true;
    portEXIT_CRITICAL(&wifi_mux);
    publish_join_target(name, ssid_len);
    const esp_err_t disc = esp_wifi_disconnect();
    bool started = true;
    if (disc != ESP_OK) {
        wifi_attempt_wait_disc = false;
        started = apply_sta_config(name, secret);
        wifi_attempt_deadline = now + WIFI_JOIN_TIMEOUT_MS;
        if (!started) clear_attempt_secret();
    } else {
        wifi_attempt_deadline = now + WIFI_DISC_WAIT_MS;
    }
    std::memset(secret, 0, sizeof(secret));
    return started;
}

static void fail_attempt(bool manual, int profile_index)
{
    clear_attempt_secret();
    publish_join_target(nullptr, 0);
    if (manual) wifi_last_failed = true;
    if (profile_index >= 0 && profile_index < WIFI_PROFILE_CAPACITY) {
        wifi_fail_mask = static_cast<uint8_t>(wifi_fail_mask | (1u << profile_index));
    }
    wifi_auto_armed = wifi_profiles.count > 0;
}

static void complete_attempt(bool write_profile, uint32_t now)
{
    wifi_profile_store_t next = wifi_profiles;
    bool changed = false;
    if (write_profile &&
        wifi_profile_note_success(&next, wifi_attempt.ssid, wifi_attempt.ssid_len,
                                  wifi_attempt.password, wifi_attempt.password_len, &changed) &&
        changed) {
        persist_profiles_copy(&next);
    }
    clear_attempt_secret();
    publish_join_target(nullptr, 0);
    wifi_fail_mask = 0;
    wifi_visible_fresh = false;
    wifi_auto_armed = false;
    wifi_last_failed = false;
    weather_retry_at = now;
}

static bool start_background_scan(void)
{
    if (scan_active || scan_draining || !sta_radio_ready()) return false;
    auto_reconnect = false;
    if (!sta_has_ip && have_sta_config) esp_wifi_disconnect();
    wifi_scan_config_t scan = {};
    scan.show_hidden = true;
    scan.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan.scan_time.active.min = 0;
    scan.scan_time.active.max = 120;
    scan.home_chan_dwell_time = 30;
    portENTER_CRITICAL(&wifi_mux);
    ++scan_epoch;
    if (scan_epoch == 0) scan_epoch = 1;
    scan_event_pending = false;
    scan_active = true;
    portEXIT_CRITICAL(&wifi_mux);
    const esp_err_t err = esp_wifi_scan_start(&scan, false);
    if (err != ESP_OK) {
        portENTER_CRITICAL(&wifi_mux);
        scan_active = false;
        portEXIT_CRITICAL(&wifi_mux);
        log_internal_heap("wifi scan start failed", err);
        return false;
    }
    return true;
}

static void publish_scan_results(void)
{
    wifi_ap_record_t records[WIFI_SCAN_LIMIT];
    uint16_t copied = WIFI_SCAN_LIMIT;
    const esp_err_t err = esp_wifi_scan_get_ap_records(&copied, records);
    if (err != ESP_OK) {
        esp_wifi_clear_ap_list();
        portENTER_CRITICAL(&wifi_mux);
        scan_active = false;
        portEXIT_CRITICAL(&wifi_mux);
        return;
    }
    char names[WIFI_SCAN_LIMIT][33] = {};
    uint8_t lengths[WIFI_SCAN_LIMIT] = {};
    bool opens[WIFI_SCAN_LIMIT] = {};
    int count = 0;
    for (uint16_t i = 0; i < copied && count < WIFI_SCAN_LIMIT; ++i) {
        const size_t length = strnlen(reinterpret_cast<const char *>(records[i].ssid), 32);
        if (length == 0 || length > 32) continue;
        bool duplicate = false;
        for (int j = 0; j < count; ++j) {
            if (lengths[j] == length && std::memcmp(names[j], records[i].ssid, length) == 0) duplicate = true;
        }
        if (duplicate) continue;
        std::memcpy(names[count], records[i].ssid, length);
        lengths[count] = static_cast<uint8_t>(length);
        opens[count] = records[i].authmode == WIFI_AUTH_OPEN;
        ++count;
    }
    portENTER_CRITICAL(&wifi_mux);
    std::memset(wifi_ssid_list, 0, sizeof(wifi_ssid_list));
    std::memset(wifi_ssid_len, 0, sizeof(wifi_ssid_len));
    std::memset(wifi_ssid_open, 0, sizeof(wifi_ssid_open));
    for (int i = 0; i < count; ++i) {
        std::memcpy(wifi_ssid_list[i], names[i], lengths[i]);
        wifi_ssid_len[i] = lengths[i];
        wifi_ssid_open[i] = opens[i];
    }
    wifi_scan_results = count;
    ++wifi_scan_revision_value;
    if (wifi_scan_revision_value == 0) wifi_scan_revision_value = 1;
    scan_active = false;
    portEXIT_CRITICAL(&wifi_mux);
    wifi_visible_fresh = true;
    ESP_LOGI(TAG, "wifi scan results=%d", count);
}

static int profile_index_of_attempt(void)
{
    return wifi_profile_find(&wifi_profiles, wifi_attempt.ssid, wifi_attempt.ssid_len);
}

static void maybe_join_visible(void)
{
    if (wifi_attempt.active || sta_has_ip || !wifi_visible_fresh) return;
    wifi_visible_ap_t visible[WIFI_SCAN_LIMIT];
    int visible_count = 0;
    portENTER_CRITICAL(&wifi_mux);
    if (wifi_requests.connect_pending) {
        portEXIT_CRITICAL(&wifi_mux);
        return;
    }
    visible_count = wifi_scan_results;
    if (visible_count > WIFI_SCAN_LIMIT) visible_count = WIFI_SCAN_LIMIT;
    for (int i = 0; i < visible_count; ++i) {
        std::memset(&visible[i], 0, sizeof(visible[i]));
        visible[i].ssid_len = wifi_ssid_len[i];
        std::memcpy(visible[i].ssid, wifi_ssid_list[i], wifi_ssid_len[i]);
        visible[i].open_network = wifi_ssid_open[i];
    }
    portEXIT_CRITICAL(&wifi_mux);
    const int pick = wifi_autojoin_pick(&wifi_profiles, visible, visible_count, wifi_fail_mask);
    if (pick < 0) {
        wifi_fail_mask = 0;
        wifi_visible_fresh = false;
        wifi_auto_due = app_millis() + WIFI_AUTO_COOLDOWN_MS;
        return;
    }
    const wifi_profile_t *item = &wifi_profiles.items[pick];
    if (!issue_sta_connect(item->ssid, item->ssid_len, item->password, item->password_len, false)) {
        fail_attempt(false, pick);
    }
}

bool wifi_service_start_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) return false;
    portENTER_CRITICAL(&wifi_mux);
    const bool enabled = sta_user_enabled && sta_desired;
    const bool ok = wifi_request_connect(&wifi_requests, ssid, password, enabled);
    if (ok) {
        std::memset(wifi_target, 0, sizeof(wifi_target));
        const size_t length = std::strlen(ssid);
        if (length <= WIFI_PROFILE_SSID_MAX) std::memcpy(wifi_target, ssid, length);
        wifi_last_failed = false;
    }
    portEXIT_CRITICAL(&wifi_mux);
    return ok;
}

bool wifi_service_init(void)
{
    // Reserve the driver's required internal memory before starting AI workers.
    // Failed attempts are released and retried by AppServices.
    wifi_driver_lock();
    hotspot_ensure_secret();
    hotspot_prepare_ssid();
    load_wifi_profiles();
    if (!sta_desired) {
        sta_user_enabled = false;
        auto_reconnect = false;
        wifi_driver_unlock();
        return true;
    }
    if (!ensure_wifi_started()) {
        wifi_driver_unlock();
        return false;
    }
    if (!sta_desired) {
        auto_reconnect = false;
        sta_user_enabled = false;
        esp_wifi_stop();
        wifi_started = false;
        clear_sta_link_state();
        wifi_driver_unlock();
        return true;
    }
    sta_user_enabled = true;
    app_log_memory("WiFi driver ready");
    weather_retry_at = app_millis();
    if (!sta_has_ip) remember_sta_restore();
    wifi_driver_unlock();
    return true;
}

void wifi_service_tick(bool allow_refresh)
{
    if (!sta_user_enabled || !sta_desired) return;
    wifi_refresh_on_gui_tick(allow_refresh);
}

void wifi_service_poll(void)
{
    if (!wifi_api_lock) {
        portENTER_CRITICAL(&wifi_mux);
        if (!wifi_api_lock) wifi_api_lock = xSemaphoreCreateRecursiveMutexStatic(&wifi_api_lock_storage);
        portEXIT_CRITICAL(&wifi_mux);
    }
    if (!wifi_api_lock || xSemaphoreTakeRecursive(wifi_api_lock, 0) != pdTRUE) return;
    if (!sta_user_enabled || !sta_desired) {
        wifi_driver_unlock();
        return;
    }
    load_wifi_profiles();
    const uint32_t now = app_millis();
    static bool saw_sta_ip = false;
    if (sta_has_ip) saw_sta_ip = true;
    else if (saw_sta_ip && !wifi_attempt.active) {
        saw_sta_ip = false;
        wifi_fail_mask = 0;
        wifi_visible_fresh = false;
        wifi_auto_armed = wifi_profiles.count > 0;
        wifi_auto_due = now;
    }
    bool scan_done = false;
    uint32_t done_epoch = 0;
    uint32_t active_epoch = 0;
    portENTER_CRITICAL(&wifi_mux);
    scan_done = scan_event_pending;
    done_epoch = scan_event_epoch;
    active_epoch = scan_epoch;
    scan_event_pending = false;
    portEXIT_CRITICAL(&wifi_mux);
    if (scan_done) {
        if (scan_draining) {
            scan_draining = false;
            esp_wifi_clear_ap_list();
        } else if (scan_active && wifi_scan_epoch_current(done_epoch, active_epoch)) {
            publish_scan_results();
        } else {
            esp_wifi_clear_ap_list();
        }
    }

    bool ip_pending = false;
    bool disc_pending = false;
    uint32_t ip_epoch = 0;
    uint32_t disc_epoch = 0;
    char joined[33] = {};
    uint8_t joined_len = 0;
    portENTER_CRITICAL(&wifi_mux);
    ip_pending = ip_event_pending;
    disc_pending = disc_event_pending;
    ip_epoch = ip_event_epoch;
    disc_epoch = disc_event_epoch;
    joined_len = ip_event_len;
    if (joined_len) std::memcpy(joined, ip_event_ssid, joined_len);
    ip_event_pending = false;
    disc_event_pending = false;
    portEXIT_CRITICAL(&wifi_mux);

    if (wifi_attempt.active && wifi_attempt_wait_disc) {
        if (disc_pending || wifi_deadline_elapsed(now, wifi_attempt_deadline)) {
            wifi_attempt_wait_disc = false;
            char secret[64] = {};
            if (wifi_attempt.password_len) std::memcpy(secret, wifi_attempt.password, wifi_attempt.password_len);
            const bool started = apply_sta_config(wifi_attempt.ssid, secret);
            std::memset(secret, 0, sizeof(secret));
            wifi_attempt_deadline = now + WIFI_JOIN_TIMEOUT_MS;
            if (!started) fail_attempt(wifi_attempt.manual, profile_index_of_attempt());
        }
    } else if (wifi_attempt.active) {
        const wifi_attempt_result_t ip_result = ip_pending
            ? wifi_attempt_on_ip(&wifi_attempt, ip_epoch, joined, joined_len, &wifi_profiles)
            : WIFI_ATTEMPT_IGNORED;
        if (ip_result == WIFI_ATTEMPT_SAVE || ip_result == WIFI_ATTEMPT_NO_WRITE) {
            complete_attempt(ip_result == WIFI_ATTEMPT_SAVE, now);
        } else {
            wifi_attempt_result_t failed = disc_pending ? wifi_attempt_on_disconnect(&wifi_attempt, disc_epoch)
                                                       : WIFI_ATTEMPT_IGNORED;
            if (failed != WIFI_ATTEMPT_FAIL) failed = wifi_attempt_on_timeout(&wifi_attempt, now, wifi_attempt_deadline);
            if (failed == WIFI_ATTEMPT_FAIL) {
                const bool manual = wifi_attempt.manual;
                const int index = profile_index_of_attempt();
                ESP_LOGW(TAG, "WiFi join failed reason=%d", disc_event_reason);
                fail_attempt(manual, index);
            }
        }
    }

    wifi_request_queue_t request = {};
    bool start_scan = false;
    portENTER_CRITICAL(&wifi_mux);
    request = wifi_requests;
    if (request.connect_pending) {
        wifi_requests.connect_pending = false;
        wifi_requests.saved_connect = false;
        std::memset(wifi_requests.password, 0, sizeof(wifi_requests.password));
        wifi_requests.password_len = 0;
    }
    if (wifi_requests.scan_pending && !scan_active && !scan_draining &&
        !wifi_attempt.active && !request.connect_pending) {
        wifi_requests.scan_pending = false;
        start_scan = true;
    }
    portEXIT_CRITICAL(&wifi_mux);

    if (request.connect_pending) {
        char secret[64] = {};
        size_t secret_len = request.password_len;
        bool ready = true;
        if (request.saved_connect) {
            const int index = wifi_profile_find(&wifi_profiles, request.ssid, request.ssid_len);
            ready = index >= 0;
            if (ready) {
                secret_len = wifi_profiles.items[index].password_len;
                if (secret_len) std::memcpy(secret, wifi_profiles.items[index].password, secret_len);
            }
        } else if (secret_len) {
            std::memcpy(secret, request.password, secret_len);
        }
        if (!ready || !issue_sta_connect(request.ssid, request.ssid_len, secret, secret_len, true)) {
            wifi_last_failed = true;
            publish_join_target(nullptr, 0);
        }
        std::memset(secret, 0, sizeof(secret));
        std::memset(request.password, 0, sizeof(request.password));
    } else if (start_scan) {
        start_background_scan();
    }

    const bool connect_busy = wifi_attempt.active || request.connect_pending;
    if (!connect_busy && !scan_active && !scan_draining && !sta_has_ip &&
        wifi_auto_scan_due(true, false, false, false, wifi_profiles.count > 0, now, wifi_auto_due) &&
        wifi_auto_armed) {
        if (wifi_visible_fresh) maybe_join_visible();
        else if (!start_background_scan()) wifi_auto_due = now + WIFI_AUTO_COOLDOWN_MS;
    } else if (!wifi_attempt.active && !sta_has_ip && wifi_visible_fresh && wifi_auto_armed) {
        maybe_join_visible();
    }
    wifi_driver_unlock();
}

extern "C" bool get_wifi_connected(void)
{
    return sta_has_ip;
}

extern "C" const char *get_wifi_connected_ssid(void)
{
    static char ssid[33];
    portENTER_CRITICAL(&wifi_mux);
    const uint8_t length = sta_has_ip ? connected_len : 0;
    std::memset(ssid, 0, sizeof(ssid));
    if (length && length <= 32) std::memcpy(ssid, connected_ssid, length);
    portEXIT_CRITICAL(&wifi_mux);
    return ssid;
}

extern "C" int do_wifi_scan(void)
{
    portENTER_CRITICAL(&wifi_mux);
    const bool enabled = sta_user_enabled && sta_desired;
    const int cached = wifi_scan_results;
    if (enabled) wifi_request_scan(&wifi_requests, true);
    portEXIT_CRITICAL(&wifi_mux);
    return cached;
}

extern "C" int get_wifi_scan_count(void)
{
    portENTER_CRITICAL(&wifi_mux);
    const int count = wifi_scan_results;
    portEXIT_CRITICAL(&wifi_mux);
    return count;
}

extern "C" const char *get_wifi_ssid(int index)
{
    static char copies[WIFI_SCAN_LIMIT][33];
    if (index < 0 || index >= WIFI_SCAN_LIMIT) return "";
    portENTER_CRITICAL(&wifi_mux);
    if (index >= wifi_scan_results) {
        portEXIT_CRITICAL(&wifi_mux);
        return "";
    }
    std::memset(copies[index], 0, sizeof(copies[index]));
    std::memcpy(copies[index], wifi_ssid_list[index], wifi_ssid_len[index]);
    portEXIT_CRITICAL(&wifi_mux);
    return copies[index];
}

extern "C" bool wifi_network_is_saved(const char *ssid)
{
    if (!ssid) return false;
    const size_t length = std::strlen(ssid);
    if (length == 0 || length > WIFI_PROFILE_SSID_MAX) return false;
    portENTER_CRITICAL(&wifi_mux);
    const bool saved = wifi_profile_find(&wifi_profiles, ssid, length) >= 0;
    portEXIT_CRITICAL(&wifi_mux);
    return saved;
}

extern "C" bool do_wifi_connect(const char *ssid, const char *password)
{
    return wifi_service_start_connect(ssid, password);
}

extern "C" bool do_wifi_connect_saved(const char *ssid)
{
    if (!ssid) return false;
    portENTER_CRITICAL(&wifi_mux);
    const bool enabled = sta_user_enabled && sta_desired;
    const size_t length = std::strlen(ssid);
    const int index = length > 0 && length <= WIFI_PROFILE_SSID_MAX ?
                      wifi_profile_find(&wifi_profiles, ssid, length) : -1;
    const bool known = index >= 0;
    const bool ok = wifi_request_saved(&wifi_requests, ssid, known, enabled);
    if (ok) {
        // Keep this accepted intent independent of later LRU changes.
        const wifi_profile_t *profile = &wifi_profiles.items[index];
        wifi_requests.saved_connect = false;
        wifi_requests.password_len = profile->password_len;
        std::memset(wifi_requests.password, 0, sizeof(wifi_requests.password));
        std::memcpy(wifi_requests.password, profile->password, profile->password_len);
        std::memset(wifi_target, 0, sizeof(wifi_target));
        std::memcpy(wifi_target, ssid, length);
        wifi_last_failed = false;
    }
    portEXIT_CRITICAL(&wifi_mux);
    return ok;
}

extern "C" bool wifi_scan_is_busy(void)
{
    portENTER_CRITICAL(&wifi_mux);
    const bool busy = scan_active || wifi_requests.scan_pending;
    portEXIT_CRITICAL(&wifi_mux);
    return busy;
}

extern "C" uint32_t wifi_scan_revision(void)
{
    portENTER_CRITICAL(&wifi_mux);
    const uint32_t revision = wifi_scan_revision_value;
    portEXIT_CRITICAL(&wifi_mux);
    return revision;
}

extern "C" bool wifi_connection_is_busy(void)
{
    portENTER_CRITICAL(&wifi_mux);
    const bool busy = wifi_attempt.active || wifi_requests.connect_pending;
    portEXIT_CRITICAL(&wifi_mux);
    return busy;
}

extern "C" const char *wifi_connection_target(void)
{
    static char target[33];
    portENTER_CRITICAL(&wifi_mux);
    std::memcpy(target, wifi_target, sizeof(target));
    portEXIT_CRITICAL(&wifi_mux);
    target[sizeof(target) - 1] = '\0';
    return target;
}

extern "C" bool wifi_scan_network_is_open(int index)
{
    portENTER_CRITICAL(&wifi_mux);
    const bool open = index >= 0 && index < wifi_scan_results && wifi_ssid_open[index];
    portEXIT_CRITICAL(&wifi_mux);
    return open;
}

extern "C" bool wifi_last_connection_failed(void)
{
    return wifi_last_failed;
}

extern "C" const char *get_wifi_status(void)
{
    static char text[24];
    if (!sta_has_ip) return "Not connected";
    portENTER_CRITICAL(&wifi_mux);
    const esp_ip4_addr_t ip = sta_ip;
    portEXIT_CRITICAL(&wifi_mux);
    if (!esp_ip4addr_ntoa(&ip, text, sizeof(text))) return "Not connected";
    return text;
}
