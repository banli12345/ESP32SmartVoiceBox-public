#include "AiDirectClient.h"
#include "IdfRuntime.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr size_t RESPONSE_LIMIT = 24576;
bool endpoint(const ai_api_config_t &config, char *url, size_t capacity)
{
    size_t n = strnlen(config.api_base, sizeof(config.api_base));
    if (n < 9 || n == sizeof(config.api_base) || strncmp(config.api_base, "https://", 8) ||
        !strcspn(config.api_base + 8, "/:")) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = config.api_base[i];
        if (c <= 32 || c >= 127 || strchr("@?#\\", c)) return false;
    }
    const size_t key = strnlen(config.api_key, sizeof(config.api_key));
    const size_t model = strnlen(config.model, sizeof(config.model));
    if (key < 8 || key == sizeof(config.api_key) || !model || model == sizeof(config.model)) return false;
    for (size_t i = 0; i < key; ++i) if ((unsigned char)config.api_key[i] <= 32 || (unsigned char)config.api_key[i] >= 127) return false;
    for (size_t i = 0; i < model; ++i) if ((unsigned char)config.model[i] <= 32 || (unsigned char)config.model[i] >= 127) return false;
    while (n && config.api_base[n - 1] == '/') --n;
    return snprintf(url, capacity, "%.*s/chat/completions", (int)n, config.api_base) < (int)capacity;
}
bool is_deepseek(const char *base)
{
    const char *origin = "https://api.deepseek.com";
    const size_t n = strlen(origin);
    return !strncmp(base, origin, n) && (!base[n] || base[n] == '/');
}
const char *http_error(int status)
{
    switch (status) {
        case 400: return "DeepSeek 请求格式错误 (HTTP 400)";
        case 401: return "DeepSeek API Key 无效 (HTTP 401)";
        case 402: return "DeepSeek 余额不足 (HTTP 402)";
        case 403: return "DeepSeek 拒绝访问 (HTTP 403)";
        case 404: return "AI 接口或模型不存在 (HTTP 404)";
        case 429: return "DeepSeek 请求过于频繁，请稍后重试 (HTTP 429)";
        default: return status >= 500 ? "DeepSeek 服务暂时异常，请重试 (HTTP 5xx)" : "AI 请求失败，请检查接口配置";
    }
}
/* Copy whole UTF-8 code points, including when the UI buffer is smaller. */
bool copy_utf8(char *out, size_t capacity, const char *text)
{
    const auto *p = reinterpret_cast<const unsigned char *>(text);
    size_t used = 0;
    while (*p) {
        size_t n = *p < 0x80 ? 1 : (*p >= 0xc2 && *p <= 0xdf ? 2 :
            (*p >= 0xe0 && *p <= 0xef ? 3 : (*p >= 0xf0 && *p <= 0xf4 ? 4 : 0)));
        if (!n) return false;
        for (size_t i = 1; i < n; ++i) if (!p[i] || (p[i] & 0xc0) != 0x80) return false;
        if ((n == 3 && ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] >= 0xa0))) ||
            (n == 4 && ((*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90)))) return false;
        if (used + n >= capacity) break;
        memcpy(out + used, p, n); used += n; p += n;
    }
    out[used] = 0;
    return used != 0;
}
}

extern "C" bool ai_direct_chat_context(const ai_api_config_t *config, const char *prompt,
    const char *previous_question, const char *previous_answer,
    char *reply, size_t reply_capacity, char *error, size_t error_capacity)
{
    if (reply && reply_capacity) reply[0] = 0;
    if (error && error_capacity) error[0] = 0;
    auto fail = [&](const char *message) { if (error && error_capacity) strlcpy(error, message, error_capacity); };
    char url[224], authorization[264];
    if (!config || !prompt || !*prompt || !reply || reply_capacity < 2 || !endpoint(*config, url, sizeof(url))) {
        fail("AI 直连配置无效"); return false;
    }
    cJSON *request = cJSON_CreateObject();
    cJSON *messages = request ? cJSON_AddArrayToObject(request, "messages") : nullptr;
    cJSON *system = messages ? cJSON_CreateObject() : nullptr;
    cJSON *user = messages ? cJSON_CreateObject() : nullptr;
    const bool history = previous_question && *previous_question && previous_answer && *previous_answer &&
        strlen(previous_question) <= 512 && strlen(previous_answer) <= 8192;
    cJSON *past_user = history && messages ? cJSON_CreateObject() : nullptr;
    cJSON *past_answer = history && messages ? cJSON_CreateObject() : nullptr;
    bool built = request && messages && system && user;
    const bool longer = strstr(prompt, "故事") || strstr(prompt, "详细") || strstr(prompt, "展开") ||
        strstr(prompt, "继续") || strstr(prompt, "story") || strstr(prompt, "continue");
    if (built) {
        built = cJSON_AddStringToObject(request, "model", config->model) &&
            cJSON_AddNumberToObject(request, "max_tokens", longer ? 2048 : 1024) && cJSON_AddBoolToObject(request, "stream", false) &&
            cJSON_AddStringToObject(system, "role", "system") &&
            cJSON_AddStringToObject(system, "content", "你是Breeze OS随身设备的中文助手。普通问题清楚简明；用户要求故事或详细解释时，给出完整内容，不要限制为三句话。故事约600到900字，有开头、经过和结尾；继续时承接上一段，不重复。闹钟设置由设备本地执行，没有执行结果时不要声称已经设置成功。") &&
            cJSON_AddStringToObject(user, "role", "user") && cJSON_AddStringToObject(user, "content", prompt);
        if (is_deepseek(config->api_base)) {
            cJSON *thinking = cJSON_AddObjectToObject(request, "thinking");
            built = built && thinking && cJSON_AddStringToObject(thinking, "type", "disabled");
        }
    }
    if (system) cJSON_AddItemToArray(messages, system);
    if (history) {
        built = built && past_user && past_answer &&
            cJSON_AddStringToObject(past_user, "role", "user") &&
            cJSON_AddStringToObject(past_user, "content", previous_question) &&
            cJSON_AddStringToObject(past_answer, "role", "assistant") &&
            cJSON_AddStringToObject(past_answer, "content", previous_answer);
        if (past_user) cJSON_AddItemToArray(messages, past_user);
        if (past_answer) cJSON_AddItemToArray(messages, past_answer);
    }
    if (user) cJSON_AddItemToArray(messages, user);
    char *body = built ? cJSON_PrintUnformatted(request) : nullptr;
    cJSON_Delete(request);
    char *response = static_cast<char *>(heap_caps_malloc(RESPONSE_LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body || !response) { cJSON_free(body); free(response); fail("AI 请求内存不足"); return false; }
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->api_key);
    esp_http_client_config_t cfg = {};
    cfg.url = url; cfg.method = HTTP_METHOD_POST; cfg.addr_type = HTTP_ADDR_TYPE_INET;
    cfg.timeout_ms = 12000; cfg.buffer_size = cfg.buffer_size_tx = 1024;
    cfg.disable_auto_redirect = true; cfg.crt_bundle_attach = esp_crt_bundle_attach;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    bool ok = false;
    const uint32_t began = app_millis();
    if (client) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "Authorization", authorization);
        bool connected = esp_http_client_open(client, strlen(body)) == ESP_OK;
        for (size_t sent = 0; connected && sent < strlen(body);) {
            const int written = esp_http_client_write(client, body + sent, strlen(body) - sent);
            if (written <= 0 || app_millis() - began >= 120000) connected = false;
            else sent += written;
        }
        size_t used = 0;
        if (connected) {
            esp_http_client_set_timeout_ms(client, 60000);
            const int64_t length = esp_http_client_fetch_headers(client);
            const int status = esp_http_client_get_status_code(client);
            printf("[AI DIRECT] chat HTTP=%d\n", status);
            connected = length >= 0 && status == 200;
            if (status != 200 && status > 0) fail(http_error(status));
            if (length > (int64_t)RESPONSE_LIMIT) { connected = false; fail("AI 回复超过接收上限"); }
        }
        uint32_t progress = app_millis();
        esp_http_client_set_timeout_ms(client, 1000);
        while (connected && app_millis() - began < 120000 && app_millis() - progress < 60000) {
            if (used == RESPONSE_LIMIT) { fail("AI 回复超过接收上限"); break; }
            int got = esp_http_client_read(client, response + used, RESPONSE_LIMIT - used);
            if (got == -ESP_ERR_HTTP_EAGAIN) { app_delay_ms(10); continue; }
            if (got < 0) break;
            if (got > 0) { used += got; progress = app_millis(); }
            if (esp_http_client_is_complete_data_received(client)) { ok = true; break; }
            if (!got) break;
        }
        esp_http_client_close(client); esp_http_client_cleanup(client);
        if (ok) {
            response[used] = 0;
            cJSON *root = cJSON_ParseWithLengthOpts(response, used + 1, nullptr, true);
            cJSON *choices = root ? cJSON_GetObjectItemCaseSensitive(root, "choices") : nullptr;
            cJSON *first = cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : nullptr;
            cJSON *message = first ? cJSON_GetObjectItemCaseSensitive(first, "message") : nullptr;
            cJSON *content = message ? cJSON_GetObjectItemCaseSensitive(message, "content") : nullptr;
            cJSON *finish = first ? cJSON_GetObjectItemCaseSensitive(first, "finish_reason") : nullptr;
            const char *hint = "\n\n输入“继续”可以接着讲。";
            const bool truncated = cJSON_IsString(content) && content->valuestring &&
                (strlen(content->valuestring) >= reply_capacity ||
                 (cJSON_IsString(finish) && !strcmp(finish->valuestring, "length")));
            const size_t reserve = truncated && reply_capacity > strlen(hint) + 16 ? strlen(hint) : 0;
            ok = cJSON_IsString(content) && content->valuestring && copy_utf8(reply, reply_capacity - reserve, content->valuestring);
            if (ok && reserve) strlcpy(reply + strlen(reply), hint, reply_capacity - strlen(reply));
            if (!ok) fail("AI 返回内容无效或为空，请重试");
            cJSON_Delete(root);
        }
    }
    memset(authorization, 0, sizeof(authorization));
    cJSON_free(body); free(response);
    if (!ok) { reply[0] = 0; if (!error || !error_capacity || !*error) fail("AI 网络或 TLS 连接失败、超时，请检查 WiFi"); }
    return ok;
}

extern "C" bool ai_direct_chat(const ai_api_config_t *config, const char *prompt,
    char *reply, size_t reply_capacity, char *error, size_t error_capacity)
{
    return ai_direct_chat_context(config, prompt, nullptr, nullptr, reply, reply_capacity, error, error_capacity);
}
