#include <Arduino.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s_std.h>
#include "SDcar.h"
#include "Sound.h"
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
static volatile bool music_audio_playing = false;
static volatile bool music_audio_stop_requested = false;
static volatile bool music_audio_pause_requested = false;
static volatile bool music_play_requested = false;
static portMUX_TYPE music_control_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool music_resume_after_ai_audio = false;
static volatile bool alarm_audio_requested = false;
static bool alarm_audio_active = false;
static uint8_t music_audio_buffer[8192];
static volatile uint8_t audio_volume_percent = 100;
static i2s_std_gpio_config_t music_i2s_gpio_config = {};
static SemaphoreHandle_t ai_audio_mutex = nullptr;
static volatile bool ai_audio_playing = false;
static volatile uint32_t music_elapsed_ms = 0;
static bool finish_ai_audio(bool success);
static void resume_music_after_ai_audio(void);
static bool video_i2s_on_sent(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user);

static SemaphoreHandle_t audio_cmd_lock = nullptr;
static SemaphoreHandle_t audio_cmd_done = nullptr;
static SemaphoreHandle_t video_wav_done = nullptr;
static TaskHandle_t video_wav_task = nullptr;
static portMUX_TYPE video_i2s_mux = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE video_ring_mux = portMUX_INITIALIZER_UNLOCKED;
static constexpr size_t VIDEO_RING_BYTES = 48 * 1024;
static constexpr size_t VIDEO_STAGE_BYTES = 2048;
static uint8_t *video_ring = nullptr;
static uint8_t video_stage[VIDEO_STAGE_BYTES];
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

extern "C" void music_select_track(uint8_t index) {
    if (index >= sdcar_get_music_track_count()) return;
    music_track_request = index;
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}

extern "C" bool music_audio_is_playing(void) { return music_audio_playing; }

extern "C" uint32_t get_music_elapsed_ms(void)
{
    return music_elapsed_ms;
}

static bool init_music_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    channel_config.auto_clear_after_cb = true;
    esp_err_t err = i2s_new_channel(&channel_config, &music_i2s_tx, nullptr);
    if (err != ESP_OK) {
        Serial.printf("Music I2S channel create failed: %s\n", esp_err_to_name(err));
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
        Serial.printf("Music I2S init failed: %s\n", esp_err_to_name(err));
        return false;
    }
    i2s_event_callbacks_t callbacks = {};
    callbacks.on_sent = video_i2s_on_sent;
    video_i2s_cb_ok = i2s_channel_register_event_callback(
                          music_i2s_tx, &callbacks, nullptr) == ESP_OK;
    if (!video_i2s_cb_ok) Serial.println("Music I2S sent callback failed");
    err = i2s_channel_enable(music_i2s_tx);
    if (err != ESP_OK) {
        Serial.printf("Music I2S enable failed: %s\n", esp_err_to_name(err));
        return false;
    }
    music_i2s_enabled = true;
    i2s_chan_info_t info = {};
    if (i2s_channel_get_info(music_i2s_tx, &info) == ESP_OK && info.total_dma_buf_size)
        music_dma_bytes = info.total_dma_buf_size;

    Serial.printf("Music I2S ready: BCLK=GPIO%d LRCLK=GPIO%d DIN=GPIO%d\n",
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
        Serial.printf("Music I2S disable before AI failed: %s\n",
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
        Serial.printf("Music I2S GPIO restore failed: %s\n",
                      esp_err_to_name(error));
        return false;
    }
    Serial.println("Music I2S GPIO routed back to I2S0");
    return true;
}

static bool set_output_sample_rate(uint32_t sample_rate)
{
    if (!music_i2s_tx || music_i2s_enabled) return false;
    if (output_sample_rate == sample_rate) return true;
    const i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    const esp_err_t error = i2s_channel_reconfig_std_clock(music_i2s_tx, &clock);
    if (error != ESP_OK) {
        Serial.printf("Audio I2S clock change failed: %s\n",
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
        Serial.printf("AI I2S enable failed: %s\n", esp_err_to_name(enable_error));
        return false;
    }
    music_i2s_enabled = true;
    ai_i2s_enabled = true;
    Serial.println("AI I2S ready: shared I2S0 at 16 kHz");
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
            Serial.printf("AI I2S silence failed: %s\n", esp_err_to_name(error));
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
    while (total_written < byte_count) {
        size_t written = 0;
        const esp_err_t write_error = i2s_channel_write(
            music_i2s_tx, data + total_written, byte_count - total_written,
            &written, pdMS_TO_TICKS(1000));
        total_written += written;
        if (write_error != ESP_OK && written == 0) {
            Serial.printf("AI I2S write failed: %s\n",
                          esp_err_to_name(write_error));
            return false;
        }
        if (written == 0) {
            Serial.println("AI I2S write failed: no progress");
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
    if (music_track_index >= track_count) music_track_index = 0;
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
        Serial.printf("Music I2S enable failed: %s\n", esp_err_to_name(error));
        return false;
    }
    music_i2s_enabled = true;
    return true;
}

#include "video_audio.inc"

extern "C" void play_alarm_wav(void) {
    alarm_audio_requested = true;
    if (music_audio_task_handle) xTaskNotifyGive(music_audio_task_handle);
}



extern "C" void music_audio_play(void)
{
    if (!music_audio_ready || !music_i2s_tx || !music_audio_task_handle) {
        Serial.println("Music: play ignored, audio task is not ready");
        return;
    }
    portENTER_CRITICAL(&music_control_mux);
    const bool output_busy = video_session || ai_i2s_held;
    if (output_busy) {
        if (video_session) music_resume_after_video = true;
        else music_resume_after_ai_audio = true;
        music_audio_stop_requested = false;
        music_audio_pause_requested = false;
        music_play_requested = false;
        portEXIT_CRITICAL(&music_control_mux);
        Serial.println("Music: play queued until output is free");
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
    Serial.printf("Music: play requested, track=%u, i2s=%s\n",
                  static_cast<unsigned>(music_track_index),
                  music_i2s_enabled ? "enabled" : "will-enable");
    xTaskNotifyGive(music_audio_task_handle);
}

extern "C" void music_audio_pause(void)
{
    portENTER_CRITICAL(&music_control_mux);
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
    /* An explicit user stop cancels any pending post-AI resume. */
    music_resume_after_ai_audio = false;
    music_track_request = -1;
    portENTER_CRITICAL(&music_control_mux);
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
    Preferences preferences;
    if (!preferences.begin("audio", true)) return;
    const uint8_t stored = preferences.getUChar("volume", 100);
    preferences.end();
    audio_volume_percent = stored <= 100 ? stored : 100;
}

extern "C" uint8_t get_audio_volume(void)
{
    return audio_volume_percent;
}

extern "C" void set_audio_volume(int percent)
{
    percent = constrain(percent, 0, 100);
    audio_volume_percent = static_cast<uint8_t>(percent);
}

extern "C" void save_audio_volume(void)
{
    Preferences preferences;
    if (!preferences.begin("audio", false)) return;
    preferences.putUChar("volume", audio_volume_percent);
    preferences.end();
}

static void resume_music_after_ai_audio(void)
{
    if (!music_resume_after_ai_audio) return;
    music_resume_after_ai_audio = false;
    music_audio_play();
    Serial.println("Music resumed after AI voice playback");
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
        Serial.println("Music: audio task creation failed");
    }
    ai_audio_mutex = xSemaphoreCreateMutex();
    if (!ai_audio_mutex) Serial.println("AI audio: mutex creation failed");
}

extern "C" bool sound_is_ready(void)
{
    return music_audio_ready;
}

extern "C" bool sound_ai_output_begin(void)
{
    if (!ai_audio_mutex || xSemaphoreTake(ai_audio_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        Serial.println("AI audio: previous playback is still active");
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
    if (!samples || !music_i2s_tx || !ai_i2s_enabled || !ai_i2s_held) return false;
    apply_audio_volume(reinterpret_cast<uint8_t *>(samples), byte_count);
    return write_ai_i2s_all(samples, byte_count);
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
    Serial.println("Pausing music before AI voice playback");
    music_audio_pause();
    vTaskDelay(pdMS_TO_TICKS(150));
}
