#include "touch.h"

#include <stddef.h>
#include <stdio.h>
#include "IdfRuntime.h"
#include "board_i2c.h"
#include "board_pins.h"

int touch_last_x;
int touch_last_y;

static i2c_master_dev_handle_t s_dev;
static bool s_ready;
static bool s_contact;
static bool s_release_edge;

static esp_err_t gt911_write_u8(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF), value};
    return board_i2c_write(s_dev, buf, sizeof(buf));
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = {(uint8_t)(reg >> 8), (uint8_t)(reg & 0xFF)};
    return board_i2c_write_read(s_dev, addr, sizeof(addr), data, len);
}

void touch_init(void)
{
    i2c_master_dev_handle_t dev = NULL;
    s_ready = false;
    s_contact = false;
    s_release_edge = false;
    if (!board_i2c_bus()) {
        puts("Touch I2C bus unavailable");
        return;
    }
    if (!s_dev) {
        if (board_i2c_add_device(TOUCH_ADDR, &dev) != ESP_OK || !dev) {
            puts("Touch device add failed");
            return;
        }
        s_dev = dev;
    }
    if (gt911_write_u8(0x8040, 0x02) != ESP_OK) {
        puts("Touch GT911 reset write failed");
        return;
    }
    app_delay_ms(200);
    s_ready = true;
    puts("Touch initialized (GT911)");
}

bool touch_has_signal(void)
{
    return true;
}

touch_sample_status_t touch_read_sample(int *x, int *y)
{
    uint8_t status = 0;
    uint8_t point[8];
    unsigned count;
    int px;
    int py;
    if (!s_ready || !s_dev) return TOUCH_SAMPLE_ERROR;
    if (gt911_read(0x814E, &status, 1) != ESP_OK) return TOUCH_SAMPLE_ERROR;
    if ((status & 0x80) == 0) return TOUCH_SAMPLE_NONE;

    count = status & 0x0F;
    if (count > 5) {
        if (gt911_write_u8(0x814E, 0x00) != ESP_OK) return TOUCH_SAMPLE_ERROR;
        return TOUCH_SAMPLE_ERROR;
    }
    if (count > 0) {
        if (gt911_read(0x814F, point, sizeof(point)) != ESP_OK) return TOUCH_SAMPLE_ERROR;
        px = point[1] | (point[2] << 8);
        py = point[3] | (point[4] << 8);
        if (px >= BOARD_SCREEN_WIDTH || py >= BOARD_SCREEN_HEIGHT) {
            if (gt911_write_u8(0x814E, 0x00) != ESP_OK) return TOUCH_SAMPLE_ERROR;
            return TOUCH_SAMPLE_ERROR;
        }
    }
    if (gt911_write_u8(0x814E, 0x00) != ESP_OK) return TOUCH_SAMPLE_ERROR;
    if (count == 0) {
        s_contact = false;
        s_release_edge = true;
        return TOUCH_SAMPLE_RELEASED;
    }
    touch_last_x = px;
    touch_last_y = py;
    if (x) *x = px;
    if (y) *y = py;
    s_contact = true;
    s_release_edge = false;
    return TOUCH_SAMPLE_PRESSED;
}

bool touch_read_point(int *x, int *y)
{
    int sample_x = touch_last_x;
    int sample_y = touch_last_y;
    const touch_sample_status_t sample = touch_read_sample(&sample_x, &sample_y);
    if (sample == TOUCH_SAMPLE_RELEASED || (sample == TOUCH_SAMPLE_ERROR && !s_contact)) return false;
    if (sample == TOUCH_SAMPLE_NONE && !s_contact) return false;
    if (x) *x = touch_last_x;
    if (y) *y = touch_last_y;
    return s_contact;
}

bool touch_touched(void)
{
    static uint32_t last_touch_read;
    const uint32_t now = app_millis();
    if (now - last_touch_read < BOARD_TOUCH_POLL_MS) return s_contact;
    if (i2c_busy) return s_contact;
    i2c_busy = true;
    last_touch_read = now;
    const bool touched = touch_read_point(NULL, NULL);
    i2c_busy = false;
    return touched;
}

bool touch_released(void)
{
    return s_release_edge;
}
