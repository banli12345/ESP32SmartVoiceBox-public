#include "HotspotNameUI.h"
#include "HotspotName.h"
#include "UiTransition.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include "lv_ime_pinyin.h"
#include <stdio.h>
#include <string.h>

extern lv_pinyin_dict_t *get_sd_pinyin_dict(void);

static lv_obj_t *screen, *return_to, *name_input, *password_input, *message;
static lv_obj_t *save_button, *show_button, *show_label, *keyboard, *ime, *candidates;
static lv_timer_t *timer;
static const lv_font_t *text_font;
static bool submitting;
static uint32_t seen_revision;
static lv_pinyin_dict_t fallback[] = {{"wo","我"},{"jia","家"},{"wang","网"},{"luo","络"},{"re","热"},{"dian","点"},{NULL,NULL}};
static const char PASSWORD_CHARS[] =
    " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";

static void hide_keyboard(void)
{
    if (ime) lv_ime_pinyin_clear(ime);
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (candidates) lv_obj_add_flag(candidates, LV_OBJ_FLAG_HIDDEN);
}
static void clear_composition_data(void)
{
    if (!ime) return;
    lv_ime_pinyin_clear(ime);
}
static void reset_composition(void)
{
    if (!ime) return;
    clear_composition_data();
    lv_ime_pinyin_set_mode(ime, LV_IME_PINYIN_MODE_K26);
    if (keyboard) lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    if (candidates) lv_obj_add_flag(candidates, LV_OBJ_FLAG_HIDDEN);
}
static void composition_guard(lv_event_t *event)
{
    if (!ime || !keyboard) return;
    /* Default handler already inserted the glyph. Stop the later IME callback
     * so a password key stays literal ASCII and never becomes a candidate. */
    if (lv_keyboard_get_textarea(keyboard) == password_input) {
        lv_event_stop_processing(event);
        return;
    }
}
static void present_keyboard(lv_obj_t *field)
{
    if (submitting || !keyboard || !field) return;
    if (lv_keyboard_get_textarea(keyboard) != field) reset_composition();
    lv_keyboard_set_textarea(keyboard, field);
    if (ime) {
        lv_ime_pinyin_refresh(ime);
        lv_obj_t *switcher = ((lv_ime_pinyin_t *)ime)->language_button;
        if (switcher) {
            if (field == password_input) lv_obj_add_flag(switcher, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_clear_flag(switcher, LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
    if (candidates) {
        lv_obj_move_foreground(candidates);
        if (field == password_input) lv_obj_add_flag(candidates, LV_OBJ_FLAG_HIDDEN);
        /* The IME reveals name candidates only after a matching syllable. */
    }
}
static void show_keyboard(lv_event_t *event)
{
    present_keyboard(event ? lv_event_get_target(event) : name_input);
}
static void back(lv_event_t *event)
{
    (void)event;
    if (submitting) return;
    if (keyboard && !lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN)) hide_keyboard();
    else if (return_to) ui_screen_back(return_to);
}
static void cancel(lv_event_t *event)
{
    (void)event;
    if (submitting) return;
    hide_keyboard();
    if (return_to) ui_screen_back(return_to);
}
static void keyboard_done(lv_event_t *event) {(void)event; hide_keyboard();}
static void set_show_caption(void)
{
    if (!show_label || !password_input) return;
    lv_label_set_text(show_label, lv_textarea_get_password_mode(password_input) ? "显示" : "隐藏");
}
static void toggle_password(lv_event_t *event)
{
    (void)event;
    if (submitting || !password_input) return;
    lv_textarea_set_password_mode(password_input, !lv_textarea_get_password_mode(password_input));
    set_show_caption();
}
static void set_editing_enabled(bool enabled)
{
    lv_state_t bit = LV_STATE_DISABLED;
    if (enabled) {
        lv_obj_clear_state(name_input, bit);
        lv_obj_clear_state(password_input, bit);
        lv_obj_clear_state(keyboard, bit);
        lv_obj_clear_state(show_button, bit);
        if (candidates) lv_obj_clear_state(candidates, bit);
    } else {
        lv_obj_add_state(name_input, bit);
        lv_obj_add_state(password_input, bit);
        lv_obj_add_state(keyboard, bit);
        lv_obj_add_state(show_button, bit);
        lv_obj_add_state(save_button, bit);
        if (candidates) lv_obj_add_state(candidates, bit);
    }
}
static bool update_count(void)
{
    const char *name_value = lv_textarea_get_text(name_input);
    const char *password_value = lv_textarea_get_text(password_input);
    char normalized[33];
    const bool name_ok = hotspot_name_normalize(name_value, normalized);
    const bool password_ok = hotspot_password_valid(password_value);
    const unsigned name_bytes = (unsigned)strlen(name_ok ? normalized : name_value);
    const unsigned password_bytes = (unsigned)strlen(password_value);
    const char *hint = "";
    if (!name_ok && !password_ok) hint = "\n名称无效，密码需8-63位";
    else if (!name_ok) hint = "\n名称不能为空且最多32字节";
    else if (!password_ok) hint = "\n密码需8-63位字母数字符号";
    char text[128];
    snprintf(text, sizeof(text), "名称%u/32  密码%u/63%s", name_bytes, password_bytes, hint);
    lv_label_set_text(message, text);
    lv_obj_set_style_text_color(message, lv_color_hex(name_ok && password_ok ? UI_COLOR_TEXT_MUTED : UI_COLOR_ERROR), 0);
    if (name_ok && password_ok && !submitting) lv_obj_clear_state(save_button, LV_STATE_DISABLED);
    else lv_obj_add_state(save_button, LV_STATE_DISABLED);
    return name_ok && password_ok;
}
static void changed(lv_event_t *event) {(void)event; if (!submitting) update_count();}
static void save(lv_event_t *event)
{
    (void)event;
    if (submitting || !update_count()) return;
    if (hotspot_credentials_request(lv_textarea_get_text(name_input), lv_textarea_get_text(password_input))) {
        submitting = true;
        set_editing_enabled(false);
        lv_label_set_text(message, "正在保存…");
        lv_obj_set_style_text_color(message, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    } else {
        hotspot_name_state_t state;
        hotspot_name_get(&state);
        lv_label_set_text(message, state.message);
        lv_obj_set_style_text_color(message, lv_color_hex(UI_COLOR_ERROR), 0);
    }
}
static void refresh(lv_timer_t *unused)
{
    (void)unused;
    if (!screen || lv_scr_act() != screen) return;
    hotspot_name_state_t state;
    hotspot_name_get(&state);
    if (state.revision == seen_revision) return;
    seen_revision = state.revision;
    if (submitting && !state.busy) {
        submitting = false;
        set_editing_enabled(true);
        if (state.result == 1) {
            reset_composition();
            lv_textarea_set_password_mode(password_input, true);
            set_show_caption();
            lv_textarea_set_text(name_input, state.name);
            lv_textarea_set_text(password_input, state.password);
        }
        update_count();
        lv_label_set_text(message, state.message);
        lv_obj_set_style_text_color(message, lv_color_hex(state.result == 1 ? UI_COLOR_SUCCESS : UI_COLOR_ERROR), 0);
        if (state.result == 1) hide_keyboard();
    }
}
static lv_obj_t *label(lv_obj_t *parent, const char *value, int x, int y, int width)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, text_font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_pos(object, x, y);
    lv_obj_set_width(object, width);
    lv_label_set_long_mode(object, LV_LABEL_LONG_WRAP);
    lv_label_set_text(object, value);
    return object;
}
static lv_obj_t *button(const char *value, int x, lv_event_cb_t callback)
{
    lv_obj_t *object = lv_btn_create(screen);
    lv_obj_set_pos(object, x, 242);
    lv_obj_set_size(object, 204, 38);
    lv_obj_set_style_radius(object, 14, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t *name = label(object, value, 0, 0, LV_SIZE_CONTENT);
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
    lv_obj_center(name);
    return object;
}
static void deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != screen) return;
    if (timer) { lv_timer_del(timer); timer = NULL; }
    /* The upstream IME destructor owns and deletes its keyboard/candidates.
     * Delete it before the screen tears down those child widgets. */
    if (ime) { lv_obj_del(ime); ime = NULL; }
    candidates = NULL;
    screen = name_input = password_input = keyboard = message = NULL;
    save_button = show_button = show_label = NULL;
}
void hotspot_name_ui_open(lv_obj_t *previous, const lv_font_t *font)
{
    return_to = previous;
    if (!screen) {
        text_font = font ? font : &lv_font_montserrat_20;
        screen = lv_obj_create(NULL);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(screen, 0, 0);
        ui_theme_apply_background(screen);
        edge_back_bind(screen, back, NULL);
        lv_obj_add_event_cb(screen, deleted, LV_EVENT_DELETE, NULL);
        lv_obj_t *title = label(screen, "热点名称和密码", 0, 16, LV_SIZE_CONTENT);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
        label(screen, "名称，最多32字节", 28, 52, 424);
        name_input = lv_textarea_create(screen);
        lv_obj_set_pos(name_input, 28, 76);
        lv_obj_set_size(name_input, 424, 42);
        lv_textarea_set_one_line(name_input, true);
        lv_textarea_set_max_length(name_input, 32);
        lv_obj_set_style_text_font(name_input, text_font, 0);
        lv_obj_set_style_border_color(name_input, lv_color_hex(UI_COLOR_BORDER), 0);
        label(screen, "密码，8-63位字母数字符号", 28, 128, 300);
        show_button = lv_btn_create(screen);
        lv_obj_set_pos(show_button, 338, 122);
        lv_obj_set_size(show_button, 114, 28);
        lv_obj_set_style_radius(show_button, 10, 0);
        lv_obj_set_style_shadow_width(show_button, 0, 0);
        lv_obj_set_style_pad_all(show_button, 0, 0);
        lv_obj_set_style_bg_color(show_button, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_color(show_button, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(show_button, 1, 0);
        lv_obj_clear_flag(show_button, LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_obj_add_event_cb(show_button, toggle_password, LV_EVENT_CLICKED, NULL);
        show_label = label(show_button, "显示", 0, 0, LV_SIZE_CONTENT);
        lv_obj_center(show_label);
        password_input = lv_textarea_create(screen);
        lv_obj_set_pos(password_input, 28, 154);
        lv_obj_set_size(password_input, 424, 42);
        lv_textarea_set_one_line(password_input, true);
        lv_textarea_set_max_length(password_input, 63);
        lv_textarea_set_accepted_chars(password_input, PASSWORD_CHARS);
        lv_textarea_set_password_mode(password_input, true);
        lv_textarea_set_password_bullet(password_input, "*");
        lv_textarea_set_password_show_time(password_input, 0);
        lv_obj_set_style_text_font(password_input, text_font, 0);
        lv_obj_set_style_border_color(password_input, lv_color_hex(UI_COLOR_BORDER), 0);
        message = label(screen, "", 28, 200, 424);
        lv_obj_set_height(message, 40);
        lv_obj_set_style_text_line_space(message, 0, 0);
        save_button = button("保存", 28, save);
        button("取消", 248, cancel);
        keyboard = lv_keyboard_create(screen);
        lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(keyboard, 0, 316);
        lv_obj_set_size(keyboard, 480, 164);
        lv_keyboard_set_textarea(keyboard, name_input);
        lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_done, LV_EVENT_CANCEL, NULL);
        lv_obj_add_event_cb(name_input, show_keyboard, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(password_input, show_keyboard, LV_EVENT_CLICKED, NULL);
        lv_obj_add_event_cb(name_input, changed, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(password_input, changed, LV_EVENT_VALUE_CHANGED, NULL);
        ime = lv_ime_pinyin_create(screen);
        lv_obj_set_size(ime, 1, 1);
        lv_obj_set_pos(ime, -2, -2);
        lv_obj_clear_flag(ime, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(ime, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ime, 0, 0);
        lv_obj_add_event_cb(keyboard, composition_guard, LV_EVENT_VALUE_CHANGED, NULL);
        lv_ime_pinyin_set_keyboard(ime, keyboard);
        lv_pinyin_dict_t *dictionary = get_sd_pinyin_dict();
        lv_ime_pinyin_set_dict(ime, dictionary ? dictionary : fallback);
        lv_ime_pinyin_set_mode(ime, LV_IME_PINYIN_MODE_K26);
        candidates = lv_ime_pinyin_get_cand_panel(ime);
        if (candidates) {
            lv_obj_set_parent(candidates, screen);
            lv_obj_set_align(candidates, LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(candidates, 0, 282);
            lv_obj_set_size(candidates, 480, 34);
            lv_obj_set_style_text_font(candidates, text_font, LV_PART_ITEMS);
            lv_obj_set_style_bg_color(candidates, lv_color_hex(UI_COLOR_SURFACE), 0);
        }
        timer = lv_timer_create(refresh, 120, NULL);
    }
    hotspot_name_state_t state;
    hotspot_name_get(&state);
    submitting = state.busy;
    seen_revision = state.revision;
    reset_composition();
    lv_textarea_set_password_mode(password_input, true);
    lv_textarea_set_password_bullet(password_input, "*");
    set_show_caption();
    lv_textarea_set_text(name_input, state.name);
    lv_textarea_set_text(password_input, state.password);
    set_editing_enabled(!submitting);
    if (submitting) {
        hide_keyboard();
        lv_label_set_text(message, "正在保存…");
        lv_obj_set_style_text_color(message, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    } else {
        update_count();
    }
    ui_screen_open(screen);
    if (!submitting) present_keyboard(name_input);
}
