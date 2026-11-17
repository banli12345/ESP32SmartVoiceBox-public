#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void games_ui_open(lv_obj_t *desktop, const lv_font_t *font, bool chinese,
                   lv_event_cb_t open_2048, lv_event_cb_t open_balance);
lv_obj_t *games_ui_get_screen(void);
#ifdef __cplusplus
}
#endif
