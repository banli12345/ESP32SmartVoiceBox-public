#include "FlipClock.h"
#include "UITheme.h"
#include <string.h>
LV_FONT_DECLARE(lv_font_flip_digits_72);

typedef struct {
    lv_obj_t *card, *upper, *lower, *fold_top, *fold_bottom;
    lv_obj_t *labels[4];
    int16_t width, height, text_y;
    int8_t shown, next;
} flip_digit_t;
struct flip_clock {lv_obj_t *root; flip_digit_t digits[6];};

static void flat(lv_obj_t *object)
{
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}
static void label_value(lv_obj_t *label, int value)
{
    char text[2] = {value < 0 ? '-' : (char)('0' + value), 0};
    lv_label_set_text(label, text);
}
static void settled(flip_digit_t *digit, int value)
{
    digit->shown = digit->next = value;
    label_value(digit->labels[0], value); label_value(digit->labels[1], value);
    lv_obj_add_flag(digit->fold_top, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(digit->fold_bottom, LV_OBJ_FLAG_HIDDEN);
}
static void flip_step(void *context, int32_t progress)
{
    flip_digit_t *digit = context;
    const int half = digit->height / 2;
    /* Old upper flap folds down to the middle hinge. The new lower flap then
     * unfolds from that hinge. Clip each half independently: no image buffers
     * or full-clock crossfade, and only the changing digit is invalidated. */
    if (progress < 500) {
        int height = half * (500 - progress) / 500;
        if (height < 1) height = 1;
        const int top = half - height;
        lv_obj_set_pos(digit->fold_top, 0, top);
        lv_obj_set_height(digit->fold_top, height);
        lv_obj_set_y(digit->labels[2], digit->text_y - top);
        lv_obj_set_style_bg_color(digit->fold_top, lv_color_mix(lv_color_hex(UI_COLOR_PANEL),
            lv_color_hex(UI_COLOR_BG), (lv_opa_t)(255 - progress / 3)), 0);
        lv_obj_clear_flag(digit->fold_top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(digit->fold_bottom, LV_OBJ_FLAG_HIDDEN);
    } else {
        int height = half * (progress - 500) / 500;
        if (height < 1) height = 1;
        lv_obj_add_flag(digit->fold_top, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(digit->fold_bottom, height);
        lv_obj_set_style_bg_color(digit->fold_bottom, lv_color_mix(lv_color_hex(UI_COLOR_SURFACE),
            lv_color_hex(UI_COLOR_PANEL), (lv_opa_t)((progress - 500) * 255 / 500)), 0);
        lv_obj_clear_flag(digit->fold_bottom, LV_OBJ_FLAG_HIDDEN);
    }
}
static void flip_done(lv_anim_t *animation)
{
    flip_digit_t *digit = animation->var;
    settled(digit, digit->next);
}
static void deleted(lv_event_t *event)
{
    flip_clock_t *clock = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != clock->root) return;
    for (unsigned i = 0; i < 6; ++i) lv_anim_del(&clock->digits[i], flip_step);
    lv_mem_free(clock);
}
static lv_obj_t *half_panel(flip_digit_t *digit, int y, uint32_t color)
{
    lv_obj_t *panel = lv_obj_create(digit->card); flat(panel);
    lv_obj_set_pos(panel, 0, y);
    lv_obj_set_size(panel, digit->width, digit->height / 2);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    return panel;
}
static lv_obj_t *digit_label(lv_obj_t *panel, const lv_font_t *font, int width, int y)
{
    lv_obj_t *label = lv_label_create(panel); flat(label);
    lv_obj_set_size(label, width, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 0, y);
    return label;
}
static void create_digit(flip_clock_t *clock, unsigned index, int x, int y, int width, int height)
{
    flip_digit_t *digit = &clock->digits[index];
    digit->width = width; digit->height = height;
    const lv_font_t *font = index < 4 ? &lv_font_flip_digits_72 : &lv_font_montserrat_32;
    digit->text_y = (height - lv_font_get_line_height(font)) / 2;
    digit->card = lv_obj_create(clock->root); flat(digit->card);
    lv_obj_set_pos(digit->card, x, y); lv_obj_set_size(digit->card, width, height);
    lv_obj_set_style_radius(digit->card, index < 4 ? 12 : 9, 0);
    lv_obj_set_style_clip_corner(digit->card, true, 0);
    lv_obj_set_style_bg_color(digit->card, lv_color_hex(UI_COLOR_SURFACE), 0);
    digit->upper = half_panel(digit, 0, UI_COLOR_PANEL);
    digit->lower = half_panel(digit, height / 2, UI_COLOR_SURFACE);
    digit->fold_top = half_panel(digit, 0, UI_COLOR_PANEL);
    digit->fold_bottom = half_panel(digit, height / 2, UI_COLOR_SURFACE);
    digit->labels[0] = digit_label(digit->upper, font, width, digit->text_y);
    digit->labels[1] = digit_label(digit->lower, font, width, digit->text_y - height / 2);
    digit->labels[2] = digit_label(digit->fold_top, font, width, digit->text_y);
    digit->labels[3] = digit_label(digit->fold_bottom, font, width, digit->text_y - height / 2);
    lv_obj_t *hinge = lv_obj_create(digit->card); flat(hinge);
    lv_obj_set_pos(hinge, 0, height / 2 - 1); lv_obj_set_size(hinge, width, 2);
    lv_obj_set_style_radius(hinge, 0, 0);
    lv_obj_set_style_bg_color(hinge, lv_color_hex(UI_COLOR_BORDER), 0);
    for (int side = 0; side < 2; ++side) {
        lv_obj_t *pin = lv_obj_create(digit->card); flat(pin);
        lv_obj_set_size(pin, 3, 10); lv_obj_set_pos(pin, side ? width - 3 : 0, height / 2 - 5);
        lv_obj_set_style_radius(pin, 1, 0);
        lv_obj_set_style_bg_color(pin, lv_color_hex(UI_COLOR_ACCENT), 0);
    }
    settled(digit, -1);
}
flip_clock_t *flip_clock_create(lv_obj_t *parent)
{
    flip_clock_t *clock = lv_mem_alloc(sizeof(*clock));
    if (!clock) return NULL;
    memset(clock, 0, sizeof(*clock));
    clock->root = lv_obj_create(parent); flat(clock->root);
    lv_obj_set_size(clock->root, 424, 112);
    lv_obj_set_style_bg_opa(clock->root, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(clock->root, deleted, LV_EVENT_DELETE, clock);
    const int x[6] = {0, 74, 168, 242, 326, 370};
    for (unsigned i = 0; i < 6; ++i) create_digit(clock, i, x[i], i < 4 ? 2 : 42, i < 4 ? 68 : 38, i < 4 ? 108 : 68);
    lv_obj_t *colon = lv_label_create(clock->root); flat(colon);
    lv_label_set_text(colon, ":"); lv_obj_set_style_text_font(colon, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(colon, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_pos(colon, 149, 32);
    return clock;
}
lv_obj_t *flip_clock_object(flip_clock_t *clock) {return clock ? clock->root : NULL;}
void flip_clock_set_time(flip_clock_t *clock, int hour, int minute, int second, bool animate)
{
    if (!clock) return;
    if (hour < 0 || hour > 23) hour = -1;
    if (minute < 0 || minute > 59) minute = -1;
    if (second < 0 || second > 59) second = -1;
    const int values[6] = {hour < 0 ? -1 : hour / 10, hour < 0 ? -1 : hour % 10,
        minute < 0 ? -1 : minute / 10, minute < 0 ? -1 : minute % 10,
        second < 0 ? -1 : second / 10, second < 0 ? -1 : second % 10};
    for (unsigned i = 0; i < 6; ++i) {
        flip_digit_t *digit = &clock->digits[i];
        if (values[i] == digit->next) continue;
        lv_anim_del(digit, flip_step);
        settled(digit, digit->next);
        if (!animate || !lv_obj_is_visible(clock->root) || digit->shown < 0 || values[i] < 0) {
            settled(digit, values[i]); continue;
        }
        digit->next = values[i];
        label_value(digit->labels[0], digit->next);
        label_value(digit->labels[2], digit->shown);
        label_value(digit->labels[3], digit->next);
        lv_anim_t animation; lv_anim_init(&animation);
        lv_anim_set_var(&animation, digit); lv_anim_set_exec_cb(&animation, flip_step);
        lv_anim_set_values(&animation, 0, 1000); lv_anim_set_time(&animation, 320);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_set_ready_cb(&animation, flip_done); lv_anim_start(&animation);
    }
}
