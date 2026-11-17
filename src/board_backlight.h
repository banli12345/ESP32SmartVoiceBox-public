#ifndef SMARTVOICEBOX_BOARD_BACKLIGHT_H
#define SMARTVOICEBOX_BOARD_BACKLIGHT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t backlight_requested_duty;
extern bool backlight_write_ok;

bool board_backlight_init(void);
bool board_backlight_write_duty(uint32_t duty);
bool board_backlight_set_sleeping(bool sleeping);
void set_backlight_brightness(int percent);
int get_backlight_brightness(void);
void board_backlight_log_state(void);

#ifdef __cplusplus
}
#endif

#endif
