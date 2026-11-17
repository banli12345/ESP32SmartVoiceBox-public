#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void screen_idle_init(void);
uint16_t screen_idle_get_timeout(void);
bool screen_idle_set_timeout(uint16_t minutes);
bool screen_idle_is_asleep(void);
/* GUI task only; true consumes the physical pointer, including its release. */
bool screen_idle_pointer(bool pressed, int x, int y, uint32_t now_ms);
void screen_idle_poll(uint32_t now_ms);
/* Any task may request a wake; GUI performs the backlight change. */
void screen_idle_request_wake(void);
/* Sensor task only. Roll/X and pitch/Y angular rates in rad/s; yaw is ignored.
 * Posts a wake notification, never touches LVGL or LEDC. */
void screen_idle_motion(float roll_rate_rads, float pitch_rate_rads, uint32_t now_ms);
#ifdef __cplusplus
}
#endif
