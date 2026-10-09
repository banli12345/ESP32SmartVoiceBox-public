#ifndef SMARTVOICEBOX_BROWSER_UI_H
#define SMARTVOICEBOX_BROWSER_UI_H
#include <stdbool.h>
#include "lvgl.h"
typedef lv_obj_t *(*browser_back_button_builder_t)(lv_obj_t *, lv_align_t, lv_coord_t, lv_event_cb_t);
void browser_ui_open(lv_obj_t *return_screen, const lv_font_t *font, bool chinese,
                     browser_back_button_builder_t build_back_button);
#endif
