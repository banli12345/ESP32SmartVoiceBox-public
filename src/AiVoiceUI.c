#include "AiVoiceUI.h"
#include "AiVoice.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include "UiTransition.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *screen, *return_screen, *current, *status, *save_button;
static lv_obj_t *rows[2], *checks[2];
static lv_timer_t *timer;
static ai_voice_t selected;
static bool chinese, submitted;
static const lv_font_t *font;
static uint32_t view_key = UINT32_MAX;

static const char *voice_name(ai_voice_t voice)
{
    if (chinese) return voice == AI_VOICE_TIDE ? "男声" : "女声";
    return voice == AI_VOICE_TIDE ? "Male" : "Female";
}

static void refresh(void)
{
    ai_voice_state_t state;
    ai_voice_get_state(&state);
    const uint32_t key = (uint32_t)state.current | ((uint32_t)selected << 2) |
        ((uint32_t)state.busy << 4) | ((uint32_t)state.failed << 5) | ((uint32_t)submitted << 6);
    if (key == view_key) return;
    view_key = key;
    char text[96];
    snprintf(text, sizeof(text), chinese ? "当前音色：%s" : "Current voice: %s", voice_name(state.current));
    if (strcmp(lv_label_get_text(current), text)) lv_label_set_text(current, text);
    const char *message = chinese ? "保存后，下次回答生效" : "Save to apply to the next reply";
    if (state.busy) message = chinese ? "正在保存..." : "Saving...";
    else if (state.failed) message = chinese ? "保存失败，请重试" : "Save failed; please retry";
    else if (submitted) message = chinese ? "已保存，下次回答生效" : "Saved; applies to the next reply";
    if (strcmp(lv_label_get_text(status), message)) lv_label_set_text(status, message);
    lv_obj_set_style_text_color(status, lv_color_hex(state.failed ? UI_COLOR_ERROR : UI_COLOR_TEXT_MUTED), 0);
    for (unsigned i = 0; i < 2; ++i) {
        const bool active = selected == (ai_voice_t)i;
        lv_obj_set_style_border_color(rows[i], lv_color_hex(active ? UI_COLOR_ACCENT : UI_COLOR_BORDER), 0);
        lv_obj_set_style_bg_color(rows[i], lv_color_hex(active ? UI_COLOR_PANEL : UI_COLOR_SURFACE), 0);
        if (active) lv_obj_clear_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(checks[i], LV_OBJ_FLAG_HIDDEN);
        if (state.busy) lv_obj_add_state(rows[i], LV_STATE_DISABLED);
        else lv_obj_clear_state(rows[i], LV_STATE_DISABLED);
    }
    if (state.busy) lv_obj_add_state(save_button, LV_STATE_DISABLED);
    else lv_obj_clear_state(save_button, LV_STATE_DISABLED);
}

static void tick(lv_timer_t *unused)
{
    (void)unused;
    if (screen && lv_scr_act() == screen) refresh();
}

static void back(lv_event_t *unused)
{
    (void)unused;
    if (timer) lv_timer_pause(timer);
    if (return_screen && lv_obj_is_valid(return_screen)) ui_screen_back(return_screen);
}

static void deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != screen) return;
    if (timer) lv_timer_del(timer);
    timer = NULL;
    screen = return_screen = current = status = save_button = NULL;
    view_key = UINT32_MAX;
}

static void choose(lv_event_t *event)
{
    ai_voice_state_t state;
    ai_voice_get_state(&state);
    if (state.busy) return;
    const uintptr_t voice = (uintptr_t)lv_event_get_user_data(event);
    if (voice > AI_VOICE_TIDE) return;
    selected = (ai_voice_t)voice;
    submitted = false;
    refresh();
}

static void save(lv_event_t *unused)
{
    (void)unused;
    if (ai_voice_request(selected)) submitted = true;
    refresh();
}

void ai_voice_ui_open(lv_obj_t *settings, const lv_font_t *chinese_font)
{
    return_screen = settings;
    if (!screen) {
        chinese = chinese_font != NULL;
        font = chinese_font ? chinese_font : &lv_font_montserrat_20;
        screen = lv_obj_create(NULL);
        lv_obj_set_size(screen, 480, 480);
        lv_obj_set_style_pad_all(screen, 0, 0);
        lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        ui_theme_apply_background(screen);
        lv_obj_add_event_cb(screen, deleted, LV_EVENT_DELETE, NULL);
        edge_back_bind(screen, back, NULL);
        lv_obj_t *title = lv_label_create(screen);
        lv_label_set_text(title, chinese ? "语音设置" : "Voice settings");
        lv_obj_set_style_text_font(title, font, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
        current = lv_label_create(screen);
        lv_obj_set_style_text_font(current, font, 0);
        lv_obj_set_style_text_color(current, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(current, LV_ALIGN_TOP_MID, 0, 67);
        for (unsigned i = 0; i < 2; ++i) {
            rows[i] = lv_btn_create(screen);
            lv_obj_set_pos(rows[i], 26, 124 + i * 106);
            lv_obj_set_size(rows[i], 428, 88);
            lv_obj_set_style_pad_all(rows[i], 16, 0);
            lv_obj_set_style_radius(rows[i], 16, 0);
            lv_obj_set_style_border_width(rows[i], 2, 0);
            lv_obj_set_style_shadow_width(rows[i], 0, 0);
            lv_obj_set_style_bg_color(rows[i], lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
            lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(rows[i], choose, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            lv_obj_t *name = lv_label_create(rows[i]);
            lv_label_set_text(name, voice_name((ai_voice_t)i));
            lv_obj_set_style_text_font(name, font, 0);
            lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT), 0);
            lv_obj_align(name, LV_ALIGN_LEFT_MID, 6, -12);
            lv_obj_t *id = lv_label_create(rows[i]);
            lv_label_set_text(id, i == AI_VOICE_TIDE ? "tide" : "stella");
            lv_obj_set_style_text_font(id, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(id, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
            lv_obj_align(id, LV_ALIGN_LEFT_MID, 6, 15);
            checks[i] = lv_label_create(rows[i]);
            lv_label_set_text(checks[i], LV_SYMBOL_OK);
            lv_obj_set_style_text_font(checks[i], &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_color(checks[i], lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
            lv_obj_align(checks[i], LV_ALIGN_RIGHT_MID, -8, 0);
        }
        save_button = lv_btn_create(screen);
        lv_obj_set_pos(save_button, 26, 354);
        lv_obj_set_size(save_button, 428, 54);
        lv_obj_set_style_bg_color(save_button, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_set_style_radius(save_button, 14, 0);
        lv_obj_set_style_shadow_width(save_button, 0, 0);
        lv_obj_add_event_cb(save_button, save, LV_EVENT_CLICKED, NULL);
        lv_obj_t *label = lv_label_create(save_button);
        lv_label_set_text(label, chinese ? "保存音色" : "Save voice");
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_center(label);
        status = lv_label_create(screen);
        lv_obj_set_width(status, 440);
        lv_label_set_long_mode(status, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(status, font, 0);
        lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 430);
        timer = lv_timer_create(tick, 125, NULL);
    }
    ai_voice_state_t state;
    ai_voice_get_state(&state);
    selected = state.busy ? state.requested : state.current;
    submitted = false;
    refresh();
    if (timer) {lv_timer_resume(timer); lv_timer_reset(timer);}
    ui_screen_open(screen);
}
