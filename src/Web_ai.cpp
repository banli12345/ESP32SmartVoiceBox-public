#include "Web_ai.h"
#include "setwifi.h"
#include "SDcar.h"
#include "Sound.h"
#include "OnlineMusic.h"
#include "music_intent.h"
#include "NetworkWs.h"
#include "IdfRuntime.h"
#include "AiConfig.h"
#include "AiDirectClient.h"
#include "AiSpeech.h"
#include "AlarmAI.h"
#include <atomic>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

static const char *TAG = "web_ai";
static constexpr uint32_t AI_I2S_SAMPLE_RATE = 16000;
static constexpr size_t AI_WS_MESSAGE_CAP = 8192;
static constexpr int AI_HTTP_BEGIN_FAILED = -1000000;
static const char *AI_CHAT_URL = "http://YOUR_SERVER_HOST:3000/api/chat";
static const char *AI_WS_URL = "ws://YOUR_SERVER_HOST:3000/esp32";

static TaskHandle_t ai_chat_task_handle = nullptr;
static std::atomic<bool> ai_chat_pending{false}, ai_chat_busy{false};
static portMUX_TYPE ai_request_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t ai_result_mutex;
static uint32_t ai_result_revision;
static char ai_chat_request[513] = {};
static EXT_RAM_BSS_ATTR char ai_chat_response[8193];
/* Owned only by AIChatTask. Local music commands may publish a different UI
 * reply while speech is streaming, so the network/speech text stays separate. */
static EXT_RAM_BSS_ATTR char ai_direct_response[8193];
static EXT_RAM_BSS_ATTR char ai_previous_question[513], ai_previous_answer[8193];
static char ai_chat_error[128] = {};
static volatile int ai_chat_result_state = 0;

static uint32_t publish_result(const char *answer, const char *error, int state)
{
    if (!ai_result_mutex || xSemaphoreTake(ai_result_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return 0;
    if (answer) strlcpy(ai_chat_response, answer, sizeof(ai_chat_response));
    strlcpy(ai_chat_error, error ? error : "", sizeof(ai_chat_error));
    ai_chat_result_state = state;
    if (++ai_result_revision == 0) ++ai_result_revision;
    const uint32_t revision = ai_result_revision;
    xSemaphoreGive(ai_result_mutex);
    return revision;
}

static void publish_speech_error(uint32_t revision, const char *error)
{
    if (!error || !*error || !ai_result_mutex || xSemaphoreTake(ai_result_mutex, pdMS_TO_TICKS(20)) != pdTRUE) return;
    if (ai_result_revision == revision) strlcpy(ai_chat_error, error, sizeof(ai_chat_error));
    xSemaphoreGive(ai_result_mutex);
}

static esp_websocket_client_handle_t ai_websocket = nullptr;
static TaskHandle_t ai_ws_dispatch_task_handle = nullptr;
static volatile bool ai_websocket_connected = false;
static network_ws_text_t ai_ws_text = {};
static char *ai_ws_slots[2] = {};
static volatile uint8_t ai_ws_slot_busy[2] = {};
static QueueHandle_t ai_ws_queue = nullptr;
static portMUX_TYPE ai_ws_mux = portMUX_INITIALIZER_UNLOCKED;
static SemaphoreHandle_t ai_play_mutex = nullptr;

static bool play_ai_wav(const char *url);
static bool handle_music_command(const char *text);
static bool handle_volume_command(const char *text);
static bool ai_is_weather_request(const char *text);
static void format_weather_reply(char *buffer, size_t buffer_size);
static bool handle_weather_command(const char *text);
static void music_set_ai_reply(const char *message);
static void ai_ws_handle_text(const char *payload);

static char *alloc_buffer(size_t bytes)
{
    void *memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = malloc(bytes);
    if (memory) memset(memory, 0, bytes);
    return static_cast<char *>(memory);
}

static int clamp_int(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static const char *json_string(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItem(object, key) : nullptr;
    if (cJSON_IsString(item) && item->valuestring) return item->valuestring;
    return "";
}

static bool json_flag(const cJSON *object, const char *key)
{
    const cJSON *item = object ? cJSON_GetObjectItem(object, key) : nullptr;
    if (cJSON_IsTrue(item)) return true;
    return cJSON_IsNumber(item) && item->valuedouble != 0;
}

static bool read_exact(esp_http_client_handle_t client, uint8_t *buffer, size_t size, uint32_t timeout_ms)
{
    size_t total = 0;
    uint32_t last_progress = app_millis();
    while (total < size) {
        if (app_millis() - last_progress >= timeout_ms) return false;
        const int count = esp_http_client_read(client, reinterpret_cast<char *>(buffer + total),
                                                static_cast<int>(size - total));
        if (count == -ESP_ERR_HTTP_EAGAIN) {
            app_delay_ms(10);
            continue;
        }
        if (count <= 0) return false;
        total += static_cast<size_t>(count);
        last_progress = app_millis();
    }
    return true;
}

static bool skip_bytes(esp_http_client_handle_t client, uint32_t size, uint32_t deadline)
{
    uint8_t discard[64];
    while (size > 0) {
        const size_t request = size < sizeof(discard) ? size : sizeof(discard);
        if (!read_exact(client, discard, request, deadline)) return false;
        size -= static_cast<uint32_t>(request);
    }
    return true;
}

static uint32_t read_le32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

static bool play_ai_wav(const char *url)
{
    if (!url || !*url) return false;
    if (ai_play_mutex) xSemaphoreTake(ai_play_mutex, portMAX_DELAY);

    bool result = false;
    bool output_open = false;
    bool success = false;
    bool fmt_found = false;
    bool data_found = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    uint32_t data_bytes = 0;
    uint32_t read_timeout_ms = 0;
    uint8_t riff_header[12] = {};
    esp_http_client_handle_t client = nullptr;
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 12000;
    config.buffer_size = 1024;
    if (strncmp(url, "https://", 8) == 0) config.crt_bundle_attach = esp_crt_bundle_attach;
    client = esp_http_client_init(&config);
    if (!client || esp_http_client_open(client, 0) != ESP_OK) goto done;
    esp_http_client_set_timeout_ms(client, 30000);
    if (esp_http_client_fetch_headers(client) < 0 && esp_http_client_get_status_code(client) <= 0) goto done;
    if (esp_http_client_get_status_code(client) != 200) {
        ESP_LOGW(TAG, "AI audio HTTP error: %d", esp_http_client_get_status_code(client));
        goto done;
    }
    // A timeout bounds stalled reads, independently of the audio's duration.
    read_timeout_ms = 30000;
    if (!read_exact(client, riff_header, sizeof(riff_header), read_timeout_ms) ||
        memcmp(riff_header, "RIFF", 4) != 0 || memcmp(riff_header + 8, "WAVE", 4) != 0) {
        ESP_LOGW(TAG, "AI audio: unsupported WAV container");
        goto done;
    }
    for (uint8_t chunk_index = 0; chunk_index < 32 && !data_found; ++chunk_index) {
        uint8_t chunk_header[8];
        if (!read_exact(client, chunk_header, sizeof(chunk_header), read_timeout_ms)) break;
        const uint32_t chunk_size = read_le32(chunk_header + 4);
        const uint32_t padded_size = chunk_size + (chunk_size & 1U);
        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            uint8_t fmt_data[16];
            if (chunk_size < 16 || chunk_size > 4096 ||
                !read_exact(client, fmt_data, sizeof(fmt_data), read_timeout_ms)) break;
            audio_format = static_cast<uint16_t>(fmt_data[0]) | (static_cast<uint16_t>(fmt_data[1]) << 8);
            channels = static_cast<uint16_t>(fmt_data[2]) | (static_cast<uint16_t>(fmt_data[3]) << 8);
            sample_rate = read_le32(fmt_data + 4);
            bits_per_sample = static_cast<uint16_t>(fmt_data[14]) | (static_cast<uint16_t>(fmt_data[15]) << 8);
            if (!skip_bytes(client, padded_size - 16, read_timeout_ms)) break;
            fmt_found = true;
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            if (!fmt_found || chunk_size == 0) break;
            data_bytes = chunk_size;
            data_found = true;
        } else if (!skip_bytes(client, padded_size, read_timeout_ms)) {
            break;
        }
    }
    if (!data_found || audio_format != 1 || channels != 1 || bits_per_sample != 16) {
        ESP_LOGW(TAG, "AI audio: unsupported WAV PCM format");
        goto done;
    }
    if (sample_rate != AI_I2S_SAMPLE_RATE) {
        ESP_LOGW(TAG, "AI audio: unexpected sample rate %lu", static_cast<unsigned long>(sample_rate));
        goto done;
    }
    if (data_bytes == 0 || (data_bytes & 1U) != 0) {
        ESP_LOGW(TAG, "AI audio: invalid WAV data length");
        goto done;
    }
    if (!sound_ai_output_begin()) goto done;
    output_open = true;
    {
        uint8_t mono_pcm[2048];
        static EXT_RAM_BSS_ATTR int16_t stereo_pcm[2048];
        uint32_t remaining = data_bytes;
        success = true;
        while (remaining > 0) {
            size_t amount = remaining < sizeof(mono_pcm) ? remaining : sizeof(mono_pcm);
            amount &= ~static_cast<size_t>(1);
            if (amount == 0 || !read_exact(client, mono_pcm, amount, read_timeout_ms)) {
                ESP_LOGW(TAG, "AI audio: WAV data stream ended early");
                success = false;
                break;
            }
            remaining -= static_cast<uint32_t>(amount);
            const size_t sample_count = amount / 2;
            for (size_t index = 0; index < sample_count; ++index) {
                const int16_t sample = static_cast<int16_t>(
                    static_cast<uint16_t>(mono_pcm[index * 2]) |
                    (static_cast<uint16_t>(mono_pcm[index * 2 + 1]) << 8));
                stereo_pcm[index * 2] = sample;
                stereo_pcm[index * 2 + 1] = sample;
            }
            if (!sound_ai_output_write(stereo_pcm, sample_count * sizeof(int16_t) * 2)) {
                success = false;
                break;
            }
        }
        if (remaining != 0) success = false;
    }
done:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    if (output_open) {
        if (success) {
            vTaskDelay(pdMS_TO_TICKS(100));
            ESP_LOGI(TAG, "AI audio playback finished");
        }
        result = sound_ai_output_finish(success);
    }
    if (ai_play_mutex) xSemaphoreGive(ai_play_mutex);
    return result;
}

static bool ai_is_weather_request(const char *text)
{
    if (!text || !*text) return false;
    return strstr(text, "天气") || strstr(text, "天气情况") || strstr(text, "气温") ||
           strstr(text, "温度") || strstr(text, "下雨") || strstr(text, "weather") ||
           strstr(text, "temperature");
}

static void format_weather_reply(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    snprintf(buffer, buffer_size, "当前%s天气：%s，温度%s摄氏度。",
             WEATHER_CITY_DISPLAY[0] ? WEATHER_CITY_DISPLAY : WEATHER_CITY,
             weather ? weather : "暂无天气数据", temp ? temp : "暂无温度数据");
}

static bool handle_weather_command(const char *text)
{
    if (!ai_is_weather_request(text)) return false;
    char reply[256];
    format_weather_reply(reply, sizeof(reply));
    music_set_ai_reply(reply);
    ESP_LOGI(TAG, "AI weather command: %s", reply);
    return true;
}

static bool handle_volume_command(const char *text)
{
    if (!text || !*text) return false;
    const bool volume_word = strstr(text, "音量") || strstr(text, "声音") ||
                             strstr(text, "volume") || strstr(text, "sound");
    if (!volume_word) return false;

    int target = -1;
    const char *scan = strstr(text, "音量");
    if (!scan) scan = strstr(text, "volume");
    if (!scan) scan = text;
    for (const char *p = scan; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            target = atoi(p);
            break;
        }
    }
    const bool quieter = strstr(text, "小一点") || strstr(text, "小声") || strstr(text, "调低") ||
                         strstr(text, "降低") || strstr(text, "quiet") || strstr(text, "down");
    const bool louder = strstr(text, "大一点") || strstr(text, "大声") || strstr(text, "调高") ||
                        strstr(text, "增大") || strstr(text, "loud") || strstr(text, "up");
    if (target < 0) {
        if (strstr(text, "静音") || strstr(text, "mute")) target = 0;
        else if (quieter) target = static_cast<int>(get_audio_volume()) - 10;
        else if (louder) target = static_cast<int>(get_audio_volume()) + 10;
        else return false;
    }
    target = clamp_int(target, 0, 100);
    set_audio_volume(target);
    save_audio_volume();
    char reply[64];
    snprintf(reply, sizeof(reply), "音量已设置为 %d%%", target);
    music_set_ai_reply(reply);
    ESP_LOGI(TAG, "AI volume command: %d%%", target);
    return true;
}

static void build_ai_server_prompt(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    if (ai_is_weather_request(ai_chat_request) && (weather || temp)) {
        snprintf(buffer, buffer_size,
                 "请直接用中文播报以下设备天气结果，不要说无法获取实时天气，也不要要求用户查看手机：城市%s，天气%s，温度%s摄氏度。用户问题是：%s。",
                 WEATHER_CITY_DISPLAY[0] ? WEATHER_CITY_DISPLAY : WEATHER_CITY,
                 weather ? weather : "暂无天气数据", temp ? temp : "暂无温度数据", ai_chat_request);
    } else {
        strlcpy(buffer, ai_chat_request, buffer_size);
    }
}

static int ai_http_post(const char *body, char *response, size_t response_size)
{
    if (response && response_size) response[0] = '\0';
    esp_http_client_config_t config = {};
    config.url = AI_CHAT_URL;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 12000;
    config.buffer_size = 1024;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return AI_HTTP_BEGIN_FAILED;
    int status = -1;
    const int body_len = static_cast<int>(strlen(body));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (esp_http_client_open(client, body_len) != ESP_OK) {
        esp_http_client_cleanup(client);
        return -1;
    }
    esp_http_client_set_timeout_ms(client, 60000);
    int written = 0;
    while (written < body_len) {
        const int count = esp_http_client_write(client, body + written, body_len - written);
        if (count <= 0) {
            status = -1;
            goto done;
        }
        written += count;
    }
    if (esp_http_client_fetch_headers(client) < 0 && esp_http_client_get_status_code(client) <= 0) {
        status = -1;
        goto done;
    }
    status = esp_http_client_get_status_code(client);
    if (status == 200 && response && response_size > 1) {
        size_t used = 0;
        const uint32_t deadline = app_millis() + 60000;
        while (used + 1 < response_size && (int32_t)(app_millis() - deadline) < 0) {
            const int count = esp_http_client_read(client, response + used,
                                                    static_cast<int>(response_size - 1 - used));
            if (count == -ESP_ERR_HTTP_EAGAIN) {
                app_delay_ms(10);
                continue;
            }
            if (count <= 0) break;
            used += static_cast<size_t>(count);
        }
        response[used] = '\0';
    }
done:
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return status;
}

static void keep_weather_reply(const char *reason)
{
    char weather_reply[256];
    format_weather_reply(weather_reply, sizeof(weather_reply));
    music_set_ai_reply(weather_reply);
    ESP_LOGI(TAG, "%s", reason);
}

static void ai_chat_task(void *parameter)
{
    (void)parameter;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&ai_request_mux);
        const bool pending = ai_chat_pending.load();
        if (pending) {ai_chat_busy.store(true); ai_chat_pending.store(false);}
        portEXIT_CRITICAL(&ai_request_mux);
        if (!pending) continue;
        publish_result(nullptr, "", 0);
        if (handle_music_command(ai_chat_request)) {ai_chat_busy.store(false); continue;}
        char local_reply[512];
        if (alarm_ai_handle(ai_chat_request, local_reply, sizeof(local_reply))) {
            const uint32_t revision = publish_result(local_reply, "", 1);
            ai_api_config_t voice = {};
            if (get_wifi_connected() && ai_config_get(&voice) && voice.speak) {
                char speech_error[128] = {};
                ai_direct_speak(&voice, local_reply, speech_error, sizeof(speech_error));
                publish_speech_error(revision, speech_error);
            }
            memset(&voice, 0, sizeof(voice));
            ai_chat_busy.store(false); continue;
        }
        const bool weather_command = handle_weather_command(ai_chat_request);
        if (handle_volume_command(ai_chat_request)) {ai_chat_busy.store(false); continue;}
        if (!get_wifi_connected()) {
            if (!weather_command) publish_result(nullptr, "WiFi 未连接", -1);
            ai_chat_busy.store(false); continue;
        }
        ai_api_config_t direct = {};
        if (!ai_config_get(&direct)) {
            if (!weather_command) publish_result(nullptr, ai_config_status(), -1);
            ai_chat_busy.store(false); continue;
        }
        /* The weather context can now include a 95-byte Chinese city label. */
        char prompt[1024], error[128] = {};
        build_ai_server_prompt(prompt, sizeof(prompt));
        const bool ok = ai_direct_chat_context(&direct, prompt, ai_previous_question, ai_previous_answer,
            ai_direct_response, sizeof(ai_direct_response), error, sizeof(error));
        if (ok) {
            if (weather_command) format_weather_reply(ai_direct_response, sizeof(ai_direct_response));
            strlcpy(ai_previous_question, ai_chat_request, sizeof(ai_previous_question));
            strlcpy(ai_previous_answer, ai_direct_response, sizeof(ai_previous_answer));
            const uint32_t revision = publish_result(ai_direct_response, "", 1);
            if (direct.speak && ai_direct_response[0]) {
                error[0] = 0;
                ai_direct_speak(&direct, ai_direct_response, error, sizeof(error));
                publish_speech_error(revision, error);
            }
        } else if (!weather_command) publish_result(nullptr, error, -1);
        memset(&direct, 0, sizeof(direct));
        ai_chat_busy.store(false);
    }
}

extern "C" bool ai_chat_submit(const char *message)
{
    if (!message || !*message) return false;
    if (!strcmp(message, "停止播报") || !strcmp(message, "停止语音")) {
        ai_speech_cancel(); return true;
    }
    if (handle_music_command(message)) return true;
    if (!ai_chat_task_handle) return false;
    portENTER_CRITICAL(&ai_request_mux);
    const bool accepted = !ai_chat_busy.load() && !ai_chat_pending.load();
    if (accepted) {
        strlcpy(ai_chat_request, message, sizeof(ai_chat_request));
        ai_chat_pending.store(true);
    }
    portEXIT_CRITICAL(&ai_request_mux);
    if (accepted) xTaskNotifyGive(ai_chat_task_handle);
    return accepted;
}

extern "C" void ai_chat_stop_speech(void) { ai_speech_cancel(); }

extern "C" bool ai_chat_speech_status(char *text, size_t capacity)
{
    ai_speech_status_t status; ai_speech_get_status(&status);
    if (status.phase == AI_SPEECH_IDLE) return false;
    if (text && capacity) {
        if (status.phase == AI_SPEECH_PLAYING) snprintf(text, capacity, "缓存完成，正在播报...");
        else if (status.expected_bytes) snprintf(text, capacity, "缓存 %u%% · %uKiB/s · %us",
            (unsigned)((uint64_t)status.cached_bytes * 100 / status.expected_bytes),
            (unsigned)(status.cache_bytes_per_second / 1024), (unsigned)(status.cache_elapsed_ms / 1000));
        else snprintf(text, capacity, "缓存 %uKiB · %uKiB/s · %us",
            (unsigned)(status.cached_bytes / 1024), (unsigned)(status.cache_bytes_per_second / 1024),
            (unsigned)(status.cache_elapsed_ms / 1000));
    }
    return true;
}

static void ai_ws_handle_text(const char *payload)
{
    cJSON *response = payload ? cJSON_Parse(payload) : nullptr;
    if (!response) {
        ESP_LOGW(TAG, "AI WebSocket JSON parse failed");
        return;
    }
    const cJSON *type = cJSON_GetObjectItem(response, "type");
    if (!cJSON_IsString(type) || !type->valuestring || strcmp(type->valuestring, "ai_reply") != 0) {
        cJSON_Delete(response);
        return;
    }
    const char *question = json_string(response, "question");
    const char *reply = json_string(response, "text");
    const char *audio_url = json_string(response, "audio_url");
    char music_command[513];
    strlcpy(music_command, question[0] ? question : reply, sizeof(music_command));
    const bool weather_command = ai_is_weather_request(music_command);
    publish_result(reply, "", 1);
    if (weather_command) {
        char weather_reply[256];
        format_weather_reply(weather_reply, sizeof(weather_reply));
        publish_result(weather_reply, "", 1);
    }
    char audio_copy[512];
    strlcpy(audio_copy, audio_url, sizeof(audio_copy));
    ESP_LOGI(TAG, "AI WebSocket reply received, audio=%s", audio_copy[0] ? "yes" : "no");
    if (handle_music_command(music_command)) { cJSON_Delete(response); return; }
    if (handle_volume_command(music_command)) publish_result(reply, "", 1);
    const bool weather_refused = weather_command &&
        (strstr(reply, "无法获取") || strstr(reply, "查看手机") || strstr(reply, "实时天气信息"));
    cJSON_Delete(response);
    if (audio_copy[0] && !weather_refused && !play_ai_wav(audio_copy)) {
        ESP_LOGW(TAG, "AI WebSocket audio playback failed");
    }
}

static void queue_ws_message(void)
{
    int slot = -1;
    portENTER_CRITICAL(&ai_ws_mux);
    for (int index = 0; index < 2; ++index) {
        if (!ai_ws_slot_busy[index] && ai_ws_slots[index]) {
            ai_ws_slot_busy[index] = 1;
            slot = index;
            break;
        }
    }
    portEXIT_CRITICAL(&ai_ws_mux);
    if (slot < 0) {
        ESP_LOGW(TAG, "AI WebSocket message dropped");
        return;
    }
    memcpy(ai_ws_slots[slot], ai_ws_text.buffer, ai_ws_text.length + 1);
    if (!ai_ws_queue || xQueueSend(ai_ws_queue, &slot, 0) != pdTRUE) {
        portENTER_CRITICAL(&ai_ws_mux);
        ai_ws_slot_busy[slot] = 0;
        portEXIT_CRITICAL(&ai_ws_mux);
    } else if (ai_ws_dispatch_task_handle) {
        xTaskNotifyGive(ai_ws_dispatch_task_handle);
    }
}

static void ai_websocket_event(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ai_websocket_connected = true;
        ESP_LOGI(TAG, "AI WebSocket connected");
        return;
    }
    if (event_id == WEBSOCKET_EVENT_DISCONNECTED || event_id == WEBSOCKET_EVENT_CLOSED) {
        ai_websocket_connected = false;
        network_ws_text_reset(&ai_ws_text);
        ESP_LOGI(TAG, "AI WebSocket disconnected");
        return;
    }
    if (event_id != WEBSOCKET_EVENT_DATA || !event_data) return;
    const auto *data = static_cast<const esp_websocket_event_data_t *>(event_data);
    if (network_ws_text_feed(&ai_ws_text, data->op_code, data->fin, data->data_ptr, data->data_len,
                             data->payload_len, data->payload_offset)) {
        queue_ws_message();
    }
}

static void ai_ws_dispatch_task(void *parameter)
{
    (void)parameter;
    for (;;) {
        int slot = -1;
        if (xQueueReceive(ai_ws_queue, &slot, portMAX_DELAY) != pdTRUE) continue;
        if (slot >= 0 && slot < 2 && ai_ws_slots[slot]) ai_ws_handle_text(ai_ws_slots[slot]);
        portENTER_CRITICAL(&ai_ws_mux);
        if (slot >= 0 && slot < 2) ai_ws_slot_busy[slot] = 0;
        portEXIT_CRITICAL(&ai_ws_mux);
    }
}

extern "C" bool ai_websocket_is_connected(void)
{
    return ai_websocket_connected;
}

extern "C" const char *ai_service_status(void)
{
    if (!get_wifi_connected()) return "WiFi 未连接";
    return ai_config_status();
}

extern "C" bool ai_chat_is_busy(void)
{
    return ai_chat_busy || ai_chat_pending || sound_ai_audio_is_playing();
}

extern "C" bool ai_audio_is_playing(void)
{
    return sound_ai_audio_is_playing();
}

extern "C" int ai_chat_get_result(char *response, size_t response_size, char *error, size_t error_size)
{
    if (!ai_result_mutex || xSemaphoreTake(ai_result_mutex, 0) != pdTRUE) return 0;
    static uint32_t seen_music_result;
    const uint32_t version = online_music_get_result(nullptr, 0);
    if (version && version != seen_music_result) {
        seen_music_result = online_music_get_result(ai_chat_response, sizeof(ai_chat_response));
        ai_chat_error[0] = 0;
        ai_chat_result_state = 1;
        ++ai_result_revision;
    }
    if (response && response_size) strlcpy(response, ai_chat_response, response_size);
    if (error && error_size) strlcpy(error, ai_chat_error, error_size);
    const int state = ai_chat_result_state;
    xSemaphoreGive(ai_result_mutex);
    return state;
}

static void music_set_ai_reply(const char *message)
{
    publish_result(message, "", 1);
}

static void music_copy_display_name(uint8_t index, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    char path[64];
    if (!sdcar_copy_music_track(index, path, sizeof(path), nullptr)) return;
    strlcpy(buffer, path + 7, buffer_size);
    char *extension = strrchr(buffer, '.');
    if (extension && strcasecmp(extension, ".wav") == 0) *extension = '\0';
}

static int music_find_track_in_text(const char *text)
{
    if (!text || !*text) return -1;
    for (uint8_t index = 0; index < sdcar_get_music_track_count(); ++index) {
        char path[64];
        if (!sdcar_copy_music_track(index, path, sizeof(path), nullptr)) continue;
        const char *file_name = path + 7;
        if (strstr(text, file_name)) return index;
        char display_name[64];
        music_copy_display_name(index, display_name, sizeof(display_name));
        if (display_name[0] && strstr(text, display_name)) return index;
    }
    return -1;
}

static int music_find_ordinal_track(const char *text)
{
    if (!text || !*text) return -1;
    static const char *const ordinals[] = {
        "第一首", "第二首", "第三首", "第四首", "第五首",
        "第六首", "第七首", "第八首", "第九首", "第十首"
    };
    for (size_t index = 0; index < sizeof(ordinals) / sizeof(ordinals[0]); ++index) {
        if (strstr(text, ordinals[index])) {
            int slot = -1;
            for (size_t n = 0; n <= index; ++n) {
                slot = sdcar_next_music_track(slot, 1, false);
                if (slot < 0) return 255;
            }
            return slot;
        }
    }
    return -1;
}

static bool handle_music_command(const char *text)
{
    if (!text || !*text) return false;
    const bool close_music = (strstr(text, "关闭") || strstr(text, "关掉")) &&
                             (strstr(text, "音乐") || strstr(text, "歌曲"));
    const bool stop = strstr(text, "停止音乐") || strstr(text, "停止播放") ||
                      strstr(text, "停止歌曲") || close_music || strstr(text, "停止");
    const bool pause = strstr(text, "暂停音乐") || strstr(text, "暂停播放") || strstr(text, "暂停");
    const bool resume = strstr(text, "继续播放") || strstr(text, "继续音乐") ||
                        strstr(text, "恢复播放") || strstr(text, "恢复");
    const bool next = strstr(text, "下一首") || strstr(text, "下一曲") || strstr(text, "下一个");
    const bool previous = strstr(text, "上一首") || strstr(text, "上一曲") || strstr(text, "上一个");
    const bool generic_play = strstr(text, "播放音乐") || strstr(text, "播放歌曲") ||
                              strstr(text, "播放一首") || strstr(text, "放歌") || strstr(text, "听歌") ||
                              strstr(text, "打开音乐") || strstr(text, "播放");
    int requested_track = music_find_track_in_text(text);
    const music_intent_t intent = music_intent_parse(text);
    /* A partial local filename must not steal an online title. */
    if (intent.kind == MUSIC_INTENT_PLAY && requested_track >= 0) {
        char local_title[64];
        music_copy_display_name(static_cast<uint8_t>(requested_track), local_title, sizeof(local_title));
        char local_path[64];
        if (!sdcar_copy_music_track(requested_track, local_path, sizeof(local_path), nullptr) ||
            (strcmp(intent.query, local_title) && strcmp(intent.query, local_path + 7))) requested_track = -1;
    }
    if (intent.force_online || intent.kind == MUSIC_INTENT_SEARCH ||
        intent.kind == MUSIC_INTENT_SELECT ||
        (intent.kind == MUSIC_INTENT_PLAY && requested_track < 0) ||
        intent.kind >= MUSIC_INTENT_STOP) {
        if (online_music_handle_command(text)) return true;
    }
    if (requested_track < 0) requested_track = music_find_ordinal_track(text);
    if (!stop && !pause && !resume && !next && !previous && requested_track < 0 && !generic_play) return false;
    if (generic_play && requested_track < 0 && !stop && !pause && !resume && !next && !previous) {
        const char *generic[] = {"播放", "播放音乐", "播放歌曲", "播放一首", "放歌", "听歌", "打开音乐"};
        bool current_track = false;
        for (const char *phrase : generic) if (!strcmp(text, phrase)) current_track = true;
        if (!current_track) {
            music_set_ai_reply("请用“播放 歌名”或“搜歌 歌名”点歌，歌名请少于 80 个汉字");
            return true;
        }
    }
    if (sdcar_next_music_track(-1, 1, false) < 0) {
        music_set_ai_reply("SD 卡 /music 目录中没有可播放的 WAV 音乐");
        ESP_LOGI(TAG, "AI music command ignored: no WAV tracks");
        return true;
    }
    if (!sound_is_ready()) {
        music_set_ai_reply("音乐输出设备尚未准备好");
        ESP_LOGI(TAG, "AI music command ignored: audio is not ready");
        return true;
    }
    if (requested_track >= static_cast<int>(sdcar_get_music_track_count())) {
        music_set_ai_reply("指定的歌曲序号不存在");
        ESP_LOGW(TAG, "AI music command ignored: track %d is out of range", requested_track + 1);
        return true;
    }
    if (stop) {
        music_audio_stop();
        music_set_ai_reply("音乐已停止");
        ESP_LOGI(TAG, "AI music command: stop");
        return true;
    }
    if (pause) {
        music_audio_pause();
        music_set_ai_reply("音乐已暂停");
        ESP_LOGI(TAG, "AI music command: pause");
        return true;
    }
    if (resume) {
        music_audio_play();
        music_set_ai_reply("继续播放音乐");
        ESP_LOGI(TAG, "AI music command: resume");
        return true;
    }

    uint8_t selected_track = get_music_track_index();
    if (next || previous) {
        const int slot = music_next_local_track(get_music_track_index(), next ? 1 : -1);
        if (slot >= 0) selected_track = static_cast<uint8_t>(slot);
    } else if (requested_track >= 0) selected_track = static_cast<uint8_t>(requested_track);
    music_select_track(selected_track);
    music_audio_play();
    char display_name[64];
    music_copy_display_name(selected_track, display_name, sizeof(display_name));
    char reply[128];
    if (next) snprintf(reply, sizeof(reply), "正在播放下一首：%s", display_name);
    else if (previous) snprintf(reply, sizeof(reply), "正在播放上一首：%s", display_name);
    else snprintf(reply, sizeof(reply), "正在播放：%s", display_name);
    music_set_ai_reply(reply);
    ESP_LOGI(TAG, "AI music command: play track %u (%s)", static_cast<unsigned>(selected_track), display_name);
    return true;
}

static void log_ai_heap(const char *action)
{
    ESP_LOGE(TAG, "%s internal_free=%u internal_largest=%u", action,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

static void ai_ws_release_unclaimed(void)
{
    if (ai_ws_dispatch_task_handle) return;
    free(ai_ws_text.buffer);
    ai_ws_text.buffer = nullptr;
    ai_ws_text.capacity = 0;
    network_ws_text_reset(&ai_ws_text);
    for (int index = 0; index < 2; ++index) {
        free(ai_ws_slots[index]);
        ai_ws_slots[index] = nullptr;
        ai_ws_slot_busy[index] = 0;
    }
    if (ai_ws_queue) {
        vQueueDelete(ai_ws_queue);
        ai_ws_queue = nullptr;
    }
}

static void ai_ws_dispatch_start(void)
{
    if (ai_websocket) return;
    if (!ai_ws_dispatch_task_handle) {
        ai_ws_release_unclaimed();
        ai_ws_text.buffer = alloc_buffer(AI_WS_MESSAGE_CAP);
        ai_ws_text.capacity = ai_ws_text.buffer ? AI_WS_MESSAGE_CAP : 0;
        ai_ws_slots[0] = alloc_buffer(AI_WS_MESSAGE_CAP);
        ai_ws_slots[1] = alloc_buffer(AI_WS_MESSAGE_CAP);
        ai_ws_queue = xQueueCreate(2, sizeof(int));
        if (!ai_ws_text.buffer || !ai_ws_slots[0] || !ai_ws_slots[1] || !ai_ws_queue ||
            xTaskCreatePinnedToCore(ai_ws_dispatch_task, "AIWsDispatch", 12288, nullptr, 1,
                                    &ai_ws_dispatch_task_handle, 0) != pdPASS) {
            ai_ws_dispatch_task_handle = nullptr;
            ai_ws_release_unclaimed();
            log_ai_heap("AI WebSocket dispatch task creation failed");
            return;
        }
    }
    esp_websocket_client_config_t config = {};
    config.uri = AI_WS_URL;
    config.disable_auto_reconnect = false;
    config.enable_close_reconnect = true;
    config.task_prio = 1;
    config.task_name = "AIWebSocket";
    config.task_stack = 6144;
    config.buffer_size = 2048;
    config.ping_interval_sec = 15;
    config.pingpong_timeout_sec = 3;
    config.reconnect_timeout_ms = 5000;
    config.network_timeout_ms = 10000;
    esp_websocket_client_handle_t client = esp_websocket_client_init(&config);
    const esp_err_t registered = client
        ? esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ai_websocket_event, nullptr)
        : ESP_FAIL;
    const esp_err_t started = (client && registered == ESP_OK) ? esp_websocket_client_start(client) : ESP_FAIL;
    if (!client || registered != ESP_OK || started != ESP_OK) {
        if (client) esp_websocket_client_destroy(client);
        log_ai_heap("AI WebSocket: task creation failed");
        return;
    }
    ai_websocket = client;
}

extern "C" void web_ai_init(void)
{
    if (ai_chat_task_handle && ai_play_mutex && ai_result_mutex) return;
    if (!ai_play_mutex) {
        ai_play_mutex = xSemaphoreCreateMutex();
        if (!ai_play_mutex) log_ai_heap("AI chat: mutex creation failed");
    }
    if (!ai_result_mutex) ai_result_mutex = xSemaphoreCreateMutex();
    if (!ai_result_mutex) {log_ai_heap("AI chat: result mutex creation failed"); return;}
    if (!ai_chat_task_handle &&
        xTaskCreatePinnedToCore(ai_chat_task, "AIChatTask", 12288, nullptr, 1,
                                &ai_chat_task_handle, 0) != pdPASS) {
        ai_chat_task_handle = nullptr;
        log_ai_heap("AI chat: task creation failed");
    }
    /* Direct API uses no persistent middle-server WebSocket/task. */
}
