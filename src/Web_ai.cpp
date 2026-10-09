#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <string.h>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "setwifi.h"
#include "SDcar.h"
#include "Sound.h"
#include "Web_ai.h"
#if __has_include("private_config.h")
#include "private_config.h"
#endif
#ifndef SMARTVOICEBOX_AI_SERVER_HOST
#define SMARTVOICEBOX_AI_SERVER_HOST ""
#endif
#ifndef SMARTVOICEBOX_AI_SERVER_PORT
#define SMARTVOICEBOX_AI_SERVER_PORT 3000
#endif
#ifndef SMARTVOICEBOX_AI_SERVER_HTTP_BASE_URL
#define SMARTVOICEBOX_AI_SERVER_HTTP_BASE_URL ""
#endif
static constexpr uint32_t AI_I2S_SAMPLE_RATE = 16000;
static TaskHandle_t ai_chat_task_handle = nullptr;
static volatile bool ai_chat_pending = false;
static volatile bool ai_chat_busy = false;
static char ai_chat_request[513] = {};
static char ai_chat_response[3073] = {};
static char ai_chat_error[128] = {};
static volatile int ai_chat_result_state = 0;
static WebSocketsClient ai_websocket;
static TaskHandle_t ai_websocket_task_handle = nullptr;
static volatile bool ai_websocket_connected = false;
static char ai_websocket_audio_url[512] = {};
static void ai_websocket_event(WStype_t type, uint8_t *payload, size_t length);
static bool play_ai_wav(const char *url);
static bool handle_music_command(const char *text);
static bool handle_volume_command(const char *text);
static bool ai_is_weather_request(const char *text);
static void format_weather_reply(char *buffer, size_t buffer_size);
static bool handle_weather_command(const char *text);
static void music_set_ai_reply(const char *message);

static void ai_websocket_task(void *parameter)
{
    (void)parameter;
    if (!SMARTVOICEBOX_AI_SERVER_HOST[0]) {
        Serial.println("AI WebSocket disabled: configure src/private_config.h");
        vTaskDelete(nullptr);
        return;
    }
    ai_websocket.begin(SMARTVOICEBOX_AI_SERVER_HOST,
                       SMARTVOICEBOX_AI_SERVER_PORT, "/esp32");
    ai_websocket.onEvent(ai_websocket_event);
    ai_websocket.setReconnectInterval(5000);
    ai_websocket.enableHeartbeat(15000, 3000, 2);
    Serial.println("AI WebSocket task ready");
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            ai_websocket_connected = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        ai_websocket.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
static bool read_ai_stream_exact(Stream *stream, uint8_t *buffer, size_t size)
{
    size_t total_read = 0;
    while (total_read < size) {
        const size_t count = stream->readBytes(
            buffer + total_read, size - total_read);
        if (count == 0) return false;
        total_read += count;
    }
    return true;
}

static bool skip_ai_stream_bytes(Stream *stream, uint32_t size)
{
    uint8_t discard[64];
    while (size > 0) {
        const size_t request = min(static_cast<size_t>(size), sizeof(discard));
        const size_t count = stream->readBytes(discard, request);
        if (count == 0) return false;
        size -= static_cast<uint32_t>(count);
    }
    return true;
}

static uint32_t read_ai_le32(const uint8_t *data)
{
    return static_cast<uint32_t>(data[0]) |
        (static_cast<uint32_t>(data[1]) << 8) |
        (static_cast<uint32_t>(data[2]) << 16) |
        (static_cast<uint32_t>(data[3]) << 24);
}

static bool play_ai_wav(const char *url)
{
    if (!url || !*url) return false;

    HTTPClient http;
    http.setConnectTimeout(12000);
    http.setTimeout(30000);
    if (!http.begin(url)) return false;

    const int status = http.GET();
    if (status != HTTP_CODE_OK) {
        Serial.printf("AI audio HTTP error: %d\n", status);
        http.end();
        return false;
    }

    WiFiClient *stream = http.getStreamPtr();
    uint8_t riff_header[12];
    if (!read_ai_stream_exact(stream, riff_header, sizeof(riff_header)) ||
        memcmp(riff_header, "RIFF", 4) != 0 ||
        memcmp(riff_header + 8, "WAVE", 4) != 0) {
        Serial.println("AI audio: unsupported WAV container");
        http.end();
        return false;
    }

    bool fmt_found = false;
    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    uint32_t sample_rate = 0;
    uint32_t data_bytes = 0;
    bool data_found = false;
    for (uint8_t chunk_index = 0; chunk_index < 32 && !data_found;
         ++chunk_index) {
        uint8_t chunk_header[8];
        if (!read_ai_stream_exact(stream, chunk_header, sizeof(chunk_header))) {
            break;
        }
        const uint32_t chunk_size = read_ai_le32(chunk_header + 4);
        const uint32_t padded_size = chunk_size + (chunk_size & 1U);

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_size < 16 || chunk_size > 4096) break;
            uint8_t fmt_data[16];
            if (!read_ai_stream_exact(stream, fmt_data, sizeof(fmt_data))) break;
            audio_format = static_cast<uint16_t>(fmt_data[0]) |
                (static_cast<uint16_t>(fmt_data[1]) << 8);
            channels = static_cast<uint16_t>(fmt_data[2]) |
                (static_cast<uint16_t>(fmt_data[3]) << 8);
            sample_rate = read_ai_le32(fmt_data + 4);
            bits_per_sample = static_cast<uint16_t>(fmt_data[14]) |
                (static_cast<uint16_t>(fmt_data[15]) << 8);
            if (!skip_ai_stream_bytes(stream, padded_size - 16)) break;
            fmt_found = true;
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            if (!fmt_found || chunk_size == 0) break;
            data_bytes = chunk_size;
            data_found = true;
        } else if (!skip_ai_stream_bytes(stream, padded_size)) {
            break;
        }
    }

    if (!data_found || audio_format != 1 || channels != 1 ||
        bits_per_sample != 16) {
        Serial.println("AI audio: unsupported WAV PCM format");
        http.end();
        return false;
    }
    if (sample_rate != AI_I2S_SAMPLE_RATE) {
        Serial.printf("AI audio: unexpected sample rate %lu\n",
                      static_cast<unsigned long>(sample_rate));
        http.end();
        return false;
    }
    if (data_bytes == 0 || (data_bytes & 1U) != 0) {
        Serial.println("AI audio: invalid WAV data length");
        http.end();
        return false;
    }

    /* Claim the output only after a playable WAV has arrived. A failed HTTP
     * request or bad header must leave music and video free to play. */
    if (!sound_ai_output_begin()) {
        http.end();
        return false;
    }

    uint8_t mono_pcm[2048];
    static int16_t stereo_pcm[2048];
    uint32_t remaining = data_bytes;
    bool success = true;
    while (remaining > 0) {
        size_t amount = min(static_cast<size_t>(remaining), sizeof(mono_pcm));
        amount &= ~static_cast<size_t>(1);
        if (amount == 0) {
            success = false;
            break;
        }
        const size_t received = stream->readBytes(mono_pcm, amount);
        if (received == 0 || (received & 1U) != 0) {
            Serial.println("AI audio: WAV data stream ended early");
            success = false;
            break;
        }
        remaining -= static_cast<uint32_t>(received);

        const size_t sample_count = received / 2;
        for (size_t index = 0; index < sample_count; ++index) {
            const int16_t sample = static_cast<int16_t>(
                static_cast<uint16_t>(mono_pcm[index * 2]) |
                (static_cast<uint16_t>(mono_pcm[index * 2 + 1]) << 8));
            stereo_pcm[index * 2] = sample;
            stereo_pcm[index * 2 + 1] = sample;
        }

        if (!sound_ai_output_write(stereo_pcm,
                                   sample_count * sizeof(int16_t) * 2)) {
            success = false;
            break;
        }
    }
    if (remaining != 0) success = false;
    http.end();

    if (success) {
        // Let the final DMA buffer reach the amplifier before stopping the channel.
        vTaskDelay(pdMS_TO_TICKS(100));
        Serial.println("AI audio playback finished");
    }
    return sound_ai_output_finish(success);
}
static bool ai_is_weather_request(const char *text)
{
    if (!text || !*text) return false;
    return strstr(text, "天气") || strstr(text, "天气情况") ||
           strstr(text, "气温") || strstr(text, "温度") ||
           strstr(text, "下雨") || strstr(text, "weather") ||
           strstr(text, "temperature");
}

static bool handle_weather_command(const char *text)
{
    if (!ai_is_weather_request(text)) return false;

    char reply[256];
    format_weather_reply(reply, sizeof(reply));
    music_set_ai_reply(reply);
    Serial.printf("AI weather command: %s\n", reply);
    return true;
}

static void format_weather_reply(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    snprintf(buffer, buffer_size,
             "当前%s天气：%s，温度%s摄氏度。",
             WEATHER_CITY_DISPLAY[0] ? WEATHER_CITY_DISPLAY : WEATHER_CITY,
             weather ? weather : "暂无天气数据",
             temp ? temp : "暂无温度数据");
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

    const bool quieter = strstr(text, "小一点") || strstr(text, "小声") ||
                         strstr(text, "调低") || strstr(text, "降低") ||
                         strstr(text, "quiet") || strstr(text, "down");
    const bool louder = strstr(text, "大一点") || strstr(text, "大声") ||
                        strstr(text, "调高") || strstr(text, "增大") ||
                        strstr(text, "loud") || strstr(text, "up");
    if (target < 0) {
        if (strstr(text, "静音") || strstr(text, "mute")) target = 0;
        else if (quieter) target = static_cast<int>(get_audio_volume()) - 10;
        else if (louder) target = static_cast<int>(get_audio_volume()) + 10;
        else return false;
    }

    target = constrain(target, 0, 100);
    set_audio_volume(target);
    save_audio_volume();
    char reply[64];
    snprintf(reply, sizeof(reply), "音量已设置为 %d%%", target);
    music_set_ai_reply(reply);
    Serial.printf("AI volume command: %d%%\n", target);
    return true;
}

static void build_ai_server_prompt(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    if (ai_is_weather_request(ai_chat_request) && (weather || temp)) {
        snprintf(buffer, buffer_size,
                 "请直接用中文播报以下设备天气结果，不要说无法获取实时天气，也不要要求用户查看手机：城市%s，天气%s，温度%s摄氏度。用户问题是：%s。",
                 WEATHER_CITY_DISPLAY[0] ? WEATHER_CITY_DISPLAY : WEATHER_CITY,
                 weather ? weather : "暂无天气数据",
                 temp ? temp : "暂无温度数据",
                 ai_chat_request);
    } else {
        strlcpy(buffer, ai_chat_request, buffer_size);
    }
}

static void ai_chat_task(void *parameter)
{
    (void)parameter;
    Serial.println("AI chat task ready");
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!ai_chat_pending) continue;
        ai_chat_pending = false;
        ai_chat_busy = true;
        Serial.printf("AI chat request: %s\n", ai_chat_request);
        ai_chat_result_state = 0;
        ai_chat_response[0] = '\0';
        ai_chat_error[0] = '\0';

        // Music commands are handled locally so they work without a second
        // server-side intent parser and do not consume the AI API.
        if (handle_music_command(ai_chat_request)) {
            ai_chat_busy = false;
            continue;
        }
        /* Keep the device weather text authoritative, but still send a
         * weather-specific prompt to the server so it can synthesize speech
         * and return an audio_url. */
        const bool weather_command = handle_weather_command(ai_chat_request);
        const bool volume_command = handle_volume_command(ai_chat_request);
        if (volume_command) {
            ai_chat_busy = false;
            continue;
        }

        if (WiFi.status() != WL_CONNECTED) {
            if (weather_command) {
                char weather_reply[256];
                format_weather_reply(weather_reply, sizeof(weather_reply));
                music_set_ai_reply(weather_reply);
                Serial.println("AI weather TTS skipped while offline; kept local reply");
                ai_chat_busy = false;
                continue;
            }
            strlcpy(ai_chat_error, "WiFi 未连接", sizeof(ai_chat_error));
            ai_chat_result_state = -1;
            ai_chat_busy = false;
            continue;
        }

        HTTPClient http;
        http.setConnectTimeout(12000);
        http.setTimeout(60000);
        if (!SMARTVOICEBOX_AI_SERVER_HTTP_BASE_URL[0]) {
        strlcpy(ai_chat_error, "AI 服务器未配置", sizeof(ai_chat_error));
        ai_chat_result_state = -1;
        ai_chat_busy = false;
        continue;
    }
    char ai_chat_url[192];
    const int ai_url_length = snprintf(ai_chat_url, sizeof(ai_chat_url),
                                       "%s/api/chat",
                                       SMARTVOICEBOX_AI_SERVER_HTTP_BASE_URL);
    if (ai_url_length < 0 ||
        static_cast<size_t>(ai_url_length) >= sizeof(ai_chat_url) ||
        !http.begin(ai_chat_url)) {
            if (weather_command) {
                char weather_reply[256];
                format_weather_reply(weather_reply, sizeof(weather_reply));
                music_set_ai_reply(weather_reply);
                Serial.println("AI weather TTS request could not connect; kept local reply");
                ai_chat_busy = false;
                continue;
            }
            strlcpy(ai_chat_error, "无法连接 AI 服务器", sizeof(ai_chat_error));
            ai_chat_result_state = -1;
            ai_chat_busy = false;
            continue;
        }
        http.addHeader("Content-Type", "application/json");
        DynamicJsonDocument request(1024);
        char server_prompt[768];
        build_ai_server_prompt(server_prompt, sizeof(server_prompt));
        request["message"] = server_prompt;
        String body;
        serializeJson(request, body);
        const int code = http.POST(body);
        Serial.printf("AI chat HTTP status: %d\n", code);
        if (code != HTTP_CODE_OK) {
            if (weather_command) {
                char weather_reply[256];
                format_weather_reply(weather_reply, sizeof(weather_reply));
                music_set_ai_reply(weather_reply);
                Serial.printf("AI weather TTS HTTP failed (%d); kept local reply\n", code);
                http.end();
                ai_chat_busy = false;
                continue;
            }
            snprintf(ai_chat_error, sizeof(ai_chat_error),
                     "AI 请求失败 (HTTP %d)", code);
            http.end();
            ai_chat_result_state = -1;
            ai_chat_busy = false;
            continue;
        }

        DynamicJsonDocument response(6144);
        const DeserializationError json_error = deserializeJson(
            response, http.getStream());
        http.end();
        if (json_error || !response["ok"].as<bool>()) {
            if (weather_command) {
                char weather_reply[256];
                format_weather_reply(weather_reply, sizeof(weather_reply));
                music_set_ai_reply(weather_reply);
                Serial.println("AI weather TTS returned invalid data; kept local reply");
                ai_chat_busy = false;
                continue;
            }
            const char *server_error = response["error"] | "服务器返回无效数据";
            strlcpy(ai_chat_error, server_error, sizeof(ai_chat_error));
            ai_chat_result_state = -1;
            ai_chat_busy = false;
            continue;
        }

        const char *reply = response["text"] | "";
        const char *audio_url = response["audio_url"] | "";
        strlcpy(ai_chat_response, reply, sizeof(ai_chat_response));
        if (weather_command) {
            char weather_reply[256];
            format_weather_reply(weather_reply, sizeof(weather_reply));
            strlcpy(ai_chat_response, weather_reply, sizeof(ai_chat_response));
            Serial.printf("AI weather TTS response received, audio=%s\n",
                          audio_url[0] ? "yes" : "no");
        }
        Serial.printf("AI text received (%u bytes), audio=%s\n",
                      static_cast<unsigned>(strlen(reply)),
                      audio_url[0] ? "yes" : "no");
        ai_chat_result_state = 1;
        const bool weather_refused = weather_command &&
            (strstr(reply, "无法获取") || strstr(reply, "查看手机") ||
             strstr(reply, "实时天气信息"));
        if (audio_url[0] && !weather_refused) {
            if (!play_ai_wav(audio_url)) {
                Serial.println("AI reply received but audio playback failed");
            }
        }
        ai_chat_busy = false;
    }
}

extern "C" bool ai_chat_submit(const char *message)
{
    if (!message || !*message || ai_chat_busy || ai_chat_pending ||
        sound_ai_audio_is_playing()) return false;
    strlcpy(ai_chat_request, message, sizeof(ai_chat_request));
    ai_chat_pending = true;
    if (ai_chat_task_handle) xTaskNotifyGive(ai_chat_task_handle);
    return ai_chat_task_handle != nullptr;
}

static void ai_websocket_event(WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_CONNECTED) {
        ai_websocket_connected = true;
        Serial.println("AI WebSocket connected");
        return;
    }
    if (type == WStype_DISCONNECTED) {
        ai_websocket_connected = false;
        Serial.println("AI WebSocket disconnected");
        return;
    }
    if (type != WStype_TEXT || !payload || length == 0) return;

    DynamicJsonDocument response(6144);
    if (deserializeJson(response, payload, length)) {
        Serial.println("AI WebSocket JSON parse failed");
        return;
    }
    if (response["type"] != "ai_reply") return;

    const char *question = response["question"] | "";
    const char *reply = response["text"] | "";
    const char *audio_url = response["audio_url"] | "";
    const char *music_command = question[0] ? question : reply;
    const bool weather_command = ai_is_weather_request(music_command);
    strlcpy(ai_chat_request, question, sizeof(ai_chat_request));
    strlcpy(ai_chat_response, reply, sizeof(ai_chat_response));
    if (weather_command) {
        char weather_reply[256];
        format_weather_reply(weather_reply, sizeof(weather_reply));
        strlcpy(ai_chat_response, weather_reply, sizeof(ai_chat_response));
    }
    strlcpy(ai_websocket_audio_url, audio_url, sizeof(ai_websocket_audio_url));
    ai_chat_result_state = 1;
    Serial.printf("AI WebSocket reply received, audio=%s\n",
                  audio_url[0] ? "yes" : "no");

    if (handle_music_command(music_command)) return;
    if (handle_volume_command(music_command)) {
        /* Keep the server's natural-language reply while retaining the local
         * volume change made from the command. */
        strlcpy(ai_chat_response, reply, sizeof(ai_chat_response));
    }

    const bool weather_refused = weather_command &&
        (strstr(reply, "无法获取") || strstr(reply, "查看手机") ||
         strstr(reply, "实时天气信息"));
    if (audio_url[0] && !weather_refused) {
        if (!play_ai_wav(audio_url)) {
            Serial.println("AI WebSocket audio playback failed");
        }
    }
}

extern "C" bool ai_websocket_is_connected(void)
{
    return ai_websocket_connected;
}

extern "C" bool ai_chat_is_busy(void)
{
    return ai_chat_busy || ai_chat_pending || sound_ai_audio_is_playing();
}

extern "C" bool ai_audio_is_playing(void)
{
    return sound_ai_audio_is_playing();
}

extern "C" int ai_chat_get_result(char *response, size_t response_size,
                                    char *error, size_t error_size)
{
    if (response && response_size) strlcpy(response, ai_chat_response, response_size);
    if (error && error_size) strlcpy(error, ai_chat_error, error_size);
    return ai_chat_result_state;
}

static void music_set_ai_reply(const char *message)
{
    strlcpy(ai_chat_response, message, sizeof(ai_chat_response));
    ai_chat_error[0] = '\0';
    ai_chat_result_state = 1;
}

static void music_copy_display_name(uint8_t index, char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0) return;
    buffer[0] = '\0';
    if (index >= sdcar_get_music_track_count()) return;
    strlcpy(buffer, sdcar_get_music_track_name(index), buffer_size);
    char *extension = strrchr(buffer, '.');
    if (extension && strcasecmp(extension, ".wav") == 0) *extension = '\0';
}

static int music_find_track_in_text(const char *text)
{
    if (!text || !*text) return -1;
    for (uint8_t index = 0; index < sdcar_get_music_track_count(); ++index) {
        const char *file_name = sdcar_get_music_track_name(index);
        if (!file_name || !*file_name) continue;
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
            return static_cast<int>(index);
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
                      strstr(text, "停止歌曲") || close_music ||
                      strstr(text, "停止");
    const bool pause = strstr(text, "暂停音乐") || strstr(text, "暂停播放") ||
                       strstr(text, "暂停");
    const bool resume = strstr(text, "继续播放") || strstr(text, "继续音乐") ||
                        strstr(text, "恢复播放") || strstr(text, "恢复");
    const bool next = strstr(text, "下一首") || strstr(text, "下一曲") ||
                      strstr(text, "下一个");
    const bool previous = strstr(text, "上一首") || strstr(text, "上一曲") ||
                          strstr(text, "上一个");
    const bool generic_play = strstr(text, "播放音乐") || strstr(text, "播放歌曲") ||
                              strstr(text, "播放一首") || strstr(text, "放歌") ||
                              strstr(text, "听歌") || strstr(text, "打开音乐") ||
                              strstr(text, "播放");
    int requested_track = music_find_track_in_text(text);
    if (requested_track < 0) requested_track = music_find_ordinal_track(text);

    if (!stop && !pause && !resume && !next && !previous &&
        requested_track < 0 && !generic_play) {
        return false;
    }

    if (sdcar_get_music_track_count() == 0) {
        music_set_ai_reply("SD 卡 /music 目录中没有可播放的 WAV 音乐");
        Serial.println("AI music command ignored: no WAV tracks");
        return true;
    }
    if (!sound_is_ready()) {
        music_set_ai_reply("音乐输出设备尚未准备好");
        Serial.println("AI music command ignored: audio is not ready");
        return true;
    }

    if (requested_track >= static_cast<int>(sdcar_get_music_track_count())) {
        music_set_ai_reply("指定的歌曲序号不存在");
        Serial.printf("AI music command ignored: track %d is out of range\n",
                      requested_track + 1);
        return true;
    }

    if (stop) {
        music_audio_stop();
        music_set_ai_reply("音乐已停止");
        Serial.println("AI music command: stop");
        return true;
    }
    if (pause) {
        music_audio_pause();
        music_set_ai_reply("音乐已暂停");
        Serial.println("AI music command: pause");
        return true;
    }
    if (resume) {
        music_audio_play();
        music_set_ai_reply("继续播放音乐");
        Serial.println("AI music command: resume");
        return true;
    }

    uint8_t selected_track = get_music_track_index();
    if (next) {
        selected_track = (get_music_track_index() + 1) % sdcar_get_music_track_count();
    } else if (previous) {
        selected_track = get_music_track_index() == 0 ?
            sdcar_get_music_track_count() - 1 : get_music_track_index() - 1;
    } else if (requested_track >= 0) {
        selected_track = static_cast<uint8_t>(requested_track);
    }

    music_select_track(selected_track);
    music_audio_play();

    char display_name[64];
    music_copy_display_name(selected_track, display_name, sizeof(display_name));
    char reply[128];
    if (next) {
        snprintf(reply, sizeof(reply), "正在播放下一首：%s", display_name);
    } else if (previous) {
        snprintf(reply, sizeof(reply), "正在播放上一首：%s", display_name);
    } else if (requested_track >= 0) {
        snprintf(reply, sizeof(reply), "正在播放：%s", display_name);
    } else {
        snprintf(reply, sizeof(reply), "正在播放：%s", display_name);
    }
    music_set_ai_reply(reply);
    Serial.printf("AI music command: play track %u (%s)\n",
                  static_cast<unsigned>(selected_track), display_name);
    return true;
}

extern "C" void web_ai_init(void)
{
    if (xTaskCreatePinnedToCore(ai_chat_task, "AIChatTask", 12288, nullptr,
                                1, &ai_chat_task_handle, 0) != pdPASS) {
        ai_chat_task_handle = nullptr;
        Serial.println("AI chat: task creation failed");
    }
    if (xTaskCreatePinnedToCore(ai_websocket_task, "AIWebSocketTask", 8192,
                                nullptr, 1, &ai_websocket_task_handle, 0) != pdPASS) {
        ai_websocket_task_handle = nullptr;
        Serial.println("AI WebSocket: task creation failed");
    }
}
