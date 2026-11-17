#include "MediaFiles.h"
#include "FileTransfer.h"
#include "SystemUpdate.h"
#include <algorithm>
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "IdfRuntime.h"
#include "MPU6050_ESP32.h"
#include "SDcar.h"
#include "Sound.h"
#include "Web_ai.h"
#include "AiConfig.h"
#include "WeatherConfig.h"
#include "AiSpeech.h"
#include "AiVoice.h"
#include "AlarmService.h"
#include "AlarmUI.h"
#include "OnlineMusic.h"
#include "MusicWeb.h"
#include "setwifi.h"
#include "QuickPanel.h"
#include "EdgeBack.h"
#include "AppLauncher.h"
#include "QuickSettingsService.h"
#include "board_backlight.h"
#include "board_i2c.h"
#include "board_lcd.h"
#include "board_pins.h"
#include "DisplayControl.h"
#include "DisplayRefresh.h"
#include "ScreenIdle.h"
#include "LifeData.h"
#include "UiTransition.h"
#include "BootSplash.h"
#include "FirmwareVersion.h"
#include "HotspotName.h"
#include "touch.h"
#include "video_draw.h"
#include "SerialUpload.h"
#include "ui/generated/gui_guider.h"
#include "ui/custom/custom.h"

lv_ui guider_ui;
volatile bool i2c_busy = false;
static bool i2c_available = false;
static SemaphoreHandle_t i2c_mutex = nullptr;
// Publish coordinates and press state atomically across SensorTask/core 0 and
// LVGLTask/core 1; separate volatile fields can mix two different samples.
static std::atomic<uint32_t> touch_pointer_snapshot{0};
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf = nullptr;
static lv_disp_drv_t disp_drv;
static float roll = 0, pitch = 0, yaw = 0, gyro_z_offset = 0;
static uint32_t last_mpu_time = 0;
static bool ui_initialized = false;
static std::atomic<bool> network_services_ready{false};
static constexpr uint32_t LVGL_STACK_BYTES = 12 * 1024;

extern "C" {
volatile uint32_t gui_fps = 0;
volatile int8_t swipe_dir = 0;
extern int current_page;
void animate_to_page(int page);
}

static volatile bool video_vsync_sync_enabled = false;
extern "C" void set_video_vsync_sync(bool enabled) { video_vsync_sync_enabled = enabled; }
extern "C" bool video_playback_active(void) { return video_vsync_sync_enabled; }

static uint32_t touch_snapshot_load(void)
{
    return touch_pointer_snapshot.load(std::memory_order_relaxed);
}

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data)
{
    static bool was_pressed = false, gesture_consumed = false;
    static int start_x = 0, start_y = 0;
    static uint32_t last_swipe = 0;
    const uint32_t pointer = touch_snapshot_load();
    const bool pressed = (pointer & 1u) != 0;
    const int x = (pointer >> 1) & 511u, y = (pointer >> 10) & 511u;
    static bool boot_release;
    if (boot_splash_busy() || boot_release) {
        boot_release = pressed;
        was_pressed = false;
        gesture_consumed = true;
        swipe_dir = 0;
        data->state = LV_INDEV_STATE_REL;
        data->point.x = x; data->point.y = y;
        return;
    }
    if (screen_idle_pointer(pressed, x, y, app_millis())) {
        was_pressed = false;
        gesture_consumed = true;
        swipe_dir = 0;
        (void)quick_panel_pointer(false, x, y);
        (void)edge_back_pointer(false, x, y, false);
        data->state = LV_INDEV_STATE_REL;
        data->point.x = x;
        data->point.y = y;
        return;
    }
    static bool alarm_seen = false, alarm_release = false;
    if (alarm_ui_alert_visible()) {
        if (!alarm_seen) {
            alarm_release = pressed;
            lv_indev_t *input = lv_indev_get_act();
            if (input) {lv_indev_reset(input, nullptr); lv_indev_wait_release(input);}
        }
        alarm_seen = true;
        if (!pressed) alarm_release = false;
        was_pressed = false; gesture_consumed = true; swipe_dir = 0;
        (void)quick_panel_pointer(false, x, y);
        (void)edge_back_pointer(false, x, y, false);
        data->state = pressed && !alarm_release ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
        data->point.x = x; data->point.y = y;
        return;
    }
    alarm_seen = false;
    static bool transition_release;
    if (ui_transition_busy() || transition_release) {
        transition_release = pressed;
        was_pressed = false;
        gesture_consumed = true;
        swipe_dir = 0;
        (void)quick_panel_pointer(false, x, y);
        (void)edge_back_pointer(false, x, y, false);
        data->state = LV_INDEV_STATE_REL;
        data->point.x = x;
        data->point.y = y;
        return;
    }
    if (pressed && !was_pressed) {
        start_x = x;
        start_y = y;
        gesture_consumed = false;
    }
    const bool panel_consumed = quick_panel_pointer(pressed, x, y);
    const bool back_consumed = edge_back_pointer(pressed, x, y,
        !panel_consumed && !quick_panel_is_visible() && !app_launcher_is_dragging() && custom_navigation_can_back());
    const bool consumed = panel_consumed || back_consumed;
    gesture_consumed = gesture_consumed || consumed || app_launcher_is_dragging();
    if (!pressed && was_pressed && !gesture_consumed && !quick_panel_is_visible() &&
        lv_scr_act() == guider_ui.screen && app_millis() - last_swipe > 400) {
        const int dx = x - start_x, dy = y - start_y;
        if (abs(dx) > 50 && abs(dx) > abs(dy) * 3 / 2) {
            if (dx > 0 && current_page > 0) swipe_dir = 1;
            else if (dx < 0 && current_page < 2) swipe_dir = -1;
            last_swipe = app_millis();
            // A page swipe must not release-click the app icon underneath it.
            lv_indev_t *input = lv_indev_get_act();
            if (input) {lv_indev_reset(input, nullptr);lv_indev_wait_release(input);}
        }
    }
    was_pressed = pressed;
    data->state = pressed && !consumed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = x;
    data->point.y = y;
}

static void touch_poll(void)
{
    static bool error_active = false;
    static uint32_t error_started = 0;
    const uint32_t now = app_millis();
    if (!i2c_mutex || xSemaphoreTake(i2c_mutex, 0) != pdTRUE) return;
    i2c_busy = true;
    int x = 0, y = 0;
    const touch_sample_status_t sample = touch_read_sample(&x, &y);
    uint32_t pointer = touch_snapshot_load();
    if (sample == TOUCH_SAMPLE_PRESSED) {
        pointer = 1u | (static_cast<uint32_t>(x) << 1) | (static_cast<uint32_t>(y) << 10);
        error_active = false;
    } else if (sample == TOUCH_SAMPLE_RELEASED) {
        pointer &= ~1u;
        error_active = false;
    } else if (sample == TOUCH_SAMPLE_ERROR) {
        if (!error_active) { error_active = true; error_started = now; }
        // A brief bus error is not a release. A sustained failure must not leave
        // a button held forever; keep the last coordinates for the release.
        if (now - error_started >= 120) pointer &= ~1u;
    } else {
        error_active = false; // Healthy status read with no new controller data.
    }
    touch_pointer_snapshot.store(pointer, std::memory_order_relaxed);
    i2c_busy = false;
    xSemaphoreGive(i2c_mutex);
}

static bool mpu_init(void)
{
    if (!MPU6050_Init()) return false;
    float sum = 0;
    for (int i = 0; i < 200; i++) {
        float ax, ay, az, gx, gy, gz, temp;
        if (MPU6050_Read(&ax, &ay, &az, &gx, &gy, &gz, &temp)) sum += gz;
        app_delay_ms(2);
    }
    gyro_z_offset = sum / 200.0f;
    last_mpu_time = app_millis();
    return true;
}

static void mpu_update(void)
{
    if (!i2c_available) return;
    float ax, ay, az, gx, gy, gz, temp;
    if (!MPU6050_Read(&ax, &ay, &az, &gx, &gy, &gz, &temp)) return;
    const float ar = atan2f(ay, sqrtf(ax * ax + az * az)) * 57.29578f;
    const float ap = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.29578f;
    const uint32_t now = app_millis();
    float dt = std::clamp((now - last_mpu_time) / 1000.0f, 0.001f, 0.05f);
    last_mpu_time = now;
    const float gz_use = fabsf(gz - gyro_z_offset) < 0.01f ? 0 : gz - gyro_z_offset;
    screen_idle_motion(gx, gy, now);
    roll = .96f * (roll + gx * dt) + .04f * ar;
    pitch = .96f * (pitch + gy * dt) + .04f * ap;
    yaw += gz_use * dt * 57.29578f;
    if (yaw > 360) yaw -= 360;
    if (yaw < 0) yaw += 360;
    update_gyro_data(roll, pitch, yaw, temp);
}

static void sensor_task(void *)
{
    TickType_t next = xTaskGetTickCount();
    uint32_t last_mpu = app_millis();
    while (true) {
        touch_poll();
        const uint32_t now = app_millis();
        if (now - last_mpu >= BOARD_MPU_POLL_MS) {
            last_mpu = now;
            if ((!video_playback_active() || screen_idle_is_asleep()) && i2c_mutex &&
                xSemaphoreTake(i2c_mutex, 0) == pdTRUE) {
                mpu_update();
                xSemaphoreGive(i2c_mutex);
            }
        }
        const TickType_t period = pdMS_TO_TICKS(BOARD_TOUCH_POLL_MS);
        // Preserve the nominal cadence without catch-up bursts after an I2C timeout.
        const TickType_t finished = xTaskGetTickCount();
        if (finished - next >= period) next = finished;
        vTaskDelayUntil(&next, period);
    }
}

static uint32_t gui_refr_frames = 0;
static uint32_t gui_refr_total_ms = 0;
static uint32_t gui_refr_max_ms = 0;
static uint32_t gui_video_presented_frames = 0;
static uint32_t gui_video_sequence_seen = 0;

static void gui_refresh_monitor_cb(lv_disp_drv_t *disp_drv, uint32_t time_ms, uint32_t px)
{
    (void)disp_drv;
    (void)px;
    gui_refr_frames++;
    gui_refr_total_ms += time_ms;
    if (time_ms > gui_refr_max_ms) gui_refr_max_ms = time_ms;
    const uint32_t sequence = get_video_frame_sequence();
    if (video_playback_active() && sequence != gui_video_sequence_seen)
        ++gui_video_presented_frames;
    gui_video_sequence_seen = sequence;
}

static void lvgl_task(void *)
{
    // Play the reveal only after initialization has handed LVGL to this task.
    boot_splash_ready();
    uint32_t last_fps_ms = app_millis();
    uint32_t last_log_ms = last_fps_ms;
    uint32_t last_backlight_log_ms = last_fps_ms;
    uint32_t fps_vsync_base = board_lcd_read_vsync_count();
    uint32_t log_frames_base = gui_refr_frames;
    uint32_t log_total_ms_base = gui_refr_total_ms;
    uint32_t log_vsync_base = board_lcd_read_vsync_count();
    uint32_t rgb_frames_base = board_lcd_read_frame_complete_count();
    uint32_t wifi_tick_max_ms = 0;
    uint32_t video_frames_base = gui_video_presented_frames;
    uint32_t video_read_frames_base = 0;
    uint32_t video_read_ms_base = 0;
    get_video_read_metrics(&video_read_frames_base, &video_read_ms_base, nullptr);
    gui_refr_max_ms = 0;

    while (true) {
        lv_timer_handler();
        display_refresh_poll();
        screen_idle_poll(app_millis());
        service_display_restart();
        if (swipe_dir) {
            int direction = swipe_dir;
            swipe_dir = 0;
            if (!quick_panel_is_visible() && lv_scr_act() == guider_ui.screen &&
                ((direction > 0 && current_page > 0) || (direction < 0 && current_page < 2))) {
                animate_to_page(direction > 0 ? current_page - 1 : current_page + 1);
            }
        }

        const uint32_t wifi_started_ms = app_millis();
        // Publish completed weather results and queue refreshes while idle.
        // Network waits run on the weather worker, outside the GUI task.
        if (network_services_ready.load()) wifi_service_tick(!(touch_snapshot_load() & 1u) && !quick_panel_is_visible() && !edge_back_is_active() && !app_launcher_is_dragging() && !ui_transition_busy());
        const uint32_t wifi_elapsed_ms = app_millis() - wifi_started_ms;
        if (wifi_elapsed_ms > wifi_tick_max_ms) wifi_tick_max_ms = wifi_elapsed_ms;

        const uint32_t now = app_millis();
        const uint32_t fps_elapsed_ms = now - last_fps_ms;
        if (fps_elapsed_ms >= 1000) {
            const uint32_t vsync_now = board_lcd_read_vsync_count();
            const uint32_t vsync_delta = vsync_now - fps_vsync_base;
            gui_fps = static_cast<uint32_t>((vsync_delta * 1000ULL + fps_elapsed_ms / 2) / fps_elapsed_ms);
            fps_vsync_base = vsync_now;
            last_fps_ms = now;
        }

        const uint32_t log_elapsed_ms = now - last_log_ms;
        if (log_elapsed_ms >= 2000) {
            if (now - last_backlight_log_ms >= 10000) {
                board_backlight_log_state();
                last_backlight_log_ms = now;
            }
            const uint32_t frame_delta = gui_refr_frames - log_frames_base;
            const uint32_t total_ms = gui_refr_total_ms - log_total_ms_base;
            const uint32_t avg_ms = frame_delta == 0 ? 0 : total_ms / frame_delta;
            const uint32_t max_ms = gui_refr_max_ms;
            const uint32_t rgb_now = board_lcd_read_frame_complete_count();
            const uint32_t rgb_delta = rgb_now - rgb_frames_base;
            const uint32_t vsync_now = board_lcd_read_vsync_count();
            const uint32_t vsync_delta = vsync_now - log_vsync_base;

            printf(
                "[GUI] lcd_fps=%u redraw=%u rgb=%u render_avg=%ums render_max=%ums wifi_max=%ums wallpaper=%s stack_free_min=%u\n",
                static_cast<unsigned>((vsync_delta * 1000ULL + log_elapsed_ms / 2) / log_elapsed_ms),
                static_cast<unsigned>(frame_delta * 1000 / log_elapsed_ms),
                static_cast<unsigned>(rgb_delta * 1000 / log_elapsed_ms),
                static_cast<unsigned>(avg_ms),
                static_cast<unsigned>(max_ms),
                static_cast<unsigned>(wifi_tick_max_ms),
                get_wallpaper_image() ? "RAM" : "SD",
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

            uint32_t video_read_frames, video_read_ms, video_read_max_ms;
            get_video_read_metrics(&video_read_frames, &video_read_ms, &video_read_max_ms);
            const uint32_t video_read_delta = video_read_frames - video_read_frames_base;
            if (video_playback_active()) {
                uint32_t displayed_frame = 0;
                int32_t drift_ms = 0;
                uint32_t audio_pos = 0;
                uint32_t audio_duration = 0;
                uint32_t audio_cycle = 0;
                uint32_t underruns = 0;
                get_video_sync_diag(&displayed_frame, &drift_ms);
                video_audio_get_clock(&audio_pos, &audio_duration, &audio_cycle, &underruns);
                const uint32_t audio_ms = audio_pos == 0 ? 0u : static_cast<uint32_t>(
                    (static_cast<uint64_t>(audio_pos) * 1000u) / 44100u);
                printf(
                    "[VIDEO] source_fps=%.1f presented_fps=%.1f sd_read_avg=%ums sd_read_max=%ums frame=%u audio_ms=%u drift_ms=%d underrun=%u\n",
                    get_video_source_fps_milli() / 1000.0,
                    (gui_video_presented_frames - video_frames_base) * 1000.0 / log_elapsed_ms,
                    static_cast<unsigned>(video_read_delta ?
                        (video_read_ms - video_read_ms_base) / video_read_delta : 0),
                    static_cast<unsigned>(video_read_max_ms),
                    static_cast<unsigned>(displayed_frame),
                    static_cast<unsigned>(audio_ms),
                    static_cast<int>(drift_ms),
                    static_cast<unsigned>(underruns));
            }
            video_frames_base = gui_video_presented_frames;
            video_read_frames_base = video_read_frames;
            video_read_ms_base = video_read_ms;

            gui_refr_max_ms = 0;
            wifi_tick_max_ms = 0;
            log_frames_base = gui_refr_frames;
            log_total_ms_base = gui_refr_total_ms;
            log_vsync_base = vsync_now;
            rgb_frames_base = rgb_now;
            last_log_ms = now;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}







static void app_services_task(void *)
{
    // AppInit used a large temporary stack for fonts and VFS initialization.
    // Let the idle task reclaim it before allocating permanent task stacks.
    app_delay_ms(100);
    app_log_memory("after AppInit reclaimed");
    if (ui_initialized) {
        if (xTaskCreatePinnedToCore(lvgl_task, "LVGLTask", LVGL_STACK_BYTES,
                                   nullptr, 2, nullptr, 1) != pdPASS) {
            puts("[BOOT] LVGLTask create failed");
        }
        if (xTaskCreatePinnedToCore(sensor_task, "SensorTask", 4096,
                                   nullptr, 1, nullptr, 0) != pdPASS) {
            puts("[BOOT] SensorTask create failed");
        }
        if (wifi_service_init()) network_services_ready.store(true);
        web_ai_init(); // Local alarm commands also work without network services.
        app_log_memory("runtime services started");
        printf("[BOOT] AppServices stack_free_min=%u\n",
               static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
    uint32_t last_network_retry = app_millis();
    uint32_t last_cache_ms = last_network_retry;
    uint32_t last_service_health = 0;
    for (;;) {
        if (app_millis() - last_cache_ms > 300000) {
            saveCurrentTimeCache();
            last_cache_ms = app_millis();
        }
        const bool service_health = !last_service_health || app_millis() - last_service_health >= 30000;
        if (service_health) { last_service_health = app_millis(); puts("[SERVICES] polling quick settings"); }
        quick_settings_service_poll();
        wifi_service_poll();
        hotspot_name_poll();
        ai_voice_poll();
        alarm_service_poll();
        if (service_health) puts("[SERVICES] polling life index");
        life_data_poll();
        online_music_service_poll();
        system_update_poll(ui_initialized && sound_is_ready() && xTaskGetHandle("LVGLTask"));
        file_transfer_poll();
        music_web_service_poll();
        if (ui_initialized && app_millis() - last_network_retry >= 10000) {
            if (network_services_ready.load() || wifi_service_init()) network_services_ready.store(true);
            web_ai_init(); // Idempotent; retry even while networking is unavailable.
            last_network_retry = app_millis();
        }
        serial_upload_poll();
        if (service_health) puts("[SERVICES] poll complete");
        app_delay_ms(2);
    }
}

static void app_init_task(void *)
{
    app_log_memory("before display");
    loadCachedTime();
    loadCachedWeather();
    sdcar_load_wallpaper_choice();
    alarm_service_init();

    const bool lcd_ok = board_lcd_init();
    if (!lcd_ok) puts("[LCD] init failed");
    if (lcd_ok) {
        board_backlight_init();
        lv_init();
        if (board_lcd_double_buffer_ready()) {
            lv_disp_draw_buf_init(&draw_buf, board_lcd_get_framebuffer(1),
                                  board_lcd_get_framebuffer(0),
                                  BOARD_SCREEN_WIDTH * BOARD_SCREEN_HEIGHT);
        } else {
            disp_draw_buf = static_cast<lv_color_t *>(heap_caps_malloc(
                sizeof(lv_color_t) * BOARD_SCREEN_WIDTH * 40,
                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, nullptr, BOARD_SCREEN_WIDTH * 40);
        }
        lv_disp_drv_init(&disp_drv);
        disp_drv.hor_res = BOARD_SCREEN_WIDTH;
        disp_drv.ver_res = BOARD_SCREEN_HEIGHT;
        disp_drv.flush_cb = display_refresh_flush;
        disp_drv.render_start_cb = display_refresh_begin;
        disp_drv.draw_buf = &draw_buf;
        disp_drv.direct_mode = board_lcd_double_buffer_ready();
        disp_drv.monitor_cb = gui_refresh_monitor_cb;
        disp_drv.draw_ctx_init = video_draw_init_ctx;
        lv_disp_drv_register(&disp_drv);
        ui_transition_set_capture(display_refresh_capture, nullptr);
        static lv_indev_drv_t indev;
        lv_indev_drv_init(&indev);
        indev.type = LV_INDEV_TYPE_POINTER;
        indev.read_cb = touch_read;
        lv_indev_drv_register(&indev);
        boot_splash_start(SMARTVOICEBOX_FIRMWARE_VERSION);
        lv_refr_now(nullptr); // Built-in fonts only; visible before SD loading.
    }
    if (!sdcar_init()) puts("SD unavailable");
    if (sdcar_is_ready() && !ai_speech_clear_cache()) puts("[AI] stale speech cache cleanup failed");
    media_files_recover_boot();
    serial_upload_boot_window();
    ai_config_init();
    weather_config_init();
    ai_voice_init();

    i2c_mutex = xSemaphoreCreateMutex();
    if (board_i2c_init()) {
        touch_init();
        i2c_available = mpu_init();
        if (!i2c_available) puts("[MPU] init failed");
    } else {
        puts("I2C unavailable");
    }

    if (lcd_ok) {
        sdcar_init_fonts();
        app_log_memory("after fonts");
        if (sdcar_is_ready()) sdcar_load_wallpaper_cache();
        app_log_memory("after wallpaper cache");
        weather_api_config_t weather_api{};
        weather_config_get(&weather_api);
        life_data_init(weather_api.life_api_key, WEATHER_CITY, weather_api.life_url,
                       weather_config_imported());
        memset(&weather_api, 0, sizeof(weather_api));
        setup_ui(&guider_ui);
        sdcar_publish_status();
        sound_init();
        quick_panel_init(chinese_font_20);
        edge_back_init();
        screen_idle_init();
        ui_initialized = true;
    }

    app_log_memory("before releasing AppInit");
    if (xTaskCreatePinnedToCore(app_services_task, "AppServices", 8192,
                               nullptr, 1, nullptr, 0) != pdPASS) {
        puts("[BOOT] AppServices create failed");
    }
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    if (app_uart_init() != ESP_OK) puts("[UART] init failed");
    app_delay_ms(500);

    const esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        printf("[NVS] %s; not erasing user data\n", esp_err_to_name(nvs));
        return;
    }
    if (nvs != ESP_OK) {
        printf("[NVS] init failed: %s\n", esp_err_to_name(nvs));
        return;
    }

    if (xTaskCreatePinnedToCore(app_init_task, "AppInit", 16384, nullptr, 2, nullptr, 1) != pdPASS) {
        puts("[BOOT] AppInit task create failed");
    }
}
