#ifndef UI_TRANSITION_H
#define UI_TRANSITION_H

#include "lvgl.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*ui_transition_capture_cb)(lv_color_t *destination, size_t pixels, void *context);

void ui_transition_set_capture(ui_transition_capture_cb callback, void *context);
void ui_transition_set_desktop(lv_obj_t *root);
void ui_screen_open(lv_obj_t *screen);
void ui_screen_back(lv_obj_t *screen);
void ui_transition_reveal(lv_obj_t *page, bool back);
bool ui_transition_busy(void);
void ui_transition_cancel(void);

#ifdef __cplusplus
}
#endif

#endif
