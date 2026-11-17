#pragma once
#include "lvgl.h"
#include "MediaFiles.h"
#ifdef __cplusplus
extern "C" {
#endif
lv_obj_t *media_actions_row(lv_obj_t *gallery,int index,media_kind_t kind,
 const char *path,const char *name,lv_event_cb_t open,void (*changed)(void));
bool media_actions_back(lv_obj_t *screen);
#ifdef __cplusplus
}
#endif
