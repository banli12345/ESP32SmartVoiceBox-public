#include "EdgeBack.h"
#include "UITheme.h"
#include <stdlib.h>

typedef struct { lv_obj_t *screen; lv_event_cb_t callback; void *data; } back_binding_t;
static back_binding_t bindings[40];
static lv_event_code_t back_event;
static lv_obj_t *bubble, *arrow, *source, *pending;
static lv_timer_t *dispatch_timer;
static bool was_down, captured, cancelled, right_edge;
static int origin_x, origin_y, distance;

static back_binding_t *binding(lv_obj_t *screen)
{
    if (!screen) return NULL;
    for (unsigned i = 0; i < 40; ++i) if (bindings[i].screen == screen) return &bindings[i];
    return NULL;
}
static void screen_deleted(lv_event_t *event)
{
    back_binding_t *entry = lv_event_get_user_data(event);
    if (lv_event_get_target(event) != entry->screen) return;
    if (source == entry->screen) {source = NULL; cancelled = true;}
    if (pending == entry->screen) pending = NULL;
    entry->screen = NULL; entry->callback = NULL; entry->data = NULL;
}
void edge_back_bind(lv_obj_t *screen, lv_event_cb_t callback, void *user_data)
{
    if (!screen || !callback) return;
    if (!back_event) back_event = (lv_event_code_t)lv_event_register_id();
    back_binding_t *entry = binding(screen);
    if (entry) {
        if (entry->callback == callback && entry->data == user_data) return;
        lv_obj_remove_event_cb_with_user_data(screen, entry->callback, entry->data);
    } else {
        for (unsigned i = 0; i < 40; ++i) if (!bindings[i].screen) {entry = &bindings[i]; break;}
        if (!entry) return;
        entry->screen = screen;
        lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, entry);
    }
    entry->callback = callback; entry->data = user_data;
    lv_obj_add_event_cb(screen, callback, back_event, user_data);
}
static lv_obj_t *find_modal(lv_obj_t *parent, unsigned depth)
{
    if (!depth) return NULL;
    for (uint32_t i = lv_obj_get_child_cnt(parent); i > 0; --i) {
        lv_obj_t *child = lv_obj_get_child(parent, (int32_t)i - 1);
        if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_check_type(child, &lv_msgbox_class)) return child;
        lv_obj_t *nested = find_modal(child, depth - 1);
        if (nested) return nested;
    }
    return NULL;
}
static void dispatch_back(lv_timer_t *timer)
{
    lv_timer_pause(timer);
    lv_obj_t *screen = pending; pending = NULL;
    if (!screen || !binding(screen) || lv_scr_act() != screen) return;
    lv_obj_t *modal = find_modal(lv_layer_top(), 3);
    if (modal) {lv_msgbox_close_async(modal); return;}
    /* Screen changes and media cleanup run after the input read has finished. */
    lv_event_send(screen, back_event, NULL);
}
void edge_back_init(void)
{
    if (bubble) return;
    bubble = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bubble, 64, 80);
    lv_obj_set_style_pad_all(bubble, 0, 0);
    lv_obj_set_style_radius(bubble, 32, 0);
    lv_obj_set_style_bg_color(bubble, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_90, 0);
    lv_obj_set_style_border_width(bubble, 0, 0);
    lv_obj_set_style_shadow_width(bubble, 0, 0);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    arrow = lv_label_create(bubble);
    lv_label_set_text(arrow, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
    lv_obj_center(arrow);
    lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
    dispatch_timer = lv_timer_create(dispatch_back, 1, NULL);
    if (dispatch_timer) lv_timer_pause(dispatch_timer);
}
bool edge_back_is_active(void) { return captured || pending != NULL; }

bool edge_back_pointer(bool pressed, int x, int y, bool enabled)
{
    if (!bubble || !dispatch_timer) return false;
    if (pressed && !was_down) {
        captured = enabled && binding(lv_scr_act()) && (x <= 14 || x >= 465) && y >= 44 && y <= 436;
        cancelled = false; source = captured ? lv_scr_act() : NULL;
        origin_x = x; origin_y = y; distance = 0; right_edge = x >= 465;
    }
    const bool consumed = captured;
    if (captured) {
        const int dx = right_edge ? origin_x - x : x - origin_x;
        const int dy = abs(y - origin_y);
        if (!enabled || source != lv_scr_act() || !source || (dy > 28 && dy > dx)) cancelled = true;
        distance = dx > 0 ? dx : 0;
        if (pressed && !cancelled && distance > 4) {
            const int travel = distance > 84 ? 84 : distance;
            int top = origin_y - 40;
            if (top < 44) top = 44;
            if (top > 356) top = 356;
            lv_obj_set_pos(bubble, right_edge ? 472 - travel / 2 : -56 + travel / 2, top);
            lv_label_set_text(arrow, right_edge ? LV_SYMBOL_RIGHT : LV_SYMBOL_LEFT);
            lv_obj_set_style_bg_color(bubble, lv_color_hex(distance >= 64 ? UI_COLOR_ACCENT_DARK : UI_COLOR_ACCENT), 0);
            lv_obj_clear_flag(bubble, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(bubble);
        } else lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
        if (!pressed) {
            if (!cancelled && distance >= 64 && dy <= 56 && binding(source)) {
                pending = source;
                lv_timer_reset(dispatch_timer); lv_timer_resume(dispatch_timer);
            }
            captured = false; source = NULL;
            lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
        }
    }
    was_down = pressed;
    return consumed;
}
