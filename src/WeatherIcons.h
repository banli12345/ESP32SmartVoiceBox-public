#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
extern const lv_img_dsc_t weather_icon_descriptors[40];
const lv_img_dsc_t *weather_icon_for_code(int code);
int weather_icon_legacy_code(const char *condition);
#ifdef __cplusplus
}
#endif
