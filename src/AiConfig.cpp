#include "AiConfig.h"
#include "Storage.h"
#include "SDcar.h"
#include "cJSON.h"
#include "nvs.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include <cstdio>
#include <cstring>
#include <cctype>

namespace {
constexpr uint32_t CONFIG_MAGIC = 0x41494331;
constexpr const char *IMPORT_PATH = "/config/ai.json";
struct StoredConfig { uint32_t magic; ai_api_config_t config; };
static portMUX_TYPE config_mux = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR ai_api_config_t active_config;
static bool ready;
static const char *status_text = "请导入 DeepSeek API 配置";

bool valid(const ai_api_config_t &config)
{
    const size_t base_len = strnlen(config.api_base, sizeof(config.api_base));
    const size_t key_len = strnlen(config.api_key, sizeof(config.api_key));
    const size_t model_len = strnlen(config.model, sizeof(config.model));
    if (base_len < 9 || base_len == sizeof(config.api_base) ||
        strncmp(config.api_base, "https://", 8) ||
        key_len < 8 || key_len == sizeof(config.api_key) ||
        !model_len || model_len == sizeof(config.model)) return false;
    if (strstr(config.api_key, "YOUR_") || strstr(config.api_base, "YOUR_") || strstr(config.model, "YOUR_")) return false;
    if (!strcspn(config.api_base + 8, "/:")) return false;
    for (size_t i = 0; i < base_len; ++i) {
        const unsigned char ch = config.api_base[i];
        if (ch <= 32 || ch >= 127 || strchr("@?#\\", ch)) return false;
    }
    for (size_t i = 0; i < key_len; ++i) if (config.api_key[i] <= 32 || config.api_key[i] >= 127) return false;
    for (size_t i = 0; i < model_len; ++i) {
        const unsigned char ch = config.model[i];
        if (!(isalnum(ch) || strchr("-_./:", ch))) return false;
    }
    return true;
}
bool field(cJSON *root, const char *name, char *out, size_t capacity)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!cJSON_IsString(item) || !item->valuestring || strlen(item->valuestring) >= capacity) return false;
    strlcpy(out, item->valuestring, capacity);
    return true;
}
void activate(const ai_api_config_t &config)
{
    portENTER_CRITICAL(&config_mux);
    active_config = config;
    ready = true;
    status_text = "DeepSeek 直连已配置";
    portEXIT_CRITICAL(&config_mux);
}
void import_sd()
{
    if (!sdcar_is_ready() || !storage_is_file(IMPORT_PATH)) return;
    StorageStream file;
    if (!file.open(IMPORT_PATH, "rb") || file.byte_count() < 8 || file.byte_count() > 2048) {
        status_text = "AI 配置文件无效，原配置保留"; return;
    }
    char text[2049];
    const size_t length = static_cast<size_t>(file.byte_count());
    if (file.read(text, length) != length) { status_text = "AI 配置文件读取失败"; return; }
    file.close();
    text[length] = 0;
    cJSON *root = cJSON_ParseWithLengthOpts(text, length + 1, nullptr, true);
    ai_api_config_t next = {};
    cJSON *version = root ? cJSON_GetObjectItem(root, "version") : nullptr;
    bool ok = root && cJSON_IsNumber(version) && version->valuedouble == 1 &&
        field(root, "api_base", next.api_base, sizeof(next.api_base)) &&
        field(root, "api_key", next.api_key, sizeof(next.api_key)) &&
        field(root, "model", next.model, sizeof(next.model));
    cJSON *speak = root ? cJSON_GetObjectItem(root, "speak") : nullptr;
    if (speak && !cJSON_IsBool(speak)) ok = false;
    next.speak = !cJSON_IsFalse(speak);
    cJSON_Delete(root);
    memset(text, 0, sizeof(text));
    if (!ok || !valid(next)) { memset(&next, 0, sizeof(next)); status_text = "AI 配置内容无效，原配置保留"; return; }
    if (ai_config_store(&next)) {
        activate(next);
        /* The import file is a one-time carrier; keep the secret only in NVS. */
        if (!storage_remove(IMPORT_PATH)) puts("[AI CONFIG] saved; import file cleanup failed");
        else puts("[AI CONFIG] imported into NVS");
    } else status_text = "AI 配置保存失败，原配置保留";
    memset(&next, 0, sizeof(next));
}
}

extern "C" void ai_config_init(void)
{
    StoredConfig stored = {};
    size_t length = sizeof(stored);
    nvs_handle_t handle = 0;
    if (nvs_open("ai_direct", NVS_READONLY, &handle) == ESP_OK) {
        const esp_err_t error = nvs_get_blob(handle, "config", &stored, &length);
        nvs_close(handle);
        if (error == ESP_OK && length == sizeof(stored) && stored.magic == CONFIG_MAGIC && valid(stored.config)) activate(stored.config);
    }
    memset(&stored, 0, sizeof(stored));
    import_sd();
}
extern "C" bool ai_config_get(ai_api_config_t *config)
{
    if (!config) return false;
    portENTER_CRITICAL(&config_mux);
    const bool available = ready;
    if (available) *config = active_config;
    else memset(config, 0, sizeof(*config));
    portEXIT_CRITICAL(&config_mux);
    return available;
}
extern "C" bool ai_config_is_ready(void)
{
    portENTER_CRITICAL(&config_mux);
    const bool available = ready;
    portEXIT_CRITICAL(&config_mux);
    return available;
}
extern "C" const char *ai_config_status(void) { return status_text; }
extern "C" bool ai_config_validate(const ai_api_config_t *config) { return config && valid(*config); }
extern "C" bool ai_config_store(const ai_api_config_t *config)
{
    if (!ai_config_validate(config)) return false;
    StoredConfig stored{};
    stored.magic = CONFIG_MAGIC;
    stored.config = *config;
    nvs_handle_t handle = 0;
    esp_err_t error = nvs_open("ai_direct", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "config", &stored, sizeof stored);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    memset(&stored, 0, sizeof stored);
    return error == ESP_OK;
}
extern "C" bool ai_config_read_saved(ai_api_config_t *config)
{
    if (!config) return false;
    memset(config, 0, sizeof(*config));
    StoredConfig stored{};
    size_t length = sizeof stored;
    nvs_handle_t handle = 0;
    bool ok = false;
    if (nvs_open("ai_direct", NVS_READONLY, &handle) == ESP_OK) {
        const esp_err_t error = nvs_get_blob(handle, "config", &stored, &length);
        nvs_close(handle);
        ok = error == ESP_OK && length == sizeof stored && stored.magic == CONFIG_MAGIC && valid(stored.config);
        if (ok) *config = stored.config;
    }
    memset(&stored, 0, sizeof stored);
    return ok;
}
