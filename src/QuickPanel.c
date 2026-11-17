#include "UiTransition.h"
#include "HotspotNameUI.h"
#include "QuickPanel.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include "Sound.h"
#include "MusicWeb.h"
#include "board_backlight.h"
#include "HotspotIcon.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

void custom_open_settings(void);
void custom_open_wifi_settings(void);

enum { PANEL_HEIGHT = 432, PANEL_OPEN_Y = 8, PANEL_CLOSED_Y = -432 };
typedef struct {
    lv_obj_t *button, *symbol, *name, *status;
    bool long_pressed, requested;
} radio_tile_t;
typedef struct {
    lv_obj_t *screen, *toggle, *status, *info;
    quick_radio_t radio;
} radio_screen_t;

static const lv_font_t *text_font;
static bool use_chinese, overlay_visible;
static lv_obj_t *overlay, *panel, *clock_label;
static lv_obj_t *brightness_slider, *volume_slider, *brightness_value, *volume_value;
static radio_tile_t tiles[QUICK_RADIO_COUNT];
static radio_screen_t bluetooth_screen, hotspot_screen;
static bool pointer_was_pressed, pointer_candidate, pointer_captured, pointer_opening;
static int pointer_start_x, pointer_start_y, pointer_panel_y;

static const char *tr(const char *chinese, const char *english)
{
    return use_chinese ? chinese : english;
}

static void label_text(lv_obj_t *label, const char *text)
{
    if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            lv_coord_t x, lv_coord_t y, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, y);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return label;
}

static void flat_object(lv_obj_t *object)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static const char *state_text(quick_radio_t radio, const quick_radio_state_t *state)
{
    if (state->busy) return tr("切换中", "Changing");
    if (state->failed) return tr("操作失败", "Try again");
    if (!state->enabled) return tr("已关闭", "Off");
    if (radio == QUICK_RADIO_BLUETOOTH)
        return state->connected ? tr("已连接", "Connected") : tr("可被发现", "Visible");
    if (radio == QUICK_RADIO_HOTSPOT) return tr("已开启", "On");
    return state->connected ? tr("已连接", "Connected") : tr("未连接", "Not connected");
}

static void update_tile(quick_radio_t radio)
{
    quick_radio_state_t state = {0};
    quick_radio_get(radio, &state);
    radio_tile_t *tile = &tiles[radio];
    if (!tile->button) return;
    if (!state.busy) tile->requested = state.enabled;
    const bool active = state.busy ? tile->requested : state.enabled;
    const uint32_t fg = active ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT;
    lv_obj_set_style_bg_color(tile->button,
        lv_color_hex(active ? UI_COLOR_ACCENT : UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(tile->button,
        lv_color_hex(active ? UI_COLOR_ACCENT_DARK : UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(tile->symbol, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(tile->name, lv_color_hex(fg), 0);
    lv_obj_set_style_text_color(tile->status,
        lv_color_hex(state.failed ? UI_COLOR_ERROR : fg), 0);
    label_text(tile->status, state_text(radio, &state));
}

static void set_panel_y(void *object, int32_t y)
{
    lv_obj_set_y(object, (lv_coord_t)y);
    const int visible = y - PANEL_CLOSED_Y;
    lv_obj_set_style_bg_opa(overlay, (lv_opa_t)(visible * LV_OPA_50 /
        (PANEL_OPEN_Y - PANEL_CLOSED_Y)), 0);
}

static void animation_done(lv_anim_t *animation)
{
    // The animation callback precedes the next LVGL layout pass.
    if (animation->end_value == PANEL_CLOSED_Y) {
        lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
        overlay_visible = false;
    }
}

static void settle_panel(bool open)
{
    if (!panel) return;
    lv_anim_del(panel, set_panel_y);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, panel);
    lv_anim_set_exec_cb(&animation, set_panel_y);
    lv_anim_set_values(&animation, lv_obj_get_y(panel), open ? PANEL_OPEN_Y : PANEL_CLOSED_Y);
    lv_anim_set_time(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&animation, animation_done);
    lv_anim_start(&animation);
}

static void dismiss_immediately(void)
{
    if (!overlay) return;
    lv_anim_del(panel, set_panel_y);
    set_panel_y(panel, PANEL_CLOSED_Y);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    overlay_visible = false;
    pointer_captured = false;
    lv_indev_t *input = lv_indev_get_act();
    if (input) lv_indev_wait_release(input);
}

void quick_panel_open(void)
{
    if (!overlay) return;
    overlay_visible = true;
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(overlay);
    for (int i = 0; i < QUICK_RADIO_COUNT; ++i) update_tile((quick_radio_t)i);
    lv_slider_set_value(brightness_slider, get_backlight_brightness(), LV_ANIM_OFF);
    lv_slider_set_value(volume_slider, get_audio_volume(), LV_ANIM_OFF);
    settle_panel(true);
}

void quick_panel_close(void)
{
    if (overlay_visible) settle_panel(false);
}

bool quick_panel_is_visible(void) { return overlay_visible || pointer_captured; }

bool quick_panel_pointer(bool pressed, int x, int y)
{
    if (!panel) return false;
    bool consumed = pointer_captured;
    if (pressed && !pointer_was_pressed) {
        pointer_start_x = x;
        pointer_start_y = y;
        pointer_panel_y = lv_obj_get_y(panel);
        pointer_opening = !overlay_visible;
        // Sliders and radio tiles keep their normal LVGL drag/press handling.
        pointer_candidate = pointer_opening && y <= 32;
        pointer_captured = pointer_opening ? false :
            ((y >= 406 && y <= 440) || (y >= 8 && y <= 92 && x < 360));
        consumed = pointer_captured;
        if (pointer_captured) lv_anim_del(panel, set_panel_y);
    }
    if (pressed && pointer_candidate && !pointer_captured) {
        const int dy = y - pointer_start_y, dx = x - pointer_start_x;
        if (dy > 12 && dy > (dx < 0 ? -dx : dx)) {
            pointer_captured = true;
            consumed = true;
            lv_anim_del(panel, set_panel_y);
            // Preserve ordinary taps in app headers, but cancel the underlying
            // button once this press becomes a downward edge gesture.
            lv_indev_t *input = lv_indev_get_act();
            if (input) {lv_indev_reset(input, NULL);lv_indev_wait_release(input);}
        }
    }
    if (pressed && pointer_captured) {
        const int dy = y - pointer_start_y;
        if (pointer_opening && dy > 8 && !overlay_visible) {
            overlay_visible = true;
            lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(overlay);
            for (int i = 0; i < QUICK_RADIO_COUNT; ++i) update_tile((quick_radio_t)i);
        }
        if (overlay_visible) {
            int offset = pointer_panel_y + dy;
            if (offset > PANEL_OPEN_Y) offset = PANEL_OPEN_Y;
            if (offset < PANEL_CLOSED_Y) offset = PANEL_CLOSED_Y;
            set_panel_y(panel, offset);
        }
    } else if (!pressed && pointer_was_pressed && pointer_captured) {
        const int dy = y - pointer_start_y;
        const int dx = x - pointer_start_x;
        const int abs_dx = dx < 0 ? -dx : dx;
        const bool open = pointer_opening ? dy >= 56 && dy > abs_dx : dy > -48;
        if (overlay_visible) settle_panel(open);
        pointer_captured = false;
        consumed = true;
    }
    pointer_was_pressed = pressed;
    if (!pressed) pointer_candidate = false;
    return consumed;
}

static void tile_event(lv_event_t *event)
{
    const quick_radio_t radio = (quick_radio_t)(intptr_t)lv_event_get_user_data(event);
    radio_tile_t *tile = &tiles[radio];
    switch (lv_event_get_code(event)) {
    case LV_EVENT_PRESSED:
        tile->long_pressed = false;
        break;
    case LV_EVENT_LONG_PRESSED:
        tile->long_pressed = true;
        quick_panel_open_radio_settings(radio);
        break;
    case LV_EVENT_CLICKED:
        if (!tile->long_pressed) {
            tile->requested = !tile->requested;
            quick_radio_request(radio, tile->requested);
            update_tile(radio);
        }
        break;
    default: break;
    }
}

static void slider_event(lv_event_t *event)
{
    const bool brightness = lv_event_get_user_data(event) != NULL;
    lv_obj_t *slider = lv_event_get_target(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_VALUE_CHANGED) {
        int percent = (int)lv_slider_get_value(slider);
        if (brightness) set_backlight_brightness(percent);
        else set_audio_volume(percent);
        char value[12];
        snprintf(value, sizeof(value), "%d%%", percent);
        label_text(brightness ? brightness_value : volume_value, value);
    } else if (!brightness && (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)) {
        save_audio_volume();
    }
}

static void settings_event(lv_event_t *event)
{
    (void)event;
    dismiss_immediately();
    custom_open_settings();
}

static void backdrop_event(lv_event_t *event)
{
    if (lv_event_get_target(event) == overlay) quick_panel_close();
}

static void radio_back_event(lv_event_t *event)
{
    (void)event;
    custom_open_settings();
}

static void radio_switch_event(lv_event_t *event)
{
    radio_screen_t *screen = lv_event_get_user_data(event);
    const bool enabled = lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED);
    tiles[screen->radio].requested = enabled;
    quick_radio_request(screen->radio, enabled);
}

static void update_radio_screen(radio_screen_t *screen)
{
    if (!screen->screen || lv_scr_act() != screen->screen) return;
    quick_radio_state_t state = {0};
    quick_radio_get(screen->radio, &state);
    if (!state.busy) {
        if (state.enabled) lv_obj_add_state(screen->toggle, LV_STATE_CHECKED);
        else lv_obj_clear_state(screen->toggle, LV_STATE_CHECKED);
    }
    label_text(screen->status, state_text(screen->radio, &state));
    char info[420];
    if (screen->radio == QUICK_RADIO_BLUETOOTH) {
        snprintf(info, sizeof(info), tr("设备名称\n%s\n\n开启后可被支持 BLE 的设备\n发现和连接。",
            "Device name\n%s\n\nDiscoverable by Bluetooth LE\ndevices while enabled."), quick_bluetooth_get_name());
    } else {
        quick_hotspot_info_t ap = {0};
        quick_hotspot_get_info(&ap);
        snprintf(info, sizeof(info), tr("热点名称  %s\n\n密码  %s\n\n地址  %s\n连接设备  %u\n\n%s\n\n手机点歌\nhttp://%s/\n%s",
            "Network  %s\n\nPassword  %s\n\nAddress  %s\nConnected devices  %u\n\n%s\n\nPhone music\nhttp://%s/\n%s"),
            ap.ssid[0] ? ap.ssid : "--", ap.password[0] ? ap.password : "--",
            ap.ip[0] ? ap.ip : "--", (unsigned)ap.clients,
            ap.internet_available ? tr("正在共享上游 WiFi 网络", "Sharing the upstream WiFi") :
                                    tr("连接 WiFi 后可共享上网", "Connect WiFi to share internet"),
            ap.ip[0] ? ap.ip : "--", music_web_is_running() ?
                tr("浏览器打开即可使用", "Open in your browser") : tr("热点开启后可使用", "Available with hotspot on"));
    }
    label_text(screen->info, info);
}

static void edit_hotspot_name(lv_event_t *event)
{
    (void)event;
    hotspot_name_ui_open(hotspot_screen.screen, text_font);
}

static void create_radio_screen(radio_screen_t *screen, quick_radio_t radio)
{
    screen->radio = radio;
    screen->screen = lv_obj_create(NULL);
    flat_object(screen->screen);
    ui_theme_apply_background(screen->screen);
    if (radio == QUICK_RADIO_HOTSPOT) lv_obj_add_flag(screen->screen, LV_OBJ_FLAG_SCROLLABLE);
    edge_back_bind(screen->screen, radio_back_event, NULL);
    make_label(screen->screen, radio == QUICK_RADIO_BLUETOOTH ? tr("蓝牙设置", "Bluetooth") :
        tr("热点设置", "Hotspot"), text_font, 30, 30, UI_COLOR_TEXT);
    lv_obj_t *card = lv_obj_create(screen->screen);
    flat_object(card);
    lv_obj_set_pos(card, 22, 90);
    lv_obj_set_size(card, 436, 112);
    lv_obj_set_style_radius(card, 20, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
    make_label(card, radio == QUICK_RADIO_BLUETOOTH ? "Bluetooth LE" : tr("个人热点", "Personal hotspot"),
        text_font, 20, 18, UI_COLOR_TEXT);
    screen->status = make_label(card, "", text_font, 20, 64, UI_COLOR_TEXT_MUTED);
    screen->toggle = lv_switch_create(card);
    lv_obj_set_pos(screen->toggle, 346, 24);
    lv_obj_set_size(screen->toggle, 66, 34);
    lv_obj_set_style_bg_color(screen->toggle, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(screen->toggle, radio_switch_event, LV_EVENT_VALUE_CHANGED, screen);
    if (radio == QUICK_RADIO_HOTSPOT) {
        lv_obj_t *edit = lv_btn_create(screen->screen);
        lv_obj_set_pos(edit, 30, 216); lv_obj_set_size(edit, 420, 46);
        lv_obj_set_style_bg_color(edit, lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_shadow_width(edit, 0, 0); lv_obj_set_style_radius(edit, 14, 0);
        lv_obj_add_event_cb(edit, edit_hotspot_name, LV_EVENT_CLICKED, NULL);
        lv_obj_t *name = make_label(edit, tr("修改名称和密码", "Edit name and password"), text_font, 0, 0, UI_COLOR_TEXT);
        lv_obj_center(name);
    }
    screen->info = make_label(screen->screen, "", text_font, 30,
        radio == QUICK_RADIO_HOTSPOT ? 280 : 226, UI_COLOR_TEXT_MUTED);
    lv_obj_set_width(screen->info, 420);
    lv_label_set_long_mode(screen->info, LV_LABEL_LONG_WRAP);
}

void quick_panel_open_radio_settings(quick_radio_t radio)
{
    if (radio < QUICK_RADIO_WIFI || radio >= QUICK_RADIO_COUNT) return;
    dismiss_immediately();
    if (radio == QUICK_RADIO_WIFI) {
        custom_open_wifi_settings();
        return;
    }
    radio_screen_t *screen = radio == QUICK_RADIO_BLUETOOTH ? &bluetooth_screen : &hotspot_screen;
    if (!screen->screen) create_radio_screen(screen, radio);
    ui_screen_open(screen->screen);
    update_radio_screen(screen);
}

static void refresh_timer(lv_timer_t *timer)
{
    (void)timer;
    if (overlay_visible) {
        for (int i = 0; i < QUICK_RADIO_COUNT; ++i) update_tile((quick_radio_t)i);
        char value[12];
        const int brightness = get_backlight_brightness();
        const int volume = get_audio_volume();
        if (!lv_obj_has_state(brightness_slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(brightness_slider, brightness, LV_ANIM_OFF);
            snprintf(value, sizeof(value), "%d%%", brightness);
            label_text(brightness_value, value);
        }
        if (!lv_obj_has_state(volume_slider, LV_STATE_PRESSED)) {
            lv_slider_set_value(volume_slider, volume, LV_ANIM_OFF);
            snprintf(value, sizeof(value), "%d%%", volume);
            label_text(volume_value, value);
        }
        time_t now = time(NULL);
        struct tm local;
        if (now > 1000000000 && localtime_r(&now, &local)) strftime(value, sizeof(value), "%H:%M", &local);
        else snprintf(value, sizeof(value), "--:--");
        label_text(clock_label, value);
    }
    update_radio_screen(&bluetooth_screen);
    update_radio_screen(&hotspot_screen);
}

void quick_panel_init(const lv_font_t *chinese_font)
{
    if (overlay) return;
    use_chinese = chinese_font != NULL;
    text_font = chinese_font ? chinese_font : &lv_font_montserrat_20;
    overlay = lv_obj_create(lv_layer_top());
    flat_object(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, 480, 480);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(overlay, backdrop_event, LV_EVENT_CLICKED, NULL);
    panel = lv_obj_create(overlay);
    flat_object(panel);
    lv_obj_set_pos(panel, 12, PANEL_CLOSED_Y);
    lv_obj_set_size(panel, 456, PANEL_HEIGHT);
    lv_obj_set_style_radius(panel, 26, 0);
    lv_obj_set_style_clip_corner(panel, true, 0);
    ui_theme_apply_background(panel);
    clock_label = make_label(panel, "--:--", &lv_font_montserrat_28, 22, 16, UI_COLOR_TEXT);
    make_label(panel, tr("快捷控制", "Quick controls"), text_font, 22, 58, UI_COLOR_TEXT_MUTED);
    lv_obj_t *settings = lv_btn_create(panel);
    flat_object(settings);
    lv_obj_set_pos(settings, 376, 22);
    lv_obj_set_size(settings, 54, 54);
    lv_obj_set_style_radius(settings, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(settings, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_t *gear = make_label(settings, LV_SYMBOL_SETTINGS, &lv_font_montserrat_24, 0, 0, UI_COLOR_ACCENT_DARK);
    lv_obj_center(gear);
    lv_obj_add_event_cb(settings, settings_event, LV_EVENT_CLICKED, NULL);
    const char *names[] = {"WiFi", tr("蓝牙", "Bluetooth"), tr("热点", "Hotspot")};
    const char *symbols[] = {LV_SYMBOL_WIFI, LV_SYMBOL_BLUETOOTH, NULL};
    for (int i = 0; i < QUICK_RADIO_COUNT; ++i) {
        radio_tile_t *tile = &tiles[i];
        tile->button = lv_btn_create(panel);
        flat_object(tile->button);
        lv_obj_set_pos(tile->button, 18 + i * 144, 100);
        lv_obj_set_size(tile->button, 132, 126);
        lv_obj_set_style_radius(tile->button, 22, 0);
        lv_obj_set_style_bg_color(tile->button, lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_bg_color(tile->button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
        tile->symbol = i == QUICK_RADIO_HOTSPOT ? hotspot_icon_create(tile->button, 56) :
            make_label(tile->button, symbols[i], &lv_font_montserrat_28, 0, 16, UI_COLOR_ACCENT_DARK);
        lv_obj_set_style_text_color(tile->symbol, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_align(tile->symbol, LV_ALIGN_TOP_MID, 0, i == QUICK_RADIO_HOTSPOT ? 8 : 15);
        tile->name = make_label(tile->button, names[i], text_font, 0, 65, UI_COLOR_TEXT);
        lv_obj_align(tile->name, LV_ALIGN_TOP_MID, 0, 65);
        tile->status = make_label(tile->button, "", use_chinese ? text_font : &lv_font_montserrat_14,
                                 0, 96, UI_COLOR_TEXT_MUTED);
        lv_obj_align(tile->status, LV_ALIGN_TOP_MID, 0, 96);
        lv_obj_add_event_cb(tile->button, tile_event, LV_EVENT_ALL, (void *)(intptr_t)i);
    }
    make_label(panel, tr("亮度", "Brightness"), text_font, 24, 240, UI_COLOR_TEXT);
    brightness_value = make_label(panel, "100%", &lv_font_montserrat_20, 365, 242, UI_COLOR_TEXT_MUTED);
    make_label(panel, tr("音量", "Volume"), text_font, 24, 314, UI_COLOR_TEXT);
    volume_value = make_label(panel, "100%", &lv_font_montserrat_20, 365, 316, UI_COLOR_TEXT_MUTED);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *slider = lv_slider_create(panel);
        lv_obj_set_pos(slider, 30, 278 + i * 74);
        lv_obj_set_size(slider, 396, 22);
        lv_slider_set_range(slider, i == 0 ? 5 : 0, 100);
        lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_PRESSED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_ON_ACCENT), LV_PART_KNOB);
        lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 2, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_KNOB);
        lv_obj_add_event_cb(slider, slider_event, LV_EVENT_ALL, i == 0 ? (void *)1 : NULL);
        if (i == 0) brightness_slider = slider;
        else volume_slider = slider;
    }
    lv_obj_t *hint = make_label(panel, tr("长按图标进入设置", "Hold a tile for settings"),
        use_chinese ? text_font : &lv_font_montserrat_14, 0, 389, UI_COLOR_TEXT_MUTED);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 389);
    lv_obj_t *handle = lv_obj_create(panel);
    flat_object(handle);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(handle, 58, 4);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(refresh_timer, 250, NULL);
}
