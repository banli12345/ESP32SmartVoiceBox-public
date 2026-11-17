#include "WeatherConfig.h"
#include "Storage.h"
#include "SDcar.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace {
constexpr char import_path[] = "/config/weather.json";
constexpr uint32_t magic = 0x57454331;
struct StoredConfig { uint32_t tag; weather_api_config_t config; };
static EXT_RAM_BSS_ATTR weather_api_config_t active;
static portMUX_TYPE config_mux = portMUX_INITIALIZER_UNLOCKED;
static bool ready, imported;
static const char *status_text = "请导入天气配置";

bool graphic(const char *s, size_t capacity)
{
    const size_t n = strnlen(s, capacity);
    if (!n || n == capacity || strstr(s, "YOUR_")) return false;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(s[i]) < 33 || static_cast<unsigned char>(s[i]) > 126) return false;
    return true;
}
bool endpoint(const char *s)
{
    if (!graphic(s, 192) || strpbrk(s, "@?#\\")) return false;
    const char *host = !strncmp(s, "https://", 8) ? s + 8 :
                       !strncmp(s, "http://", 7) ? s + 7 : nullptr;
    if (!host || !*host) return false;
    const char *end = strchr(host, '/');
    if (!end) end = host + strlen(host);
    const char *colon = static_cast<const char *>(memchr(host, ':', end - host));
    const char *host_end = colon ? colon : end;
    if (host == host_end) return false;
    bool alnum = false;
    for (const char *p = host; p < host_end; ++p) {
        const bool letter = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9');
        if (!letter && *p != '-' && *p != '.') return false;
        alnum |= letter;
    }
    if (!alnum) return false;
    if (colon) {
        if (colon + 1 == end) return false;
        unsigned port = 0;
        for (const char *p = colon + 1; p < end; ++p) {
            if (*p < '0' || *p > '9') return false;
            port = port * 10 + static_cast<unsigned>(*p - '0');
            if (port > 65535) return false;
        }
        if (!port) return false;
    }
    return true;
}
bool valid(const weather_api_config_t &c)
{
    return graphic(c.api_key, sizeof c.api_key) && graphic(c.life_api_key, sizeof c.life_api_key) &&
           endpoint(c.now_url) && endpoint(c.daily_url) && endpoint(c.life_url);
}
bool field(cJSON *root, const char *name, char *out, size_t capacity, const char *fallback = nullptr)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    const char *value = !item ? fallback : cJSON_IsString(item) ? item->valuestring : nullptr;
    if (!value || strlen(value) >= capacity) return false;
    memcpy(out, value, strlen(value) + 1);
    return true;
}
bool bounded_json(const char *s, size_t length)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = s[i];
        if (!c) return false;
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < length && !memcmp(s + i + 1, "0000", 4)) return false;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}
bool unique_fields(cJSON *root)
{
    const char *names[] = {"version", "api_key", "life_api_key", "now_url", "daily_url", "life_url"};
    for (const char *name : names) {
        unsigned count = 0;
        for (cJSON *item = root->child; item; item = item->next)
            if (item->string && !strcmp(item->string, name) && ++count > 1) return false;
    }
    return true;
}
void activate(const weather_api_config_t &config)
{
    portENTER_CRITICAL(&config_mux);
    active = config;
    ready = true;
    status_text = "天气接口已配置";
    portEXIT_CRITICAL(&config_mux);
}
void import_sd()
{
    if (!sdcar_is_ready() || !storage_is_file(import_path)) return;
    StorageStream file;
    if (!file.open(import_path, "rb") || file.byte_count() < 8 || file.byte_count() > 2048) {
        status_text = "天气配置文件无效，原配置保留";
        return;
    }
    char text[2049];
    const size_t length = static_cast<size_t>(file.byte_count());
    if (file.read(text, length) != length) {
        status_text = "天气配置读取失败，原配置保留";
        memset(text, 0, sizeof text);
        return;
    }
    file.close();
    text[length] = 0;
    const size_t bom = length >= 3 && !memcmp(text, "\xef\xbb\xbf", 3) ? 3 : 0;
    cJSON *root = bounded_json(text + bom, length - bom) ?
                  cJSON_ParseWithLengthOpts(text + bom, length - bom + 1, nullptr, true) : nullptr;
    weather_api_config_t next{};
    cJSON *version = root ? cJSON_GetObjectItemCaseSensitive(root, "version") : nullptr;
    const bool ok = cJSON_IsObject(root) && unique_fields(root) && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        field(root, "api_key", next.api_key, sizeof next.api_key) &&
        field(root, "life_api_key", next.life_api_key, sizeof next.life_api_key, next.api_key) &&
        field(root, "now_url", next.now_url, sizeof next.now_url, WEATHER_DEFAULT_NOW_URL) &&
        field(root, "daily_url", next.daily_url, sizeof next.daily_url, WEATHER_DEFAULT_DAILY_URL) &&
        field(root, "life_url", next.life_url, sizeof next.life_url, WEATHER_DEFAULT_LIFE_URL) && valid(next);
    cJSON_Delete(root);
    memset(text, 0, sizeof text);
    if (!ok) {
        memset(&next, 0, sizeof next);
        status_text = "天气配置内容无效，原配置保留";
        puts("[WEATHER CONFIG] rejected import; previous configuration retained");
        return;
    }
    if (weather_config_store(&next)) {
        activate(next);
        imported = true;
        if (!storage_remove(import_path)) puts("[WEATHER CONFIG] saved; import file cleanup failed");
        else puts("[WEATHER CONFIG] imported into NVS");
    } else {
        status_text = "天气配置保存失败，原配置保留";
        puts("[WEATHER CONFIG] save failed; previous configuration retained");
    }
    memset(&next, 0, sizeof next);
}
}
extern "C" void weather_config_init(void)
{
    portENTER_CRITICAL(&config_mux);
    ready = imported = false;
    active = {};
    status_text = "请导入天气配置";
    portEXIT_CRITICAL(&config_mux);
    StoredConfig stored{};
    size_t length = sizeof stored;
    nvs_handle_t handle = 0;
    if (nvs_open("weather_api", NVS_READONLY, &handle) == ESP_OK) {
        const esp_err_t error = nvs_get_blob(handle, "config", &stored, &length);
        nvs_close(handle);
        if (error == ESP_OK && length == sizeof stored && stored.tag == magic && valid(stored.config)) activate(stored.config);
    }
    memset(&stored, 0, sizeof stored);
    import_sd();
}
extern "C" bool weather_config_get(weather_api_config_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&config_mux);
    if (ready) *out = active;
    else memset(out, 0, sizeof(*out));
    const bool available = ready;
    portEXIT_CRITICAL(&config_mux);
    return available;
}
extern "C" bool weather_config_is_ready(void)
{
    portENTER_CRITICAL(&config_mux);
    const bool available = ready;
    portEXIT_CRITICAL(&config_mux);
    return available;
}
extern "C" bool weather_config_imported(void) { return imported; }
extern "C" const char *weather_config_status(void) { return status_text; }
extern "C" bool weather_config_validate(const weather_api_config_t *config) { return config && valid(*config); }
extern "C" bool weather_config_store(const weather_api_config_t *config)
{
    if (!weather_config_validate(config)) return false;
    StoredConfig stored{};
    stored.tag = magic;
    stored.config = *config;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("weather_api", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "config", &stored, sizeof stored);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    memset(&stored, 0, sizeof stored);
    return error == ESP_OK;
}
extern "C" bool weather_config_read_saved(weather_api_config_t *config)
{
    if (!config) return false;
    memset(config, 0, sizeof(*config));
    StoredConfig stored{};
    size_t length = sizeof stored;
    nvs_handle_t handle = 0;
    bool ok = false;
    if (nvs_open("weather_api", NVS_READONLY, &handle) == ESP_OK) {
        const esp_err_t error = nvs_get_blob(handle, "config", &stored, &length);
        nvs_close(handle);
        ok = error == ESP_OK && length == sizeof stored && stored.tag == magic && valid(stored.config);
        if (ok) *config = stored.config;
    }
    memset(&stored, 0, sizeof stored);
    return ok;
}
