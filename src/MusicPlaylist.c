#include "UiTransition.h"
#include "MusicPlaylist.h"
#include "Sound.h"
#include "SDcar.h"
#include "UITheme.h"
#include "EdgeBack.h"
#include <stdio.h>
#include <string.h>

typedef struct { uint8_t index; uint32_t generation; } SongRow;
static lv_obj_t *screen, *player_screen, *list, *summary, *notice, *dialog;
static uint32_t rendered_revision;
static SongRow selected;
static bool deleting;
static const lv_font_t *font(void) {return chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_20;}
static void text_style(lv_obj_t *obj, uint32_t color) {
    lv_obj_set_style_text_font(obj, font(), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}
static void close_dialog(void) {if (dialog) {lv_obj_del(dialog); dialog = NULL;}}
static void back_cb(lv_event_t *event) {
    (void)event;
    if (dialog) close_dialog();
    else if (player_screen) ui_screen_back(player_screen);
}
static void cancel_cb(lv_event_t *event) {(void)event; close_dialog();}
static void confirm_cb(lv_event_t *event) {
    (void)event;
    if (music_delete_track(selected.index, selected.generation)) {
        deleting = true;
        lv_label_set_text(notice, "正在删除歌曲和同名歌词…");
        lv_obj_add_state(list, LV_STATE_DISABLED);
    } else lv_label_set_text(notice, "正在处理上一首，请稍后重试");
    close_dialog();
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, lv_event_cb_t cb, bool danger) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 164, 50); lv_obj_set_pos(b, x, 184);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(danger ? UI_COLOR_ERROR : UI_COLOR_ACCENT), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = lv_label_create(b); lv_label_set_text(label, text);
    text_style(label, UI_COLOR_ON_ACCENT); lv_obj_center(label);
    return b;
}
static void ask_delete(SongRow *song) {
    char path[64]; uint32_t generation;
    if (deleting || !sdcar_copy_music_track(song->index, path, sizeof(path), &generation) || generation != song->generation) return;
    selected = *song;
    close_dialog();
    dialog = lv_obj_create(screen);
    lv_obj_set_size(dialog, 480, 480); lv_obj_set_pos(dialog, 0, 0);
    lv_obj_set_style_radius(dialog, 0, 0); lv_obj_set_style_border_width(dialog, 0, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x182418), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_60, 0); lv_obj_set_style_pad_all(dialog, 0, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *box = lv_obj_create(dialog); lv_obj_set_size(box, 420, 280); lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_radius(box, 22, 0); lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(box); lv_label_set_text(title, "删除这首歌？"); text_style(title, UI_COLOR_TEXT);
    lv_obj_t *body = lv_label_create(box); lv_obj_set_width(body, 372); lv_obj_set_pos(body, 0, 40);
    char message[180];
    snprintf(message, sizeof(message), "%s\n\n删除 SD 卡中的歌曲文件\n和同名歌词（WAV / LRC）", path + 7);
    lv_label_set_text(body, message); text_style(body, UI_COLOR_TEXT_MUTED);
    button(box, "取消", 0, cancel_cb, false);
    button(box, "删除文件", 188, confirm_cb, true);
}
static void row_cb(lv_event_t *event) {
    SongRow *song = lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {lv_mem_free(song); return;}
    if (deleting || dialog) return;
    if (lv_event_get_code(event) == LV_EVENT_LONG_PRESSED) ask_delete(song);
    else if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
        if (music_select_library_track(song->index, song->generation)) {
            music_audio_play(); ui_screen_back(player_screen);
        } else lv_label_set_text(notice, "歌单已更新，请重新选择");
    }
}
static void refresh(void) {
    rendered_revision = sdcar_music_catalog_revision();
    lv_obj_clean(list);
    unsigned count = 0;
    for (unsigned i = 0; i < sdcar_get_music_track_count(); ++i) {
        char path[64]; uint32_t generation;
        if (!sdcar_copy_music_track((uint8_t)i, path, sizeof(path), &generation)) continue;
        SongRow *song = lv_mem_alloc(sizeof(*song));
        if (!song) break;
        *song = (SongRow){(uint8_t)i, generation}; ++count;
        lv_obj_t *row = lv_btn_create(list);
        lv_obj_set_width(row, LV_PCT(100)); lv_obj_set_height(row, 74);
        lv_obj_set_style_radius(row, 16, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_add_event_cb(row, row_cb, LV_EVENT_ALL, song);
        lv_obj_t *name = lv_label_create(row);
        char title[64]; snprintf(title, sizeof(title), "%s", path + 7);
        char *dot = strrchr(title, '.'); if (dot) *dot = 0;
        lv_label_set_text(name, title); text_style(name, UI_COLOR_TEXT);
        lv_obj_set_width(name, 360); lv_label_set_long_mode(name, LV_LABEL_LONG_WRAP);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *arrow = lv_label_create(row); lv_label_set_text(arrow, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_color(arrow, lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, 0, 0);
    }
    char text[80]; snprintf(text, sizeof(text), "共 %u 首，点击播放，长按删除", count);
    lv_label_set_text(summary, text);
    if (!count) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "还没有本地歌曲\n请通过随身音乐网页保存到 SD 卡");
        text_style(empty, UI_COLOR_TEXT_MUTED); lv_obj_set_width(empty, 416);
    }
}
static void poll(lv_timer_t *timer) {
    (void)timer;
    if (deleting && music_delete_status() != 1) {
        deleting = false; lv_obj_clear_state(list, LV_STATE_DISABLED);
        lv_label_set_text(notice, music_delete_status() == 2 ? "已删除歌曲及同名歌词" : "删除失败：请等待保存完成或检查 SD 卡后重试");
    }
    if (lv_scr_act() == screen && !dialog && !deleting && rendered_revision != sdcar_music_catalog_revision()) refresh();
}
void music_playlist_open(lv_obj_t *player) {
    player_screen = player;
    if (!screen) {
        screen = lv_obj_create(NULL); ui_theme_apply_background(screen);
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        edge_back_bind(screen, back_cb, NULL);
        lv_obj_t *title = lv_label_create(screen); lv_label_set_text(title, "所有歌单");
        text_style(title, UI_COLOR_TEXT); lv_obj_set_pos(title, 24, 18);
        summary = lv_label_create(screen); text_style(summary, UI_COLOR_TEXT_MUTED); lv_obj_set_pos(summary, 24, 54);
        list = lv_obj_create(screen); lv_obj_set_pos(list, 16, 92); lv_obj_set_size(list, 448, 342);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_pad_all(list, 4, 0); lv_obj_set_style_pad_row(list, 10, 0);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN); lv_obj_set_scroll_dir(list, LV_DIR_VER);
        notice = lv_label_create(screen); lv_obj_set_width(notice, 432); lv_obj_set_pos(notice, 24, 442);
        text_style(notice, UI_COLOR_TEXT_MUTED); lv_label_set_long_mode(notice, LV_LABEL_LONG_DOT);
        lv_label_set_text(notice, "侧边内拉可返回音乐页");
        lv_timer_create(poll, 250, NULL);
    }
    refresh(); ui_screen_open(screen);
}
