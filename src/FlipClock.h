#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct flip_clock flip_clock_t;
flip_clock_t *flip_clock_create(lv_obj_t *parent);
lv_obj_t *flip_clock_object(flip_clock_t *clock);
void flip_clock_set_time(flip_clock_t *clock, int hour, int minute, int second, bool animate);
#ifdef __cplusplus
}
#endif
