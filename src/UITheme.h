#ifndef SMARTVOICEBOX_UI_THEME_H
#define SMARTVOICEBOX_UI_THEME_H

#include "lvgl.h"

// Grass gradient theme for secondary screens; legacy screens keep their styles.
#define UI_COLOR_BG            0xD4E7C2
#define UI_COLOR_BG_BOTTOM     0xF4F8EC
#define UI_COLOR_SURFACE       0xFCFDF8
#define UI_COLOR_PANEL         0xE4EFDC
#define UI_COLOR_PRESSED       0xCDDEB9
#define UI_COLOR_DISABLED      0xE5EBDD
#define UI_COLOR_TEXT          0x283B26
#define UI_COLOR_TEXT_MUTED    0x4C6044
#define UI_COLOR_TEXT_DISABLED 0x718368
#define UI_COLOR_ACCENT        0x4D7436
#define UI_COLOR_ACCENT_DARK   0x365A26
#define UI_COLOR_BORDER        0xA9C292
#define UI_COLOR_SUCCESS       0x326741
#define UI_COLOR_WARNING       0x865D17
#define UI_COLOR_ERROR         0xAD3C32
#define UI_COLOR_LINK          0x286279
#define UI_COLOR_ON_ACCENT     0xFFFFFF
#define UI_COLOR_MEDIA_BG      0x101A13
#define UI_COLOR_SKY_TOP       0x6DBAEA
#define UI_COLOR_SKY_BOTTOM    0xE5F4FB
#define UI_COLOR_SKY_TEXT      0x21465F
#define UI_COLOR_SKY_MUTED     0x294157
#define UI_COLOR_SKY_BORDER    0xB2D9EC
#define UI_COLOR_SKY_CARD      0xF7FCFF
#define UI_COLOR_CLOUD         0xFFFFFF
#define UI_COLOR_SUN           0xFFE6A4

static inline void ui_theme_apply_background(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_BG), 0);
    lv_obj_set_style_bg_grad_color(object, lv_color_hex(UI_COLOR_BG_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(object, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(object, 0, 0);
    lv_obj_set_style_bg_grad_stop(object, 255, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(UI_COLOR_TEXT), 0);
}

#endif
