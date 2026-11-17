#include "MusicLibraryLock.h"
#include <atomic>
#include "IdfRuntime.h"
#include "Storage.h"
#include <algorithm>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s_std.h>
#include "SDcar.h"
#include "Sound.h"
#include <atomic>
#include "Mp3Source.h"
#include "OnlineMusic.h"
#include "wav_pcm.h"
#include "video_wav_reader.h"
static constexpr gpio_num_t MUSIC_I2S_BCLK = GPIO_NUM_1;
static constexpr gpio_num_t MUSIC_I2S_LRCLK = GPIO_NUM_2;
static constexpr gpio_num_t MUSIC_I2S_DOUT = GPIO_NUM_40;
static constexpr uint32_t MUSIC_SAMPLE_RATE = 44100;
static constexpr uint32_t AI_I2S_SAMPLE_RATE = 16000;
static i2s_chan_handle_t music_i2s_tx = nullptr;
static bool music_i2s_enabled = false;
static bool ai_i2s_enabled = false;
static uint32_t output_sample_rate = MUSIC_SAMPLE_RATE;
static bool music_audio_ready = false;
static TaskHandle_t music_audio_task_handle = nullptr;
static volatile uint8_t music_track_index = 0;
enum MusicPlayMode : uint8_t { MUSIC_MODE_SINGLE = 0, MUSIC_MODE_LOOP_ALL = 1, MUSIC_MODE_SEQUENTIAL = 2 };
static volatile uint8_t music_play_mode = MUSIC_MODE_LOOP_ALL;
static volatile int16_t music_track_request = -1;
static uint32_t music_track_request_generation;
static std::atomic<int> music_delete_state{0};
static uint8_t music_delete_index;
static uint32_t music_delete_generation;
static volatile bool music_audio_playing = false;
static volatile bool music_audio_stop_requested = false;
static volatile bool music_audio_pause_requested = false;
static volatile bool music_play_requested = false;
static portMUX_TYPE music_control_mux = portMUX_INITIALIZER_UNLOCKED;
static EXT_RAM_BSS_ATTR char online_path[96], online_title[256];
static EXT_RAM_BSS_ATTR char online_request_path[96], online_request_title[256];
static bool online_mode, online_request_pending, online_cancel_requested;
static bool online_pending_play;
static uint32_t online_job, online_request_job;
static mp3_source_t *online_decoder = nullptr; /* Owned only by MusicAudioTask. */
static volatile bool music_resume_after_ai_audio = false;
static volatile bool alarm_audio_requested = false;
static bool alarm_audio_active = false;
static std::atomic<bool> alarm_clock_running{false};
static std::atomic<uint32_t> alarm_clock_started{0}, alarm_clock_duration{0};
static bool alarm_preempts_ai(void)
{
    return alarm_clock_running.load(std::memory_order_acquire) || alarm_audio_requested;
}
/* i2s_channel_write copies CPU staging into driver-owned internal DMA buffers. */
static EXT_RAM_BSS_ATTR uint8_t music_audio_buffer[8192];
static volatile uint8_t audio_volume_percent = 100;
static i2s_std_gpio_config_t music_i2s_gpio_config = {};
static SemaphoreHandle_t ai_audio_mutex = nullptr;
static volatile bool ai_audio_playing = false;
static volatile uint32_t music_elapsed_ms = 0;
static bool finish_ai_audio(bool success);
static void resume_music_after_ai_audio(void);
static bool video_i2s_on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user);
static void mute_music_i2s(void);

static SemaphoreHandle_t audio_cmd_lock = nullptr;
static SemaphoreHandle_t audio_cmd_done = nullptr;
static SemaphoreHandle_t video_wav_done = nullptr;
static TaskHandle_t video_wav_task = nullptr;
static portMUX_TYPE video_i2s_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE video_ring_mux = portMUX_INITIALIZER_UNLOCKED;
static constexpr size_t VIDEO_RING_BYTES = 48 * 1024;
static constexpr size_t VIDEO_STAGE_BYTES = 2048;
static uint8_t *video_ring = nullptr;
static EXT_RAM_BSS_ATTR uint8_t video_stage[VIDEO_STAGE_BYTES];
static size_t video_ring_read = 0;
static size_t video_ring_fill = 0;
static video_wav_reader_t video_wav_file = VIDEO_WAV_READER_INIT;
static char video_wav_path[96] = {};
static char video_cmd_path[96] = {};
static uint32_t video_cmd_frames = 0;
static uint32_t video_cmd_fps = 0;
static uint32_t video_data_offset = 0;
static uint32_t video_data_bytes = 0;
static uint32_t video_data_remaining = 0;
static uint32_t video_duration_samples = 0;
static uint32_t video_fps_milli = 0;
static uint64_t video_pcm_accepted = 0;
static uint64_t video_i2s_sent_bytes = 0;
static uint32_t video_i2s_skip_bytes = 0;
static uint32_t video_underruns = 0;
static uint32_t video_logged_cycle = 0;
static uint32_t music_dma_bytes = 6u * 240u * 4u;
static volatile bool video_i2s_count = false;
static volatile bool video_i2s_cb_ok = false;
static volatile bool video_session = false;
static volatile bool video_prepared = false;
static volatile bool video_running = false;
static volatile bool video_need_rearm = false;
static volatile bool video_wav_stop = false;
static volatile bool video_wav_error = false;
static volatile bool ai_release_request = false;
static volatile bool ai_i2s_held = false;
static volatile bool music_resume_after_video = false;
static volatile bool video_cmd_ok = false;
static volatile uint8_t video_cmd = 0;
static bool video_prepare_pending = false;
static bool video_start_pending = false;
enum : uint8_t {
    VIDEO_CMD_NONE = 0,
    VIDEO_CMD_PREPARE = 1,
    VIDEO_CMD_START = 2,
    VIDEO_CMD_HALT = 3,
    VIDEO_CMD_RELEASE = 4
};

static void apply_audio_volume(uint8_t *data, size_t byte_count)
{
    const uint8_t volume = audio_volume_percent;
    if (volume >= 100) return;

    /* Use a listening-friendly curve: low slider values become much quieter
     * than a linear sample multiplier (3% is close to mute). */
    const float normalized = static_cast<float>(volume) / 100.0f;
    const int32_t gain_q15 = static_cast<int32_t>(
        lroundf(powf(normalized, 2.2f) * 32767.0f));
    const size_t sample_count = byte_count / sizeof(int16_t);
    for (size_t i = 0; i < sample_count; ++i) {
        int16_t sample;
        memcpy(&sample, data + i * sizeof(sample), sizeof(sample));
        const int32_t scaled = (static_cast<int32_t>(sample) * gain_q15) >> 15;
        sample = static_cast<int16_t>(scaled);
        memcpy(data + i * sizeof(sample), &sample, sizeof(sample));
    }
}
extern "C" uint8_t get_music_track_count(void) {
    return sdcar_get_music_track_count();
}

extern "C" uint8_t get_music_track_index(void) { return music_track_index; }
extern "C" uint8_t get_music_play_mode(void) { return music_play_mode; }

extern "C" void set_music_play_mode(uint8_t mode) {
    if (mode <= MUSIC_MODE_SEQUENTIAL) music_play_mode = mode;
}

extern "C" const char *get_music_track_name(uint8_t index) {
    return sdcar_get_music_track_name(index);
}

extern "C" int music_next_local_track(int index, int direction) {
    return sdcar_next_music_track(index, direction, true);
}
extern "C" bool music_select_library_track(uint8_t index, uint32_t generation) {
    char path[64]; uint32_t actual;
    if (!sdcar_copy_music_track(index, path, sizeof(path), &actual) || actual != generation) return false;
    online_music_cancel_pending();
    portENTER_CRITICAL(&music_control_mux);
    if (music_delete_state == 1) {portEXIT_CRITICAL(&music_control_mux); return false;}
    online_cancel_requested = true;
    online_request_pending = false;
    music_track_request_generation = generation;
    music_track_request = index;
    portEXIT_CRITICAL(&music_control_mux);
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
    return true;
}
extern "C" void music_select_track(uint8_t index) {
    uint32_t generation;
    if (sdcar_copy_music_track(index, nullptr, 0, &generation)) music_select_library_track(index, generation);
}
extern "C" bool music_delete_track(uint8_t index, uint32_t generation) {
    if (!music_audio_task_handle) return false;
    portENTER_CRITICAL(&music_control_mux);
    if (music_delete_state == 1) {portEXIT_CRITICAL(&music_control_mux); return false;}
    music_delete_index = index;
    music_delete_generation = generation;
    music_delete_state = 1;
    portEXIT_CRITICAL(&music_control_mux);
    xTaskNotifyGive(music_audio_task_handle);
    return true;
}
extern "C" int music_delete_status(void) {return music_delete_state;}

/* Sole consumer of the local audio FILE*. No GUI task may unlink an open song. */
static void service_music_delete(void)
{
    portENTER_CRITICAL(&music_control_mux);
    const bool pending = music_delete_state == 1;
    const uint8_t index = music_delete_index;
    const uint32_t expected = music_delete_generation;
    portEXIT_CRITICAL(&music_control_mux);
    if (!pending) return;
    if (!music_library_try_lock()) {music_delete_state = -1; return;}
    uint32_t actual;
    bool ok = sdcar_copy_music_track(index, nullptr, 0, &actual) && actual == expected;
    if (ok) {
        /* A paused or AI/video-suspended local stream is still an open file. */
        if (music_track_index == index) {
            sdcar_close_music();
            if (!music_audio_is_online()) {
                music_audio_playing = false;
                music_audio_stop_requested = true;
                music_audio_pause_requested = false;
                music_play_requested = false;
                music_resume_after_ai_audio = false;
                music_resume_after_video = false;
                music_elapsed_ms = 0;
            }
        }
        if (music_track_request == index) music_track_request = -1;
        ok = sdcar_delete_music_track(index, expected);
        if (ok && music_track_index == index) {
            const int next = sdcar_next_music_track(index, 1, true);
            music_track_index = next >= 0 ? (uint8_t)next : 0;
        }
    }
    music_library_unlock();
    music_delete_state = ok ? 2 : -1;
    printf("[MUSIC LIBRARY] delete index=%u ok=%d\n", index, ok);
}

extern "C" bool music_audio_is_playing(void) { return music_audio_playing; }

extern "C" uint32_t get_music_elapsed_ms(void)
{
    return music_elapsed_ms;
}

extern "C" bool music_audio_is_online(void)
{
    portENTER_CRITICAL(&music_control_mux);
    const bool active = online_mode || online_request_pending;
    portEXIT_CRITICAL(&music_control_mux);
    return active;
}

extern "C" bool music_audio_online_uses_path(const char *path)
{
    if (!path) return false;
    portENTER_CRITICAL(&music_control_mux);
    const bool used = (online_mode && !strcmp(path, online_path)) ||
                      (online_request_pending && !strcmp(path, online_request_path));
    portEXIT_CRITICAL(&music_control_mux);
    return used;
}

extern "C" bool music_audio_copy_active_online_path(char *path, size_t size)
{
    if (!path || !size) return false;
    portENTER_CRITICAL(&music_control_mux);
    const bool active = online_mode && online_path[0];
    if (active) strlcpy(path, online_path, size);
    else path[0] = 0;
    portEXIT_CRITICAL(&music_control_mux);
    return active;
}

extern "C" void music_audio_copy_title(char *text, size_t size)
{
    if (!text || !size) return;
    portENTER_CRITICAL(&music_control_mux);
    if (online_mode) strlcpy(text, online_title, size);
    else {
        char path[64];
        if (sdcar_copy_music_track(music_track_index, path, sizeof(path), nullptr)) strlcpy(text, path + 7, size);
        else strlcpy(text, "", size);
    }
    portEXIT_CRITICAL(&music_control_mux);
}

extern "C" void music_audio_wake(void)
{
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

extern "C" void music_audio_discard_online_queue(void)
{
    portENTER_CRITICAL(&music_control_mux);
    online_request_pending = false;
    portEXIT_CRITICAL(&music_control_mux);
}

extern "C" bool music_audio_queue_online(const char *path, const char *title, uint32_t job, bool paused)
{
    if (!path || !title || !*path || strlen(path) >= sizeof(online_request_path) ||
        !music_audio_ready || !music_audio_task_handle) return false;
    portENTER_CRITICAL(&music_control_mux);
    strlcpy(online_request_path, path, sizeof(online_request_path));
    strlcpy(online_request_title, title, sizeof(online_request_title));
    online_cancel_requested = false;
    online_request_pending = true;
    online_request_job = job;
    online_pending_play = !paused;
    music_track_request = -1;
    if (!video_session && !ai_i2s_held) music_audio_playing = !paused;
    music_audio_stop_requested = false;
    music_audio_pause_requested = paused;
    music_play_requested = false;
    portEXIT_CRITICAL(&music_control_mux);
    return true;
}

static bool init_music_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channel_config, &music_i2s_tx, nullptr);
    if (err != ESP_OK) {
        printf("Music I2S channel create failed: %s\n", esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t standard_config = {};
    standard_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MUSIC_SAMPLE_RATE);
    standard_config.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    standard_config.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    music_i2s_gpio_config.mclk = I2S_GPIO_UNUSED;
    music_i2s_gpio_config.bclk = MUSIC_I2S_BCLK;
    music_i2s_gpio_config.ws = MUSIC_I2S_LRCLK;
    music_i2s_gpio_config.dout = MUSIC_I2S_DOUT;
    music_i2s_gpio_config.din = I2S_GPIO_UNUSED;
    music_i2s_gpio_config.invert_flags.mclk_inv = false;
    music_i2s_gpio_config.invert_flags.bclk_inv = false;
    music_i2s_gpio_config.invert_flags.ws_inv = false;
    standard_config.gpio_cfg = music_i2s_gpio_config;

    err = i2s_channel_init_std_mode(music_i2s_tx, &standard_config);
    if (err != ESP_OK) {
        printf("Music I2S init failed: %s\n", esp_err_to_name(err));
        return false;
    }
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_sent = video_i2s_on_sent;
    video_i2s_cb_ok = i2s_channel_register_event_callback(
                          music_i2s_tx, &callbacks, nullptr) == ESP_OK;
    if (!video_i2s_cb_ok) puts("Music I2S sent callback failed");
    err = i2s_channel_enable(music_i2s_tx);
    if (err != ESP_OK) {
        printf("Music I2S enable failed: %s\n", esp_err_to_name(err));
        return false;
    }
    music_i2s_enabled = true;
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(music_i2s_tx, &info) == ESP_OK && info.total_dma_buf_size)
        music_dma_bytes = info.total_dma_buf_size;

    printf("Music I2S ready: BCLK=GPIO%d LRCLK=GPIO%d DIN=GPIO%d\n",
                  (int)MUSIC_I2S_BCLK, (int)MUSIC_I2S_LRCLK,
                  (int)MUSIC_I2S_DOUT);
    return true;
}

static bool disable_music_i2s_for_ai(void)
{
    if (!music_i2s_tx) return false;
    if (!music_i2s_enabled) return true;
    const esp_err_t error = i2s_channel_disable(music_i2s_tx);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        printf("Music I2S disable before AI failed: %s\n",
                      esp_err_to_name(error));
        return false;
    }
    music_i2s_enabled = false;
    return true;
}

static bool route_music_i2s_gpio(void)
{
    if (!music_i2s_tx) return false;
    const esp_err_t error = i2s_channel_reconfig_std_gpio(
        music_i2s_tx, &music_i2s_gpio_config);
    if (error != ESP_OK) {
        printf("Music I2S GPIO restore failed: %s\n",
                      esp_err_to_name(error));
        return false;
    }
    puts("Music I2S GPIO routed back to I2S0");
    return true;
}

static bool set_output_sample_rate(uint32_t sample_rate)
{
    if (!music_i2s_tx || music_i2s_enabled) return false;
    if (output_sample_rate == sample_rate) return true;
    const i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    const esp_err_t error = i2s_channel_reconfig_std_clock(music_i2s_tx, &clock);
    if (error != ESP_OK) {
        printf("Audio I2S clock change failed: %s\n",
                      esp_err_to_name(error));
        return false;
    }
    output_sample_rate = sample_rate;
    return true;
}

static bool init_ai_i2s(void)
{
    /* MusicAudioTask has yielded the one physical output. Reuse its DMA
     * channel instead of allocating I2S1 and rerouting the same GPIOs. */
    if (!ai_i2s_held || !disable_music_i2s_for_ai() ||
        !set_output_sample_rate(AI_I2S_SAMPLE_RATE)) return false;
    const esp_err_t enable_error = i2s_channel_enable(music_i2s_tx);
    if (enable_error != ESP_OK) {
        printf("AI I2S enable failed: %s\n", esp_err_to_name(enable_error));
        return false;
    }
    music_i2s_enabled = true;
    ai_i2s_enabled = true;
    puts("AI I2S ready: shared I2S0 at 16 kHz");
    return true;
}

static bool finish_ai_audio(bool success)
{
    if (!ai_audio_playing || !ai_i2s_held) return false;
    if (ai_i2s_enabled && music_i2s_enabled) {
        /* Overwrite the shared DMA before changing rate: a music resume must
         * not replay the end of the AI reply at 44.1 kHz. */
        memset(music_audio_buffer, 0, sizeof(music_audio_buffer));
        size_t written = 0;
        const esp_err_t error = i2s_channel_write(
            music_i2s_tx, music_audio_buffer, sizeof(music_audio_buffer),
            &written, pdMS_TO_TICKS(100));
        if (error != ESP_OK || written < music_dma_bytes) {
            printf("AI I2S silence failed: %s\n", esp_err_to_name(error));
            success = false;
        }
    }
    if (!disable_music_i2s_for_ai() ||
        !set_output_sample_rate(MUSIC_SAMPLE_RATE)) success = false;
    ai_i2s_enabled = false;
    /* Restore 44.1 kHz before publishing that music/video may own the DMA. */
    ai_i2s_held = false;
    ai_audio_playing = false;
    resume_music_after_ai_audio();
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
    if (ai_audio_mutex) xSemaphoreGive(ai_audio_mutex);
    return success;
}

static bool write_ai_i2s_all(const int16_t *samples, size_t byte_count)
{
    const uint8_t *data = reinterpret_cast<const uint8_t *>(samples);
    size_t total_written = 0;
    const uint32_t began = app_millis();
    while (total_written < byte_count) {
        if (app_millis() - began >= 2000) {
            puts("AI I2S write failed: progress timeout");
            return false;
        }
        size_t written = 0;
        const esp_err_t write_error = i2s_channel_write(
            music_i2s_tx, data + total_written, byte_count - total_written,
            &written, pdMS_TO_TICKS(1000));
        total_written += written;
        if (write_error != ESP_OK && written == 0) {
            printf("AI I2S write failed: %s\n",
                          esp_err_to_name(write_error));
            return false;
        }
        if (written == 0) {
            puts("AI I2S write failed: no progress");
            return false;
        }
    }
    return true;
}

static bool open_music_wav(void)
{
    uint8_t track_count = sdcar_get_music_track_count();
    if (track_count == 0) {
        sdcar_scan_music_tracks();
        track_count = sdcar_get_music_track_count();
    }
    if (track_count == 0) return false;
    if (!sdcar_music_track_valid(music_track_index)) {
        const int first = sdcar_next_music_track(-1, 1, false);
        if (first < 0) return false;
        music_track_index = (uint8_t)first;
    }
    const bool opened = sdcar_open_music_wav(music_track_index);
    if (opened) music_elapsed_ms = 0;
    return opened;
}

static bool open_alarm_wav(void) {
    return sdcar_open_alarm_wav();
}

static bool enable_music_i2s_channel(void)
{
    if (!music_i2s_tx || ai_i2s_held) return false;
    if (output_sample_rate != MUSIC_SAMPLE_RATE &&
        (!disable_music_i2s_for_ai() || !set_output_sample_rate(MUSIC_SAMPLE_RATE)))
        return false;
    if (music_i2s_enabled) return true;

    if (!route_music_i2s_gpio()) return false;

    esp_err_t error = i2s_channel_enable(music_i2s_tx);
    if (error == ESP_ERR_INVALID_STATE) {
        /* Recover from a stale software flag or an interrupted stop. */
        i2s_channel_disable(music_i2s_tx);
        error = i2s_channel_enable(music_i2s_tx);
    }
    if (error != ESP_OK) {
        printf("Music I2S enable failed: %s\n", esp_err_to_name(error));
        return false;
    }
    music_i2s_enabled = true;
    return true;
}

static bool enable_online_i2s(uint32_t rate)
{
    if (rate < 8000 || rate > 48000 || !music_i2s_tx || ai_i2s_held) return false;
    if (output_sample_rate != rate &&
        (!disable_music_i2s_for_ai() || !set_output_sample_rate(rate))) return false;
    if (music_i2s_enabled) return true;
    if (!route_music_i2s_gpio() || i2s_channel_enable(music_i2s_tx) != ESP_OK) return false;
    music_i2s_enabled = true;
    return true;
}

static void close_online_decoder(void)
{
    mp3_source_close(online_decoder);
    online_decoder = nullptr;
}

static void service_online_requests(void)
{
    bool cancel, replace, pause_output = false;
    portENTER_CRITICAL(&music_control_mux);
    cancel = online_cancel_requested;
    replace = online_request_pending && !ai_i2s_held && !video_session;
    online_cancel_requested = false;
    if (cancel) { online_mode = false; online_path[0] = online_title[0] = 0; }
    if (replace) {
        strlcpy(online_path, online_request_path, sizeof(online_path));
        strlcpy(online_title, online_request_title, sizeof(online_title));
        online_request_pending = false;
        online_mode = true;
        online_job = online_request_job;
        music_audio_playing = online_pending_play;
        pause_output = !online_pending_play;
        music_elapsed_ms = 0;
    }
    portEXIT_CRITICAL(&music_control_mux);
    if (cancel || replace) {
        close_online_decoder();
        if (replace) sdcar_close_music();
        if (pause_output) mute_music_i2s();
    }
}

static bool service_online_frame(void)
{
    if (!online_decoder) online_decoder = mp3_source_open(online_path);
    mp3_source_frame_t frame = {};
    const int got = online_decoder ? mp3_source_read_frame(online_decoder, &frame) : -1;
    if (got <= 0) {
        if (got < 0) online_music_playback_error(online_job, "在线歌曲解码失败，请选择其他版本重试");
        close_online_decoder();
        printf("[NETMUSIC] playback ended result=%d elapsed_ms=%u\n", got, (unsigned)music_elapsed_ms);
        return false;
    }
    const size_t count = static_cast<size_t>(frame.frames) * 4;
    if (!frame.pcm || !frame.frames || count > sizeof(music_audio_buffer) ||
        (frame.channels != 1 && frame.channels != 2) || !enable_online_i2s(frame.sample_rate)) {
        online_music_playback_error(online_job, "在线歌曲音频输出失败");
        return false;
    }
    if (frame.channels == 2) memcpy(music_audio_buffer, frame.pcm, count);
    else {
        int16_t *out = reinterpret_cast<int16_t *>(music_audio_buffer);
        for (unsigned i = 0; i < frame.frames; ++i) out[i * 2] = out[i * 2 + 1] = frame.pcm[i];
    }
    apply_audio_volume(music_audio_buffer, count);
    size_t sent = 0;
    while (sent < count && music_audio_playing && !music_audio_pause_requested && !music_audio_stop_requested) {
        size_t written = 0;
        const esp_err_t error = i2s_channel_write(music_i2s_tx, music_audio_buffer + sent,
            count - sent, &written, pdMS_TO_TICKS(100));
        sent += written;
        if ((error != ESP_OK && error != ESP_ERR_TIMEOUT) || written == 0) {
            online_music_playback_error(online_job, "在线歌曲 I2S 播放失败");
            return false;
        }
    }
    music_elapsed_ms += static_cast<uint32_t>(sent * 1000ULL / (frame.sample_rate * 4ULL));
    return true;
}

#include "video_audio.inc"

extern "C" void play_alarm_wav(void) {
    sound_alarm_start(1);
}

extern "C" void sound_alarm_start(uint16_t minutes)
{
    if (!minutes || minutes > 60) return;
    alarm_clock_started.store(app_millis(), std::memory_order_relaxed);
    alarm_clock_duration.store((uint32_t)minutes * 60000u, std::memory_order_relaxed);
    alarm_clock_running.store(true, std::memory_order_release);
    alarm_audio_requested = true;
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

extern "C" void sound_alarm_stop(void)
{
    alarm_clock_running.store(false, std::memory_order_release);
    alarm_audio_requested = false;
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

extern "C" bool sound_alarm_is_active(void)
{
    return alarm_clock_running.load(std::memory_order_acquire);
}



extern "C" void music_audio_play(void)
{
    if (!music_audio_ready || !music_i2s_tx || !music_audio_task_handle) {
        puts("Music: play ignored, audio task is not ready");
        return;
    }
    online_music_note_pause(false);
    portENTER_CRITICAL(&music_control_mux);
    if (online_request_pending) online_pending_play = true;
    const bool output_busy = video_session || ai_i2s_held;
    if (output_busy) {
        if (video_session) music_resume_after_video = true;
        else music_resume_after_ai_audio = true;
        music_audio_stop_requested = false;
        music_audio_pause_requested = false;
        music_play_requested = false;
        portEXIT_CRITICAL(&music_control_mux);
        puts("Music: play queued until output is free");
        return;
    }
    portEXIT_CRITICAL(&music_control_mux);
    portENTER_CRITICAL(&music_control_mux);
    music_play_requested = music_audio_stop_requested || music_audio_pause_requested;
    music_audio_playing = true;
    if (!music_play_requested) {
        music_audio_stop_requested = false;
        music_audio_pause_requested = false;
    }
    portEXIT_CRITICAL(&music_control_mux);
    printf("Music: play requested, track=%u, i2s=%s\n",
                  static_cast<unsigned>(music_track_index),
                  music_i2s_enabled ? "enabled" : "will-enable");
    xTaskNotifyGive(music_audio_task_handle);
}

extern "C" void music_audio_pause(void)
{
    online_music_note_pause(true);
    portENTER_CRITICAL(&music_control_mux);
    if (online_request_pending) online_pending_play = false;
    music_play_requested = false;
    music_audio_playing = false;
    music_audio_stop_requested = false;
    music_audio_pause_requested = true;
    if (video_session) music_resume_after_video = false;
    portEXIT_CRITICAL(&music_control_mux);
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

extern "C" void music_audio_stop(void)
{
    online_music_cancel_pending();
    /* An explicit user stop cancels any pending post-AI resume. */
    music_resume_after_ai_audio = false;
    music_track_request = -1;
    portENTER_CRITICAL(&music_control_mux);
    online_cancel_requested = true;
    online_request_pending = false;
    music_play_requested = false;
    music_audio_playing = false;
    music_audio_pause_requested = false;
    music_audio_stop_requested = true;
    if (video_session) music_resume_after_video = false;
    portEXIT_CRITICAL(&music_control_mux);
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

static void load_audio_volume(void)
{
    uint8_t stored = 100;
    if (!storage_nvs_get_u8("audio", "volume", &stored) || stored > 100) stored = 100;
    audio_volume_percent = stored;
}

extern "C" uint8_t get_audio_volume(void)
{
    return audio_volume_percent;
}

extern "C" void set_audio_volume(int percent)
{
    percent = std::clamp(percent, 0, 100);
    audio_volume_percent = static_cast<uint8_t>(percent);
}

extern "C" void save_audio_volume(void)
{
    storage_nvs_set_u8("audio", "volume", audio_volume_percent);
}

static void resume_music_after_ai_audio(void)
{
    if (!music_resume_after_ai_audio) return;
    music_resume_after_ai_audio = false;
    music_audio_play();
    puts("Music resumed after AI voice playback");
}

extern "C" void sound_init(void)
{
    load_audio_volume();
    sdcar_scan_music_tracks();
    audio_cmd_lock = xSemaphoreCreateMutex();
    audio_cmd_done = xSemaphoreCreateBinary();
    music_audio_ready = init_music_i2s();
    if (music_audio_ready && xTaskCreatePinnedToCore(
            music_audio_task, "MusicAudioTask", 6144, nullptr, 2,
            &music_audio_task_handle, 0) != pdPASS) {
        music_audio_ready = false;
        music_audio_task_handle = nullptr;
        puts("Music: audio task creation failed");
    }
    ai_audio_mutex = xSemaphoreCreateMutex();
    if (!ai_audio_mutex) puts("AI audio: mutex creation failed");
}

extern "C" bool sound_is_ready(void)
{
    return music_audio_ready;
}

extern "C" bool sound_ai_output_begin(void)
{
    if (alarm_preempts_ai()) return false;
    if (!ai_audio_mutex || xSemaphoreTake(ai_audio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        puts("AI audio: previous playback is still active");
        return false;
    }
    ai_audio_playing = true;
    if (!request_ai_release()) {
        ai_audio_playing = false;
        xSemaphoreGive(ai_audio_mutex);
        return false;
    }
    if (!init_ai_i2s()) return finish_ai_audio(false);
    return true;
}

extern "C" bool sound_ai_output_write(int16_t *samples, size_t byte_count)
{
    if (alarm_preempts_ai()) return false;
    if (!samples || !music_i2s_tx || !ai_i2s_enabled || !ai_i2s_held) return false;
    apply_audio_volume(reinterpret_cast<uint8_t *>(samples), byte_count);
    return write_ai_i2s_all(samples, byte_count);
}

extern "C" bool sound_ai_output_begin_rate(uint32_t sample_rate)
{
    if (alarm_preempts_ai()) return false;
    if (sample_rate == AI_I2S_SAMPLE_RATE) return sound_ai_output_begin();
    if (sample_rate != 24000 || !ai_audio_mutex ||
        xSemaphoreTake(ai_audio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) return false;
    ai_audio_playing = true;
    if (!request_ai_release()) {
        ai_audio_playing = false;
        xSemaphoreGive(ai_audio_mutex);
        return false;
    }
    if (!disable_music_i2s_for_ai() || !set_output_sample_rate(sample_rate) ||
        i2s_channel_enable(music_i2s_tx) != ESP_OK) return finish_ai_audio(false);
    music_i2s_enabled = ai_i2s_enabled = true;
    puts("AI direct speech: shared I2S0 at 24 kHz");
    return true;
}

extern "C" bool sound_ai_output_finish(bool success)
{
    return finish_ai_audio(success);
}

extern "C" bool sound_ai_audio_is_playing(void)
{
    return ai_audio_playing;
}

extern "C" void sound_pause_music_for_ai_audio(void)
{
    if (!music_audio_is_playing()) return;
    music_resume_after_ai_audio = true;
    puts("Pausing music before AI voice playback");
    music_audio_pause();
    vTaskDelay(pdMS_TO_TICKS(150));
}
