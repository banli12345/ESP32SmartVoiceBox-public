#include "WeatherPageUI.h"

#include "UITheme.h"
#include "WeatherIcons.h"
#include "setwifi.h"

#include <stdio.h>
#include <string.h>

extern lv_font_t *chinese_font_20;
extern lv_font_t *chinese_font_32;

static lv_obj_t *weather_city_button;
static lv_obj_t *weather_city_label;
static lv_obj_t *weather_icon;
static lv_obj_t *weather_condition;
static lv_obj_t *weather_temperature;
static lv_obj_t *weather_status;
static lv_obj_t *weather_row_text[WEATHER_FORECAST_DAYS];
static int weather_forecast_lines;
static lv_font_t city_font_with_fallback;

static const lv_font_t *font20(void)
{
    return chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_20;
}

static const lv_font_t *font32(void)
{
    if (!chinese_font_32) return &lv_font_montserrat_32;
    /* The SD city font contains Hanzi only. LONG_DOT also needs ASCII dots. */
    city_font_with_fallback = *chinese_font_32;
    city_font_with_fallback.fallback = &lv_font_montserrat_32;
    return &city_font_with_fallback;
}

static lv_coord_t advance(const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t glyph;
    if (!font->get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.adv_w <= 0)
        return lv_font_get_line_height(font);
    return glyph.adv_w;
}

static const char *shown_city(void)
{
    const char *name = chinese_font_32 ? WEATHER_CITY_DISPLAY : WEATHER_CITY;
    return name && *name ? name : "待更新";
}

static const lv_img_dsc_t *current_icon(void)
{
    int code = get_weather_code();
    return weather_icon_for_code(code < 0 ? weather_icon_legacy_code(weather) : code);
}

static void style_plate(lv_obj_t *image)
{
    lv_obj_set_style_bg_color(image, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(image, 16, 0);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE);
}

static void style_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_text_color(card, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
}

void weather_page_ui_clear(void)
{
    weather_city_button = NULL;
    weather_city_label = NULL;
    weather_icon = NULL;
    weather_condition = NULL;
    weather_temperature = NULL;
    weather_status = NULL;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) weather_row_text[i] = NULL;
    weather_forecast_lines = 0;
}

static void fill_forecast(int index)
{
    char text[160];
    if (index >= WEATHER_FORECAST_COUNT) {
        lv_label_set_text(weather_row_text[index], "待更新");
        return;
    }
    const char *date = WEATHER_FORECAST_DATE[index][0] ? WEATHER_FORECAST_DATE[index] : "--";
    const char *day = WEATHER_FORECAST_DAY_TEXT[index][0] ? WEATHER_FORECAST_DAY_TEXT[index] : "待更新";
    const char *night = WEATHER_FORECAST_NIGHT_TEXT[index][0] ? WEATHER_FORECAST_NIGHT_TEXT[index] : "待更新";
    const char *high = WEATHER_FORECAST_HIGH[index][0] ? WEATHER_FORECAST_HIGH[index] : "--";
    const char *low = WEATHER_FORECAST_LOW[index][0] ? WEATHER_FORECAST_LOW[index] : "--";
    if (weather_forecast_lines <= 2) {
        snprintf(text, sizeof(text), "%.11s  %.31s  高%.7s℃\n夜间 %.31s  低%.7s℃",
                 date, day, high, night, low);
    } else {
        snprintf(text, sizeof(text), "%.11s  %.31s\n夜间 %.31s\n高%.7s℃   低%.7s℃",
                 date, day, night, high, low);
    }
    lv_label_set_text(weather_row_text[index], text);
}

void weather_page_ui_refresh_status(void)
{
    if (!weather_status) return;
    const char *failure = get_weather_status_message();
    const char *shown;
    char buffer[96];
    bool problem = failure && *failure;
    if (problem) {
        shown = failure;
    } else if ((!weather || !*weather) && (!temp || !*temp)) {
        shown = "待更新";
    } else if (updateTime && *updateTime) {
        if (strlen(updateTime) >= 16 && updateTime[10] == 'T' && updateTime[13] == ':')
            snprintf(buffer, sizeof(buffer), "更新于 %.5s · 心知天气", updateTime + 11);
        else
            snprintf(buffer, sizeof(buffer), "更新于 %s", updateTime);
        shown = buffer;
    } else {
        shown = "数据来源：心知天气";
    }
    if (strcmp(lv_label_get_text(weather_status), shown) != 0)
        lv_label_set_text(weather_status, shown);
    lv_obj_set_style_text_color(weather_status,
                                lv_color_hex(problem ? UI_COLOR_ERROR : UI_COLOR_TEXT_MUTED), 0);
}

void weather_page_ui_refresh(void)
{
    if (weather_city_label) lv_label_set_text(weather_city_label, shown_city());
    if (weather_icon) lv_img_set_src(weather_icon, current_icon());
    if (weather_condition) {
        const char *condition = weather && *weather ? weather : "待更新";
        lv_label_set_text(weather_condition, condition);
    }
    if (weather_temperature) {
        char reading[24];
        if (temp && *temp) snprintf(reading, sizeof(reading), "%s°C", temp);
        else snprintf(reading, sizeof(reading), "%s", "--°C");
        lv_label_set_text(weather_temperature, reading);
    }
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i)
        if (weather_row_text[i]) fill_forecast(i);
    weather_page_ui_refresh_status();
}

void weather_page_ui_mount(lv_obj_t *page, lv_event_cb_t on_city)
{
    const lv_font_t *body = font20();
    const lv_font_t *city_font = font32();
    const lv_coord_t cjk = advance(body, 0x6674);
    const lv_coord_t digit = advance(body, '8');
    const lv_coord_t line = lv_font_get_line_height(body);
    const lv_coord_t city_line = lv_font_get_line_height(city_font);
    const lv_coord_t margin = 12;
    const lv_coord_t inner = 480 - margin * 2;
    /* Ten CJK glyphs, a date, and a signed two-digit temperature must fit. */
    const lv_coord_t phrase = cjk * 10 + 4;
    const lv_coord_t date_w = digit * 10 + cjk + 8;
    const lv_coord_t temp_w = cjk * 2 + digit * 4 + 8;
    const int lines = (date_w + phrase + temp_w + 16 <= inner) ? 2 : 3;
    const lv_coord_t row_h = lines * line + 14;
    const lv_coord_t status_h = line + 6;
    const lv_coord_t header_h = city_line + 12;
    lv_coord_t y = 8;

    weather_page_ui_clear();
    weather_forecast_lines = lines;
    ui_theme_apply_background(page);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC |
                            LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, chinese_font_20 ? "天气" : "Weather");
    lv_obj_set_style_text_font(title, body, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_pos(title, margin, y + (header_h - line) / 2);

    weather_city_button = lv_btn_create(page);
    lv_obj_set_size(weather_city_button, inner - 88, header_h);
    lv_obj_set_pos(weather_city_button, margin + 80, y);
    style_card(weather_city_button);
    lv_obj_set_style_pad_top(weather_city_button, 0, 0);
    lv_obj_set_style_pad_bottom(weather_city_button, 0, 0);
    lv_obj_set_style_bg_color(weather_city_button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_add_event_cb(weather_city_button, on_city, LV_EVENT_CLICKED, NULL);
    weather_city_label = lv_label_create(weather_city_button);
    lv_obj_set_size(weather_city_label, inner - 112, city_line);
    lv_label_set_long_mode(weather_city_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(weather_city_label, city_font, 0);
    lv_obj_set_style_text_color(weather_city_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(weather_city_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(weather_city_label, LV_ALIGN_CENTER, 0, 0);
    y += header_h + 8;

    lv_coord_t hero_h = 128;
    lv_coord_t rows_bottom = y + hero_h + 8 + line + 6 + (row_h + 6) * WEATHER_FORECAST_DAYS;
    /* Desktop page dots and FPS occupy the last 30 pixels above this page. */
    lv_coord_t limit = 480 - 32 - status_h - 6;
    if (rows_bottom > limit && hero_h > 104) {
        hero_h -= rows_bottom - limit;
        if (hero_h < 104) hero_h = 104;
    }

    lv_obj_t *hero = lv_obj_create(page);
    lv_obj_set_pos(hero, margin, y);
    lv_obj_set_size(hero, inner, hero_h);
    style_card(hero);

    weather_icon = lv_img_create(hero);
    style_plate(weather_icon);
    lv_obj_align(weather_icon, LV_ALIGN_LEFT_MID, 4, 0);

    weather_temperature = lv_label_create(hero);
    lv_obj_set_style_text_font(weather_temperature, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(weather_temperature, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(weather_temperature, LV_ALIGN_TOP_LEFT, 84, 8);

    weather_condition = lv_label_create(hero);
    lv_obj_set_width(weather_condition, inner - 116);
    lv_label_set_long_mode(weather_condition, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(weather_condition, body, 0);
    lv_obj_set_style_text_color(weather_condition, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(weather_condition, LV_ALIGN_BOTTOM_LEFT, 84, -8);
    y += hero_h + 8;

    lv_obj_t *caption = lv_label_create(page);
    lv_label_set_text(caption, chinese_font_20 ? "未来三天" : "Next 3 days");
    lv_obj_set_style_text_font(caption, body, 0);
    lv_obj_set_style_text_color(caption, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_pos(caption, margin + 4, y);
    y += line + 6;

    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_pos(row, margin, y);
        lv_obj_set_size(row, inner, row_h);
        style_card(row);
        weather_row_text[i] = lv_label_create(row);
        lv_obj_set_width(weather_row_text[i], inner - 20);
        lv_label_set_long_mode(weather_row_text[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(weather_row_text[i], body, 0);
        lv_obj_set_style_text_color(weather_row_text[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(weather_row_text[i], LV_ALIGN_LEFT_MID, 2, 0);
        y += row_h + 6;
    }

    weather_status = lv_label_create(page);
    lv_obj_set_size(weather_status, inner, status_h);
    lv_obj_align(weather_status, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_obj_set_style_text_font(weather_status, body, 0);
    lv_label_set_long_mode(weather_status, LV_LABEL_LONG_DOT);
    lv_label_set_text(weather_status, "");
    weather_page_ui_refresh();
}
