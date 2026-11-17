#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Persistent ids retain the pre-1.18 app identities. Id 2 is retired Balance,
 * now merged into Games (id 1). Never reuse retired ids for another app. */
enum { APP_ID_GYRO, APP_ID_GAMES, APP_ID_OLD_BALANCE, APP_ID_MUSIC,
       APP_ID_WEATHER, APP_ID_CLOCK, APP_ID_PHOTOS, APP_ID_BROWSER,
       APP_ID_VIDEO, APP_ID_SETTINGS, APP_ID_CALENDAR, APP_ID_AI,
       APP_ID_WORDS };
typedef struct {
    const char *name;
    const void *image;
    lv_event_cb_t open;
    uint8_t persist_id;
} app_launcher_item_t;
void app_launcher_create(lv_obj_t *page, const lv_font_t *font, bool chinese,
                         const app_launcher_item_t *items, unsigned count);
bool app_launcher_is_dragging(void);
void app_launcher_cancel_drag(void);
#ifdef __cplusplus
}
#endif
