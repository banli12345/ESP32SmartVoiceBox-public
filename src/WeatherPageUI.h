#pragma once

#include "lvgl.h"

/* Drop widget pointers before the page objects are deleted. */
void weather_page_ui_clear(void);
/* Build the weather application page. City clicks use on_city. */
void weather_page_ui_mount(lv_obj_t *page, lv_event_cb_t on_city);
void weather_page_ui_refresh(void);
void weather_page_ui_refresh_status(void);
