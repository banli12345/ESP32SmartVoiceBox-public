#include "board_backlight.h"

#include <math.h>
#include <stdio.h>
#include "board_pins.h"
#include "driver/ledc.h"

uint32_t backlight_requested_duty;
bool backlight_write_ok;

static int s_percent = 100;
static bool s_attach_ok;
static bool s_sleeping;

int get_backlight_brightness(void) { return s_percent; }

static int clamp_int(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

bool board_backlight_write_duty(uint32_t duty)
{
    if (!s_attach_ok) return false;
    if (s_sleeping) duty = 0;
    if (ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty) != ESP_OK) return false;
    if (ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) != ESP_OK) return false;
    return true;
}

bool board_backlight_set_sleeping(bool sleeping)
{
    const bool previous = s_sleeping;
    s_sleeping = sleeping;
    const bool ok = board_backlight_write_duty(sleeping ? 0 : backlight_requested_duty);
    if (!ok) s_sleeping = previous;
    backlight_write_ok = ok;
    return ok;
}

void board_backlight_log_state(void)
{
    printf("[BACKLIGHT] percent=%d requested_duty=%u actual_duty=%u freq_hz=%u attach_ok=%u write_ok=%u sleeping=%u\n",
           s_percent,
           (unsigned)backlight_requested_duty,
           (unsigned)ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0),
           (unsigned)ledc_get_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0),
           (unsigned)s_attach_ok,
           (unsigned)backlight_write_ok, (unsigned)s_sleeping);
}

void set_backlight_brightness(int percent)
{
    percent = clamp_int(percent, 5, 100);
    const uint32_t max_duty = (1UL << BOARD_BL_RESOLUTION) - 1;
    const uint32_t min_duty = max_duty * BOARD_BL_MIN_DUTY_PERCENT / 100;
    s_percent = percent;
    backlight_requested_duty = min_duty + (uint32_t)(powf(percent / 100.0f, 0.20f) * (max_duty - min_duty));
    backlight_write_ok = board_backlight_write_duty(backlight_requested_duty);
    board_backlight_log_state();
}

bool board_backlight_init(void)
{
    s_sleeping = false;
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BOARD_BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
        .deconfigure = false,
    };
    ledc_channel_config_t channel = {
        .gpio_num = BOARD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
        .flags = {
            .output_invert = 0,
        },
    };
    s_attach_ok = ledc_timer_config(&timer) == ESP_OK &&
                  ledc_channel_config(&channel) == ESP_OK;
    if (!s_attach_ok) {
        puts("[BACKLIGHT] LEDC init failed");
        return false;
    }
    set_backlight_brightness(100);
    return true;
}
