#include "UiTransition.h"
#include "ScreenIdleUI.h"
#include "ScreenIdle.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include <stdio.h>

static lv_obj_t *screen, *return_screen, *status;
static lv_obj_t *rows[9], *checks[9];
static const uint16_t minutes[] = {1, 2, 3, 5, 10, 20, 30, 60, 0};
static bool use_chinese;
static const lv_font_t *font;

static void go_back(lv_event_t *event)
{
    (void)event;
    if (return_screen) ui_screen_back(return_screen);
}

static void refresh_selection(void)
{
    const uint16_t selected = screen_idle_get_timeout();
    for (unsigned i = 0; i < 9; ++i) {
        const bool active = selected == minutes[i];
        lv_obj_set_style_border_color(rows[i],
            lv_color_hex(active ? UI_COLOR_ACCENT : UI_COLOR_BORDER), 0);
        lv_obj_set_style_bg_color(rows[i],
            lv_color_hex(active ? UI_COLOR_PANEL : UI_COLOR_SURFACE), 0);
        if (active) lv_obj_clear_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void choose_timeout(lv_event_t *event)
{
    const unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (i >= 9) return;
    const bool saved = screen_idle_set_timeout(minutes[i]);
    lv_label_set_text(status, saved ? (use_chinese ? "已保存，触摸后重新计时" : "Saved; touch restarts the timer") :
                                    (use_chinese ? "保存失败，请重试" : "Save failed; please try again"));
    lv_obj_set_style_text_color(status, lv_color_hex(saved ? UI_COLOR_TEXT_MUTED : UI_COLOR_ERROR), 0);
    refresh_selection();
}

void screen_idle_ui_open(lv_obj_t *settings, const lv_font_t *chinese_font)
{
    return_screen = settings;
    if (!screen) {
        use_chinese = chinese_font != NULL;
        font = chinese_font ? chinese_font : &lv_font_montserrat_20;
        screen = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        ui_theme_apply_background(screen);
        edge_back_bind(screen, go_back, NULL);
        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, use_chinese ? "息屏时间" : "Screen timeout");
        lv_obj_set_style_text_font(title, font, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
        lv_obj_t *hint = lv_label_create(screen);
        lv_label_set_text(hint, use_chinese ? "双击屏幕或转动设备亮屏" : "Double-tap or move to wake");
        lv_obj_set_style_text_font(hint, font, 0);
        lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 58);
        status = lv_label_create(screen);
        lv_obj_set_style_text_font(status, font, 0);
        lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 89);

        lv_obj_t *list = lv_obj_create(screen);
        lv_obj_set_pos(list, 20, 124);
        lv_obj_set_size(list, 440, 344);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_pad_all(list, 4, 0);
        lv_obj_set_style_pad_row(list, 8, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_scroll_dir(list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
        for (unsigned i = 0; i < 9; ++i) {
            lv_obj_t *row = rows[i] = lv_btn_create(list);
            lv_obj_set_size(row, lv_pct(100), 52);
            lv_obj_set_style_radius(row, 14, 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_shadow_width(row, 0, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(row, choose_timeout, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            lv_obj_t *label = lv_label_create(row);
            char text[32];
            if (minutes[i]) snprintf(text, sizeof(text), use_chinese ? "%u 分钟" : "%u min", (unsigned)minutes[i]);
            else snprintf(text, sizeof(text), "%s", use_chinese ? "永不" : "Never");
            lv_label_set_text(label, text);
            lv_obj_set_style_text_font(label, font, 0);
            lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);
            checks[i] = lv_label_create(row);
            lv_label_set_text(checks[i], LV_SYMBOL_OK);
            lv_obj_set_style_text_font(checks[i], &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(checks[i], lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
            lv_obj_align(checks[i], LV_ALIGN_RIGHT_MID, -8, 0);
        }
    }
    lv_label_set_text(status, use_chinese ? "触摸后重新计时，音乐继续播放" : "Touch resets timer; audio keeps playing");
    lv_obj_set_style_text_color(status, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    refresh_selection();
    ui_screen_open(screen);
    for (unsigned i = 0; i < 9; ++i)
        if (screen_idle_get_timeout() == minutes[i]) lv_obj_scroll_to_view(rows[i], LV_ANIM_OFF);
}
