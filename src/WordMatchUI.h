#pragma once

#include "lvgl.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Match English and Chinese cards from the SD word book.
 * fallback_font is not owned. The screen keeps the CET-4 font only while open. */
void word_match_ui_open(lv_obj_t *return_to, const lv_font_t *fallback_font, bool chinese);
lv_obj_t *word_match_ui_get_screen(void);

#ifdef __cplusplus
}
#endif
