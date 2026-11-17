#include "board_lcd.h"

#include <stddef.h>
#include <stdio.h>
#include "board_backlight.h"
#include "board_pins.h"
#include "DisplayControl.h"
#include "IdfRuntime.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/lcd_ll.h"
#include "lvgl.h"

static esp_lcd_panel_handle_t _panel_handle;
static uint16_t *s_framebuffers[2];
static bool s_double_buffer_ready;
static SemaphoreHandle_t s_frame_done;
static uint32_t rgb_frame_complete_count;
static uint32_t rgb_vsync_count;
static bool rgb_pause_requested;
static bool rgb_pause_complete;
static portMUX_TYPE rgb_frame_complete_lock = portMUX_INITIALIZER_UNLOCKED;
static display_restart_status_t display_restart_status = DISPLAY_RESTART_IDLE;
static const uint32_t DISPLAY_RESTART_TIMEOUT_MS = 500;

static void stop_rgb_at_vsync(void);

static void spi_cs(int level) { gpio_set_level(BOARD_LCD_SPI_CS, level); }
static void spi_sck(int level) { gpio_set_level(BOARD_LCD_SPI_SCK, level); }
static void spi_mosi(int level) { gpio_set_level(BOARD_LCD_SPI_MOSI, level); }

static void spi_write9(int dc, uint8_t value)
{
    spi_mosi(dc);
    spi_sck(1);
    spi_sck(0);
    uint8_t bit = 0x80;
    while (bit) {
        spi_mosi((value & bit) ? 1 : 0);
        spi_sck(1);
        bit >>= 1;
        spi_sck(0);
    }
}

static void spi_begin(void) { spi_cs(0); }
static void spi_end(void) { spi_cs(1); }
static void spi_cmd(uint8_t cmd) { spi_write9(0, cmd); }
static void spi_data(uint8_t data) { spi_write9(1, data); }

static void spi_cmd_bytes(uint8_t cmd, const uint8_t *data, size_t len)
{
    spi_cmd(cmd);
    for (size_t i = 0; i < len; ++i) spi_data(data[i]);
}

static bool st7701_type1_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BOARD_LCD_SPI_CS) | (1ULL << BOARD_LCD_SPI_SCK) |
                        (1ULL << BOARD_LCD_SPI_MOSI),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) return false;
    spi_cs(1);
    spi_sck(0);
    spi_mosi(0);

    spi_begin();
    spi_cmd(0x01);
    spi_end();
    app_delay_ms(120);

    static const uint8_t page0[] = {0x77, 0x01, 0x00, 0x00, 0x10};
    static const uint8_t c0[] = {0x3B, 0x00};
    static const uint8_t c1[] = {0x0D, 0x02};
    static const uint8_t c2[] = {0x31, 0x05};
    static const uint8_t b0[] = {
        0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
        0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18};
    static const uint8_t b1[] = {
        0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
        0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18};
    static const uint8_t page1[] = {0x77, 0x01, 0x00, 0x00, 0x11};
    static const uint8_t e0[] = {0x00, 0x1B, 0x02};
    static const uint8_t e1[] = {0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00, 0x00, 0x44, 0x44};
    static const uint8_t e2[] = {0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00, 0xEC, 0xA0, 0x00, 0x00};
    static const uint8_t e3[] = {0x00, 0x00, 0x11, 0x11};
    static const uint8_t e4[] = {0x44, 0x44};
    static const uint8_t e5[] = {
        0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
        0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0};
    static const uint8_t e6[] = {0x00, 0x00, 0x11, 0x11};
    static const uint8_t e7[] = {0x44, 0x44};
    static const uint8_t e8[] = {
        0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
        0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0};
    static const uint8_t eb[] = {0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40};
    static const uint8_t ec[] = {0x3C, 0x00};
    static const uint8_t ed[] = {
        0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA};
    static const uint8_t page13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
    static const uint8_t page00[] = {0x77, 0x01, 0x00, 0x00, 0x00};

    spi_begin();
    spi_cmd_bytes(0xFF, page0, sizeof(page0));
    spi_cmd_bytes(0xC0, c0, sizeof(c0));
    spi_cmd_bytes(0xC1, c1, sizeof(c1));
    spi_cmd_bytes(0xC2, c2, sizeof(c2));
    spi_cmd_bytes(0xCD, (const uint8_t[]){0x08}, 1);
    spi_cmd_bytes(0xB0, b0, sizeof(b0));
    spi_cmd_bytes(0xB1, b1, sizeof(b1));
    spi_cmd_bytes(0xFF, page1, sizeof(page1));
    spi_cmd_bytes(0xB0, (const uint8_t[]){0x60}, 1);
    spi_cmd_bytes(0xB1, (const uint8_t[]){0x32}, 1);
    spi_cmd_bytes(0xB2, (const uint8_t[]){0x07}, 1);
    spi_cmd_bytes(0xB3, (const uint8_t[]){0x80}, 1);
    spi_cmd_bytes(0xB5, (const uint8_t[]){0x49}, 1);
    spi_cmd_bytes(0xB7, (const uint8_t[]){0x85}, 1);
    spi_cmd_bytes(0xB8, (const uint8_t[]){0x21}, 1);
    spi_cmd_bytes(0xC1, (const uint8_t[]){0x78}, 1);
    spi_cmd_bytes(0xC2, (const uint8_t[]){0x78}, 1);
    spi_cmd_bytes(0xE0, e0, sizeof(e0));
    spi_cmd_bytes(0xE1, e1, sizeof(e1));
    spi_cmd_bytes(0xE2, e2, sizeof(e2));
    spi_cmd_bytes(0xE3, e3, sizeof(e3));
    spi_cmd_bytes(0xE4, e4, sizeof(e4));
    spi_cmd_bytes(0xE5, e5, sizeof(e5));
    spi_cmd_bytes(0xE6, e6, sizeof(e6));
    spi_cmd_bytes(0xE7, e7, sizeof(e7));
    spi_cmd_bytes(0xE8, e8, sizeof(e8));
    spi_cmd_bytes(0xEB, eb, sizeof(eb));
    spi_cmd_bytes(0xEC, ec, sizeof(ec));
    spi_cmd_bytes(0xED, ed, sizeof(ed));
    spi_cmd_bytes(0xFF, page13, sizeof(page13));
    spi_cmd_bytes(0xE5, (const uint8_t[]){0xE4}, 1);
    spi_cmd_bytes(0xFF, page00, sizeof(page00));
    spi_cmd(0x21);
    spi_cmd_bytes(0x3A, (const uint8_t[]){0x60}, 1);
    spi_cmd(0x11);
    spi_end();
    app_delay_ms(120);

    spi_begin();
    spi_cmd(0x29);
    spi_end();
    return true;
}

void board_lcd_release_spi_pins(void)
{
    gpio_reset_pin(BOARD_LCD_SPI_SCK);
    gpio_reset_pin(BOARD_LCD_SPI_MOSI);
    gpio_set_direction(BOARD_LCD_SPI_SCK, GPIO_MODE_INPUT);
    gpio_set_direction(BOARD_LCD_SPI_MOSI, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BOARD_LCD_SPI_SCK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(BOARD_LCD_SPI_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_level(BOARD_LCD_SPI_CS, 1);
    app_delay_ms(2);
}

static bool IRAM_ATTR rgb_frame_complete_cb(esp_lcd_panel_handle_t panel,
                                            const esp_lcd_rgb_panel_event_data_t *edata,
                                            void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
    portENTER_CRITICAL_ISR(&rgb_frame_complete_lock);
    ++rgb_frame_complete_count;
    portEXIT_CRITICAL_ISR(&rgb_frame_complete_lock);
    BaseType_t woken = pdFALSE;
    if (s_frame_done) xSemaphoreGiveFromISR(s_frame_done, &woken);
    return woken == pdTRUE;
}

static bool IRAM_ATTR rgb_vsync_cb(esp_lcd_panel_handle_t panel,
                                   const esp_lcd_rgb_panel_event_data_t *edata,
                                   void *user_ctx)
{
    (void)panel;
    (void)edata;
    (void)user_ctx;
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

static void IRAM_ATTR stop_rgb_at_vsync(void)
{
    lcd_ll_stop(LCD_LL_GET_HW(0));
}

esp_err_t board_lcd_restart_controller(void)
{
    if (!_panel_handle) return ESP_ERR_INVALID_STATE;
    const esp_err_t reset_result = esp_lcd_panel_reset(_panel_handle);
    const esp_err_t init_result = esp_lcd_panel_init(_panel_handle);
    return reset_result != ESP_OK ? reset_result : init_result;
}

static uint32_t read_rgb_vsync_count(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const uint32_t count = rgb_vsync_count;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return count;
}

static bool read_rgb_pause_complete(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const bool paused = rgb_pause_complete;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return paused;
}

bool display_restart_request(void)
{
    if (display_restart_status == DISPLAY_RESTART_PENDING ||
        display_restart_status == DISPLAY_RESTART_RUNNING) return false;
    display_restart_status = DISPLAY_RESTART_PENDING;
    return true;
}

display_restart_status_t display_restart_get_status(void)
{
    return display_restart_status;
}

void service_display_restart(void)
{
    if (display_restart_status != DISPLAY_RESTART_PENDING) return;
    display_restart_status = DISPLAY_RESTART_RUNNING;
    const uint32_t saved_duty = backlight_requested_duty;
    const bool blanked = board_backlight_write_duty(0);

    const uint32_t started_ms = app_millis();
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_complete = false;
    rgb_pause_requested = true;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    while (!read_rgb_pause_complete() &&
           app_millis() - started_ms < DISPLAY_RESTART_TIMEOUT_MS) vTaskDelay(1);
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_requested = false;
    const bool paused = rgb_pause_complete;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);

    const uint32_t vsync_base = read_rgb_vsync_count();
    esp_err_t result = paused ? board_lcd_restart_controller() : ESP_ERR_TIMEOUT;
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    rgb_pause_complete = false;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    if (result == ESP_OK) {
        while (read_rgb_vsync_count() - vsync_base < 2) {
            if (app_millis() - started_ms >= DISPLAY_RESTART_TIMEOUT_MS) {
                result = ESP_ERR_TIMEOUT;
                break;
            }
            vTaskDelay(1);
        }
    }

    if (result == ESP_OK) {
        lv_obj_invalidate(lv_scr_act());
        lv_obj_invalidate(lv_layer_top());
    }
    const bool restored = board_backlight_write_duty(saved_duty);
    backlight_write_ok = restored;
    display_restart_status = result == ESP_OK && blanked && restored ?
        DISPLAY_RESTART_SUCCEEDED : DISPLAY_RESTART_FAILED;
    printf("[DISPLAY] restart=%s result=%s elapsed_ms=%u blank_ok=%u restore_ok=%u\n",
           display_restart_status == DISPLAY_RESTART_SUCCEEDED ? "ok" : "failed",
           esp_err_to_name(result), (unsigned)(app_millis() - started_ms),
           (unsigned)blanked, (unsigned)restored);
}

uint32_t board_lcd_read_frame_complete_count(void)
{
    portENTER_CRITICAL(&rgb_frame_complete_lock);
    const uint32_t count = rgb_frame_complete_count;
    portEXIT_CRITICAL(&rgb_frame_complete_lock);
    return count;
}

uint32_t board_lcd_read_vsync_count(void)
{
    return read_rgb_vsync_count();
}

bool board_lcd_wait_frame_complete(uint32_t after, uint32_t timeout_ms)
{
    const TickType_t started = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(timeout_ms);
    if (!budget) budget = 1;
    // The semaphore is a wake hint, not a frame count: stale/coalesced wakes
    // must never authorize reuse of a framebuffer still being scanned.
    while (board_lcd_read_frame_complete_count() == after) {
        const TickType_t elapsed = xTaskGetTickCount() - started;
        if (elapsed >= budget || !s_frame_done) return false;
        (void)xSemaphoreTake(s_frame_done, budget - elapsed);
    }
    return true;
}

bool board_lcd_recover_scanout(void)
{
    if (!_panel_handle) return false;
    // RGB reset/init uses no shared LCD-init SPI pins (the SD card owns them).
    if (esp_lcd_rgb_panel_set_pclk(_panel_handle, BOARD_LCD_SAFE_PCLK_HZ) != ESP_OK)
        return false;
    if (board_lcd_restart_controller() != ESP_OK) return false;
    const uint32_t before = board_lcd_read_frame_complete_count();
    const bool ok = board_lcd_wait_frame_complete(before, 100);
    printf("[LCD] scanout recovery pclk=%u result=%s\n",
           (unsigned)BOARD_LCD_SAFE_PCLK_HZ, ok ? "ok" : "failed");
    return ok;
}

uint16_t *board_lcd_get_framebuffer(int index)
{
    if (index < 0 || index > 1) return NULL;
    return s_framebuffers[index];
}

bool board_lcd_double_buffer_ready(void)
{
    return s_double_buffer_ready;
}

esp_err_t board_lcd_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color)
{
    if (!_panel_handle) return ESP_ERR_INVALID_STATE;
    return esp_lcd_panel_draw_bitmap(_panel_handle, x_start, y_start, x_end, y_end, color);
}

bool board_lcd_init(void)
{
    s_frame_done = xSemaphoreCreateBinary();
    if (!s_frame_done) {
        puts("[LCD] frame semaphore allocation failed");
        return false;
    }
    if (!st7701_type1_init()) {
        puts("[LCD] ST7701 SPI init failed");
        board_lcd_release_spi_pins();
        return false;
    }
    board_lcd_release_spi_pins();

    esp_lcd_rgb_panel_config_t panel_config = {0};
    panel_config.clk_src = LCD_CLK_SRC_DEFAULT;
    panel_config.timings.pclk_hz = BOARD_LCD_PCLK_HZ;
    panel_config.timings.h_res = BOARD_SCREEN_WIDTH;
    panel_config.timings.v_res = BOARD_SCREEN_HEIGHT;
    panel_config.timings.hsync_pulse_width = BOARD_LCD_HSYNC_PULSE_WIDTH;
    panel_config.timings.hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH;
    panel_config.timings.hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH;
    panel_config.timings.vsync_pulse_width = BOARD_LCD_VSYNC_PULSE_WIDTH;
    panel_config.timings.vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH;
    panel_config.timings.vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH;
    panel_config.timings.flags.hsync_idle_low = (BOARD_LCD_HSYNC_POLARITY == 0) ? 1 : 0;
    panel_config.timings.flags.vsync_idle_low = (BOARD_LCD_VSYNC_POLARITY == 0) ? 1 : 0;
    panel_config.timings.flags.de_idle_high = 0;
    panel_config.timings.flags.pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG;
    panel_config.timings.flags.pclk_idle_high = 0;
    panel_config.data_width = 16;
    panel_config.bits_per_pixel = 16;
    panel_config.num_fbs = 2;
    panel_config.bounce_buffer_size_px = BOARD_SCREEN_WIDTH * BOARD_LCD_BOUNCE_LINES;
    panel_config.dma_burst_size = 64;
    panel_config.hsync_gpio_num = BOARD_LCD_HSYNC;
    panel_config.vsync_gpio_num = BOARD_LCD_VSYNC;
    panel_config.de_gpio_num = BOARD_LCD_DE;
    panel_config.pclk_gpio_num = BOARD_LCD_PCLK;
    panel_config.disp_gpio_num = GPIO_NUM_NC;
    panel_config.data_gpio_nums[0] = BOARD_LCD_B0;
    panel_config.data_gpio_nums[1] = BOARD_LCD_B1;
    panel_config.data_gpio_nums[2] = BOARD_LCD_B2;
    panel_config.data_gpio_nums[3] = BOARD_LCD_B3;
    panel_config.data_gpio_nums[4] = BOARD_LCD_B4;
    panel_config.data_gpio_nums[5] = BOARD_LCD_G0;
    panel_config.data_gpio_nums[6] = BOARD_LCD_G1;
    panel_config.data_gpio_nums[7] = BOARD_LCD_G2;
    panel_config.data_gpio_nums[8] = BOARD_LCD_G3;
    panel_config.data_gpio_nums[9] = BOARD_LCD_G4;
    panel_config.data_gpio_nums[10] = BOARD_LCD_G5;
    panel_config.data_gpio_nums[11] = BOARD_LCD_R0;
    panel_config.data_gpio_nums[12] = BOARD_LCD_R1;
    panel_config.data_gpio_nums[13] = BOARD_LCD_R2;
    panel_config.data_gpio_nums[14] = BOARD_LCD_R3;
    panel_config.data_gpio_nums[15] = BOARD_LCD_R4;
    panel_config.flags.disp_active_low = 1;
    panel_config.flags.fb_in_psram = 1;
    panel_config.flags.double_fb = 0;
    panel_config.flags.no_fb = 0;
    panel_config.flags.bb_invalidate_cache = 0;

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &_panel_handle);
    if (err != ESP_OK) {
        printf("[LCD] rgb panel: %s\n", esp_err_to_name(err));
        _panel_handle = NULL;
        return false;
    }
    err = esp_lcd_panel_reset(_panel_handle);
    if (err != ESP_OK) {
        printf("[LCD] reset: %s\n", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_init(_panel_handle);
    if (err != ESP_OK) {
        printf("[LCD] init: %s\n", esp_err_to_name(err));
        return false;
    }

    esp_lcd_rgb_panel_event_callbacks_t callbacks = {0};
    callbacks.on_frame_buf_complete = rgb_frame_complete_cb;
    callbacks.on_vsync = rgb_vsync_cb;
    err = esp_lcd_rgb_panel_register_event_callbacks(_panel_handle, &callbacks, NULL);
    if (err != ESP_OK) {
        printf("[LCD] callbacks: %s\n", esp_err_to_name(err));
        return false;
    }

    void *fb0 = NULL;
    void *fb1 = NULL;
    err = esp_lcd_rgb_panel_get_frame_buffer(_panel_handle, 2, &fb0, &fb1);
    if (err != ESP_OK || !fb0 || !fb1) {
        printf("[LCD] framebuffers: %s\n", esp_err_to_name(err));
        return false;
    }
    s_framebuffers[0] = fb0;
    s_framebuffers[1] = fb1;
    s_double_buffer_ready = true;
    return true;
}
