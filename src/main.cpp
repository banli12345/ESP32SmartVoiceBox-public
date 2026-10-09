#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Preferences.h>
#include <WiFi.h>
#include <lvgl.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#include "SDcar.h"
#include "Sound.h"
#include "Web_ai.h"
#include "setwifi.h"
#include "touch.h"
#include "video_draw.h"
#include "SerialUpload.h"
#include "DisplayControl.h"
#include "ui/generated/gui_guider.h"
#include "ui/custom/custom.h"

lv_ui guider_ui;
volatile bool i2c_busy = false;
static bool i2c_available = false;
static SemaphoreHandle_t i2c_mutex = nullptr;
static volatile bool touch_pressed = false;
static volatile int touch_point_x = 0, touch_point_y = 0;
static uint16_t *rgb_framebuffers[2] = {nullptr, nullptr};
static bool rgb_double_buffer_ready = false;
static uint32_t rgb_frame_complete_count = 0;
static uint32_t rgb_vsync_count = 0;
static bool rgb_pause_requested = false;
static bool rgb_pause_complete = false;
static portMUX_TYPE rgb_frame_complete_lock = portMUX_INITIALIZER_UNLOCKED;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf = nullptr;
static lv_disp_drv_t disp_drv;
static Adafruit_MPU6050 mpu;
static float roll = 0, pitch = 0, yaw = 0, gyro_z_offset = 0;
static uint32_t last_mpu_time = 0;
static int clock_reminder_hour = 0, clock_reminder_minute = 0;
static bool clock_reminder_enabled = false;

extern "C" {
volatile uint32_t gui_fps = 0;
volatile int8_t swipe_dir = 0;
extern int current_page;
void animate_to_page(int page);
}

#define GFX_BL 38
#define GFX_BL_FREQ 5000
#define GFX_BL_RESOLUTION 8
#define GFX_BL_MIN_DUTY_PERCENT 60
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
#define RGB_PCLK_HZ 10000000L
#define RGB_BOUNCE_LINES 20

static_assert(CONFIG_ARDUINO_RUNNING_CORE == 1,
              "RGB restart requires the panel ISR and LVGLTask on core 1");

static void IRAM_ATTR stop_rgb_at_vsync(void);

static bool IRAM_ATTR rgb_frame_complete_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    portENTER_CRITICAL_ISR(&rgb_frame_complete_lock); ++rgb_frame_complete_count; portEXIT_CRITICAL_ISR(&rgb_frame_complete_lock); return false;
}
static bool IRAM_ATTR rgb_vsync_cb(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx) {
    (void)panel; (void)edata; (void)user_ctx;
    portENTER_CRITICAL_ISR(&rgb_frame_complete_lock);
    ++rgb_vsync_count;
    if (rgb_pause_requested) {
        stop_rgb_at_vsync();
        rgb_pause_requested = false;
        rgb_pause_complete = true;
    }
    portEXIT_CRITICAL_ISR(&rgb_frame_complete_lock);
    return false;
}
static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(18,17,16,21,11,12,13,14,0,8,20,3,46,9,10,4,5,6,7,15,1,10,8,50,1,10,8,20,0,RGB_PCLK_HZ,false,0,0,SCREEN_WIDTH*RGB_BOUNCE_LINES,2);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(SCREEN_WIDTH,SCREEN_HEIGHT,rgbpanel,0,true,bus,GFX_NOT_DEFINED,st7701_type1_init_operations,sizeof(st7701_type1_init_operations));
static void stop_rgb_at_vsync(void) { rgbpanel->stopTransmissionAtVsync(); }
static volatile bool video_vsync_sync_enabled = false;
extern "C" void set_video_vsync_sync(bool enabled) { video_vsync_sync_enabled = enabled; }
extern "C" bool video_playback_active(void) { return video_vsync_sync_enabled; }

static int backlight_percent = 100;
static uint32_t backlight_requested_duty = 0;
static bool backlight_attach_ok = false;
static bool backlight_write_ok = false;

static void log_backlight_state(void)
{
    Serial.printf(
        "[BACKLIGHT] percent=%d requested_duty=%u actual_duty=%u freq_hz=%u attach_ok=%u write_ok=%u\n",
        backlight_percent,
        static_cast<unsigned>(backlight_requested_duty),
        static_cast<unsigned>(ledcRead(GFX_BL)),
        static_cast<unsigned>(ledcReadFreq(GFX_BL)),
        static_cast<unsigned>(backlight_attach_ok),
        static_cast<unsigned>(backlight_write_ok));
}

extern "C" void set_backlight_brightness(int percent)
{
    percent = constrain(percent, 5, 100);
    const uint32_t max_duty = (1UL << GFX_BL_RESOLUTION) - 1;
    const uint32_t min_duty = max_duty * GFX_BL_MIN_DUTY_PERCENT / 100;
    backlight_percent = percent;
    backlight_requested_duty = min_duty + static_cast<uint32_t>(
        powf(percent / 100.0f, 0.20f) * (max_duty - min_duty));
    backlight_write_ok = ledcWrite(GFX_BL, backlight_requested_duty);
    log_backlight_state();
}

static void init_backlight(void)
{
    backlight_attach_ok = ledcAttach(GFX_BL, GFX_BL_FREQ, GFX_BL_RESOLUTION);
    set_backlight_brightness(100);
}
static void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    if (rgb_double_buffer_ready) {
        // Keep RGB bounce-buffer refill interrupts enabled while submitting
        // the buffer through the driver's public API.
        const esp_err_t result = rgbpanel->drawBitmap(
            area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_p);
        if (result == ESP_OK) {
            // Require a completion AFTER submitting the buffer. Capturing the
            // target before the call could count a completion for the old frame.
            portENTER_CRITICAL(&rgb_frame_complete_lock);
            const uint32_t target = rgb_frame_complete_count + 1;
            portEXIT_CRITICAL(&rgb_frame_complete_lock);
            for (;;) {
                portENTER_CRITICAL(&rgb_frame_complete_lock);
                const uint32_t done = rgb_frame_complete_count;
                portEXIT_CRITICAL(&rgb_frame_complete_lock);
                if (static_cast<int32_t>(done - target) >= 0) break;
                vTaskDelay(1);
            }
        }
    } else {
        gfx->draw16bitRGBBitmap(area->x1, area->y1,
                               reinterpret_cast<uint16_t *>(&color_p->full), w, h);
    }
    lv_disp_flush_ready(disp);
}
static void touch_read(lv_indev_drv_t *,lv_indev_data_t *data){data->state=touch_pressed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;data->point.x=touch_point_x;data->point.y=touch_point_y;}
static void touch_poll(void){static uint32_t last=0,last_swipe=0;static int16_t start=0;static bool was=false;uint32_t now=millis();if(now-last<20)return;last=now;if(!i2c_mutex||xSemaphoreTake(i2c_mutex,0)!=pdTRUE)return;i2c_busy=true;if(touch.read()){TP_Point p=touch.getPoint(0);if(!was)start=p.x;touch_point_x=p.x;touch_point_y=p.y;touch_pressed=true;was=true;}else{if(was&&now-last_swipe>400){int16_t dx=touch_point_x-start;if(dx>50&&current_page>0){swipe_dir=1;last_swipe=now;}else if(dx<-50&&current_page<2){swipe_dir=-1;last_swipe=now;}}touch_pressed=false;was=false;}i2c_busy=false;xSemaphoreGive(i2c_mutex);}
static bool mpu_init(void){if(!mpu.begin())return false;mpu.setAccelerometerRange(MPU6050_RANGE_8_G);mpu.setGyroRange(MPU6050_RANGE_500_DEG);mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);float sum=0;for(int i=0;i<200;i++){sensors_event_t a,g,t;mpu.getEvent(&a,&g,&t);sum+=g.gyro.z;delay(2);}gyro_z_offset=sum/200.0f;last_mpu_time=millis();return true;}
static void mpu_update(void){if(!i2c_available)return;sensors_event_t a,g,t;if(!mpu.getEvent(&a,&g,&t))return;float ar=atan2f(a.acceleration.y,sqrtf(a.acceleration.x*a.acceleration.x+a.acceleration.z*a.acceleration.z))*57.29578f,ap=atan2f(-a.acceleration.x,sqrtf(a.acceleration.y*a.acceleration.y+a.acceleration.z*a.acceleration.z))*57.29578f;uint32_t now=millis();float dt=constrain((now-last_mpu_time)/1000.0f,0.001f,0.05f);last_mpu_time=now;float gz=fabsf(g.gyro.z-gyro_z_offset)<0.01f?0:g.gyro.z-gyro_z_offset;roll=.96f*(roll+g.gyro.x*dt)+.04f*ar;pitch=.96f*(pitch+g.gyro.y*dt)+.04f*ap;yaw+=gz*dt*57.29578f;if(yaw>360)yaw-=360;if(yaw<0)yaw+=360;update_gyro_data(roll,pitch,yaw,t.temperature);}
static void sensor_task(void *){while(true){touch_poll();if(!video_playback_active()&&i2c_mutex&&xSemaphoreTake(i2c_mutex,pdMS_TO_TICKS(5))==pdTRUE){mpu_update();xSemaphoreGive(i2c_mutex);}vTaskDelay(pdMS_TO_TICKS(20));}}
// monitor_cb runs on LVGLTask once per completed refresh. Loop iterations are not frames.
// On-screen gui_fps is the LCD VSYNC count over elapsed millis. Redraw counters stay diagnostic.
// Render max is cleared with the 2 s log window. RGB completions and VSYNC share the existing ISR lock.
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

static uint32_t read_rgb_frame_complete_count(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const uint32_t count = rgb_frame_complete_count;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return count;
}

static uint32_t read_rgb_vsync_count(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const uint32_t count = rgb_vsync_count;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return count;
}

static display_restart_status_t display_restart_status = DISPLAY_RESTART_IDLE;
static const uint32_t DISPLAY_RESTART_TIMEOUT_MS = 500;

static bool read_rgb_pause_complete(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const bool paused = rgb_pause_complete;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return paused;
}

extern "C" bool display_restart_request(void)
{
    if (display_restart_status == DISPLAY_RESTART_PENDING ||
        display_restart_status == DISPLAY_RESTART_RUNNING) return false;
    display_restart_status = DISPLAY_RESTART_PENDING;
    return true;
}

extern "C" display_restart_status_t display_restart_get_status(void)
{
    return display_restart_status;
}

static void service_display_restart(void)
{
    if (display_restart_status != DISPLAY_RESTART_PENDING) return;
    display_restart_status = DISPLAY_RESTART_RUNNING;
    const uint32_t saved_duty = backlight_requested_duty;
    const bool blanked = ledcWrite(GFX_BL, 0);

    // LCD initialization SPI shares GPIO47/48 with SD_MMC. Do not run
    // gfx->begin() again: it also allocates new LVGL framebuffer pointers.
    const uint32_t started_ms = millis();
    // Setup and LVGLTask run on core 1 in this build. Pausing in the VSYNC
    // callback lets that ISR finish before its owning task reinitializes RGB.
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_complete = false;
    rgb_pause_requested = true;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    while (!read_rgb_pause_complete() &&
           millis() - started_ms < DISPLAY_RESTART_TIMEOUT_MS) vTaskDelay(1);
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_requested = false;
    const bool paused = rgb_pause_complete;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);

    // Cancel a late pause on timeout; if it already happened, always resume.
    const uint32_t vsync_base = read_rgb_vsync_count();
    esp_err_t result = paused ? rgbpanel->restartController() : ESP_ERR_TIMEOUT;
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_complete = false;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    if (result == ESP_OK) {
        // Observe two new frame boundaries after restarting the LCD engine.
        while (read_rgb_vsync_count() - vsync_base < 2) {
            if (millis() - started_ms >= DISPLAY_RESTART_TIMEOUT_MS) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
            vTaskDelay(1);
        }
    }

    if (result == ESP_OK) {
        // The next normal LVGL refresh redraws the page on the same buffers.
        // Avoid a nested refresh or an additional blocking flush here.
        lv_obj_invalidate(lv_scr_act());
        lv_obj_invalidate(lv_layer_top());
    }
    const bool restored = ledcWrite(GFX_BL, saved_duty);
    backlight_write_ok = restored;
    display_restart_status = result == ESP_OK && blanked && restored ?
        DISPLAY_RESTART_SUCCEEDED : DISPLAY_RESTART_FAILED;
    Serial.printf("[DISPLAY] restart=%s result=%s elapsed_ms=%u blank_ok=%u restore_ok=%u\n",
                  display_restart_status == DISPLAY_RESTART_SUCCEEDED ? "ok" : "failed",
                  esp_err_to_name(result), static_cast<unsigned>(millis() - started_ms),
                  static_cast<unsigned>(blanked), static_cast<unsigned>(restored));
}

static void lvgl_task(void *)
{
    uint32_t last_fps_ms = millis();
    uint32_t last_log_ms = last_fps_ms;
    uint32_t last_cache_ms = last_fps_ms;
    uint32_t last_backlight_log_ms = last_fps_ms;
    uint32_t fps_vsync_base = read_rgb_vsync_count();
    uint32_t log_frames_base = gui_refr_frames;
    uint32_t log_total_ms_base = gui_refr_total_ms;
    uint32_t log_vsync_base = read_rgb_vsync_count();
    uint32_t rgb_frames_base = read_rgb_frame_complete_count();
    uint32_t wifi_tick_max_ms = 0;
    uint32_t video_frames_base = gui_video_presented_frames;
    uint32_t video_read_frames_base = 0;
    uint32_t video_read_ms_base = 0;
    get_video_read_metrics(&video_read_frames_base, &video_read_ms_base, nullptr);
    gui_refr_max_ms = 0;

    while (true) {
        lv_timer_handler();
        service_display_restart();
        if (swipe_dir) {
            int direction = swipe_dir;
            swipe_dir = 0;
            if ((direction > 0 && current_page > 0) || (direction < 0 && current_page < 2)) {
                animate_to_page(direction > 0 ? current_page - 1 : current_page + 1);
            }
        }

        const uint32_t wifi_started_ms = millis();
        wifi_service_tick();
        const uint32_t wifi_elapsed_ms = millis() - wifi_started_ms;
        if (wifi_elapsed_ms > wifi_tick_max_ms) wifi_tick_max_ms = wifi_elapsed_ms;

        const uint32_t now = millis();
        if (now - last_cache_ms > 300000) {
            saveCurrentTimeCache();
            last_cache_ms = now;
        }

        const uint32_t fps_elapsed_ms = now - last_fps_ms;
        if (fps_elapsed_ms >= 1000) {
            const uint32_t vsync_now = read_rgb_vsync_count();
            const uint32_t vsync_delta = vsync_now - fps_vsync_base;
            gui_fps = static_cast<uint32_t>((vsync_delta * 1000ULL + fps_elapsed_ms / 2) / fps_elapsed_ms);
            fps_vsync_base = vsync_now;
            last_fps_ms = now;
        }

        const uint32_t log_elapsed_ms = now - last_log_ms;
        if (log_elapsed_ms >= 2000) {
            if (now - last_backlight_log_ms >= 10000) {
                log_backlight_state();
                last_backlight_log_ms = now;
            }
            const uint32_t frame_delta = gui_refr_frames - log_frames_base;
            const uint32_t total_ms = gui_refr_total_ms - log_total_ms_base;
            const uint32_t avg_ms = frame_delta == 0 ? 0 : total_ms / frame_delta;
            const uint32_t max_ms = gui_refr_max_ms;
            const uint32_t rgb_now = read_rgb_frame_complete_count();
            const uint32_t rgb_delta = rgb_now - rgb_frames_base;
            const uint32_t vsync_now = read_rgb_vsync_count();
            const uint32_t vsync_delta = vsync_now - log_vsync_base;

            Serial.printf(
                "[GUI] lcd_fps=%u redraw=%u rgb=%u render_avg=%ums render_max=%ums wifi_max=%ums wallpaper=%s\n",
                static_cast<unsigned>((vsync_delta * 1000ULL + log_elapsed_ms / 2) / log_elapsed_ms),
                static_cast<unsigned>(frame_delta * 1000 / log_elapsed_ms),
                static_cast<unsigned>(rgb_delta * 1000 / log_elapsed_ms),
                static_cast<unsigned>(avg_ms),
                static_cast<unsigned>(max_ms),
                static_cast<unsigned>(wifi_tick_max_ms),
                get_wallpaper_image() ? "RAM" : "SD");

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
                Serial.printf(
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

extern "C" {
int wifi_scan_results=0;char wifi_ssid_list[20][33]={};
bool get_wifi_connected(void){return WiFi.status()==WL_CONNECTED;}
const char* get_wifi_connected_ssid(void){static char s[33]={};if(WiFi.status()!=WL_CONNECTED)return s;strlcpy(s,WiFi.SSID().c_str(),sizeof(s));return s;}
int do_wifi_scan(void){WiFi.mode(WIFI_STA);WiFi.scanDelete();int n=WiFi.scanNetworks(false,true,false,300);wifi_scan_results=0;for(int i=0;i<n&&wifi_scan_results<20;i++){String s=WiFi.SSID(i);if(s.isEmpty()||s.length()>=33)continue;bool dup=false;for(int j=0;j<wifi_scan_results;j++)if(s==wifi_ssid_list[j])dup=true;if(!dup)strlcpy(wifi_ssid_list[wifi_scan_results++],s.c_str(),33);}WiFi.scanDelete();return wifi_scan_results;}
int get_wifi_scan_count(void){return wifi_scan_results;}const char* get_wifi_ssid(int i){return i>=0&&i<wifi_scan_results?wifi_ssid_list[i]:"";}bool do_wifi_connect(const char*s,const char*p){return wifi_service_start_connect(s,p);}const char* get_wifi_status(void){static char b[24];if(WiFi.status()!=WL_CONNECTED)return "Not connected";strlcpy(b,WiFi.localIP().toString().c_str(),sizeof(b));return b;}
}
extern "C" void get_clock_reminder(int*h,int*m,bool*e){if(h)*h=clock_reminder_hour;if(m)*m=clock_reminder_minute;if(e)*e=clock_reminder_enabled;}
extern "C" void save_clock_reminder(int h,int m,bool e){if(h<0||h>23||m<0||m>59)return;clock_reminder_hour=h;clock_reminder_minute=m;clock_reminder_enabled=e;Preferences p;if(p.begin("clock",false)){p.putUChar("hour",h);p.putUChar("minute",m);p.putBool("enabled",e);p.end();}}

void setup(void){Serial.begin(115200);delay(500);loadCachedTime();loadCachedWeather();sdcar_load_wallpaper_choice();esp_lcd_rgb_panel_event_callbacks_t cb={};cb.on_frame_buf_complete=rgb_frame_complete_cb;cb.on_vsync=rgb_vsync_cb;rgbpanel->setEventCallbacks(&cb,nullptr);if(!gfx->begin())return;rgb_framebuffers[0]=rgbpanel->getFrameBuffer(0);rgb_framebuffers[1]=rgbpanel->getFrameBuffer(1);rgb_double_buffer_ready=rgb_framebuffers[0]&&rgb_framebuffers[1];if(!sdcar_init())Serial.println("SD unavailable");serial_upload_boot_window();init_backlight();i2c_mutex=xSemaphoreCreateMutex();touch_init();lv_init();sdcar_init_fonts();if(sdcar_is_ready())sdcar_load_wallpaper_cache();if(rgb_double_buffer_ready)lv_disp_draw_buf_init(&draw_buf,rgb_framebuffers[1],rgb_framebuffers[0],SCREEN_WIDTH*SCREEN_HEIGHT);else{disp_draw_buf=(lv_color_t*)heap_caps_malloc(sizeof(lv_color_t)*SCREEN_WIDTH*40,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);lv_disp_draw_buf_init(&draw_buf,disp_draw_buf,nullptr,SCREEN_WIDTH*40);}lv_disp_drv_init(&disp_drv);disp_drv.hor_res=SCREEN_WIDTH;disp_drv.ver_res=SCREEN_HEIGHT;disp_drv.flush_cb=my_disp_flush;disp_drv.draw_buf=&draw_buf;disp_drv.full_refresh=rgb_double_buffer_ready;disp_drv.monitor_cb=gui_refresh_monitor_cb;disp_drv.draw_ctx_init=video_draw_init_ctx;lv_disp_drv_register(&disp_drv);static lv_indev_drv_t indev;lv_indev_drv_init(&indev);indev.type=LV_INDEV_TYPE_POINTER;indev.read_cb=touch_read;lv_indev_drv_register(&indev);setup_ui(&guider_ui);sdcar_publish_status();sound_init();web_ai_init();i2c_available=mpu_init();wifi_service_init();xTaskCreatePinnedToCore(lvgl_task,"LVGLTask",4096,nullptr,2,nullptr,1);xTaskCreatePinnedToCore(sensor_task,"SensorTask",4096,nullptr,1,nullptr,0);}
void loop(void){serial_upload_poll();delay(2);}
