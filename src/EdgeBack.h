#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void edge_back_init(void);
void edge_back_bind(lv_obj_t *screen, lv_event_cb_t callback, void *user_data);
bool edge_back_pointer(bool pressed, int x, int y, bool enabled);
bool edge_back_is_active(void);
#ifdef __cplusplus
}
#endif
