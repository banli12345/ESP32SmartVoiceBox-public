#include "MusicLibraryLock.h"
#include "OnlineMusic.h"
#include "MusicSave.h"
#include "music_intent.h"
#include "Sound.h"
#include "Storage.h"
#include "SDcar.h"
#include "setwifi.h"
#include "IdfRuntime.h"
#include "cJSON.h"
#include "esp_attr.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {
constexpr unsigned MAX_SONGS = 5;
constexpr size_t MAX_MP3_BYTES = 20u * 1024u * 1024u;
constexpr size_t SEARCH_BYTES = 24u * 1024u;
using Song = online_music_song_t;
struct Request {
    uint32_t generation;
    bool search_only, save_library, save_only;
    int index;
    char query[256];
    char id[24], title[128], artist[160], path[96];
};
struct PlayMeta { char id[24], title[128], artist[160], path[96]; };
static portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint32_t> generation{1};
static bool request_pending, worker_running, preparing;
static bool prepare_paused;
static bool upload_preparing;
static uint32_t worker_exited, reply_version;
static EXT_RAM_BSS_ATTR char latest_reply[1536];
static EXT_RAM_BSS_ATTR Song songs[MAX_SONGS];
static EXT_RAM_BSS_ATTR Request pending_request;
static unsigned song_count;
static int selection = -1;
static int saved_local_index = -1;
static EXT_RAM_BSS_ATTR char saved_local_path[64];
static char cached_ids[2][24];
static const char *cache_paths[] = {"/.online/song0.mp3", "/.online/song1.mp3"};
static PlayMeta play_meta[2];

bool current(uint32_t value) { return generation.load() == value; }
void publish_locked(const char *message)
{
    strlcpy(latest_reply, message ? message : "", sizeof(latest_reply));
    if (++reply_version == 0) ++reply_version;
}
void publish(uint32_t value, const char *message)
{
    portENTER_CRITICAL(&state_mux);
    if (current(value)) publish_locked(message);
    portEXIT_CRITICAL(&state_mux);
}
const char *json_text(const cJSON *object, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

struct Headers { char location[1024]; char type[96]; bool overflow; int status; };
static const char kNoPlayable[] = "网易云未提供可播放音源，请选择其他版本；部分歌曲可能需要登录或会员权限";
static const char kForbidden[] = "网易云拒绝访问该音源";
static const char kConnect[] = "无法连接网易云音源，请检查网络后重试";
static const char kHttpFail[] = "网易云音源请求失败，请稍后重试";
static const char kBadFormat[] = "音源格式不受支持，请选择其他版本";
static const char kTooBig[] = "音源文件过大，无法缓存";
static const char kNoSpace[] = "SD 卡空间不足，无法缓存歌曲";
static const char kSpaceUnknown[] = "无法确认 SD 卡剩余容量";
static const char kSdCache[] = "SD 卡缓存失败，请检查存储卡";
static const char kInterrupted[] = "歌曲下载中断，请检查网络后重试";
static const char kCacheBusy[] = "在线缓存正在使用，请稍后重试";
static const char kNoMem[] = "歌曲准备失败：外部内存不足";
bool fail_reason(const char **reason, const char *message)
{
    if (reason && !*reason) *reason = message;
    return false;
}
bool accepted_audio(const char *type)
{
    return !strncasecmp(type, "audio/mpeg", 10) || !strncasecmp(type, "application/octet-stream", 24);
}
bool html_or_json(const char *type)
{
    return !strncasecmp(type, "text/html", 9) || !strncasecmp(type, "application/json", 16) || !strncasecmp(type, "text/json", 9);
}
/* Exactly one OnlineMusic worker owns these scratch buffers. Keep its TLS
 * call stack small enough to coexist with the BLE controller and RGB DMA. */
static EXT_RAM_BSS_ATTR char worker_query_url[1280], worker_redirect_url[1280];
static EXT_RAM_BSS_ATTR char worker_reply[1536], worker_title[320], worker_path[96];
static EXT_RAM_BSS_ATTR Headers worker_headers;
esp_err_t on_http_event(esp_http_client_event_t *event)
{
    Headers *headers = static_cast<Headers *>(event->user_data);
    if (event->event_id != HTTP_EVENT_ON_HEADER || !headers || !event->header_key || !event->header_value) return ESP_OK;
    if (!strcasecmp(event->header_key, "Location")) {
        headers->overflow = strlen(event->header_value) >= sizeof(headers->location);
        strlcpy(headers->location, event->header_value, sizeof(headers->location));
    } else if (!strcasecmp(event->header_key, "Content-Type")) {
        strlcpy(headers->type, event->header_value, sizeof(headers->type));
    }
    return ESP_OK;
}
bool netease_url(const char *url)
{
    if (!url) return false;
    const char *host = !strncmp(url, "https://", 8) ? url + 8 :
                       !strncmp(url, "http://", 7) ? url + 7 : nullptr;
    if (!host) return false;
    const size_t len = strcspn(host, "/:?#");
    if (!len || len > 120 || memchr(host, '@', len)) return false;
    const char *allowed[] = {"music.163.com", ".music.163.com", ".music.126.net"};
    for (const char *suffix : allowed) {
        const size_t n = strlen(suffix);
        if (len >= n && !strncasecmp(host + len - n, suffix, n) &&
            (suffix[0] == '.' || len == n)) return true;
    }
    return false;
}
void close_http(esp_http_client_handle_t client)
{
    if (client) { esp_http_client_close(client); esp_http_client_cleanup(client); }
}
esp_http_client_handle_t open_http(const char *initial, Headers *headers, uint32_t value, int64_t *length)
{
    auto &url = worker_redirect_url;
    if (!initial || strlcpy(url, initial, sizeof(url)) >= sizeof(url)) return nullptr;
    for (unsigned hop = 0; hop < 5 && current(value); ++hop) {
        if (!netease_url(url)) break;
        *headers = {};
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.timeout_ms = 12000;
        cfg.addr_type = HTTP_ADDR_TYPE_INET;
        cfg.buffer_size = 1024;
        cfg.buffer_size_tx = 512;
        cfg.disable_auto_redirect = true;
        cfg.event_handler = on_http_event;
        cfg.user_data = headers;
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client) return nullptr;
        esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0");
        esp_http_client_set_header(client, "Referer", "https://music.163.com/");
        const esp_err_t open_error = esp_http_client_open(client, 0);
        if (open_error != ESP_OK) {
            printf("[NETMUSIC] connect failed hop=%u error=%d internal=%u largest=%u\n", hop, (int)open_error,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            close_http(client); return nullptr;
        }
        *length = esp_http_client_fetch_headers(client);
        headers->status = esp_http_client_get_status_code(client);
        const int status = headers->status;
        printf("[NETMUSIC] response hop=%u status=%d length=%lld\n", hop, status, (long long)*length);
        if (status == 200) {
            esp_http_client_set_timeout_ms(client, 1000);
            return client;
        }
        if ((status == 301 || status == 302 || status == 303 || status == 307 || status == 308) &&
            headers->location[0] && !headers->overflow) {
            strlcpy(url, headers->location, sizeof(url));
            close_http(client);
            continue;
        }
        printf("[NETMUSIC] source HTTP %d\n", status);
        close_http(client);
        return nullptr;
    }
    return nullptr;
}
bool query_url(const char *query, char *url, size_t capacity)
{
    const char *base = "https://music.163.com/api/search/get/web?type=1&limit=5&offset=0&s=";
    size_t used = strlcpy(url, base, capacity);
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(query); *p; ++p) {
        if (used + 4 >= capacity) return false;
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') url[used++] = *p;
        else { url[used++] = '%'; url[used++] = hex[*p >> 4]; url[used++] = hex[*p & 15]; }
    }
    url[used] = 0;
    return true;
}
bool search(const Request &request, Song *found, unsigned *count)
{
    *count = 0;
    auto &url = worker_query_url;
    if (!query_url(request.query, url, sizeof(url))) return false;
    char *body = static_cast<char *>(heap_caps_malloc(SEARCH_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) return false;
    auto &headers = worker_headers;
    headers = {};
    int64_t length = 0;
    esp_http_client_handle_t client = open_http(url, &headers, request.generation, &length);
    bool success = false;
    size_t used = 0;
    uint32_t progress = app_millis();
    while (client && current(request.generation) && length <= static_cast<int64_t>(SEARCH_BYTES) && used < SEARCH_BYTES && app_millis() - progress < 8000) {
        int got = esp_http_client_read(client, body + used, SEARCH_BYTES - used);
        if (got > 0) { used += got; progress = app_millis(); }
        else if (got == -ESP_ERR_HTTP_EAGAIN) { app_delay_ms(5); continue; }
        else if (got <= 0) { success = got == 0 && esp_http_client_is_complete_data_received(client); break; }
        if (esp_http_client_is_complete_data_received(client)) { success = true; break; }
    }
    close_http(client);
    body[used] = 0;
    cJSON *root = success && current(request.generation) ? cJSON_Parse(body) : nullptr;
    free(body);
    cJSON *code = root ? cJSON_GetObjectItem(root, "code") : nullptr;
    cJSON *result = root ? cJSON_GetObjectItem(root, "result") : nullptr;
    cJSON *list = result ? cJSON_GetObjectItem(result, "songs") : nullptr;
    success = cJSON_IsNumber(code) && code->valueint == 200;
    if (success && cJSON_IsArray(list)) {
        cJSON *entry;
        cJSON_ArrayForEach(entry, list) {
            cJSON *id = cJSON_GetObjectItem(entry, "id");
            if (*count == MAX_SONGS) break;
            if (!cJSON_IsNumber(id) || id->valuedouble < 1 || id->valuedouble > 9007199254740991.0 || !json_text(entry, "name")[0]) continue;
            Song &song = found[*count];
            song = {};
            snprintf(song.id, sizeof(song.id), "%.0f", id->valuedouble);
            strlcpy(song.title, json_text(entry, "name"), sizeof(song.title));
            cJSON *artists = cJSON_GetObjectItem(entry, "artists");
            if (!artists) artists = cJSON_GetObjectItem(entry, "ar");
            cJSON *artist;
            cJSON_ArrayForEach(artist, artists) {
                if (song.artist[0]) strlcat(song.artist, " / ", sizeof(song.artist));
                strlcat(song.artist, json_text(artist, "name"), sizeof(song.artist));
            }
            ++*count;
        }
    }
    cJSON_Delete(root);
    printf("[NETMUSIC] search completed ok=%d results=%u\n", success, *count);
    return success;
}

bool download(const Request &request, const Song &song, char *path, size_t path_size, const char **reason)
{
    if (reason) *reason = nullptr;
    for (unsigned i = 0; i < 2; ++i) {
        if (!strcmp(cached_ids[i], song.id) && storage_is_file(cache_paths[i])) {
            strlcpy(path, cache_paths[i], path_size);
            return true;
        }
    }
    if (!get_wifi_connected()) return fail_reason(reason, kConnect);
    if (!sdcar_is_ready() || !storage_mkdir("/.online")) return fail_reason(reason, kSdCache);
    unsigned slot;
    const uint32_t wait_began = app_millis();
    for (;;) {
        slot = music_audio_online_uses_path(cache_paths[0]) ? 1 : 0;
        if (!music_audio_online_uses_path(cache_paths[slot])) break;
        if (!current(request.generation)) return false;
        if (app_millis() - wait_began > 1500) return fail_reason(reason, kCacheBusy);
        app_delay_ms(10);
    }
    auto &headers = worker_headers;
    headers = {};
    char url[160];
    snprintf(url, sizeof(url), "https://music.163.com/song/media/outer/url?id=%s.mp3", song.id);
    int64_t length = 0;
    esp_http_client_handle_t client = open_http(url, &headers, request.generation, &length);
    printf("[NETMUSIC] media id=%s status=%d mime=%s length=%lld\n",
        song.id, headers.status, headers.type[0] ? headers.type : "-", (long long)length);
    if (!current(request.generation)) { close_http(client); return false; }
    if (!client) {
        const char *why = kHttpFail;
        if (headers.status == 404 || headers.status == 410) why = kNoPlayable;
        else if (headers.status == 403) why = kForbidden;
        else if (headers.status == 0) why = kConnect;
        return fail_reason(reason, why);
    }
    if (headers.status == 200 && html_or_json(headers.type)) { close_http(client); return fail_reason(reason, kNoPlayable); }
    if (!accepted_audio(headers.type)) { close_http(client); return fail_reason(reason, kBadFormat); }
    if (length > static_cast<int64_t>(MAX_MP3_BYTES)) { close_http(client); return fail_reason(reason, kTooBig); }
    if (length > 0) {
        const uint64_t free_bytes = storage_free_bytes();
        if (free_bytes == 0 || free_bytes < static_cast<uint64_t>(length) + 1024u * 1024u) {
            close_http(client);
            return fail_reason(reason, free_bytes == 0 ? kSpaceUnknown : kNoSpace);
        }
    }
    uint8_t *buffer = static_cast<uint8_t *>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    StorageStream file;
    bool success = buffer && file.open("/.online/download.part", "wb");
    if (!success && reason && !*reason) *reason = buffer ? kSdCache : kNoMem;
    size_t total = 0;
    uint32_t progress = app_millis(), began = progress;
    bool complete = false;
    while (success && current(request.generation) && total < MAX_MP3_BYTES && app_millis() - progress < 8000 && app_millis() - began < 120000) {
        const int got = esp_http_client_read(client, reinterpret_cast<char *>(buffer), 4096);
        if (got > 0) {
            if (total + static_cast<size_t>(got) > MAX_MP3_BYTES) { success = false; if (reason && !*reason) *reason = kTooBig; break; }
            if (file.write(buffer, got) != static_cast<size_t>(got)) { success = false; if (reason && !*reason) *reason = kSdCache; break; }
            total += got;
            progress = app_millis();
        } else if (got == -ESP_ERR_HTTP_EAGAIN) { app_delay_ms(5); continue; }
        else if (got < 0) { success = false; if (reason && !*reason) *reason = kInterrupted; break; }
        if (esp_http_client_is_complete_data_received(client)) { complete = true; break; }
        if (got == 0) { success = false; if (reason && !*reason) *reason = kInterrupted; break; }
        app_delay_ms(1);
    }
    const bool length_ok = length < 0 || length == 0 || static_cast<size_t>(length) == total;
    if (success && (!complete || total < 128 || !length_ok)) {
        success = false;
        if (current(request.generation) && reason && !*reason) *reason = kInterrupted;
    }
    if (success && !current(request.generation)) success = false;
    if (success && !file.flush()) { success = false; if (reason && !*reason) *reason = kSdCache; }
    file.close();
    free(buffer);
    close_http(client);
    if (success && (!current(request.generation) || music_audio_online_uses_path(cache_paths[slot]))) {
        if (current(request.generation) && reason && !*reason) *reason = kCacheBusy;
        success = false;
    }
    if (success) {
        cached_ids[slot][0] = 0;
        if (storage_exists(cache_paths[slot])) success = storage_remove(cache_paths[slot]);
        if (success) success = storage_rename("/.online/download.part", cache_paths[slot]);
        if (success) { strlcpy(cached_ids[slot], song.id, sizeof(cached_ids[slot])); strlcpy(path, cache_paths[slot], path_size); }
        else if (reason && !*reason) *reason = kSdCache;
    }
    if (!success) storage_remove("/.online/download.part");
    printf("[NETMUSIC] download ok=%d bytes=%u\n", success, static_cast<unsigned>(total));
    return success;
}

char *lyrics_for(const Request &request, const Song &song, bool *request_failed)
{
    *request_failed = false;
    if (!get_wifi_connected()) {*request_failed = true; return nullptr;}
    constexpr size_t LIMIT = 96u * 1024u;
    char *body = static_cast<char *>(heap_caps_malloc(LIMIT + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!body) {*request_failed = true; return nullptr;}
    snprintf(worker_query_url, sizeof(worker_query_url),
        "https://music.163.com/api/song/lyric?id=%s&lv=-1&kv=-1&tv=-1", song.id);
    int64_t length = 0;
    auto client = open_http(worker_query_url, &worker_headers, request.generation, &length);
    size_t used = 0; bool complete = false;
    uint32_t progress = app_millis(), began = progress;
    while (client && current(request.generation) && length <= (int64_t)LIMIT && used < LIMIT &&
           app_millis() - progress < 8000 && app_millis() - began < 20000) {
        const int got = esp_http_client_read(client, body + used, LIMIT - used);
        if (got > 0) {used += got; progress = app_millis();}
        else if (got == -ESP_ERR_HTTP_EAGAIN) {app_delay_ms(5); continue;}
        else if (got < 0) break;
        if (esp_http_client_is_complete_data_received(client)) {complete = true; break;}
        if (!got) break;
    }
    close_http(client); body[used] = 0;
    cJSON *root = complete && current(request.generation) ? cJSON_ParseWithLengthOpts(body, used + 1, nullptr, true) : nullptr;
    free(body);
    const cJSON *code = root ? cJSON_GetObjectItem(root, "code") : nullptr;
    const cJSON *lrc = root ? cJSON_GetObjectItem(root, "lrc") : nullptr;
    const char *text = json_text(lrc, "lyric");
    char *copy = nullptr;
    *request_failed = !cJSON_IsNumber(code) || code->valueint != 200 || strlen(text) > 65536;
    if (!*request_failed && *text) {
        copy = static_cast<char *>(heap_caps_malloc(strlen(text) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (copy) strcpy(copy, text);
        else *request_failed = true;
    }
    cJSON_Delete(root);
    return copy;
}
void save_progress(uint32_t job, uint64_t bytes)
{
    snprintf(worker_reply, sizeof(worker_reply), "正在保存 WAV 到 SD 卡：%.1f MiB\n歌曲可继续播放；输入“停止”取消未完成的保存", bytes / 1048576.0);
    publish(job, worker_reply);
}
void remember_play(const Song &song, const char *path)
{
    for (unsigned i = 0; i < 2; ++i) {
        if (strcmp(path, cache_paths[i])) continue;
        strlcpy(play_meta[i].id, song.id, sizeof(play_meta[i].id));
        strlcpy(play_meta[i].title, song.title, sizeof(play_meta[i].title));
        strlcpy(play_meta[i].artist, song.artist, sizeof(play_meta[i].artist));
        strlcpy(play_meta[i].path, path, sizeof(play_meta[i].path));
    }
}
bool match_active(char *path, size_t path_size, PlayMeta *meta)
{
    if (!music_audio_copy_active_online_path(path, path_size)) return false;
    for (unsigned i = 0; i < 2; ++i) {
        if (play_meta[i].id[0] && !strcmp(play_meta[i].path, path)) {
            if (meta) *meta = play_meta[i];
            return true;
        }
    }
    return false;
}
void save_cached(const Request &request)
{
    if (!current(request.generation)) return;
    char live[96];
    if (!music_audio_copy_active_online_path(live, sizeof(live)) || strcmp(live, request.path)) {
        publish(request.generation, "当前歌曲已切换，未保存");
        return;
    }
    if (!sdcar_is_ready() || !storage_is_file(request.path)) {
        publish(request.generation, "当前歌曲缓存不存在，无法保存到歌单");
        return;
    }
    Song song = {};
    strlcpy(song.id, request.id, sizeof(song.id));
    strlcpy(song.title, request.title, sizeof(song.title));
    strlcpy(song.artist, request.artist, sizeof(song.artist));
    bool lyric_failed = false;
    char *lyrics = lyrics_for(request, song, &lyric_failed);
    if (!current(request.generation)) { free(lyrics); return; }
    music_save_result_t saved = {};
    char error[128] = {};
    publish(request.generation, "正在转换并保存 WAV 与歌词到 SD 卡…歌曲继续当前播放");
    const bool locked = music_library_try_lock();
    if (!locked) strlcpy(error, "歌单正在修改，请稍后重试保存", sizeof(error));
    const bool ok = locked && music_save_mp3(request.path, song.title, song.artist, song.id, lyrics,
        request.generation, current, save_progress, &saved, error, sizeof(error));
    free(lyrics);
    if (ok && current(request.generation)) {
        const int local_index = sdcar_register_music_track(saved.wav_path);
        music_library_unlock();
        portENTER_CRITICAL(&state_mux);
        if (current(request.generation) && local_index >= 0) {
            saved_local_index = local_index;
            strlcpy(saved_local_path, saved.wav_path, sizeof(saved_local_path));
        }
        portEXIT_CRITICAL(&state_mux);
        snprintf(worker_reply, sizeof(worker_reply), "%s：%s\n%s%s\n%s",
            saved.reused ? "WAV 已存在，保留原文件" : "WAV 已保存", saved.wav_path,
            saved.lyrics_saved ? "LRC：" : "歌词：", saved.lyrics_saved ? saved.lrc_path :
                (error[0] ? error : lyric_failed ? "获取失败，可再次点击下载重试" : "平台未提供可用的 LRC"),
            local_index >= 0 ? "已加入设备本地音乐，可离线播放" : "文件已保存；本地列表已满，请整理音乐目录后重启");
        publish(request.generation, worker_reply);
    } else {
        if (locked) music_library_unlock();
        if (current(request.generation)) {
            snprintf(worker_reply, sizeof(worker_reply), "当前歌曲仍在播放；WAV 保存失败：%s", error[0] ? error : "请检查 SD 卡");
            publish(request.generation, worker_reply);
        }
    }
}

void process_request(const Request &request)
{
    if (request.save_only) { save_cached(request); return; }
    if (request.index < 0 && !get_wifi_connected()) { publish(request.generation, "WiFi 未连接，请先连接可上网的网络"); return; }
    Song *found = static_cast<Song *>(heap_caps_calloc(MAX_SONGS, sizeof(Song), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!found) { publish(request.generation, "歌曲准备失败：外部内存不足"); return; }
    unsigned count = 0;
    int selected = request.index;
    if (selected >= 0) {
        portENTER_CRITICAL(&state_mux);
        count = song_count;
        memcpy(found, songs, sizeof(songs));
        portEXIT_CRITICAL(&state_mux);
    } else {
        bool success = search(request, found, &count);
        if (!current(request.generation)) { free(found); return; }
        if (!success || !count) {
            portENTER_CRITICAL(&state_mux);
            if (current(request.generation)) { song_count = 0; selection = -1; publish_locked(success ? "网易云未找到匹配的歌曲" : "网易云搜索失败，请检查网络后重试"); }
            portEXIT_CRITICAL(&state_mux);
            free(found); return;
        }
        portENTER_CRITICAL(&state_mux);
        if (current(request.generation)) { memcpy(songs, found, sizeof(songs)); song_count = count; selection = request.search_only ? -1 : 0; }
        portEXIT_CRITICAL(&state_mux);
        selected = 0;
    }
    if (selected < 0 || static_cast<unsigned>(selected) >= count) { publish(request.generation, "该歌曲序号不在最近的搜索结果中"); free(found); return; }
    auto &reply = worker_reply;
    if (request.search_only) {
        size_t used = snprintf(reply, sizeof(reply), "网易云搜索结果：\n");
        for (unsigned i = 0; i < count && used < sizeof(reply) - 1; ++i) {
            int n = snprintf(reply + used, sizeof(reply) - used, "%u. %s - %s\n", i + 1, found[i].title, found[i].artist);
            if (n < 0 || static_cast<size_t>(n) >= sizeof(reply) - used) { used = sizeof(reply) - 1; break; }
            used += n;
        }
        if (used + 64 < sizeof(reply)) strlcat(reply, "输入“播放第1首”开始播放", sizeof(reply));
        publish(request.generation, reply);
        free(found); return;
    }
    const Song &song = found[selected];
    if (!sdcar_is_ready()) { publish(request.generation, "请插入 SD 卡，在线歌曲需要缓存后播放"); free(found); return; }
    snprintf(reply, sizeof(reply), "正在缓存：%s - %s\n准备期间可以输入“停止”取消", song.title, song.artist);
    publish(request.generation, reply);
    auto &path = worker_path;
    const char *reason = nullptr;
    if (!download(request, song, path, sizeof(path), &reason)) {
        publish(request.generation, reason ? reason : "歌曲无法播放：音源不可用、网络中断或 SD 卡空间不足；可以搜索后选择其他版本");
        free(found); return;
    }
    auto &title = worker_title;
    snprintf(title, sizeof(title), "%s - %s", song.title, song.artist);
    bool queued = false;
    portENTER_CRITICAL(&state_mux);
    if (current(request.generation)) {
        if (music_audio_queue_online(path, title, request.generation, prepare_paused)) {
            queued = true;
            selection = selected;
            remember_play(song, path);
            snprintf(reply, sizeof(reply), prepare_paused ? "已缓存，保持暂停：%s\n输入“继续播放”开始" : "已缓存，已提交播放：%s\n支持暂停、继续、停止、下一首", title);
            publish_locked(reply);
        } else publish_locked("播放未开始，请先关闭视频或等待 AI 语音播放结束");
    }
    portEXIT_CRITICAL(&state_mux);
    if (queued) music_audio_wake();
    free(found);
}
void worker(void *)
{
    for (;;) {
        Request request;
        portENTER_CRITICAL(&state_mux);
        if (!request_pending) {
            preparing = false;
            worker_running = false;
            worker_exited = app_millis();
            portEXIT_CRITICAL(&state_mux);
            break;
        }
        request = pending_request;
        request_pending = false;
        portEXIT_CRITICAL(&state_mux);
        process_request(request);
    }
    printf("[NETMUSIC] worker finished stack_free_min=%u\n", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    vTaskDelete(nullptr);
}
}

extern "C" void online_music_service_poll(void)
{
    portENTER_CRITICAL(&state_mux);
    const bool start = request_pending && !worker_running && app_millis() - worker_exited >= 100;
    if (start) worker_running = true;
    portEXIT_CRITICAL(&state_mux);
    if (start && xTaskCreatePinnedToCore(worker, "OnlineMusic", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
        portENTER_CRITICAL(&state_mux);
        worker_running = request_pending = preparing = false;
        publish_locked("在线点歌启动失败：内存不足，请关闭蓝牙后重试");
        portEXIT_CRITICAL(&state_mux);
    }
}
extern "C" void online_music_cancel_pending(void)
{
    portENTER_CRITICAL(&state_mux);
    generation.fetch_add(1);
    request_pending = false;
    upload_preparing = false;
    if (preparing) publish_locked("已取消歌曲搜索、缓存或 WAV 保存");
    preparing = false;
    portEXIT_CRITICAL(&state_mux);
}
extern "C" uint32_t online_music_get_result(char *reply, size_t capacity)
{
    portENTER_CRITICAL(&state_mux);
    if (reply && capacity) strlcpy(reply, latest_reply, capacity);
    const uint32_t version = reply_version;
    portEXIT_CRITICAL(&state_mux);
    return version;
}
extern "C" void online_music_playback_error(uint32_t job, const char *message)
{
    portENTER_CRITICAL(&state_mux);
    if (current(job)) publish_locked(message);
    portEXIT_CRITICAL(&state_mux);
}
extern "C" void online_music_note_pause(bool paused)
{
    portENTER_CRITICAL(&state_mux);
    prepare_paused = paused;
    portEXIT_CRITICAL(&state_mux);
}
extern "C" uint32_t online_music_begin_upload(void)
{
    portENTER_CRITICAL(&state_mux);
    const uint32_t job = generation.fetch_add(1) + 1;
    request_pending = preparing = false;
    upload_preparing = true;
    prepare_paused = false;
    music_audio_discard_online_queue();
    song_count = 0;
    selection = -1;
    publish_locked("正在接收手机上传的 MP3…");
    portEXIT_CRITICAL(&state_mux);
    return job;
}
extern "C" bool online_music_job_current(uint32_t job) { return current(job); }
extern "C" bool online_music_finish_upload(uint32_t job, const char *path, const char *title)
{
    bool queued = false;
    portENTER_CRITICAL(&state_mux);
    if (current(job)) {
        upload_preparing = false;
        queued = music_audio_queue_online(path, title, job, prepare_paused);
        publish_locked(queued ? "手机歌曲已接收，开始播放" : "歌曲未开始播放，请先关闭视频或等待 AI 语音结束");
    }
    portEXIT_CRITICAL(&state_mux);
    if (queued) music_audio_wake();
    return queued;
}
extern "C" void online_music_abort_upload(uint32_t job, const char *message)
{
    portENTER_CRITICAL(&state_mux);
    if (current(job)) { upload_preparing = false; publish_locked(message); }
    portEXIT_CRITICAL(&state_mux);
}
static bool handle_command(const char *text, bool save_to_library)
{
    const music_intent_t intent = music_intent_parse(text);
    bool active;
    unsigned count;
    int selected;
    portENTER_CRITICAL(&state_mux);
    active = preparing || upload_preparing;
    count = song_count;
    selected = selection;
    portEXIT_CRITICAL(&state_mux);
    active = active || music_audio_is_online();
    if (intent.kind == MUSIC_INTENT_NONE) return false;
    if (intent.kind >= MUSIC_INTENT_STOP && intent.kind <= MUSIC_INTENT_RESUME) {
        if (!active) return false;
        if (intent.kind == MUSIC_INTENT_RESUME && !music_audio_is_online()) {
            online_music_note_pause(false);
            publish(generation.load(), "歌曲尚在准备，完成后将自动播放；输入“停止”可以取消");
            return true;
        }
        if (intent.kind == MUSIC_INTENT_STOP) music_audio_stop();
        else if (intent.kind == MUSIC_INTENT_PAUSE) music_audio_pause();
        else music_audio_play();
        publish(generation.load(), intent.kind == MUSIC_INTENT_STOP ? "音乐已停止" : intent.kind == MUSIC_INTENT_PAUSE ? "音乐已暂停" : "继续播放音乐");
        return true;
    }
    int index = intent.index;
    if (intent.kind == MUSIC_INTENT_SELECT && !count && !intent.force_online) return false;
    if (intent.kind == MUSIC_INTENT_NEXT || intent.kind == MUSIC_INTENT_PREVIOUS) {
        if (!active) return false;
        if (!count) { publish(generation.load(), "请先搜索歌曲"); return true; }
        index = intent.kind == MUSIC_INTENT_NEXT ? (selected + 1) % count : (selected + count - 1) % count;
    }
    Request request = {};
    request.index = index;
    request.search_only = intent.kind == MUSIC_INTENT_SEARCH;
    request.save_library = false;
    (void)save_to_library;
    strlcpy(request.query, intent.query, sizeof(request.query));
    portENTER_CRITICAL(&state_mux);
    if (intent.kind == MUSIC_INTENT_NEXT || intent.kind == MUSIC_INTENT_PREVIOUS) {
        if (song_count) {
            const int base = selection >= 0 ? selection : (intent.kind == MUSIC_INTENT_NEXT ? -1 : 0);
            request.index = (base + (intent.kind == MUSIC_INTENT_NEXT ? 1 : static_cast<int>(song_count) - 1)) % song_count;
        }
    }
    if (request.index >= 0) selection = request.index;
    else { song_count = 0; selection = -1; }
    request.generation = generation.fetch_add(1) + 1;
    upload_preparing = false;
    prepare_paused = false;
    music_audio_discard_online_queue();
    pending_request = request;
    request_pending = preparing = true;
    publish_locked(request.index >= 0 ? "正在准备所选歌曲…" : "正在搜索网易云歌曲…");
    portEXIT_CRITICAL(&state_mux);
    return true;
}

extern "C" bool online_music_handle_command(const char *text) {return handle_command(text, false);}
extern "C" bool online_music_handle_web_command(const char *text, bool save) {return handle_command(text, save);}
extern "C" unsigned online_music_copy_songs(online_music_song_t *out, unsigned capacity)
{
    portENTER_CRITICAL(&state_mux);
    const unsigned count = song_count < capacity ? song_count : capacity;
    if (out && count) memcpy(out, songs, count * sizeof(*out));
    portEXIT_CRITICAL(&state_mux);
    return count;
}
extern "C" bool online_music_is_preparing(void)
{
    portENTER_CRITICAL(&state_mux);
    const bool active = preparing || upload_preparing;
    portEXIT_CRITICAL(&state_mux);
    return active;
}
extern "C" bool online_music_can_download_current(void)
{
    portENTER_CRITICAL(&state_mux);
    const bool busy = preparing || upload_preparing;
    char path[96];
    const bool ready = !busy && match_active(path, sizeof(path), nullptr);
    portEXIT_CRITICAL(&state_mux);
    return ready;
}
extern "C" bool online_music_download_current(void)
{
    portENTER_CRITICAL(&state_mux);
    bool accepted = false;
    char path[96];
    PlayMeta meta;
    if (!preparing && !upload_preparing && match_active(path, sizeof(path), &meta)) {
        Request request = {};
        request.save_only = true;
        request.index = -1;
        request.generation = generation.fetch_add(1) + 1;
        strlcpy(request.id, meta.id, sizeof(request.id));
        strlcpy(request.title, meta.title, sizeof(request.title));
        strlcpy(request.artist, meta.artist, sizeof(request.artist));
        strlcpy(request.path, meta.path, sizeof(request.path));
        pending_request = request;
        request_pending = preparing = true;
        publish_locked("正在把当前歌曲保存到歌单…");
        accepted = true;
    }
    portEXIT_CRITICAL(&state_mux);
    return accepted;
}

extern "C" int online_music_saved_track(char *path, size_t capacity)
{
    portENTER_CRITICAL(&state_mux);
    char actual_path[64];
    const int index = saved_local_index >= 0 && sdcar_copy_music_track(saved_local_index, actual_path, sizeof(actual_path), nullptr) &&
        !strcmp(actual_path, saved_local_path) ? saved_local_index : -1;
    if (path && capacity) strlcpy(path, index >= 0 ? saved_local_path : "", capacity);
    portEXIT_CRITICAL(&state_mux);
    return index;
}
