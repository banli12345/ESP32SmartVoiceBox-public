#ifndef QUICK_PANEL_H
#define QUICK_PANEL_H

#include "lvgl.h"
#include "QuickSettingsService.h"

#ifdef __cplusplus
extern "C" {
#endif

void quick_panel_init(const lv_font_t *chinese_font);
void quick_panel_open(void);
void quick_panel_close(void);
bool quick_panel_is_visible(void);
// Called from the LVGL input read callback, never from the sensor task.
// A consumed edge/handle drag must not be forwarded to application widgets.
bool quick_panel_pointer(bool pressed, int x, int y);
void quick_panel_open_radio_settings(quick_radio_t radio);

#ifdef __cplusplus
}
#endif
#endif
