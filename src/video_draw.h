#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif
void video_draw_init_ctx(lv_disp_drv_t *driver, lv_draw_ctx_t *ctx);
void video_draw_set_image(lv_obj_t *image);
#ifdef __cplusplus
}
#endif
