#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GUI task only. Two full-size RGB framebuffers, with LVGL direct mode. */
void display_refresh_begin(lv_disp_drv_t *driver);
void display_refresh_flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels);
void display_refresh_poll(void);
bool display_refresh_capture(lv_color_t *destination, size_t pixels, void *context);

#ifdef __cplusplus
}
#endif
