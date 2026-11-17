#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void alarm_ui_init(const lv_font_t *chinese_font);
void alarm_ui_open(lv_obj_t *parent, const lv_font_t *chinese_font);
bool alarm_ui_alert_visible(void);
#ifdef __cplusplus
}
#endif
