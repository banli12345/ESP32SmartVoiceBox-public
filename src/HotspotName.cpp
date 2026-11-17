#include "HotspotName.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "nvs.h"

extern "C" bool wifi_hotspot_apply_credentials(const char* name, const char* password);

namespace {

enum class NameErr : int { Ok = 0, Empty, TooLong, Bad };

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
hotspot_name_state_t g_state = {};
struct StoredCredentials {
    uint8_t version;
    char name[33];
    char password[64];
};
StoredCredentials g_pending = {};
constexpr const char* kCredentialsKey = "credentials";
bool g_has_job = false;

constexpr const char* kNs = "hotspot_cfg";
constexpr const char* kKey = "ssid";

void copy_msg(char dst[128], const char* src) {
    std::memset(dst, 0, 128);
    if (src == nullptr) {
        return;
    }
    std::size_t i = 0;
    while (i < 127 && src[i] != '\0') {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

const char* err_text(NameErr err) {
    switch (err) {
    case NameErr::Empty:
        return "名称不能为空";
    case NameErr::TooLong:
        return "名称不能超过32字节";
    case NameErr::Bad:
    case NameErr::Ok:
    default:
        return "名称包含无效字符";
    }
}

bool next_cp(const uint8_t* s, std::size_t len, std::size_t& i, uint32_t& cp) {
    if (i >= len) {
        return false;
    }
    const uint8_t c = s[i];
    if (c <= 0x7F) {
        cp = c;
        i += 1;
        return true;
    }
    if ((c & 0xE0) == 0xC0) {
        if (c < 0xC2 || i + 1 >= len || (s[i + 1] & 0xC0) != 0x80) {
            return false;
        }
        cp = (uint32_t(c & 0x1F) << 6) | uint32_t(s[i + 1] & 0x3F);
        i += 2;
        return true;
    }
    if ((c & 0xF0) == 0xE0) {
        if (i + 2 >= len) {
            return false;
        }
        const uint8_t c1 = s[i + 1];
        const uint8_t c2 = s[i + 2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) {
            return false;
        }
        if ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0)) {
            return false;
        }
        cp = (uint32_t(c & 0x0F) << 12) | (uint32_t(c1 & 0x3F) << 6) | uint32_t(c2 & 0x3F);
        i += 3;
        return true;
    }
    if ((c & 0xF8) == 0xF0) {
        if (c > 0xF4 || i + 3 >= len) {
            return false;
        }
        const uint8_t c1 = s[i + 1];
        const uint8_t c2 = s[i + 2];
        const uint8_t c3 = s[i + 3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) {
            return false;
        }
        if ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 > 0x8F)) {
            return false;
        }
        cp = (uint32_t(c & 0x07) << 18) | (uint32_t(c1 & 0x3F) << 12) |
             (uint32_t(c2 & 0x3F) << 6) | uint32_t(c3 & 0x3F);
        if (cp > 0x10FFFF) {
            return false;
        }
        i += 4;
        return true;
    }
    return false;
}

NameErr normalize_name(const char* text, char out[33]) {
    if (out != nullptr) {
        std::memset(out, 0, 33);
    }
    if (text == nullptr || out == nullptr) {
        return NameErr::Bad;
    }

    std::size_t n = 0;
    while (n < 128 && text[n] != '\0') {
        ++n;
    }
    if (n == 128) {
        return NameErr::Bad;
    }

    std::size_t begin = 0;
    while (begin < n && static_cast<uint8_t>(text[begin]) == 0x20) {
        ++begin;
    }
    std::size_t end = n;
    while (end > begin && static_cast<uint8_t>(text[end - 1]) == 0x20) {
        --end;
    }
    if (begin == end) {
        return NameErr::Empty;
    }
    const std::size_t len = end - begin;
    if (len > 32) {
        return NameErr::TooLong;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(text + begin);
    std::size_t i = 0;
    while (i < len) {
        uint32_t cp = 0;
        if (!next_cp(bytes, len, i, cp)) {
            return NameErr::Bad;
        }
        if (cp <= 0x1F || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F)) {
            return NameErr::Bad;
        }
    }

    std::memcpy(out, text + begin, len);
    out[len] = '\0';
    return NameErr::Ok;
}

bool password_valid(const char* text) {
    if (!text) return false;
    std::size_t length = 0;
    while (length < 64 && text[length]) {
        const unsigned char c = static_cast<unsigned char>(text[length]);
        if (c < 0x20 || c > 0x7e) return false;
        ++length;
    }
    return length >= 8 && length <= 63;
}

bool read_credentials(StoredCredentials& value) {
    value = {};
    nvs_handle_t handle = 0;
    if (nvs_open(kNs, NVS_READONLY, &handle) != ESP_OK) return false;
    std::size_t length = sizeof(value);
    const esp_err_t err = nvs_get_blob(handle, kCredentialsKey, &value, &length);
    nvs_close(handle);
    char normalized[33];
    if (err != ESP_OK || length != sizeof(value) || value.version != 1 ||
        !std::memchr(value.name, 0, sizeof(value.name)) ||
        !std::memchr(value.password, 0, sizeof(value.password)) ||
        normalize_name(value.name, normalized) != NameErr::Ok || !password_valid(value.password)) {
        value = {};
        return false;
    }
    std::memcpy(value.name, normalized, sizeof(value.name));
    return true;
}

void finish(int result, const char* message, const StoredCredentials* current) {
    portENTER_CRITICAL(&g_mux);
    if (current) {
        std::memcpy(g_state.name, current->name, sizeof(g_state.name));
        std::memcpy(g_state.password, current->password, sizeof(g_state.password));
    }
    copy_msg(g_state.message, message);
    g_state.result = result;
    g_state.busy = false;
    g_has_job = false;
    g_state.revision++;
    portEXIT_CRITICAL(&g_mux);
}

}  // namespace

extern "C" bool hotspot_name_normalize(const char* text, char out[33]) {
    return normalize_name(text, out) == NameErr::Ok;
}

extern "C" void hotspot_name_restore(const char* fallback, char out[33]) {
    char loaded[33];
    std::memset(loaded, 0, sizeof(loaded));

    nvs_handle_t handle = 0;
    const esp_err_t opened = nvs_open(kNs, NVS_READONLY, &handle);
    if (opened == ESP_OK) {
        std::size_t need = 0;
        esp_err_t err = nvs_get_str(handle, kKey, nullptr, &need);
        if (err == ESP_OK && need > 0 && need <= 33) {
            char raw[33];
            std::memset(raw, 0, sizeof(raw));
            std::size_t len = sizeof(raw);
            err = nvs_get_str(handle, kKey, raw, &len);
            if (err == ESP_OK && len > 0 && len <= sizeof(raw) && raw[len - 1] == 0) {
                char normalized[33];
                if (normalize_name(raw, normalized) == NameErr::Ok) {
                    std::memcpy(loaded, normalized, sizeof(loaded));
                }
            }
        }
        nvs_close(handle);
    }

    StoredCredentials saved;
    if (read_credentials(saved)) std::memcpy(loaded, saved.name, sizeof(loaded));

    if (loaded[0] == '\0') {
        if (normalize_name(fallback, loaded) != NameErr::Ok) {
            std::memset(loaded, 0, sizeof(loaded));
        }
    }

    if (out != nullptr) {
        std::memcpy(out, loaded, 33);
    }

    portENTER_CRITICAL(&g_mux);
    std::memcpy(g_state.name, loaded, 33);
    g_state.revision++;
    portEXIT_CRITICAL(&g_mux);
}

extern "C" bool hotspot_password_valid(const char* text) {
    return password_valid(text);
}

extern "C" void hotspot_password_restore(const char* fallback, char out[64]) {
    StoredCredentials saved;
    char loaded[64] = {};
    if (read_credentials(saved)) std::memcpy(loaded, saved.password, sizeof(loaded));
    else if (password_valid(fallback)) std::memcpy(loaded, fallback, std::strlen(fallback));
    if (out) std::memcpy(out, loaded, sizeof(loaded));
    portENTER_CRITICAL(&g_mux);
    std::memcpy(g_state.password, loaded, sizeof(g_state.password));
    g_state.revision++;
    portEXIT_CRITICAL(&g_mux);
}

extern "C" bool hotspot_name_request(const char* name) {
    hotspot_name_state_t state;
    hotspot_name_get(&state);
    return hotspot_credentials_request(name, state.password);
}

extern "C" bool hotspot_credentials_request(const char* name, const char* password) {
    char normalized[33];
    const NameErr err = normalize_name(name, normalized);
    const bool valid_password = password_valid(password);
    portENTER_CRITICAL(&g_mux);
    if (g_state.busy) {
        portEXIT_CRITICAL(&g_mux);
        return false;
    }
    if (err != NameErr::Ok || !valid_password) {
        copy_msg(g_state.message, err != NameErr::Ok ? err_text(err) : "密码需8-63位英文、数字或符号");
        g_state.result = -1;
        g_state.revision++;
        portEXIT_CRITICAL(&g_mux);
        return false;
    }
    g_pending = {};
    g_pending.version = 1;
    std::memcpy(g_pending.name, normalized, sizeof(normalized));
    std::memcpy(g_pending.password, password, std::strlen(password));
    g_has_job = true;
    g_state.busy = true;
    g_state.result = 0;
    copy_msg(g_state.message, "正在保存");
    g_state.revision++;
    portEXIT_CRITICAL(&g_mux);
    return true;
}

extern "C" void hotspot_name_poll(void) {
    StoredCredentials next = {}, previous = {};
    portENTER_CRITICAL(&g_mux);
    if (!g_state.busy || !g_has_job) {
        portEXIT_CRITICAL(&g_mux);
        return;
    }
    next = g_pending;
    previous.version = 1;
    std::memcpy(previous.name, g_state.name, sizeof(previous.name));
    std::memcpy(previous.password, g_state.password, sizeof(previous.password));
    g_has_job = false;
    portEXIT_CRITICAL(&g_mux);

    nvs_handle_t handle = 0;
    if (nvs_open(kNs, NVS_READWRITE, &handle) != ESP_OK) {
        finish(-1, "无法打开存储", nullptr);
        return;
    }
    if (!wifi_hotspot_apply_credentials(next.name, next.password)) {
        nvs_close(handle);
        finish(-1, "热点设置应用失败", nullptr);
        return;
    }
    // One versioned NVS entry keeps the SSID and passphrase together.
    esp_err_t err = nvs_set_blob(handle, kCredentialsKey, &next, sizeof(next));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK) {
        // A failed commit may have reached flash. Attempt to restore the old pair.
        if (password_valid(previous.password)) {
            if (nvs_set_blob(handle, kCredentialsKey, &previous, sizeof(previous)) == ESP_OK) nvs_commit(handle);
        }
        nvs_close(handle);
        if (wifi_hotspot_apply_credentials(previous.name, previous.password)) {
            finish(-1, "保存失败，已恢复原设置", nullptr);
        } else {
            finish(-1, "设置已更改，但保存失败", &next);
        }
        return;
    }
    nvs_close(handle);
    finish(1, "已保存", &next);
}

extern "C" void hotspot_name_get(hotspot_name_state_t* out) {
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&g_mux);
    *out = g_state;
    portEXIT_CRITICAL(&g_mux);
}
