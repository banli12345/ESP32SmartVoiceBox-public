#include "AiSpeech.h"
#include "AiSpeechBuffer.h"
#include "AiVoice.h"
#include "Sound.h"
#include "Storage.h"
#include "SDcar.h"
#include "IdfRuntime.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {
constexpr size_t kHttpReadBytes = 4096;
constexpr size_t kCacheWriteBytes = 16u * 1024u;
constexpr size_t kPlayReadBytes = 4096;
constexpr size_t kPullBytes = 2048;
constexpr size_t kStereoBytes = 4096;
constexpr size_t kPrebufferBytes = 36000;
constexpr size_t kRingPreferred = 128u * 1024u;
constexpr size_t kRingFallback = 64u * 1024u;
constexpr size_t kStackBytes = 4096;
constexpr size_t kMaxAudioBytes = 24u * 1024u * 1024u;
constexpr uint32_t kTotalLimitMs = 600000;
constexpr uint32_t kStallLimitMs = 30000;
constexpr int kProdRun = 0;
constexpr int kProdOk = 1;
constexpr int kProdFail = 2;
std::atomic<bool> speech_cancelled{false};
std::atomic<bool> speech_running{false};
std::atomic<int> speech_phase{AI_SPEECH_IDLE};
std::atomic<uint32_t> cached_bytes{0}, expected_bytes{0};
enum { kMeterIdle = 0, kMeterLive = 1, kMeterFrozen = 2 };
portMUX_TYPE meter_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> meter_state{kMeterIdle};
std::atomic<uint32_t> meter_origin{0};
std::atomic<uint32_t> meter_frozen_elapsed{0};
std::atomic<uint32_t> meter_frozen_bps{0};

uint32_t cache_rate(uint32_t bytes, uint32_t elapsed_ms)
{
    if (elapsed_ms == 0) return 0;
    const uint64_t rate = (static_cast<uint64_t>(bytes) * 1000ull) / elapsed_ms;
    return rate > 0xffffffffull ? 0xffffffffu : static_cast<uint32_t>(rate);
}

void meter_write(uint32_t state, uint32_t origin, uint32_t elapsed, uint32_t bps)
{
    portENTER_CRITICAL(&meter_mux);
    meter_origin.store(origin, std::memory_order_relaxed);
    meter_frozen_elapsed.store(elapsed, std::memory_order_relaxed);
    meter_frozen_bps.store(bps, std::memory_order_relaxed);
    meter_state.store(state, std::memory_order_relaxed);
    portEXIT_CRITICAL(&meter_mux);
}

struct MeterView {
    uint32_t state;
    uint32_t origin;
    uint32_t elapsed;
    uint32_t bps;
};

MeterView meter_read(void)
{
    // Only four loads under the same short critical section as the writer.
    // A reader must not spin waiting for a preempted, lower-priority AI task.
    portENTER_CRITICAL(&meter_mux);
    MeterView view = {
        meter_state.load(std::memory_order_relaxed),
        meter_origin.load(std::memory_order_relaxed),
        meter_frozen_elapsed.load(std::memory_order_relaxed),
        meter_frozen_bps.load(std::memory_order_relaxed),
    };
    portEXIT_CRITICAL(&meter_mux);
    return view;
}

/* Network-phase modem boost. Destructor and release() both restore, and only once. */
struct WifiPsGuard {
    bool armed = false;
    wifi_ps_type_t saved = WIFI_PS_NONE;
    int from = -1;
    int boost = 0;
    int restored = 0;

    void engage(void)
    {
        wifi_ps_type_t mode = WIFI_PS_NONE;
        const esp_err_t got = esp_wifi_get_ps(&mode);
        if (got != ESP_OK) {
            printf("[AI DIRECT] wifi ps get failed err=%d\n", (int)got);
            return;
        }
        from = (int)mode;
        if (mode == WIFI_PS_NONE) return;
        const esp_err_t set = esp_wifi_set_ps(WIFI_PS_NONE);
        if (set != ESP_OK) {
            printf("[AI DIRECT] wifi ps set none failed err=%d\n", (int)set);
            return;
        }
        saved = mode;
        armed = true;
        boost = 1;
    }

    void release(void)
    {
        if (!armed) return;
        armed = false;
        esp_err_t err = ESP_OK;
        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            err = esp_wifi_set_ps(saved);
            if (err == ESP_OK) break;
            if (attempt < 2) app_delay_ms(5);
        }
        if (err != ESP_OK) {
            restored = 0;
            printf("[AI DIRECT] wifi ps restore failed err=%d\n", (int)err);
            return;
        }
        restored = 1;
    }

    ~WifiPsGuard() { release(); }
};
constexpr const char *kCacheDir = "/.cache";
constexpr const char *kCachePath = "/.cache/breeze-ai-speech.pcm";

struct SpeechHeaders { char type[128]; };

struct SpeechJob {
    AiSpeechRing *ring;
    int16_t *stereo;
    uint8_t *pull;
    TaskHandle_t producer;
    TaskHandle_t consumer;
    std::atomic<int> prod;
    std::atomic<bool> cons_fail;
    std::atomic<bool> cons_done;
    std::atomic<bool> play_ok;
    std::atomic<size_t> played;
    std::atomic<unsigned> refill;
    uint32_t began;
};

esp_err_t speech_header(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Content-Type")) {
        auto *headers = static_cast<SpeechHeaders *>(event->user_data);
        strlcpy(headers->type, event->header_value, sizeof(headers->type));
    }
    return ESP_OK;
}

bool deepseek_base(const char *base)
{
    static constexpr char origin[] = "https://api.deepseek.com";
    const size_t n = sizeof(origin) - 1;
    return base && !strncmp(base, origin, n) && (!base[n] || base[n] == '/');
}

bool pcm_type(const char *type)
{
    if (strncasecmp(type, "audio/pcm", 9) || (type[9] && type[9] != ';' && type[9] != ' ')) return false;
    for (const char *p = strchr(type, ';'); p; p = strchr(p, ';')) {
        ++p;
        while (*p == ' ') ++p;
        const char *end = strchr(p, ';');
        const size_t length = end ? (size_t)(end - p) : strlen(p);
        if ((!strncasecmp(p, "rate=", 5) && (length != 10 || strncmp(p + 5, "24000", 5))) ||
            (!strncasecmp(p, "bits=", 5) && (length != 7 || strncmp(p + 5, "16", 2))) ||
            (!strncasecmp(p, "channels=", 9) && (length != 10 || p[9] != '1'))) return false;
    }
    return true;
}

void expand_stereo(const uint8_t *mono, size_t even, int16_t *stereo)
{
    const size_t samples = even / 2;
    for (size_t i = 0; i < samples; ++i) {
        const int16_t sample = static_cast<int16_t>(static_cast<uint16_t>(mono[i * 2]) |
                                                     (static_cast<uint16_t>(mono[i * 2 + 1]) << 8));
        stereo[i * 2] = stereo[i * 2 + 1] = sample;
    }
}

/* Player task. Blocks instead of spinning. External delete retires it; it does not return on device. */
void tts_player(void *arg)
{
    auto *job = static_cast<SpeechJob *>(arg);
    bool open = false;
    bool ok = true;
    bool have_odd = false;
    uint8_t odd = 0;

    auto fail = [&]() {
        ok = false;
        job->cons_fail.store(true, std::memory_order_release);
        xTaskNotifyGive(job->producer);
    };
    auto timed_out = [&]() { return speech_cancelled.load() || app_millis() - job->began >= kTotalLimitMs; };

    while (job->prod.load(std::memory_order_acquire) == kProdRun &&
           ai_speech_ring_fill(job->ring) < kPrebufferBytes) {
        if (timed_out()) { fail(); break; }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
    }

    while (ok && !job->cons_fail.load(std::memory_order_acquire)) {
        if (timed_out()) { fail(); break; }
        const int state = job->prod.load(std::memory_order_acquire);
        if (state == kProdFail) { fail(); break; }

        uint8_t *dst = job->pull;
        size_t held = 0;
        if (have_odd) { dst[0] = odd; held = 1; have_odd = false; }
        const size_t got = ai_speech_ring_read(job->ring, dst + held, kPullBytes - held);
        const size_t available = held + got;
        if (available < 2) {
            if (available) { odd = dst[0]; have_odd = true; }
            if (state == kProdRun) {
                /* Source is still coming. Pace the I2S with silence so a DMA underrun
                   cannot replay the previous block. This is not a claim that underruns are zero. */
                job->refill.fetch_add(1, std::memory_order_relaxed);
                memset(job->stereo, 0, kStereoBytes);
                while (ok && job->prod.load(std::memory_order_acquire) == kProdRun &&
                       ai_speech_ring_fill(job->ring) < kPrebufferBytes) {
                    if (timed_out()) { fail(); break; }
                    if (open && !sound_ai_output_write(job->stereo, kStereoBytes)) { fail(); break; }
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(open ? 1 : 20));
                }
                if (!ok) break;
                continue;
            }
            if (available) fail();
            break;
        }
        const size_t even = available & ~size_t(1);
        const bool tail_odd = (available & 1u) && state != kProdRun && ai_speech_ring_fill(job->ring) == 0;
        if (tail_odd && !open) { fail(); break; }
        if (available & 1u) { odd = dst[even]; have_odd = true; }
        expand_stereo(dst, even, job->stereo);
        if (!open) {
            open = sound_ai_output_begin_rate(24000);
            if (!open) { fail(); break; }
        }
        if (!sound_ai_output_write(job->stereo, even * 2)) { fail(); break; }
        job->played.fetch_add(even, std::memory_order_relaxed);
        xTaskNotifyGive(job->producer);
        if (tail_odd) { fail(); break; }
    }

    if (ok && have_odd) fail();
    if (open) {
        if (!sound_ai_output_finish(ok)) ok = false;
    } else if (ok) {
        ok = false;
    }
    job->play_ok.store(ok, std::memory_order_release);
    job->cons_done.store(true, std::memory_order_release);
    xTaskNotifyGive(job->producer);
    vTaskDelay(portMAX_DELAY);
}

AiSpeechRing *make_ring(void)
{
    if (AiSpeechRing *ring = ai_speech_ring_create(kRingPreferred)) return ring;
    return ai_speech_ring_create(kRingFallback);
}

void join_player(SpeechJob &job, bool abort)
{
    if (!job.consumer) return;
    if (abort) job.prod.store(kProdFail, std::memory_order_release);
    xTaskNotifyGive(job.consumer);
    while (!job.cons_done.load(std::memory_order_acquire)) {
        if (speech_cancelled.load() || app_millis() - job.began >= kTotalLimitMs) {
            job.prod.store(kProdFail, std::memory_order_release);
            xTaskNotifyGive(job.consumer);
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
    // The consumer runs on the caller's core. Once this caller is running,
    // that task is no longer running; IDF vTaskDelete retires its static TCB
    // synchronously. Never kill it while Sound still owns I2S or an audio mutex.
    vTaskDelete(job.consumer);
    job.consumer = nullptr;
}
}

extern "C" bool ai_direct_speak(const ai_api_config_t *config, const char *text, char *error, size_t capacity)
{
    if (error && capacity) error[0] = 0;
    auto fail = [&](const char *message) { if (error && capacity && !error[0]) strlcpy(error, message, capacity); };
    bool idle = false;
    if (!speech_running.compare_exchange_strong(idle, true)) {fail("语音任务正忙，请稍后重试"); return false;}
    speech_cancelled.store(false);
    cached_bytes.store(0); expected_bytes.store(0);
    const uint32_t cache_entered = app_millis();
    meter_write(kMeterLive, cache_entered, 0, 0);
    speech_phase.store(AI_SPEECH_CACHING, std::memory_order_release);
    const char *voice = "-";
    bool ok = false, created = false;
    size_t total = 0, played = 0, peak = 0;
    unsigned refill = 0, eagain = 0, sd_writes = 0;
    uint32_t read_max = 0, full_wait = 0;
    uint32_t ttfb_ms = 0, recv_ms = 0, http_wait_ms = 0, sd_wait_ms = 0, flush_ms = 0;
    uint32_t cache_bps = 0, audio_ms = 0, dl_vs_audio_x1000 = 0;
    char authorization[sizeof(config->api_key) + 8] = {};
    char *body = nullptr;
    uint8_t *read_buf = nullptr, *pull = nullptr;
    int16_t *stereo = nullptr;
    StackType_t *stack = nullptr;
    StaticTask_t *tcb = nullptr;
    AiSpeechRing *ring = nullptr;
    esp_http_client_handle_t client = nullptr;
    StorageStream cache;
    SpeechJob job = {};
    WifiPsGuard ps_guard;

    if (!config || !text || !*text || !deepseek_base(config->api_base)) {
        fail("文字已回复，当前服务未配置语音接口"); goto cleanup;
    }
    {
        const char *snap = ai_voice_get_id();
        if (snap && snap[0]) voice = snap;
    }
    if (!sdcar_is_ready() || (!storage_is_dir(kCacheDir) && !storage_mkdir(kCacheDir))) {
        fail("文字已回复，完整语音缓存需要可写的SD卡"); goto cleanup;
    }
    if (storage_exists(kCachePath) && !storage_remove(kCachePath)) {
        fail("文字已回复，无法清理旧语音缓存"); goto cleanup;
    }
    if (!cache.open(kCachePath, "wb")) {fail("文字已回复，无法创建SD语音缓存"); goto cleanup;}
    created = true;
    read_buf = static_cast<uint8_t *>(heap_caps_malloc(kCacheWriteBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!read_buf) {fail("文字已回复，语音内存不足"); goto cleanup;}
    {
        cJSON *request = cJSON_CreateObject();
        bool built = request && cJSON_AddStringToObject(request, "text", text) &&
            cJSON_AddStringToObject(request, "voice_id", voice) &&
            cJSON_AddStringToObject(request, "format", "pcm") &&
            cJSON_AddBoolToObject(request, "filter_markdown", true);
        body = built ? cJSON_PrintUnformatted(request) : nullptr;
        cJSON_Delete(request);
    }
    if (!body) {fail("文字已回复，语音请求内存不足"); goto cleanup;}
    snprintf(authorization, sizeof(authorization), "Bearer %s", config->api_key);
    {
        ps_guard.engage();
        SpeechHeaders headers = {};
        esp_http_client_config_t cfg = {};
        cfg.url = "https://api.deepseek.com/v1/tools/tts";
        cfg.method = HTTP_METHOD_POST; cfg.addr_type = HTTP_ADDR_TYPE_INET;
        cfg.timeout_ms = 12000; cfg.buffer_size = (int)kHttpReadBytes; cfg.buffer_size_tx = 1024;
        cfg.disable_auto_redirect = true; cfg.crt_bundle_attach = esp_crt_bundle_attach;
        cfg.event_handler = speech_header; cfg.user_data = &headers;
        client = esp_http_client_init(&cfg);
        if (!client) goto cleanup;
        // Set both headers before open(), which sends the HTTP request headers.
        const uint32_t request_began = app_millis();
        if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
            esp_http_client_set_header(client, "Authorization", authorization) != ESP_OK ||
            esp_http_client_open(client, (int)strlen(body)) != ESP_OK) goto cleanup;
        const uint32_t began = app_millis();
        for (size_t sent = 0; sent < strlen(body);) {
            if (speech_cancelled.load() || app_millis() - began >= kTotalLimitMs) goto cleanup;
            const int n = esp_http_client_write(client, body + sent, (int)(strlen(body) - sent));
            if (n <= 0) goto cleanup;
            sent += n;
        }
        esp_http_client_set_timeout_ms(client, 30000);
        const int64_t length = esp_http_client_fetch_headers(client);
        ttfb_ms = app_millis() - request_began;
        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            if (error && capacity) snprintf(error, capacity, "文字已回复，语音请求失败 (HTTP %d)", status);
            goto cleanup;
        }
        if (!pcm_type(headers.type)) {fail("文字已回复，语音格式不受支持"); goto cleanup;}
        if (length > (int64_t)kMaxAudioBytes) {fail("语音超过24MiB缓存上限"); goto cleanup;}
        if (length > 0) expected_bytes.store((uint32_t)length);
        esp_http_client_set_timeout_ms(client, 1000);
        uint32_t progress = app_millis();
        size_t pending = 0;
        const uint32_t recv_began = app_millis();
        bool recv_open = true;
        bool recv_fail = false;
        auto end_recv = [&]() {
            if (!recv_open) return;
            recv_ms = app_millis() - recv_began;
            recv_open = false;
            audio_ms = (uint32_t)((total * 1000ull) / 48000ull);
            cache_bps = recv_ms ? (uint32_t)((total * 1000ull) / recv_ms) : 0;
            dl_vs_audio_x1000 = audio_ms ? (uint32_t)(((uint64_t)recv_ms * 1000ull) / audio_ms) : 0;
        };
        auto write_pending = [&]() {
            if (!pending) return true;
            const uint32_t t0 = app_millis();
            const size_t wrote = cache.write(read_buf, pending);
            sd_wait_ms += app_millis() - t0;
            ++sd_writes;
            if (wrote != pending) {fail("SD语音缓存写入失败，请检查空间或存储卡"); return false;}
            total += pending;
            cached_bytes.store((uint32_t)total);
            pending = 0;
            return true;
        };
        for (;;) {
            if (speech_cancelled.load() || app_millis() - began >= kTotalLimitMs ||
                app_millis() - progress >= kStallLimitMs) {recv_fail = true; break;}
            const size_t room = (kCacheWriteBytes - pending) < kHttpReadBytes ? (kCacheWriteBytes - pending) : kHttpReadBytes;
            const uint32_t t0 = app_millis();
            const int got = esp_http_client_read(client, reinterpret_cast<char *>(read_buf + pending), (int)room);
            const uint32_t waited = app_millis() - t0;
            http_wait_ms += waited;
            if (waited > read_max) read_max = waited;
            if (got == -ESP_ERR_HTTP_EAGAIN) {++eagain; app_delay_ms(10); continue;}
            if (got < 0) {recv_fail = true; break;}
            if (got > 0) {
                if ((size_t)got > kMaxAudioBytes - total - pending) {fail("语音超过24MiB缓存上限"); recv_fail = true; break;}
                pending += (size_t)got;
                progress = app_millis();
            }
            const bool complete = esp_http_client_is_complete_data_received(client);
            if ((pending == kCacheWriteBytes || (complete && pending)) && !write_pending()) {recv_fail = true; break;}
            if (complete) {
                if (!total || (total & 1u) || (length > 0 && (uint64_t)length != total)) {
                    fail("语音缓存不完整，未开始播放"); recv_fail = true;
                }
                break;
            }
            if (!got) {fail("语音下载中断，未开始播放"); recv_fail = true; break;}
        }
        end_recv();
        if (recv_fail) goto cleanup;
    }
    // Release TLS/network memory before creating the audio worker. Playback
    // reads this completed local file and never calls the HTTP client again.
    esp_http_client_close(client); esp_http_client_cleanup(client); client = nullptr;
    cJSON_free(body); body = nullptr;
    memset(authorization, 0, sizeof(authorization));
    if (speech_cancelled.load()) goto cleanup;
    {
        const uint32_t flush_began = app_millis();
        const bool flushed = cache.flush();
        flush_ms = app_millis() - flush_began;
        if (!flushed) {fail("SD语音缓存写入失败，请检查存储卡"); goto cleanup;}
    }
    cache.close();
    if (!cache.open(kCachePath, "rb") || cache.byte_count() != total) {
        fail("SD语音缓存校验失败，未开始播放"); goto cleanup;
    }
    {
        const uint32_t cache_elapsed = app_millis() - cache_entered;
        meter_write(kMeterFrozen, cache_entered, cache_elapsed, cache_rate((uint32_t)total, cache_elapsed));
    }
    ps_guard.release();
    printf("[AI DIRECT] cache_pace voice=%s bytes=%u ttfb_ms=%u recv_ms=%u http_wait_ms=%u sd_wait_ms=%u sd_writes=%u flush_ms=%u bps=%u audio_ms=%u dl_vs_audio_x1000=%u ps_from=%d boost=%d restore=%d\n",
           voice, (unsigned)total, (unsigned)ttfb_ms, (unsigned)recv_ms, (unsigned)http_wait_ms,
           (unsigned)sd_wait_ms, sd_writes, (unsigned)flush_ms, (unsigned)cache_bps,
           (unsigned)audio_ms, (unsigned)dl_vs_audio_x1000, ps_guard.from, ps_guard.boost, ps_guard.restored);
    app_log_memory("AI cache ready, before playback");
    ring = make_ring();
    pull = static_cast<uint8_t *>(heap_caps_malloc(kPullBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    stereo = static_cast<int16_t *>(heap_caps_malloc(kStereoBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    stack = static_cast<StackType_t *>(heap_caps_malloc(kStackBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    tcb = static_cast<StaticTask_t *>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!ring || !pull || !stereo || !stack || !tcb) {
        printf("[AI MEMORY] play_alloc ring=%u pull=%u stereo=%u stack=%u tcb=%u\n",
               (unsigned)(ring != nullptr), (unsigned)(pull != nullptr), (unsigned)(stereo != nullptr),
               (unsigned)(stack != nullptr), (unsigned)(tcb != nullptr));
        app_log_memory("AI playback allocation failed");
        fail(!stack || !tcb ? "语音已缓存，播放任务内存不足" : "语音已缓存，播放缓冲内存不足");
        goto cleanup;
    }
    job.ring = ring; job.stereo = stereo; job.pull = pull;
    job.producer = xTaskGetCurrentTaskHandle(); job.began = app_millis();
    job.prod.store(kProdRun); job.cons_fail.store(false); job.cons_done.store(false);
    job.play_ok.store(false); job.played.store(0); job.refill.store(0);
    if (!job.producer) {fail("语音播放任务创建失败"); goto cleanup;}
    job.consumer = xTaskCreateStaticPinnedToCore(tts_player, "AiTtsPlay", kStackBytes, &job, 3, stack, tcb, xPortGetCoreID());
    if (!job.consumer) {fail("语音播放任务创建失败"); goto cleanup;}
    speech_phase.store(AI_SPEECH_PLAYING, std::memory_order_release);
    ok = true;
    for (size_t fed = 0; ok && fed < total;) {
        if (speech_cancelled.load() || job.cons_fail.load() || app_millis() - job.began >= kTotalLimitMs) {ok = false; break;}
        const size_t want = total - fed < kPlayReadBytes ? total - fed : kPlayReadBytes;
        const size_t got = cache.read(read_buf, want);
        if (!got) {fail("SD语音缓存读取失败"); ok = false; break;}
        size_t off = 0;
        while (off < got) {
            if (speech_cancelled.load() || job.cons_fail.load() || app_millis() - job.began >= kTotalLimitMs) {ok = false; break;}
            const size_t n = ai_speech_ring_write(ring, read_buf + off, got - off);
            if (!n) {
                const uint32_t began = app_millis();
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(20));
                const uint32_t waited = app_millis() - began;
                if (waited > full_wait) full_wait = waited;
                continue;
            }
            off += n; xTaskNotifyGive(job.consumer);
        }
        fed += off;
    }
    if (speech_cancelled.load()) ok = false;
    job.prod.store(ok ? kProdOk : kProdFail, std::memory_order_release);
    join_player(job, false);
    ok = ok && job.play_ok.load() && job.cons_done.load();
    played = job.played.load(); refill = job.refill.load(); peak = ai_speech_ring_peak(ring);
cleanup:
    ps_guard.release();
    join_player(job, true);
    if (client) {esp_http_client_close(client); esp_http_client_cleanup(client);}
    cache.close();
    if (created && !storage_remove(kCachePath)) {
        fail("语音临时缓存清理失败，下次播报前将重试，请检查SD卡");
        puts("[AI DIRECT] cache cleanup failed");
    }
    ai_speech_ring_destroy(ring);
    heap_caps_free(read_buf); heap_caps_free(pull); heap_caps_free(stereo);
    heap_caps_free(stack); heap_caps_free(tcb); cJSON_free(body);
    memset(authorization, 0, sizeof(authorization));
    if (!ok && (!error || !capacity || !error[0])) fail(speech_cancelled.load() ? "播报已停止，文字已保留" : "文字已回复，语音缓存或播放失败");
    printf("[AI DIRECT] speech cache=sd voice=%s recv=%u play=%u peak=%u refill=%u read_max_ms=%u full_wait_ms=%u eagain=%u ok=%d ttfb_ms=%u recv_ms=%u http_wait_ms=%u sd_wait_ms=%u sd_writes=%u flush_ms=%u bps=%u audio_ms=%u dl_vs_audio_x1000=%u\n",
           voice, (unsigned)total, (unsigned)played, (unsigned)peak, refill, (unsigned)read_max, (unsigned)full_wait, eagain, ok ? 1 : 0,
           (unsigned)ttfb_ms, (unsigned)recv_ms, (unsigned)http_wait_ms, (unsigned)sd_wait_ms, sd_writes,
           (unsigned)flush_ms, (unsigned)cache_bps, (unsigned)audio_ms, (unsigned)dl_vs_audio_x1000);
    meter_write(kMeterIdle, 0, 0, 0);
    speech_phase.store(AI_SPEECH_IDLE, std::memory_order_release);
    speech_running.store(false, std::memory_order_release);
    return ok;
}

extern "C" void ai_speech_cancel(void) { speech_cancelled.store(true); }
extern "C" void ai_speech_get_status(ai_speech_status_t *status)
{
    if (!status) return;
    status->phase = static_cast<ai_speech_phase_t>(speech_phase.load(std::memory_order_acquire));
    status->cached_bytes = cached_bytes.load(std::memory_order_acquire);
    status->expected_bytes = expected_bytes.load(std::memory_order_acquire);
    status->cache_elapsed_ms = 0;
    status->cache_bytes_per_second = 0;
    if (status->phase == AI_SPEECH_IDLE) return;
    const MeterView meter = meter_read();
    if (status->phase == AI_SPEECH_PLAYING || meter.state == kMeterFrozen) {
        status->cache_elapsed_ms = meter.elapsed;
        status->cache_bytes_per_second = meter.bps;
        return;
    }
    const uint32_t elapsed = app_millis() - meter.origin;
    status->cache_elapsed_ms = elapsed;
    status->cache_bytes_per_second = cache_rate(status->cached_bytes, elapsed);
}

extern "C" bool ai_speech_clear_cache(void)
{
    bool idle = false;
    if (!speech_running.compare_exchange_strong(idle, true)) return false;
    const bool ok = sdcar_is_ready() && storage_remove(kCachePath);
    speech_running.store(false, std::memory_order_release);
    return ok;
}
