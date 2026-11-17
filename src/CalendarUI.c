#include "UiTransition.h"
#include "CalendarUI.h"
#include "LifeData.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static lv_obj_t *screen, *return_to, *month_title, *grid, *detail_title, *detail_body;
static lv_obj_t *cells[42], *solar_labels[42], *lunar_labels[42];
static const lv_font_t *large_font, *small_font;
static int view_year, view_month, selected_day;
static int start_x, start_y;
static bool moved;
static uint32_t seen_revision;

static bool now_local(struct tm *out)
{
    const time_t stamp = time(NULL);
    return stamp > 1000000000 && localtime_r(&stamp, out) != NULL;
}

static int month_days(int year, int month)
{
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

static int first_weekday(int year, int month)
{
    if (month < 3) {month += 12; --year;}
    const int c = year / 100, y = year % 100;
    return (1 + 13 * (month + 1) / 5 + y + y / 4 + c / 4 + 5 * c + 6) % 7;
}

static void set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}

static void render(void)
{
    char text[256];
    struct tm today;
    const bool have_today = now_local(&today);
    if (!view_year) {
        set_text(month_title, "等待校时");
        set_text(detail_title, "连接 WiFi 后自动校时");
        set_text(detail_body, "校时后可查询农历、生肖、节气和节日");
        for (int i = 0; i < 42; ++i) lv_obj_add_flag(cells[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }
    snprintf(text, sizeof(text), "%04d年%02d月", view_year, view_month);
    set_text(month_title, text);
    const int first = first_weekday(view_year, view_month), count = month_days(view_year, view_month);
    for (int i = 0; i < 42; ++i) {
        const int day = i - first + 1;
        if (day < 1 || day > count) {lv_obj_add_flag(cells[i], LV_OBJ_FLAG_HIDDEN); continue;}
        lv_obj_clear_flag(cells[i], LV_OBJ_FLAG_HIDDEN);
        snprintf(text, sizeof(text), "%d", day);
        set_text(solar_labels[i], text);
        life_calendar_day_t info;
        if (life_data_get_day(view_year, view_month, day, &info))
            set_text(lunar_labels[i], !strcmp(info.lunar_day, "初一") ? info.lunar_month : info.lunar_day);
        else set_text(lunar_labels[i], "");
        const bool selected = day == selected_day;
        const bool is_today = have_today && today.tm_year + 1900 == view_year && today.tm_mon + 1 == view_month && today.tm_mday == day;
        lv_obj_set_style_bg_color(cells[i], lv_color_hex(selected ? UI_COLOR_ACCENT : UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(cells[i], selected ? LV_OPA_COVER : LV_OPA_60, 0);
        lv_obj_set_style_border_width(cells[i], 0, 0);
        lv_obj_set_style_outline_width(cells[i], is_today ? 1 : 0, 0);
        lv_obj_set_style_text_color(solar_labels[i], lv_color_hex(selected ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_color(lunar_labels[i], lv_color_hex(selected ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT_MUTED), 0);
    }
    life_calendar_day_t info;
    if (life_data_get_day(view_year, view_month, selected_day, &info)) {
        snprintf(text, sizeof(text), "%02d月%02d日  农历%s%s", view_month, selected_day, info.lunar_month, info.lunar_day);
        set_text(detail_title, text);
        size_t used = (size_t)snprintf(text, sizeof(text), "生肖：%s", info.zodiac);
        if (info.solar_term[0] && used < sizeof(text)) used += (size_t)snprintf(text + used, sizeof(text) - used, "  节气：%s", info.solar_term);
        if (info.festival[0] && used < sizeof(text)) snprintf(text + used, sizeof(text) - used, "\n节日：%s", info.festival);
        set_text(detail_body, text);
    } else {
        snprintf(text, sizeof(text), "%04d-%02d-%02d  农历资料待更新", view_year, view_month, selected_day);
        set_text(detail_title, text);
        life_data_calendar_status(text, sizeof(text));
        set_text(detail_body, text);
    }
}

static void select_month(int year, int month)
{
    if (year < 1901 || year > 2099) return;
    view_year = year; view_month = month;
    struct tm today;
    selected_day = now_local(&today) && year == today.tm_year + 1900 && month == today.tm_mon + 1 ? today.tm_mday : 1;
    life_data_request_day(year, month, selected_day);
    life_data_request_month(year, month);
    render();
}

static void change_month(int delta)
{
    if (!view_year) return;
    int year = view_year, month = view_month + delta;
    if (month < 1) {month = 12; --year;}
    if (month > 12) {month = 1; ++year;}
    select_month(year, month);
}

static void navigate(lv_event_t *event) {change_month((int)(intptr_t)lv_event_get_user_data(event));}
static void back(lv_event_t *event) {(void)event; if (return_to) ui_screen_back(return_to);}
static void today_clicked(lv_event_t *event)
{
    (void)event;
    struct tm now;
    if (now_local(&now)) select_month(now.tm_year + 1900, now.tm_mon + 1);
}
static void day_clicked(lv_event_t *event)
{
    if (moved || !view_year) return;
    const int index = (int)(intptr_t)lv_event_get_user_data(event);
    const int day = index - first_weekday(view_year, view_month) + 1;
    if (day < 1 || day > month_days(view_year, view_month)) return;
    selected_day = day;
    life_data_request_day(view_year, view_month, day);
    render();
}
static void gesture(lv_event_t *event)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {start_x = point.x; start_y = point.y; moved = false; return;}
    const int dx = point.x - start_x, dy = point.y - start_y;
    if (abs(dx) > 12 || abs(dy) > 12) moved = true;
    if (code == LV_EVENT_RELEASED && abs(dy) > 24 && abs(dy) > abs(dx)) change_month(dy < 0 ? 1 : -1);
}
static void refresh(lv_timer_t *timer)
{
    (void)timer;
    if (lv_scr_act() != screen) return;
    struct tm now;
    if (!view_year && now_local(&now)) select_month(now.tm_year + 1900, now.tm_mon + 1);
    const uint32_t revision = life_data_revision();
    if (seen_revision != revision) {seen_revision = revision; render();}
}
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, int x, int y, const char *text)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text(object, text);
    lv_obj_set_pos(object, x, y);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, const char *text, const lv_font_t *font, lv_event_cb_t cb, intptr_t data)
{
    lv_obj_t *object = lv_btn_create(parent);
    lv_obj_set_pos(object, x, y); lv_obj_set_size(object, width, 32);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_radius(object, 12, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_add_event_cb(object, cb, LV_EVENT_CLICKED, (void *)data);
    lv_obj_center(label(object, font, 0, 0, text));
    return object;
}
void calendar_ui_open(lv_obj_t *return_screen, const lv_font_t *font20, const lv_font_t *font16)
{
    return_to = return_screen;
    if (!screen) {
        large_font = font20 ? font20 : &lv_font_montserrat_20;
        /* The SD 16px subset lacks lunar/weekday glyphs; use the complete font. */
        (void)font16;
        small_font = large_font;
        screen = lv_obj_create(NULL);
        lv_obj_set_style_pad_all(screen, 0, 0); lv_obj_set_style_border_width(screen, 0, 0);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE); ui_theme_apply_background(screen);
        edge_back_bind(screen, back, NULL);
        lv_obj_t *title = label(screen, large_font, 0, 0, "日历"); lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
        button(screen, 394, 12, 66, "今天", small_font, today_clicked, 0);
        button(screen, 22, 48, 44, LV_SYMBOL_LEFT, &lv_font_montserrat_16, navigate, -1);
        button(screen, 414, 48, 44, LV_SYMBOL_RIGHT, &lv_font_montserrat_16, navigate, 1);
        month_title = label(screen, large_font, 0, 0, ""); lv_obj_align(month_title, LV_ALIGN_TOP_MID, 0, 52);
        grid = lv_obj_create(screen); lv_obj_set_pos(grid, 14, 84); lv_obj_set_size(grid, 452, 278);
        lv_obj_set_style_pad_all(grid, 0, 0); lv_obj_set_style_border_width(grid, 0, 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0); lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(grid, gesture, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(grid, gesture, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(grid, gesture, LV_EVENT_RELEASED, NULL);
        static const char *weekdays[] = {"日","一","二","三","四","五","六"};
        for (int i = 0; i < 7; ++i) {
            lv_obj_t *name = label(grid, small_font, 2 + i * 64, 0, weekdays[i]);
            lv_obj_set_width(name, 60); lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        }
        for (int i = 0; i < 42; ++i) {
            cells[i] = lv_btn_create(grid);
            lv_obj_set_pos(cells[i], 2 + i % 7 * 64, 26 + i / 7 * 42); lv_obj_set_size(cells[i], 60, 40);
            lv_obj_set_style_pad_all(cells[i], 0, 0); lv_obj_set_style_radius(cells[i], 10, 0);
            lv_obj_set_style_shadow_width(cells[i], 0, 0);
            lv_obj_set_style_border_color(cells[i], lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
            lv_obj_set_style_outline_color(cells[i], lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
            lv_obj_set_style_outline_pad(cells[i], 0, 0);
            lv_obj_set_style_bg_color(cells[i], lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
            lv_obj_clear_flag(cells[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_flag(cells[i], LV_OBJ_FLAG_EVENT_BUBBLE);
            lv_obj_add_event_cb(cells[i], day_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            solar_labels[i] = label(cells[i], &lv_font_montserrat_14, 0, 0, ""); lv_obj_align(solar_labels[i], LV_ALIGN_TOP_MID, 0, 0);
            lunar_labels[i] = label(cells[i], small_font, 0, 0, "");
            lv_obj_set_width(lunar_labels[i], 60); lv_label_set_long_mode(lunar_labels[i], LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_align(lunar_labels[i], LV_TEXT_ALIGN_CENTER, 0); lv_obj_align(lunar_labels[i], LV_ALIGN_TOP_MID, 0, 16);
        }
        lv_obj_t *details = lv_obj_create(screen); lv_obj_set_pos(details, 20, 372); lv_obj_set_size(details, 440, 98);
        lv_obj_set_style_pad_all(details, 0, 0); lv_obj_set_style_radius(details, 16, 0);
        lv_obj_set_style_bg_color(details, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_color(details, lv_color_hex(UI_COLOR_BORDER), 0); lv_obj_set_style_border_width(details, 1, 0);
        lv_obj_clear_flag(details, LV_OBJ_FLAG_SCROLLABLE);
        detail_title = label(details, large_font, 12, 8, ""); lv_obj_set_width(detail_title, 412);
        lv_label_set_long_mode(detail_title, LV_LABEL_LONG_DOT);
        detail_body = label(details, small_font, 12, 36, ""); lv_obj_set_width(detail_body, 412);
        lv_obj_set_height(detail_body, 56); lv_label_set_long_mode(detail_body, LV_LABEL_LONG_WRAP);
        lv_timer_create(refresh, 300, NULL);
    }
    ui_screen_open(screen);
    struct tm now;
    if (now_local(&now)) select_month(now.tm_year + 1900, now.tm_mon + 1);
    else {view_year = 0; render();}
    seen_revision = life_data_revision();
}
