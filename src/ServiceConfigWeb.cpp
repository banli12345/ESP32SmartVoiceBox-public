#include "ServiceConfigWeb.h"
#include "AiConfig.h"
#include "WeatherConfig.h"
#include "SystemUpdate.h"
#include "setwifi.h"
#include "IdfRuntime.h"
#include "service_config_page.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_system.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
char token[33];
std::atomic<bool> reboot_pending{false};
std::atomic<uint32_t> reboot_at{0};

esp_err_t reply(httpd_req_t *req, cJSON *root, const char *status = "200 OK")
{
    char *body = root ? cJSON_PrintUnformatted(root) : nullptr;
    cJSON_Delete(root);
    if (!body) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    const esp_err_t result = httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    cJSON_free(body);
    return result;
}
esp_err_t result(httpd_req_t *req, bool ok, const char *message, const char *status = nullptr)
{
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddBoolToObject(root, "ok", ok);
        if (!ok) cJSON_AddStringToObject(root, "error", message);
    }
    return reply(req, root, status ? status : ok ? "200 OK" : "400 Bad Request");
}
bool local_request(httpd_req_t *req)
{
    if (!wifi_hotspot_get_enabled()) return false;
    char ip[16]{}, host[48]{}, with_port[24]{}, origin[80]{};
    wifi_hotspot_copy_info(nullptr, 0, nullptr, 0, ip, sizeof ip, nullptr, nullptr);
    if (!ip[0] || httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) != ESP_OK) return false;
    snprintf(with_port, sizeof with_port, "%s:80", ip);
    if (strcmp(host, ip) && strcmp(host, with_port)) return false;
    if (httpd_req_get_hdr_value_len(req, "Origin")) {
        if (httpd_req_get_hdr_value_str(req, "Origin", origin, sizeof origin) != ESP_OK) return false;
        char expected[48], port_origin[48];
        snprintf(expected, sizeof expected, "http://%s", ip);
        snprintf(port_origin, sizeof port_origin, "http://%s:80", ip);
        if (strcmp(origin, expected) && strcmp(origin, port_origin)) return false;
    }
    return true;
}
bool authorized(httpd_req_t *req)
{
    if (!local_request(req)) return false;
    char received[33]{};
    if (!token[0] || httpd_req_get_hdr_value_len(req, "X-Config-Token") != 32 ||
        httpd_req_get_hdr_value_str(req, "X-Config-Token", received, sizeof received) != ESP_OK) return false;
    unsigned difference = 0;
    for (unsigned i = 0; i < 32; ++i) difference |= static_cast<unsigned char>(received[i] ^ token[i]);
    return difference == 0;
}
bool bounded_json(const char *body, size_t n)
{
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < n; ++i) {
        const char c = body[i];
        if (!c) return false;
        if (quoted) {
            if (escaped) {
                if (c == 'u' && i + 4 < n && !memcmp(body + i + 1, "0000", 4)) return false;
                escaped = false;
            } else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 6) return false; }
        else if (c == '}' || c == ']') { if (!depth) return false; --depth; }
    }
    return !quoted && depth == 0;
}
void discard(cJSON *root)
{
    for (const char *field : {"api_key", "life_api_key"}) {
        cJSON *value = root ? cJSON_GetObjectItemCaseSensitive(root, field) : nullptr;
        if (cJSON_IsString(value) && value->valuestring) memset(value->valuestring, 0, strlen(value->valuestring));
    }
    cJSON_Delete(root);
}
cJSON *read_body(httpd_req_t *req)
{
    char type[64]{};
    if (!req->content_len || req->content_len > 3072 ||
        httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof type) != ESP_OK ||
        (strcmp(type, "application/json") && strcmp(type, "application/json; charset=utf-8"))) return nullptr;
    char *body = static_cast<char *>(heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) return nullptr;
    size_t used = 0;
    while (used < req->content_len) {
        const int n = httpd_req_recv(req, body + used, req->content_len - used);
        if (n <= 0 || static_cast<size_t>(n) > req->content_len - used) break;
        used += n;
    }
    body[used] = 0;
    cJSON *root = used == req->content_len && bounded_json(body, used) ?
        cJSON_ParseWithLengthOpts(body, used + 1, nullptr, true) : nullptr;
    memset(body, 0, req->content_len + 1);
    free(body);
    bool valid = cJSON_IsObject(root);
    if (valid) for (cJSON *a = root->child; a; a = a->next)
        for (cJSON *b = a->next; b; b = b->next)
            if (a->string && b->string && !strcmp(a->string, b->string)) valid = false;
    if (!valid) { discard(root); return nullptr; }
    return root;
}
bool update_string(cJSON *root, const char *name, char *out, size_t capacity, bool preserve_blank = false)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!value) return true;
    if (!cJSON_IsString(value) || !value->valuestring || strlen(value->valuestring) >= capacity) return false;
    if (!preserve_blank || value->valuestring[0]) memcpy(out, value->valuestring, strlen(value->valuestring) + 1);
    return true;
}
bool update_bool(cJSON *root, const char *name, bool *out)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!value) return true;
    if (!cJSON_IsBool(value)) return false;
    *out = cJSON_IsTrue(value);
    return true;
}
esp_err_t page(httpd_req_t *req)
{
    if (!local_request(req)) return result(req, false, "请通过设备热点地址打开", "403 Forbidden");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "no-referrer");
    return httpd_resp_send(req, SERVICE_CONFIG_PAGE, sizeof(SERVICE_CONFIG_PAGE) - 1);
}
esp_err_t status(httpd_req_t *req)
{
    if (!local_request(req)) return result(req, false, "请通过设备热点地址打开", "403 Forbidden");
    cJSON *root = cJSON_CreateObject();
    if (!root) return reply(req, nullptr);
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "token", token);
    {
        ai_api_config_t saved{}, active{};
        const bool configured = ai_config_read_saved(&saved);
        const bool running = ai_config_get(&active);
        cJSON *ai = cJSON_AddObjectToObject(root, "ai");
        cJSON_AddBoolToObject(ai, "configured", configured);
        cJSON_AddBoolToObject(ai, "has_key", configured);
        cJSON_AddBoolToObject(ai, "pending", configured && (!running || memcmp(&saved, &active, sizeof saved)));
        cJSON_AddStringToObject(ai, "api_base", configured ? saved.api_base : "https://api.deepseek.com");
        cJSON_AddStringToObject(ai, "model", configured ? saved.model : "deepseek-flash");
        cJSON_AddBoolToObject(ai, "speak", configured ? saved.speak : true);
        memset(&saved, 0, sizeof saved); memset(&active, 0, sizeof active);
    }
    {
        weather_api_config_t saved{}, active{};
        const bool configured = weather_config_read_saved(&saved);
        const bool running = weather_config_get(&active);
        cJSON *weather = cJSON_AddObjectToObject(root, "weather");
        cJSON_AddBoolToObject(weather, "configured", configured);
        cJSON_AddBoolToObject(weather, "has_key", configured);
        cJSON_AddBoolToObject(weather, "life_has_key", configured);
        cJSON_AddBoolToObject(weather, "pending", configured && (!running || memcmp(&saved, &active, sizeof saved)));
        cJSON_AddBoolToObject(weather, "life_use_weather_key", !configured || !strcmp(saved.api_key, saved.life_api_key));
        cJSON_AddStringToObject(weather, "now_url", configured ? saved.now_url : WEATHER_DEFAULT_NOW_URL);
        cJSON_AddStringToObject(weather, "daily_url", configured ? saved.daily_url : WEATHER_DEFAULT_DAILY_URL);
        cJSON_AddStringToObject(weather, "life_url", configured ? saved.life_url : WEATHER_DEFAULT_LIFE_URL);
        memset(&saved, 0, sizeof saved); memset(&active, 0, sizeof active);
    }
    return reply(req, root);
}
esp_err_t save_ai(httpd_req_t *req)
{
    if (!authorized(req)) return result(req, false, "页面已失效或来源无效，请刷新后重试", "403 Forbidden");
    if (reboot_pending.load()) return result(req, false, "设备即将重启，请稍后再保存");
    if (system_update_is_busy()) return result(req, false, "系统更新中，请稍后再保存");
    cJSON *root = read_body(req);
    if (!root) return result(req, false, "配置格式或长度无效");
    ai_api_config_t next{};
    const bool saved = ai_config_read_saved(&next);
    if (!saved && ai_config_is_ready()) { discard(root); return result(req, false, "暂时无法读取配置，请重试"); }
    if (!saved) { strcpy(next.api_base, "https://api.deepseek.com"); strcpy(next.model, "deepseek-flash"); next.speak = true; }
    bool ok = update_string(root, "api_base", next.api_base, sizeof next.api_base) &&
        update_string(root, "model", next.model, sizeof next.model) &&
        update_string(root, "api_key", next.api_key, sizeof next.api_key, true) &&
        update_bool(root, "speak", &next.speak) && ai_config_validate(&next);
    discard(root);
    const char *error = "AI地址、密钥或模型无效；首次配置需要填写密钥";
    if (ok) { ok = ai_config_store(&next); error = "配置保存失败，原配置保留，请重试"; }
    memset(&next, 0, sizeof next);
    return result(req, ok, error);
}
esp_err_t save_weather(httpd_req_t *req)
{
    if (!authorized(req)) return result(req, false, "页面已失效或来源无效，请刷新后重试", "403 Forbidden");
    if (reboot_pending.load()) return result(req, false, "设备即将重启，请稍后再保存");
    if (system_update_is_busy()) return result(req, false, "系统更新中，请稍后再保存");
    cJSON *root = read_body(req);
    if (!root) return result(req, false, "配置格式或长度无效");
    weather_api_config_t next{};
    const bool saved = weather_config_read_saved(&next);
    if (!saved && weather_config_is_ready()) { discard(root); return result(req, false, "暂时无法读取配置，请重试"); }
    if (!saved) { strcpy(next.now_url, WEATHER_DEFAULT_NOW_URL); strcpy(next.daily_url, WEATHER_DEFAULT_DAILY_URL); strcpy(next.life_url, WEATHER_DEFAULT_LIFE_URL); }
    bool inherit = !saved || !strcmp(next.api_key, next.life_api_key);
    bool ok = update_string(root, "now_url", next.now_url, sizeof next.now_url) &&
        update_string(root, "daily_url", next.daily_url, sizeof next.daily_url) &&
        update_string(root, "life_url", next.life_url, sizeof next.life_url) &&
        update_string(root, "api_key", next.api_key, sizeof next.api_key, true) &&
        update_string(root, "life_api_key", next.life_api_key, sizeof next.life_api_key, true) &&
        update_bool(root, "life_use_weather_key", &inherit);
    if (inherit) memcpy(next.life_api_key, next.api_key, sizeof next.api_key);
    ok = ok && weather_config_validate(&next);
    discard(root);
    const char *error = "天气地址或密钥无效；首次配置需要填写密钥";
    if (ok) { ok = weather_config_store(&next); error = "配置保存失败，原配置保留，请重试"; }
    memset(&next, 0, sizeof next);
    return result(req, ok, error);
}
esp_err_t reboot(httpd_req_t *req)
{
    if (!authorized(req)) return result(req, false, "页面已失效或来源无效，请刷新后重试", "403 Forbidden");
    if (system_update_is_busy()) return result(req, false, "系统更新中，请稍后再重启");
    cJSON *root = read_body(req);
    if (!root) return result(req, false, "请求无效");
    discard(root);
    const esp_err_t sent = result(req, true, nullptr);
    if (sent == ESP_OK) {
        reboot_at.store(app_millis() + 1500, std::memory_order_relaxed);
        reboot_pending.store(true, std::memory_order_release);
    }
    return sent;
}
}

bool service_config_web_register(httpd_handle_t server)
{
    uint8_t random[16]; esp_fill_random(random, sizeof random);
    for (unsigned i = 0; i < sizeof random; ++i) snprintf(token + i * 2, 3, "%02x", random[i]);
    const httpd_uri_t routes[] = {
        {.uri="/settings", .method=HTTP_GET, .handler=page, .user_ctx=nullptr},
        {.uri="/api/config/status", .method=HTTP_GET, .handler=status, .user_ctx=nullptr},
        {.uri="/api/config/ai", .method=HTTP_POST, .handler=save_ai, .user_ctx=nullptr},
        {.uri="/api/config/weather", .method=HTTP_POST, .handler=save_weather, .user_ctx=nullptr},
        {.uri="/api/config/reboot", .method=HTTP_POST, .handler=reboot, .user_ctx=nullptr}
    };
    for (const auto &route : routes) if (httpd_register_uri_handler(server, &route) != ESP_OK) return false;
    return true;
}
void service_config_web_poll(void)
{
    if (reboot_pending.load(std::memory_order_acquire) &&
        static_cast<int32_t>(app_millis() - reboot_at.load(std::memory_order_relaxed)) >= 0 && !system_update_is_busy()) {
        reboot_pending.store(false, std::memory_order_release);
        esp_restart();
    }
}
