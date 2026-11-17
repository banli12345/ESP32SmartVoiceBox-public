#include "HomeInfo.h"
#include "LifeData.h"
#include "UITheme.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static lv_obj_t *life_panel, *life_values[4];
static lv_obj_t *date_panel, *date_row, *gregorian_label, *weekday_label;
static lv_obj_t *lunar_label, *term_label, *festival_label;

static void text(lv_obj_t *label, const char *value)
{
    if (strcmp(lv_label_get_text(label), value)) lv_label_set_text(label, value);
}

/* Small vector icons use the same foreground color as the four readings. */
static lv_point_t point(const lv_area_t *area, int x, int y)
{
    const int size = lv_area_get_width(area);
    return (lv_point_t){(lv_coord_t)(area->x1 + x * size / 28), (lv_coord_t)(area->y1 + y * size / 28)};
}
static void segment(lv_draw_ctx_t *draw, const lv_area_t *area, lv_color_t color, int x1, int y1, int x2, int y2)
{
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color; line.width = 2; line.round_start = 1; line.round_end = 1;
    const lv_point_t a = point(area, x1, y1), b = point(area, x2, y2);
    lv_draw_line(draw, &line, &a, &b);
}
static void box(lv_draw_ctx_t *draw, const lv_area_t *area, lv_color_t color, int x1, int y1, int x2, int y2, int radius, bool filled)
{
    lv_draw_rect_dsc_t style;
    lv_draw_rect_dsc_init(&style);
    style.bg_color = color; style.bg_opa = filled ? LV_OPA_COVER : LV_OPA_TRANSP;
    style.border_color = color; style.border_width = filled ? 0 : 2;
    style.radius = radius;
    const lv_point_t a = point(area, x1, y1), b = point(area, x2, y2);
    const lv_area_t bounds = {a.x, a.y, b.x, b.y};
    lv_draw_rect(draw, &style, &bounds);
}
static void icon_draw(lv_event_t *event)
{
    lv_obj_t *icon = lv_event_get_target(event);
    lv_draw_ctx_t *draw = lv_event_get_draw_ctx(event);
    lv_area_t area; lv_obj_get_coords(icon, &area);
    const lv_color_t color = lv_obj_get_style_text_color(icon, 0);
    const int kind = (int)(intptr_t)lv_event_get_user_data(event);
    if (kind == 0) {
        static const int8_t shirt[][2] = {{9,3},{5,5},{2,11},{6,14},{8,11},{8,24},{20,24},{20,11},{22,14},{26,11},{23,5},{19,3},{17,7},{11,7},{9,3}};
        for (unsigned i = 1; i < sizeof(shirt) / sizeof(shirt[0]); ++i)
            segment(draw, &area, color, shirt[i-1][0], shirt[i-1][1], shirt[i][0], shirt[i][1]);
    } else if (kind == 1) {
        box(draw, &area, color, 9,9,19,19,LV_RADIUS_CIRCLE,false);
        static const int8_t rays[][4] = {{14,2,14,5},{14,23,14,26},{2,14,5,14},{23,14,26,14},{5,5,7,7},{21,21,23,23},{5,23,7,21},{21,7,23,5}};
        for (unsigned i = 0; i < sizeof(rays) / sizeof(rays[0]); ++i)
            segment(draw, &area, color, rays[i][0], rays[i][1], rays[i][2], rays[i][3]);
    } else if (kind == 2) {
        box(draw, &area, color, 2,2,26,26,LV_RADIUS_CIRCLE,false);
        box(draw, &area, color, 8,9,11,12,LV_RADIUS_CIRCLE,true);
        box(draw, &area, color, 17,9,20,12,LV_RADIUS_CIRCLE,true);
        lv_draw_arc_dsc_t smile; lv_draw_arc_dsc_init(&smile);
        smile.color = color; smile.width = 2; smile.rounded = 1;
        const lv_point_t center = point(&area, 14, 14);
        lv_draw_arc(draw, &smile, &center, (uint16_t)(lv_area_get_width(&area) * 8 / 28), 20, 160);
    } else {
        box(draw, &area, color, 16,2,22,8,LV_RADIUS_CIRCLE,true);
        static const int8_t runner[][4] = {{8,8,13,10},{13,10,17,14},{17,14,24,12},{15,11,12,16},{12,16,8,21},{8,21,3,21},{12,16,18,20},{18,20,17,26}};
        for (unsigned i = 0; i < sizeof(runner) / sizeof(runner[0]); ++i)
            segment(draw, &area, color, runner[i][0], runner[i][1], runner[i][2], runner[i][3]);
    }
}
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, const char *value)
{
    lv_obj_t *object = lv_label_create(parent);
    lv_obj_set_style_text_font(object, font, 0);
    lv_obj_set_style_text_color(object, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_label_set_text(object, value);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}
static lv_obj_t *panel(lv_obj_t *parent, int width, int height)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(object, 16, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_90, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return object;
}
void home_info_create(lv_obj_t *page, lv_obj_t *clock_card, const lv_font_t *font)
{
    if (!font) font = &lv_font_montserrat_20;
    life_panel = panel(page, 256, 90);
    lv_obj_align(life_panel, LV_ALIGN_TOP_RIGHT, -14, 10);
    lv_obj_t *title = label(life_panel, font, "生活指数");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 3);
    for (int i = 0; i < 4; ++i) {
        const int x = 8 + (i % 2) * 120, y = 31 + (i / 2) * 27;
        lv_obj_t *icon = lv_obj_create(life_panel);
        lv_obj_remove_style_all(icon); lv_obj_set_size(icon, 24, 24); lv_obj_set_pos(icon, x, y);
        lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(icon, icon_draw, LV_EVENT_DRAW_MAIN, (void *)(intptr_t)i);
        life_values[i] = label(life_panel, font, "：--");
        lv_obj_set_pos(life_values[i], x + 27, y + 1); lv_obj_set_width(life_values[i], 88);
        lv_label_set_long_mode(life_values[i], LV_LABEL_LONG_DOT);
    }

    date_panel = panel(clock_card, 408, 142); lv_obj_set_pos(date_panel, 8, 214);
    lv_obj_set_style_pad_hor(date_panel, 8, 0); lv_obj_set_style_pad_ver(date_panel, 5, 0);
    lv_obj_set_flex_flow(date_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(date_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(date_panel, 4, 0);
    date_row = lv_obj_create(date_panel); lv_obj_remove_style_all(date_row);
    lv_obj_set_size(date_row, 388, 34);
    lv_obj_clear_flag(date_row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    gregorian_label = label(date_row, &lv_font_montserrat_28, "---- -- --");
    lv_obj_align(gregorian_label, LV_ALIGN_CENTER, 0, 0);
    weekday_label = label(date_row, font, "");
    lv_obj_align_to(weekday_label, gregorian_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lunar_label = label(date_panel, font, "农历待校时");
    term_label = label(date_panel, font, "");
    festival_label = label(date_panel, font, "");
    lv_obj_t *rows[] = {lunar_label, term_label, festival_label};
    for (unsigned i = 0; i < 3; ++i) {
        lv_obj_set_width(rows[i], 388); lv_label_set_long_mode(rows[i], LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(rows[i], LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_add_flag(term_label, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(festival_label, LV_OBJ_FLAG_HIDDEN);
}
static void optional(lv_obj_t *object, const char *prefix, const char *value)
{
    if (!value || !value[0]) { lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN); return; }
    char buffer[160]; snprintf(buffer, sizeof(buffer), "%s%s", prefix, value);
    text(object, buffer); lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}
void home_info_update(const struct tm *date)
{
    if (!date_panel) return;
    char buffer[160]; life_calendar_day_t day;
    if (date) {
        static const char *week[] = {"周日","周一","周二","周三","周四","周五","周六"};
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
        text(gregorian_label, buffer);
        text(weekday_label, date->tm_wday >= 0 && date->tm_wday < 7 ? week[date->tm_wday] : "");
    } else { text(gregorian_label, "---- -- --"); text(weekday_label, ""); }
    lv_obj_update_layout(date_row);
    lv_obj_align_to(weekday_label, gregorian_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    if (date && life_data_get_day(date->tm_year + 1900, date->tm_mon + 1, date->tm_mday, &day)) {
        snprintf(buffer, sizeof(buffer), "农历%s%s", day.lunar_month, day.lunar_day); text(lunar_label, buffer);
        optional(term_label, "节气 · ", day.solar_term);
        optional(festival_label, "节日 · ", day.festival);
    } else {
        text(lunar_label, date ? "当前日期超出农历范围" : "农历待校时");
        optional(term_label, "", ""); optional(festival_label, "", "");
    }
    life_indices_t indices = {0};
    const bool have = life_data_get_indices(&indices);
    const char *values[] = {indices.dressing, indices.uv, indices.flu, indices.sport};
    for (int i = 0; i < 4; ++i) {
        snprintf(buffer, sizeof(buffer), "：%s", have && values[i][0] ? values[i] : "--");
        text(life_values[i], buffer);
    }
}
