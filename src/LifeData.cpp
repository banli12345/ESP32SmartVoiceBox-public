#include "LifeData.h"
#include "LocalLunar.h"
#include "IdfRuntime.h"
#include "setwifi.h"
#include "Storage.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint32_t> s_rev{0};
static std::atomic<uint32_t> s_gen{1};
static std::atomic<bool> s_busy{false};

static char s_key[96];
static char s_endpoint[192];
static bool s_ignore_cache;
static uint32_t s_source_tag;
static char s_city[32];
static life_indices_t s_idx;
static bool s_have;
static char s_today[11];
static uint32_t s_slot, s_seen_gen, s_attempt_at;
static bool s_attempted;
static const char *s_health_state;
static uint32_t s_health_at;

static void health(const char *state, uint32_t now) {
    if (s_health_state == state && (uint32_t)(now - s_health_at) < 30000) return;
    s_health_state = state; s_health_at = now;
    printf("[LIFE] state=%s\n", state);
}

static uint32_t mono_ms(void) { return app_millis(); }

static bool time_date(char out[11]) {
    time_t now = time(nullptr);
    if (now < 1000000000) return false;
    struct tm tm{};
    if (!localtime_r(&now, &tm)) return false;
    return strftime(out, 11, "%Y-%m-%d", &tm) == 10;
}

static void copy_utf8(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!src) src = "";
    while (src[n] && n + 1 < cap) {
        unsigned char c = (unsigned char)src[n];
        size_t need = 1;
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0) need = 2;
            else if ((c & 0xF0) == 0xE0) need = 3;
            else if ((c & 0xF8) == 0xF0) need = 4;
            else break;
        }
        if (n + need >= cap) break;
        for (size_t k = 1; k < need; ++k)
            if (((unsigned char)src[n + k] & 0xC0) != 0x80) { need = 0; break; }
        if (!need) break;
        n += need;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool brief_ok(const char *s) {
    if (!s || !s[0]) return false;
    size_t i = 0;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        size_t need = 1;
        if (c <= 0x7F) { if (c < 32 || c == 127) return false; need = 1; }
        else if ((c & 0xE0) == 0xC0) { if (c < 0xC2) return false; need = 2; }
        else if ((c & 0xF0) == 0xE0) need = 3;
        else if ((c & 0xF8) == 0xF0) { if (c > 0xF4) return false; need = 4; }
        else return false;
        if (i + need >= 48) return false;
        for (size_t k = 1; k < need; ++k)
            if (((unsigned char)s[i + k] & 0xC0) != 0x80) return false;
        unsigned char c1 = (unsigned char)s[i + 1];
        if (need == 3 && ((c == 0xE0 && c1 < 0xA0) || (c == 0xED && c1 >= 0xA0))) return false;
        if (need == 4 && ((c == 0xF0 && c1 < 0x90) || (c == 0xF4 && c1 >= 0x90))) return false;
        i += need;
    }
    return i > 0 && strcmp(s, "暂缺") != 0 && strcmp(s, "暂无") != 0;
}

static bool pct(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p; ++p) {
        bool raw = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
                   (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~';
        if (raw) {
            if (n + 1 >= cap) return false;
            dst[n++] = (char)*p;
        } else {
            if (n + 3 >= cap) return false;
            snprintf(dst + n, 4, "%%%02X", *p);
            n += 3;
        }
    }
    if (n >= cap) return false;
    dst[n] = 0;
    return true;
}

static int valid_count(const char *dressing, const char *uv, const char *flu, const char *sport) {
    int n = 0;
    if (dressing && dressing[0] && brief_ok(dressing)) ++n;
    if (uv && uv[0] && brief_ok(uv)) ++n;
    if (flu && flu[0] && brief_ok(flu)) ++n;
    if (sport && sport[0] && brief_ok(sport)) ++n;
    return n;
}

static bool copy_index(char dst[48], const char *src) {
    dst[0] = 0;
    if (!src || !src[0]) return true;
    if (!brief_ok(src)) return false;
    memcpy(dst, src, strlen(src) + 1);
    return true;
}

static bool publish(uint32_t gen, const char *date, const char *city,
                    const char *dressing, const char *uv, const char *flu, const char *sport) {
    char nowd[11];
    if (!time_date(nowd) || strcmp(nowd, date) != 0) return false;
    char d[48], u[48], f[48], p[48];
    if (!copy_index(d, dressing) || !copy_index(u, uv) || !copy_index(f, flu) || !copy_index(p, sport))
        return false;
    if (!d[0] && !u[0] && !f[0] && !p[0]) return false;
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (gen == s_gen.load(std::memory_order_relaxed) && strcmp(city, s_city) == 0) {
        memset(&s_idx, 0, sizeof s_idx);
        memcpy(s_idx.date, date, 11);
        memcpy(s_idx.city, city, strnlen(city, 31) + 1);
        memcpy(s_idx.dressing, d, strlen(d) + 1);
        memcpy(s_idx.uv, u, strlen(u) + 1);
        memcpy(s_idx.flu, f, strlen(f) + 1);
        memcpy(s_idx.sport, p, strlen(p) + 1);
        s_have = true;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (ok) s_rev.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

static cJSON *parse_json(const char *data, size_t length) {
    int depth = 0; bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        const char c = data[i];
        if (!c) return nullptr;
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 16) return nullptr; }
        else if (c == '}' || c == ']') { if (--depth < 0) return nullptr; }
    }
    if (quoted || depth) return nullptr;
    const char *end = nullptr;
    cJSON *root = cJSON_ParseWithLengthOpts(data, length + 1, &end, true);
    if (root && end != data + length) { cJSON_Delete(root); return nullptr; }
    return root;
}

static bool index_field(cJSON *item, char *dst) {
    dst[0] = 0;
    if (!cJSON_IsString(item)) return false;
    if (!item->valuestring[0]) return true;
    if (!brief_ok(item->valuestring)) return false;
    memcpy(dst, item->valuestring, strlen(item->valuestring) + 1);
    return true;
}

static bool cache_take(const char *date, const char *city, char *dressing, char *uv, char *flu, char *sport) {
    StorageStream f;
    if (!storage_exists("/.calendar/indices.json") || !f.open("/.calendar/indices.json", "rb")) return false;
    size_t n = f.byte_count();
    if (n == 0 || n > 1024) { f.close(); return false; }
    char *buf = (char *)heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) { f.close(); return false; }
    bool ok = f.read(buf, n) == n;
    f.close();
    if (!ok) { heap_caps_free(buf); return false; }
    buf[n] = 0;
    cJSON *root = parse_json(buf, n);
    heap_caps_free(buf);
    if (!root) return false;
    cJSON *v = cJSON_GetObjectItem(root, "v");
    cJSON *d = cJSON_GetObjectItem(root, "date");
    cJSON *c = cJSON_GetObjectItem(root, "city");
    cJSON *source = cJSON_GetObjectItem(root, "source");
    char td[48]{}, tu[48]{}, tf[48]{}, tp[48]{};
    ok = cJSON_IsNumber(v) && v->valuedouble == 3 && cJSON_IsString(d) && cJSON_IsString(c) &&
         cJSON_IsNumber(source) && source->valuedouble == static_cast<double>(s_source_tag) &&
         strcmp(d->valuestring, date) == 0 && strcmp(c->valuestring, city) == 0 &&
         index_field(cJSON_GetObjectItem(root, "dressing"), td) &&
         index_field(cJSON_GetObjectItem(root, "uv"), tu) &&
         index_field(cJSON_GetObjectItem(root, "flu"), tf) &&
         index_field(cJSON_GetObjectItem(root, "sport"), tp) &&
         valid_count(td, tu, tf, tp) > 0;
    if (ok) {
        memcpy(dressing, td, 48);
        memcpy(uv, tu, 48);
        memcpy(flu, tf, 48);
        memcpy(sport, tp, 48);
    }
    cJSON_Delete(root);
    return ok;
}

static void cache_store(const char *date, const char *city,
                        const char *dressing, const char *uv, const char *flu, const char *sport) {
    cJSON *o = cJSON_CreateObject();
    if (!o) return;
    cJSON_AddNumberToObject(o, "v", 3);
    cJSON_AddNumberToObject(o, "source", s_source_tag);
    cJSON_AddStringToObject(o, "date", date);
    cJSON_AddStringToObject(o, "city", city);
    cJSON_AddStringToObject(o, "dressing", dressing ? dressing : "");
    cJSON_AddStringToObject(o, "uv", uv ? uv : "");
    cJSON_AddStringToObject(o, "flu", flu ? flu : "");
    cJSON_AddStringToObject(o, "sport", sport ? sport : "");
    char *txt = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (!txt || strlen(txt) > 1024) { cJSON_free(txt); return; }
    size_t n = strlen(txt);
    if (!storage_is_dir("/.calendar") && !storage_mkdir("/.calendar")) { cJSON_free(txt); return; }
    if (storage_exists("/.calendar/indices.tmp")) storage_remove("/.calendar/indices.tmp");
    StorageStream f;
    bool wrote = f.open("/.calendar/indices.tmp", "wb") && f.write(txt, n) == n && f.flush();
    f.close();
    cJSON_free(txt);
    if (!wrote) return;
    if (storage_exists("/.calendar/indices.json")) storage_remove("/.calendar/indices.json");
    storage_rename("/.calendar/indices.tmp", "/.calendar/indices.json");
}

static void take_brief(cJSON *daily, const char *name, char *dst) {
    dst[0] = 0;
    cJSON *br = cJSON_GetObjectItem(cJSON_GetObjectItem(daily, name), "brief");
    if (cJSON_IsString(br) && brief_ok(br->valuestring))
        memcpy(dst, br->valuestring, strlen(br->valuestring) + 1);
}

/* Basic responses use an object; the days parameter switches to dated rows. */
static bool parse_indices(cJSON *root, const char *date, char *dressing, char *uv, char *flu, char *sport) {
    dressing[0] = uv[0] = flu[0] = sport[0] = 0;
    cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "results"), 0);
    cJSON *suggestion = cJSON_GetObjectItem(item, "suggestion");
    cJSON *daily = nullptr;
    if (cJSON_IsArray(suggestion)) {
        cJSON *row = nullptr;
        cJSON_ArrayForEach(row, suggestion) {
            cJSON *day = cJSON_GetObjectItem(row, "date");
            if (cJSON_IsString(day) && !strcmp(day->valuestring, date)) {
                daily = row;
                break;
            }
        }
    } else if (cJSON_IsObject(suggestion)) {
        cJSON *updated = cJSON_GetObjectItem(item, "last_update");
        if (!cJSON_IsString(updated) || strlen(updated->valuestring) < 10 ||
            memcmp(updated->valuestring, date, 10) != 0) return false;
        daily = suggestion;
    }
    if (!daily) return false;
    take_brief(daily, "dressing", dressing);
    take_brief(daily, "uv", uv);
    take_brief(daily, "flu", flu);
    take_brief(daily, "sport", sport);
    return valid_count(dressing, uv, flu, sport) > 0;
}

static int fetch_indices(const char *endpoint, const char *key, const char *city, const char *date,
                         char *dressing, char *uv, char *flu, char *sport) {
    char ek[96 * 3 + 1], ec[32 * 3 + 1], url[768];
    if (!pct(ek, sizeof ek, key) || !pct(ec, sizeof ec, city)) return 2;
    const int count = snprintf(url, sizeof url, "%s?key=%s&location=%s&language=zh-Hans",
                               endpoint, ek, ec);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(url)) return 2;
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.method = HTTP_METHOD_GET;
    cfg.addr_type = HTTP_ADDR_TYPE_INET;
    cfg.timeout_ms = 6000;
    cfg.buffer_size = 1024;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    char *body = nullptr;
    cJSON *root = nullptr;
    int code = 2;
    if (!client) goto done;
    if (esp_http_client_open(client, 0) != ESP_OK) goto done;
    {
        int len = esp_http_client_fetch_headers(client);
        if (len < 0 || esp_http_client_get_status_code(client) != 200 || len > 16384) goto done;
        body = (char *)heap_caps_malloc(16385, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!body) { code = 3; goto done; }
        int total = 0;
        bool eof = false;
        TickType_t t0 = xTaskGetTickCount();
        while ((uint32_t)(xTaskGetTickCount() - t0) < pdMS_TO_TICKS(6000)) {
            int space = 16384 - total;
            int n = esp_http_client_read(client, body + total, space > 0 ? space : 1);
            if (n == (int)ESP_ERR_HTTP_EAGAIN || n == -((int)ESP_ERR_HTTP_EAGAIN)) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (n == 0) { eof = true; break; }
            if (space == 0 || n < 0 || n > space) goto done;
            total += n;
        }
        if (!eof || total <= 0 || (len > 0 && total != len) || !esp_http_client_is_complete_data_received(client)) goto done;
        body[total] = 0;
        root = parse_json(body, (size_t)total);
        if (!root || !parse_indices(root, date, dressing, uv, flu, sport)) goto done;
        code = 1;
    }
done:
    if (root) cJSON_Delete(root);
    if (body) heap_caps_free(body);
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
    memset(url, 0, sizeof url);
    memset(ek, 0, sizeof ek);
    memset(ec, 0, sizeof ec);
    return code;
}

static void life_worker(void *) {
    char key[96], endpoint[192], city[32], date[11];
    bool ignore_cache;
    char dressing[48], uv[48], flu[48], sport[48];
    uint32_t gen = 0;
    int code = 2;
    int count = 0;
    memset(dressing, 0, sizeof dressing);
    memset(uv, 0, sizeof uv);
    memset(flu, 0, sizeof flu);
    memset(sport, 0, sizeof sport);
    portENTER_CRITICAL(&s_mux);
    gen = s_gen.load(std::memory_order_relaxed);
    memcpy(key, s_key, sizeof key);
    memcpy(endpoint, s_endpoint, sizeof endpoint);
    ignore_cache = s_ignore_cache;
    memcpy(city, s_city, sizeof city);
    portEXIT_CRITICAL(&s_mux);
    if (time_date(date) && key[0] && endpoint[0] && city[0]) {
        if (!ignore_cache && cache_take(date, city, dressing, uv, flu, sport) &&
            publish(gen, date, city, dressing, uv, flu, sport)) {
            code = 1;
            count = valid_count(dressing, uv, flu, sport);
        } else {
            memset(dressing, 0, sizeof dressing);
            memset(uv, 0, sizeof uv);
            memset(flu, 0, sizeof flu);
            memset(sport, 0, sizeof sport);
            code = fetch_indices(endpoint, key, city, date, dressing, uv, flu, sport);
            if (code == 1 && publish(gen, date, city, dressing, uv, flu, sport)) {
                cache_store(date, city, dressing, uv, flu, sport);
                portENTER_CRITICAL(&s_mux);
                if (gen == s_gen.load(std::memory_order_relaxed)) s_ignore_cache = false;
                portEXIT_CRITICAL(&s_mux);
                count = valid_count(dressing, uv, flu, sport);
            } else if (code == 1) code = 2;
        }
    }
    if (code != 1) count = 0;
    memset(key, 0, sizeof key);
    printf("[LIFE] indices result=%s count=%u stack_free_min=%u\n", code == 1 ? "ready" : "retry",
           static_cast<unsigned>(count),
           static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    s_busy.store(false, std::memory_order_release);
    vTaskDelete(nullptr);
}

static bool ram_fresh(const char *date) {
    bool ok;
    portENTER_CRITICAL(&s_mux);
    ok = s_have && strcmp(s_idx.date, date) == 0 && strcmp(s_idx.city, s_city) == 0 &&
         (s_idx.dressing[0] || s_idx.uv[0] || s_idx.flu[0] || s_idx.sport[0]);
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

extern "C" void life_data_init(const char *seniverse_key, const char *city,
                               const char *endpoint, bool force_refresh) {
    portENTER_CRITICAL(&s_mux);
    memset(s_key, 0, sizeof s_key);
    memset(s_endpoint, 0, sizeof s_endpoint);
    memset(s_city, 0, sizeof s_city);
    memset(&s_idx, 0, sizeof s_idx);
    s_have = false;
    copy_utf8(s_key, sizeof s_key, seniverse_key);
    copy_utf8(s_endpoint, sizeof s_endpoint, endpoint);
    /* Cache identity only: retain no plaintext key in the derived SD cache. */
    s_source_tag = 2166136261U;
    const char *parts[] = {s_key, s_endpoint};
    for (const char *part : parts) {
        for (const unsigned char *p = reinterpret_cast<const unsigned char *>(part); *p; ++p)
            s_source_tag = (s_source_tag ^ *p) * 16777619U;
        s_source_tag = (s_source_tag ^ 0xffU) * 16777619U;
    }
    s_ignore_cache = force_refresh;
    copy_utf8(s_city, sizeof s_city, city);
    portEXIT_CRITICAL(&s_mux);
    printf("[LIFE] init key_present=%u city_present=%u\n", s_key[0] != 0, s_city[0] != 0);
}

extern "C" void life_data_set_city(const char *city) {
    portENTER_CRITICAL(&s_mux);
    copy_utf8(s_city, sizeof s_city, city);
    memset(&s_idx, 0, sizeof s_idx);
    s_have = false;
    s_gen.fetch_add(1, std::memory_order_relaxed);
    portEXIT_CRITICAL(&s_mux);
    s_rev.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void life_data_poll(void) {
    const uint32_t now = mono_ms();
    if ((uint32_t)(now - s_slot) < 1000) return;
    s_slot = now;
    const uint32_t generation = s_gen.load(std::memory_order_acquire);
    if (generation != s_seen_gen) { s_seen_gen = generation; s_attempted = false; }
    char date[11];
    if (!time_date(date)) { health("waiting_clock", now); return; }
    if (strcmp(s_today, date)) {
        memcpy(s_today, date, sizeof s_today);
        s_rev.fetch_add(1, std::memory_order_relaxed);
        s_attempted = false;
    }
    if (s_busy.load(std::memory_order_acquire)) { health("requesting", now); return; }
    if (!get_wifi_connected()) { health("waiting_wifi", now); return; }
    if (ram_fresh(date)) { health("ready", now); return; }
    if (s_attempted && (uint32_t)(now - s_attempt_at) < 300000) { health("retry_backoff", now); return; }
    char key[96], city[32];
    portENTER_CRITICAL(&s_mux);
    memcpy(key, s_key, sizeof key); memcpy(city, s_city, sizeof city);
    portEXIT_CRITICAL(&s_mux);
    if (!key[0] || !city[0]) { health("missing_config", now); return; }
    s_attempt_at = now; s_attempted = true;
    s_busy.store(true, std::memory_order_release);
    health("starting", now);
    if (xTaskCreatePinnedToCore(life_worker, "LifeIndex", 7168, nullptr, 1, nullptr, 0) != pdPASS) {
        s_busy.store(false, std::memory_order_release);
        health("task_allocation_failed", now);
    }
}

extern "C" void life_data_request_month(int, int) {}
extern "C" void life_data_request_day(int, int, int) {}

extern "C" bool life_data_get_day(int year, int month, int day, life_calendar_day_t *out) {
    return local_lunar_get(year, month, day, out);
}

extern "C" bool life_data_get_indices(life_indices_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    char date[11];
    if (!time_date(date)) return false;
    life_indices_t snap{};
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_have && strcmp(s_idx.date, date) == 0 && strcmp(s_idx.city, s_city) == 0) {
        snap = s_idx;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!ok) return false;
    if (snap.dressing[0] && !brief_ok(snap.dressing)) snap.dressing[0] = 0;
    if (snap.uv[0] && !brief_ok(snap.uv)) snap.uv[0] = 0;
    if (snap.flu[0] && !brief_ok(snap.flu)) snap.flu[0] = 0;
    if (snap.sport[0] && !brief_ok(snap.sport)) snap.sport[0] = 0;
    if (!snap.dressing[0] && !snap.uv[0] && !snap.flu[0] && !snap.sport[0]) return false;
    *out = snap;
    return true;
}

extern "C" bool life_data_get_dressing(life_dressing_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof *out);
    life_indices_t snap{};
    if (!life_data_get_indices(&snap) || !snap.dressing[0] || !brief_ok(snap.dressing)) return false;
    memcpy(out->date, snap.date, sizeof out->date);
    memcpy(out->city, snap.city, sizeof out->city);
    memcpy(out->brief, snap.dressing, sizeof out->brief);
    return true;
}

extern "C" void life_data_calendar_status(char *out, size_t capacity) {
    if (!out || !capacity) return;
    snprintf(out, capacity, "%s", "本地农历换算，支持1901—2099年");
}

extern "C" uint32_t life_data_revision(void) {
    return s_rev.load(std::memory_order_relaxed);
}
