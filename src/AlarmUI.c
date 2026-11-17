#include "AlarmUI.h"
#include "AlarmService.h"
#include "ScreenIdle.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include "UITheme.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *page, *parent_page, *hour, *minute, *enabled, *repeat, *duration;
static lv_obj_t *remaining, *notice, *save_button, *days[7];
static lv_obj_t *alert, *alert_time, *alert_left;
static const lv_font_t *font;
static lv_timer_t *timer;
static bool chinese, loading, edited, time_edited;
static uint8_t mask;
static const char *form_error;
static uint32_t shown_revision;
static alarm_config_t source;

static void label_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static lv_obj_t *text(lv_obj_t *p, const char *value, int x, int y)
{
    lv_obj_t *o = lv_label_create(p);
    lv_label_set_text(o, value);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_pos(o, x, y);
    return o;
}
static void style_card(lv_obj_t *o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 16, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 12, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
static void paint_days(void)
{
    for (unsigned i = 0; i < 7; ++i) {
        lv_obj_set_style_bg_color(days[i], lv_color_hex(mask & (1u << i) ? UI_COLOR_ACCENT : UI_COLOR_PANEL), 0);
        lv_obj_set_style_text_color(days[i], lv_color_hex(mask & (1u << i) ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(days[i], 0), lv_color_hex(mask & (1u << i) ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT), 0);
    }
}
static void load_form(const alarm_snapshot_t *s)
{
    loading = true; form_error = NULL; source = s->config; mask = source.weekdays;
    lv_roller_set_selected(hour, source.hour, LV_ANIM_OFF);
    lv_roller_set_selected(minute, source.minute, LV_ANIM_OFF);
    if (source.enabled) lv_obj_add_state(enabled, LV_STATE_CHECKED);
    else lv_obj_clear_state(enabled, LV_STATE_CHECKED);
    lv_dropdown_set_selected(repeat, !mask ? 0 : mask == 127 ? 1 : mask == 31 ? 2 : 3);
    lv_dropdown_set_selected(duration, source.ring_minutes - 1);
    paint_days(); loading = false; edited = time_edited = false; shown_revision = s->revision;
}
static void changed(lv_event_t *e)
{
    if (loading) return;
    edited = true; form_error = NULL;
    lv_obj_t *target = lv_event_get_target(e);
    if (target == hour || target == minute) time_edited = true;
    if (target == repeat) {
        const unsigned mode = lv_dropdown_get_selected(repeat);
        mask = mode == 0 ? 0 : mode == 1 ? 127 : mode == 2 ? 31 : mask;
        paint_days();
    }
}
static void choose_day(lv_event_t *e)
{
    const unsigned i = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (i >= 7) return;
    form_error = NULL; mask ^= 1u << i; lv_dropdown_set_selected(repeat, 3); edited = true;
    paint_days();
}
static void save(lv_event_t *unused)
{
    (void)unused;
    alarm_snapshot_t s; alarm_get_snapshot(&s);
    alarm_config_t c = source;
    c.hour = lv_roller_get_selected(hour); c.minute = lv_roller_get_selected(minute);
    c.enabled = lv_obj_has_state(enabled, LV_STATE_CHECKED);
    c.ring_minutes = lv_dropdown_get_selected(duration) + 1;
    const unsigned mode = lv_dropdown_get_selected(repeat);
    c.weekdays = mode == 0 ? 0 : mode == 1 ? 127 : mode == 2 ? 31 : mask;
    if (mode == 3 && !mask) {form_error = chinese ? "请选择重复星期" : "Choose at least one weekday"; label_text(notice, form_error); return;}
    if (c.weekdays || time_edited || source.weekdays || c.once_epoch <= s.now_epoch) c.once_epoch = 0;
    if (!alarm_request_save(&c, NULL)) {
        form_error = chinese ? "暂未保存，请检查时间或稍后重试" : "Not saved; check the clock or retry";
        label_text(notice, form_error);
        return;
    }
    edited = false; form_error = NULL;
    label_text(notice, chinese ? "正在保存..." : "Saving...");
}
static void back(lv_event_t *unused)
{
    (void)unused;
    if (parent_page && lv_obj_is_valid(parent_page)) ui_screen_back(parent_page);
}
static void stop(lv_event_t *unused)
{
    (void)unused; alarm_dismiss();
    if (alert) {lv_obj_del(alert); alert = alert_time = alert_left = NULL;}
}
static void make_alert(void)
{
    alert = lv_obj_create(lv_layer_top());
    lv_obj_set_size(alert, 480, 480); lv_obj_set_pos(alert, 0, 0);
    lv_obj_set_style_bg_color(alert, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(alert, LV_OPA_50, 0);
    lv_obj_set_style_border_width(alert, 0, 0); lv_obj_set_style_pad_all(alert, 0, 0);
    lv_obj_clear_flag(alert, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *card = lv_obj_create(alert); style_card(card); lv_obj_set_size(card, 424, 262); lv_obj_center(card);
    lv_obj_t *title = text(card, chinese ? "闹钟响了" : "Alarm", 0, 0); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    alert_time = text(card, "", 0, 50); lv_obj_set_style_text_font(alert_time, &lv_font_montserrat_32, 0);
    lv_obj_align(alert_time, LV_ALIGN_TOP_MID, 0, 53);
    alert_left = text(card, "", 0, 110); lv_obj_align(alert_left, LV_ALIGN_TOP_MID, 0, 112);
    lv_obj_t *button = lv_btn_create(card); lv_obj_set_size(button, 352, 56);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_add_event_cb(button, stop, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = text(button, chinese ? "停止响铃" : "Stop alarm", 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_ON_ACCENT), 0); lv_obj_center(label);
}
static void tick(lv_timer_t *unused)
{
    (void)unused;
    alarm_snapshot_t s; alarm_get_snapshot(&s);
    char buf[128];
    if (s.ringing) {
        if (!alert) make_alert();
        screen_idle_request_wake();
        snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)s.config.hour, (unsigned)s.config.minute);
        label_text(alert_time, buf);
        snprintf(buf, sizeof(buf), chinese ? "%u分%u秒后停止" : "Stops in %u:%02u", (unsigned)(s.ring_seconds_left / 60), (unsigned)(s.ring_seconds_left % 60));
        label_text(alert_left, buf); lv_obj_move_foreground(alert);
    } else if (alert) {lv_obj_del(alert); alert = alert_time = alert_left = NULL;}
    if (!page || lv_scr_act() != page) return;
    if (s.revision != shown_revision && !edited && !s.save_busy) load_form(&s);
    if (s.save_busy) {
        label_text(notice, chinese ? "正在保存..." : "Saving..."); lv_obj_add_state(save_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(save_button, LV_STATE_DISABLED);
        label_text(notice, form_error ? form_error : s.save_failed ? (chinese ? "保存失败，请重试" : "Save failed; please retry") :
            edited ? (chinese ? "修改后请保存" : "Save your changes") : (chinese ? "仅保留一个闹钟，新设置会替换原提醒" : "One alarm; saving replaces the previous one"));
    }
    if (s.ringing) snprintf(buf, sizeof(buf), "%s", chinese ? "正在响铃" : "Ringing");
    else if (!s.config.enabled) snprintf(buf, sizeof(buf), "%s", chinese ? "闹钟已关闭" : "Alarm is off");
    else if (!s.time_valid) snprintf(buf, sizeof(buf), "%s", chinese ? "等待校时，请连接WiFi" : "Waiting for clock sync; connect WiFi");
    else if (!s.next_epoch) snprintf(buf, sizeof(buf), "%s", chinese ? "本次时间已过，请重新设置" : "Time passed; set a new alarm");
    else {
        const unsigned n = s.seconds_to_alarm;
        if (n >= 86400) snprintf(buf, sizeof(buf), chinese ? "还有%u天%u小时%u分提醒" : "In %ud %uh %um", n / 86400, n / 3600 % 24, n / 60 % 60);
        else if (n >= 3600) snprintf(buf, sizeof(buf), chinese ? "还有%u小时%u分提醒" : "In %uh %um", n / 3600, n / 60 % 60);
        else snprintf(buf, sizeof(buf), chinese ? "还有%u分%u秒提醒" : "In %um %us", n / 60, n % 60);
    }
    label_text(remaining, buf);
}
void alarm_ui_init(const lv_font_t *chinese_font)
{
    if (timer) return;
    chinese = chinese_font != NULL; font = chinese_font ? chinese_font : &lv_font_montserrat_20;
    timer = lv_timer_create(tick, 250, NULL);
}
bool alarm_ui_alert_visible(void) { return alert != NULL; }
void alarm_ui_open(lv_obj_t *parent, const lv_font_t *chinese_font)
{
    alarm_ui_init(chinese_font); parent_page = parent;
    if (!page) {
        page = lv_obj_create(NULL); ui_theme_apply_background(page);
        lv_obj_set_size(page, 480, 480); lv_obj_set_style_pad_all(page, 0, 0);
        lv_obj_set_style_border_width(page, 0, 0); lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
        edge_back_bind(page, back, NULL);
        lv_obj_t *title = text(page, chinese ? "闹钟" : "Alarm", 0, 18); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);
        enabled = lv_switch_create(page); lv_obj_set_pos(enabled, 395, 16);
        lv_obj_set_style_bg_color(enabled, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_event_cb(enabled, changed, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_t *summary = lv_obj_create(page); style_card(summary); lv_obj_set_pos(summary, 24, 60); lv_obj_set_size(summary, 432, 52);
        remaining = text(summary, "", 0, 0); lv_obj_center(remaining);
        lv_obj_t *body = lv_obj_create(page); lv_obj_set_pos(body, 12, 124); lv_obj_set_size(body, 456, 270);
        lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(body, 0, 0);
        lv_obj_set_style_pad_all(body, 8, 0); lv_obj_set_style_pad_row(body, 12, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN); lv_obj_set_scroll_dir(body, LV_DIR_VER);
        lv_obj_t *time_card = lv_obj_create(body); style_card(time_card); lv_obj_set_size(time_card, lv_pct(100), 146);
        hour = lv_roller_create(time_card); minute = lv_roller_create(time_card);
        char options[240]; size_t used = 0;
        for (unsigned i = 0; i < 24; ++i) used += snprintf(options + used, sizeof(options) - used, "%s%02u", i ? "\n" : "", i);
        lv_roller_set_options(hour, options, LV_ROLLER_MODE_NORMAL);
        used = 0; for (unsigned i = 0; i < 60; ++i) used += snprintf(options + used, sizeof(options) - used, "%s%02u", i ? "\n" : "", i);
        lv_roller_set_options(minute, options, LV_ROLLER_MODE_NORMAL);
        lv_obj_t *rollers[] = {hour, minute};
        for (unsigned i = 0; i < 2; ++i) {
            lv_obj_set_style_text_font(rollers[i], &lv_font_montserrat_32, 0);
            lv_obj_set_style_bg_color(rollers[i], lv_color_hex(UI_COLOR_ACCENT), LV_PART_SELECTED);
            lv_obj_set_style_text_color(rollers[i], lv_color_hex(UI_COLOR_ON_ACCENT), LV_PART_SELECTED);
            lv_roller_set_visible_row_count(rollers[i], 3); lv_obj_set_size(rollers[i], 118, 118);
            lv_obj_set_pos(rollers[i], 74 + i * 142, 0);
            lv_obj_add_event_cb(rollers[i], changed, LV_EVENT_VALUE_CHANGED, NULL);
        }
        lv_obj_t *colon = text(time_card, ":", 197, 42); lv_obj_set_style_text_font(colon, &lv_font_montserrat_32, 0);
        lv_obj_t *repeat_card = lv_obj_create(body); style_card(repeat_card); lv_obj_set_size(repeat_card, lv_pct(100), 136);
        text(repeat_card, chinese ? "重复" : "Repeat", 4, 5);
        repeat = lv_dropdown_create(repeat_card); lv_obj_set_pos(repeat, 154, 0); lv_obj_set_width(repeat, 230);
        lv_obj_set_style_text_font(repeat, font, 0);
        lv_obj_set_style_text_font(repeat, &lv_font_montserrat_16, LV_PART_INDICATOR);
        lv_obj_set_style_text_font(lv_dropdown_get_list(repeat), font, LV_PART_MAIN);
        lv_obj_set_style_text_font(lv_dropdown_get_list(repeat), font, LV_PART_SELECTED);
        lv_dropdown_set_options(repeat, chinese ? "仅一次\n每天\n周一到周五\n自选星期" : "Once\nEvery day\nMon to Fri\nCustom days");
        lv_obj_add_event_cb(repeat, changed, LV_EVENT_VALUE_CHANGED, NULL);
        static const char *cn[] = {"一","二","三","四","五","六","日"};
        static const char *en[] = {"M","T","W","T","F","S","S"};
        for (unsigned i = 0; i < 7; ++i) {
            days[i] = lv_btn_create(repeat_card); lv_obj_set_size(days[i], 45, 42); lv_obj_set_pos(days[i], i * 55, 62);
            lv_obj_set_style_pad_all(days[i], 0, 0); lv_obj_set_style_shadow_width(days[i], 0, 0);
            lv_obj_add_event_cb(days[i], choose_day, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
            lv_obj_t *day = text(days[i], chinese ? cn[i] : en[i], 0, 0); lv_obj_set_style_text_color(day, lv_color_hex(UI_COLOR_TEXT), 0); lv_obj_center(day);
        }
        lv_obj_t *ring_card = lv_obj_create(body); style_card(ring_card); lv_obj_set_size(ring_card, lv_pct(100), 72);
        text(ring_card, chinese ? "响铃时长" : "Ring time", 4, 8);
        duration = lv_dropdown_create(ring_card); lv_obj_set_pos(duration, 180, 0); lv_obj_set_width(duration, 204); lv_obj_set_style_text_font(duration, font, 0);
        lv_obj_set_style_text_font(duration, &lv_font_montserrat_16, LV_PART_INDICATOR);
        lv_obj_set_style_text_font(lv_dropdown_get_list(duration), font, LV_PART_MAIN);
        lv_obj_set_style_text_font(lv_dropdown_get_list(duration), font, LV_PART_SELECTED);
        char ring_options[900]; used = 0;
        for (unsigned i = 1; i <= 60; ++i) used += snprintf(ring_options + used, sizeof(ring_options) - used, chinese ? "%s%u 分钟" : "%s%u min", i > 1 ? "\n" : "", i);
        lv_dropdown_set_options(duration, ring_options); lv_obj_add_event_cb(duration, changed, LV_EVENT_VALUE_CHANGED, NULL);
        save_button = lv_btn_create(page); lv_obj_set_pos(save_button, 24, 404); lv_obj_set_size(save_button, 432, 48);
        lv_obj_set_style_bg_color(save_button, lv_color_hex(UI_COLOR_ACCENT_DARK), 0); lv_obj_add_event_cb(save_button, save, LV_EVENT_CLICKED, NULL);
        lv_obj_t *caption = text(save_button, chinese ? "保存闹钟" : "Save alarm", 0, 0);
        lv_obj_set_style_text_color(caption, lv_color_hex(UI_COLOR_ON_ACCENT), 0); lv_obj_center(caption);
        notice = text(page, "", 0, 459); lv_obj_set_width(notice, 464); lv_obj_set_style_text_align(notice, LV_TEXT_ALIGN_CENTER, 0); lv_obj_set_pos(notice, 8, 458);
    }
    alarm_snapshot_t s; alarm_get_snapshot(&s); load_form(&s); ui_screen_open(page); tick(NULL);
}
