#include "AppLauncher.h"
#include "UITheme.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

enum { MAX_APPS = 12, CARD_W = 102, CARD_H = 104, STEP_X = 114, STEP_Y = 114, GRID_X = 18, GRID_Y = 88 };
static lv_obj_t *root, *hint, *target;
static lv_obj_t *buttons[MAX_APPS];
static app_launcher_item_t apps[MAX_APPS];
static uint8_t order[MAX_APPS], original[MAX_APPS];
static unsigned app_count;
static int dragging = -1, press_x, press_y, start_x, start_y;
static bool block_click, chinese_labels;

static const char *tr(const char *zh, const char *en) { return chinese_labels ? zh : en; }
static void flat(lv_obj_t *object)
{
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}
static void load_order(void)
{
    for (unsigned i = 0; i < app_count; ++i) order[i] = i;
    uint8_t saved[MAX_APPS + 2]; size_t length = sizeof(saved);
    nvs_handle_t handle;
    if (nvs_open("app_layout", NVS_READONLY, &handle) != ESP_OK) return;
    const esp_err_t error = nvs_get_blob(handle, "order", saved, &length); nvs_close(handle);
    if (error != ESP_OK || length < 2 || saved[1] > MAX_APPS || length != saved[1] + 2 || (saved[0] != 1 && saved[0] != 2)) return;
    /* Reject corrupt blobs before touching order. Version 1 stored positional
     * ids; these equal the original app identities (0..11). Version 2 stores
     * persist_id, so merging/adding apps no longer resets the other icons. */
    for (unsigned i = 0; i < saved[1]; ++i) {
        if (saved[0] == 1 && saved[i + 2] >= saved[1]) return;
        for (unsigned j = 0; j < i; ++j) if (saved[j + 2] == saved[i + 2]) return;
    }
    uint8_t result[MAX_APPS]; unsigned used = 0; uint16_t seen = 0;
    for (unsigned i = 0; i < saved[1]; ++i) {
        unsigned id = saved[i + 2];
        if (saved[0] == 1 && id == APP_ID_OLD_BALANCE) id = APP_ID_GAMES;
        for (unsigned j = 0; j < app_count; ++j) {
            if (apps[j].persist_id == id && !(seen & (1U << j))) {
                result[used++] = j; seen |= 1U << j; break;
            }
        }
    }
    for (unsigned j = 0; j < app_count; ++j) if (!(seen & (1U << j))) result[used++] = j;
    memcpy(order, result, app_count);
}
static bool save_order(void)
{
    uint8_t saved[MAX_APPS + 2] = {2, (uint8_t)app_count};
    for (unsigned i = 0; i < app_count; ++i) saved[i + 2] = apps[order[i]].persist_id;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("app_layout", NVS_READWRITE, &handle);
    if (error == ESP_OK) {
        error = nvs_set_blob(handle, "order", saved, app_count + 2);
        if (error == ESP_OK) error = nvs_commit(handle);
        nvs_close(handle);
    }
    return error == ESP_OK;
}
static void set_x(void *object, int32_t value) {lv_obj_set_x(object, value);}
static void set_y(void *object, int32_t value) {lv_obj_set_y(object, value);}
static void move_axis(lv_obj_t *object, lv_anim_exec_xcb_t setter, int from, int to)
{
    lv_anim_del(object, setter);
    if (from == to) return;
    lv_anim_t animation; lv_anim_init(&animation);
    lv_anim_set_var(&animation, object); lv_anim_set_exec_cb(&animation, setter);
    lv_anim_set_values(&animation, from, to); lv_anim_set_time(&animation, 130);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out); lv_anim_start(&animation);
}
static void layout(bool animate)
{
    for (unsigned slot = 0; slot < app_count; ++slot) {
        const unsigned id = order[slot];
        if ((int)id == dragging) continue;
        const int x = GRID_X + (slot % 4) * STEP_X;
        const int y = GRID_Y + (slot / 4) * STEP_Y;
        if (animate) {
            move_axis(buttons[id], set_x, lv_obj_get_x(buttons[id]), x);
            move_axis(buttons[id], set_y, lv_obj_get_y(buttons[id]), y);
        } else {
            lv_anim_del(buttons[id], set_x); lv_anim_del(buttons[id], set_y);
            lv_obj_set_pos(buttons[id], x, y);
        }
    }
}
bool app_launcher_is_dragging(void) {return dragging >= 0;}
void app_launcher_cancel_drag(void)
{
    if (dragging < 0 || !root) return;
    lv_obj_set_style_border_width(buttons[dragging], 1, 0);
    lv_obj_set_style_border_color(buttons[dragging], lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_bg_color(buttons[dragging], lv_color_hex(UI_COLOR_SURFACE), 0);
    memcpy(order, original, app_count); dragging = -1; block_click = true;
    lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN); layout(false);
    lv_label_set_text(hint, tr("长按图标拖动排序", "Hold an icon to move it"));
}
static void deleted(lv_event_t *event)
{
    if (lv_event_get_target(event) != root) return;
    root = hint = target = NULL; app_count = 0; dragging = -1; block_click = true;
    memset(buttons, 0, sizeof(buttons));
}
static void icon_event(lv_event_t *event)
{
    const unsigned id = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    const lv_event_code_t code = lv_event_get_code(event);
    if (!root || id >= app_count) return;
    lv_indev_t *input = lv_indev_get_act();
    lv_point_t point = {0}; if (input) lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        block_click = false; press_x = point.x; press_y = point.y;
        start_x = lv_obj_get_x(buttons[id]); start_y = lv_obj_get_y(buttons[id]);
    } else if (code == LV_EVENT_LONG_PRESSED && !block_click && input) {
        dragging = (int)id; block_click = true;
        memcpy(original, order, app_count);
        lv_obj_move_foreground(buttons[id]);
        lv_obj_set_style_border_width(buttons[id], 2, 0);
        lv_obj_set_style_border_color(buttons[id], lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(buttons[id], lv_color_hex(UI_COLOR_PRESSED), 0);
        lv_obj_set_pos(target, start_x, start_y);
        lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(hint, tr("拖到新位置后松手", "Drag to a new position, then release"));
    } else if (code == LV_EVENT_PRESSING) {
        const int dx = point.x - press_x, dy = point.y - press_y;
        if (dragging < 0) {if (abs(dx) > 12 || abs(dy) > 12) block_click = true; return;}
        int x = start_x + dx, y = start_y + dy;
        if (x < 4) x = 4;
        if (x > 474 - CARD_W) x = 474 - CARD_W;
        if (y < 74) y = 74;
        if (y > 434 - CARD_H) y = 434 - CARD_H;
        lv_obj_set_pos(buttons[id], x, y);
        int col = (x - GRID_X + STEP_X / 2) / STEP_X;
        int row = (y - GRID_Y + STEP_Y / 2) / STEP_Y;
        if (col < 0) col = 0;
        if (col > 3) col = 3;
        if (row < 0) row = 0;
        if (row > 2) row = 2;
        const unsigned count = app_count;
        if (!count || count > MAX_APPS || id >= count) return;
        unsigned slot = row * 4 + col;
        if (slot >= count) slot = count - 1;
        unsigned old = 0; while (old < count && order[old] != id) ++old;
        if (old == count) { app_launcher_cancel_drag(); return; }
        if (old < slot) memmove(order + old, order + old + 1, slot - old);
        else if (old > slot) memmove(order + slot + 1, order + slot, old - slot);
        order[slot] = id;
        if (old != slot) layout(true);
        lv_obj_set_pos(target, GRID_X + (slot % 4) * STEP_X, GRID_Y + (slot / 4) * STEP_Y);
    } else if (code == LV_EVENT_RELEASED && dragging >= 0) {
        lv_obj_set_style_border_width(buttons[id], 1, 0);
        lv_obj_set_style_border_color(buttons[id], lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_bg_color(buttons[id], lv_color_hex(UI_COLOR_SURFACE), 0);
        dragging = -1; layout(false); lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
        const bool changed = memcmp(order, original, app_count) != 0;
        const bool saved = !changed || save_order();
        lv_label_set_text(hint, saved ? tr("长按图标拖动排序", "Hold an icon to move it") :
            tr("顺序保存失败，请重试", "Could not save order. Try again"));
    } else if (code == LV_EVENT_PRESS_LOST) app_launcher_cancel_drag();
    else if (code == LV_EVENT_CLICKED && !block_click && apps[id].open) {
        /* Opening weather may delete this entire grid. Do not access it again. */
        apps[id].open(event);
    }
}
void app_launcher_create(lv_obj_t *page, const lv_font_t *font, bool chinese,
                         const app_launcher_item_t *items, unsigned count)
{
    if (!page || !items || !count || count > MAX_APPS || root) return;
    for (unsigned i = 0; i < count; ++i)
        for (unsigned j = 0; j < i; ++j) if (items[i].persist_id == items[j].persist_id) return;
    chinese_labels = chinese; app_count = count;
    memcpy(apps, items, count * sizeof(*items)); load_order();
    root = lv_obj_create(page); flat(root);
    lv_obj_set_pos(root, 0, 0); lv_obj_set_size(root, 480, 480);
    lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_add_event_cb(root, deleted, LV_EVENT_DELETE, NULL);
    lv_obj_t *title = lv_label_create(root);
    lv_label_set_text(title, tr("应用", "Apps")); lv_obj_set_style_text_font(title, font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);
    hint = lv_label_create(root);
    lv_label_set_text(hint, tr("长按图标拖动排序", "Hold an icon to move it"));
    lv_obj_set_style_text_font(hint, chinese ? font : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 44);
    target = lv_obj_create(root); flat(target);
    lv_obj_set_size(target, CARD_W, CARD_H);
    lv_obj_set_style_radius(target, 20, 0);
    lv_obj_set_style_bg_color(target, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(target, 2, 0);
    lv_obj_set_style_border_color(target, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_clear_flag(target, LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
    for (unsigned id = 0; id < count; ++id) {
        lv_obj_t *card = buttons[id] = lv_btn_create(root); flat(card);
        lv_obj_add_flag(card, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_style_radius(card, 20, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_add_event_cb(card, icon_event, LV_EVENT_ALL, (void *)(uintptr_t)id);
        lv_obj_t *icon = lv_img_create(card); lv_img_set_src(icon, apps[id].image);
        lv_img_header_t header;
        if (lv_img_decoder_get_info(apps[id].image, &header) == LV_RES_OK && header.w && header.h) {
            unsigned largest = header.w > header.h ? header.w : header.h;
            lv_img_set_zoom(icon, (uint16_t)(60 * 256 / largest));
        }
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 7);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *label = lv_label_create(card); lv_label_set_text(label, apps[id].name);
        lv_obj_set_width(label, CARD_W - 8); lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
    layout(false);
}
