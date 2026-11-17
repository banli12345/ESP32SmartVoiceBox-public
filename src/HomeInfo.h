#pragma once
#include "lvgl.h"
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
void home_info_create(lv_obj_t *page, lv_obj_t *clock_card, const lv_font_t *chinese_font);
void home_info_update(const struct tm *date);
#ifdef __cplusplus
}
#endif
