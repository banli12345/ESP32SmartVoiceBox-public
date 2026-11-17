#include "OwnMusicUI.h"
#include "OnlineMusic.h"
#include "Sound.h"
#include "SDcar.h"
#include "MusicPlaylist.h"
#include "UiTransition.h"
#include "UITheme.h"
#include "EdgeBack.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

extern bool get_wifi_connected(void);
#define MUSIC_RESULTS 5
typedef struct {
    lv_obj_t *screen, *return_screen, *query, *search, *download, *now, *phase;
    lv_obj_t *list, *reply_label, *saved, *keyboard, *ime, *candidates, *details, *details_body, *editor_hint;
    lv_obj_t *controls[4];
    lv_timer_t *timer;
    online_music_song_t songs[MUSIC_RESULTS], snapshot[MUSIC_RESULTS];
    unsigned count;
    uint32_t revision;
    bool editing, force_list;
    char reply[1536], saved_path[64];
} own_music_page_t;
static own_music_page_t *music_page;
static void poll(lv_timer_t *timer);
static void edit_query(bool visible);

static const lv_font_t *font(void) { return chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_20; }
/* The 16px weather subset does not contain arbitrary song/status characters. */
static const lv_font_t *small_font(void) { return chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_16; }
static void text_style(lv_obj_t *obj, bool small)
{
    lv_obj_set_style_text_font(obj, small ? small_font() : font(), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(UI_COLOR_TEXT), 0);
}
static void set_text(lv_obj_t *obj, const char *value)
{
    if (strcmp(lv_label_get_text(obj), value)) lv_label_set_text(obj, value);
}
static void enable(lv_obj_t *obj, bool value)
{
    if (value) lv_obj_clear_state(obj, LV_STATE_DISABLED);
    else lv_obj_add_state(obj, LV_STATE_DISABLED);
}
static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const char *text, bool small)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, width); text_style(obj, small);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT); lv_label_set_text(obj, text);
    return obj;
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, const char *text,
                        lv_event_cb_t callback, void *data)
{
    lv_obj_t *obj = lv_btn_create(parent);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, width, 40);
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(UI_COLOR_DISABLED), LV_STATE_DISABLED);
    lv_obj_set_style_shadow_width(obj, 0, 0); lv_obj_set_style_radius(obj, 12, 0);
    lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *text_obj = lv_label_create(obj); text_style(text_obj, false);
    lv_label_set_text(text_obj, text); lv_obj_center(text_obj);
    return obj;
}
static void notice(const char *message)
{
    if (!music_page) return;
    snprintf(music_page->reply, sizeof(music_page->reply), "%s", message);
    set_text(music_page->reply_label, message);
    if (music_page->editing) set_text(music_page->editor_hint, message);
    if (music_page->details_body) set_text(music_page->details_body, message);
}
static bool online_ready(bool playback)
{
    if (online_music_is_preparing()) { notice("正在准备或保存，可点停止后重试"); return false; }
    if (!get_wifi_connected()) { notice("请先在设置中连接可上网的 WiFi"); return false; }
    if (playback && !sdcar_is_ready()) { notice("播放和保存需要 SD 卡，请检查存储卡"); return false; }
    return true;
}
static void search_clicked(lv_event_t *event)
{
    (void)event;
    if (!music_page || !online_ready(false)) return;
    const char *query = lv_textarea_get_text(music_page->query);
    while (isspace((unsigned char)*query)) ++query;
    size_t length = strlen(query);
    while (length && isspace((unsigned char)query[length - 1])) --length;
    if (!length) { notice("请输入歌名或歌手"); return; }
    char command[256];
    int n = snprintf(command, sizeof(command), "搜歌 %.*s", (int)length, query);
    if (n < 0 || (size_t)n >= sizeof(command)) { notice("搜索文字太长，请缩短后重试"); return; }
    if (online_music_handle_web_command(command, false)) {
        edit_query(false); poll(NULL);
    } else notice("搜索没有提交，请稍后重试");
}
static void song_clicked(lv_event_t *event)
{
    if (!music_page || !online_ready(true)) return;
    unsigned index = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned count = online_music_copy_songs(music_page->snapshot, MUSIC_RESULTS);
    if (index >= music_page->count || index >= count ||
        memcmp(&music_page->snapshot[index], &music_page->songs[index], sizeof(online_music_song_t))) {
        music_page->force_list = true; notice("搜索结果已更新，请重新选择"); return;
    }
    char command[48]; snprintf(command, sizeof(command), "播放第%u首", index + 1);
    if (!online_music_handle_web_command(command, false))
        notice("播放没有提交，请稍后重试");
    else poll(NULL);
}
static void render_songs(unsigned count)
{
    memcpy(music_page->songs, music_page->snapshot, count * sizeof(online_music_song_t));
    music_page->count = count; music_page->force_list = false;
    lv_obj_clean(music_page->list);
    for (unsigned i = 0; i < count; ++i) {
        lv_obj_t *row = button(music_page->list, 0, 0, 432, "", song_clicked, (void *)(uintptr_t)i);
        lv_obj_clean(row); lv_obj_set_height(row, 66); lv_obj_set_style_pad_all(row, 7, 0);
        label(row, 0, 0, 358, music_page->songs[i].title, false);
        label(row, 0, 29, 358, music_page->songs[i].artist, true);
        lv_obj_t *play = label(row, 384, 13, 28, LV_SYMBOL_PLAY, false);
        lv_obj_set_style_text_font(play, &lv_font_montserrat_20, 0);
    }
    if (!count) label(music_page->list, 6, 12, 416, "输入歌名或歌手，搜索后点击歌曲播放", true);
}
static void download_clicked(lv_event_t *event)
{
    (void)event;
    if (!music_page) return;
    if (!online_music_can_download_current() || !online_music_download_current())
        notice("没有可保存的当前在线歌曲");
    else poll(NULL);
}
static void controls_clicked(lv_event_t *event)
{
    if (!music_page) return;
    const unsigned action = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (action == 0) music_audio_pause();
    else if (action == 1) music_audio_play();
    else if (action == 2) { music_audio_stop(); notice("已请求停止，未完成的保存将取消"); }
    else {
        if (!sdcar_is_ready()) { notice("请先插入 SD 卡"); return; }
        if (online_music_is_preparing()) { notice("请等待当前保存完成"); return; }
        char expected[64], actual[64]; uint32_t generation = 0;
        int index = online_music_saved_track(expected, sizeof(expected));
        if (index < 0 || !sdcar_copy_music_track((uint8_t)index, actual, sizeof(actual), &generation) ||
            strcmp(expected, actual) || !music_select_library_track((uint8_t)index, generation)) {
            notice("没有可播放的已保存歌曲，可进入本地歌单"); return;
        }
        music_audio_play();
    }
    poll(NULL);
}
static void close_details(lv_event_t *event)
{
    (void)event;
    if (music_page && music_page->details) {
        lv_obj_del(music_page->details); music_page->details = NULL; music_page->details_body = NULL;
    }
}
static void show_details(lv_event_t *event)
{
    (void)event;
    if (!music_page || music_page->details) return;
    lv_obj_t *overlay = music_page->details = lv_obj_create(music_page->screen);
    lv_obj_set_pos(overlay, 0, 0); lv_obj_set_size(overlay, 480, 480);
    lv_obj_set_style_pad_all(overlay, 16, 0); lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    label(overlay, 0, 0, 300, "播放与保存状态", false);
    lv_obj_t *scroll = lv_obj_create(overlay); lv_obj_set_pos(scroll, 0, 42); lv_obj_set_size(scroll, 440, 338);
    lv_obj_set_style_pad_all(scroll, 8, 0); lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_t *body = music_page->details_body = label(scroll, 0, 0, 400, music_page->reply, false);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    button(overlay, 138, 394, 164, "关闭", close_details, NULL);
}
static void playlist_clicked(lv_event_t *event)
{
    (void)event;
    if (music_page) { edit_query(false); music_playlist_open(music_page->screen); }
}
static void edit_query(bool visible)
{
    if (!music_page) return;
    music_page->editing = visible;
    lv_obj_set_y(music_page->query, visible ? 198 : 58);
    lv_obj_set_y(music_page->search, visible ? 198 : 58);
    lv_obj_t *hidden[] = {music_page->download, music_page->now, music_page->phase, music_page->list, music_page->reply_label,
                          music_page->controls[0],music_page->controls[1],music_page->controls[2],music_page->controls[3]};
    for (unsigned i = 0; i < sizeof(hidden)/sizeof(*hidden); ++i) {
        if (visible) lv_obj_add_flag(hidden[i], LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(hidden[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (visible) {
        lv_obj_clear_flag(music_page->editor_hint, LV_OBJ_FLAG_HIDDEN);
        set_text(music_page->editor_hint, "输入歌名或歌手，选择候选字后搜索");
        lv_obj_clear_flag(music_page->keyboard, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(music_page->keyboard);
        if (music_page->ime) lv_ime_pinyin_refresh(music_page->ime);
        if (music_page->candidates) lv_obj_move_foreground(music_page->candidates);
    } else {
        if (music_page->ime) lv_ime_pinyin_clear(music_page->ime);
        lv_obj_add_flag(music_page->editor_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(music_page->keyboard, LV_OBJ_FLAG_HIDDEN);
        if (music_page->candidates) lv_obj_add_flag(music_page->candidates, LV_OBJ_FLAG_HIDDEN);
    }
}
static void query_event(lv_event_t *event) { (void)event; if (music_page) edit_query(true); }
static void keyboard_event(lv_event_t *event)
{
    if (!music_page) return;
    if (lv_event_get_code(event) == LV_EVENT_READY) search_clicked(event);
    else edit_query(false);
}
static void exit_page(lv_event_t *event)
{
    (void)event;
    if (!music_page) return;
    if (music_page->details) { close_details(NULL); return; }
    if (music_page->editing) { edit_query(false); return; }
    own_music_page_t *old = music_page; music_page = NULL;
    lv_timer_del(old->timer); ui_screen_back(old->return_screen);
    lv_obj_del_async(old->screen); lv_mem_free(old);
}
static void screen_event(lv_event_t *event)
{
    if (!music_page || lv_event_get_target(event) != music_page->screen) return;
    if (lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOADED) lv_timer_pause(music_page->timer);
    else if (lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) { lv_timer_resume(music_page->timer); poll(NULL); }
}
static void poll(lv_timer_t *timer)
{
    (void)timer;
    if (!music_page || lv_scr_act() != music_page->screen) return;
    const bool busy = online_music_is_preparing();
    enable(music_page->search, !busy); enable(music_page->list, !busy);
    enable(music_page->download, online_music_can_download_current());
    char title[256]; music_audio_copy_title(title, sizeof(title));
    set_text(music_page->now, title[0] ? title : "等待选择歌曲");
    set_text(music_page->phase, busy ? "正在准备或保存，点击下方状态查看详情" :
             music_audio_is_playing() ? "播放中" : "可搜索、播放或打开本地歌单");
    uint32_t revision = online_music_get_result(NULL, 0);
    if (revision != music_page->revision) {
        music_page->revision = online_music_get_result(music_page->reply, sizeof(music_page->reply));
        if (!music_page->reply[0]) snprintf(music_page->reply, sizeof(music_page->reply), "准备就绪，点击此处可查看播放和保存详情");
        set_text(music_page->reply_label, music_page->reply);
        if (music_page->details_body) set_text(music_page->details_body, music_page->reply);
    }
    unsigned count = online_music_copy_songs(music_page->snapshot, MUSIC_RESULTS);
    if (music_page->force_list || count != music_page->count ||
        memcmp(music_page->snapshot, music_page->songs, count * sizeof(online_music_song_t))) render_songs(count);
    int saved = online_music_saved_track(music_page->saved_path, sizeof(music_page->saved_path));
    enable(music_page->saved, !busy && saved >= 0);
}

void own_music_ui_open(lv_obj_t *return_screen)
{
    if (music_page) { ui_screen_open(music_page->screen); return; }
    own_music_page_t *p = lv_mem_alloc(sizeof(*p));
    if (!p) return;
    memset(p, 0, sizeof(*p)); music_page = p; p->return_screen = return_screen;
    p->revision = UINT32_MAX; p->force_list = true;
    p->screen = lv_obj_create(NULL); ui_theme_apply_background(p->screen);
    lv_obj_set_style_pad_all(p->screen, 0, 0); lv_obj_set_style_border_width(p->screen, 0, 0);
    lv_obj_clear_flag(p->screen, LV_OBJ_FLAG_SCROLLABLE);
    label(p->screen, 16, 12, 290, "随身音乐", false);
    button(p->screen, 338, 5, 130, "本地歌单", playlist_clicked, NULL);
    p->query = lv_textarea_create(p->screen); lv_obj_set_pos(p->query, 12, 58); lv_obj_set_size(p->query, 340, 44);
    lv_textarea_set_one_line(p->query, true); lv_textarea_set_max_length(p->query, 48); text_style(p->query, false);
    lv_textarea_set_placeholder_text(p->query, "输入歌名或歌手");
    lv_obj_set_style_bg_color(p->query, lv_color_hex(UI_COLOR_SURFACE), 0);
    p->search = button(p->screen, 364, 58, 104, "搜索", search_clicked, NULL);
    p->editor_hint = label(p->screen, 16, 112, 448, "", false);
    lv_label_set_long_mode(p->editor_hint, LV_LABEL_LONG_WRAP);
    p->now = label(p->screen, 16, 116, 300, "等待选择歌曲", false);
    p->download = button(p->screen, 328, 108, 140, "下载", download_clicked, NULL);
    p->phase = label(p->screen, 16, 180, 448, "", true);
    p->list = lv_obj_create(p->screen); lv_obj_set_pos(p->list, 12, 208); lv_obj_set_size(p->list, 456, 170);
    lv_obj_set_style_pad_all(p->list, 4, 0); lv_obj_set_style_pad_row(p->list, 7, 0);
    lv_obj_set_style_border_width(p->list, 0, 0); lv_obj_set_style_bg_opa(p->list, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(p->list, LV_FLEX_FLOW_COLUMN); lv_obj_set_scroll_dir(p->list, LV_DIR_VER);
    const char *controls[] = {"暂停", "继续", "停止", "播放WAV"};
    for (unsigned i = 0; i < 4; ++i) p->controls[i] = button(p->screen, 12 + i * 116, 392, 108, controls[i], controls_clicked, (void *)(uintptr_t)i);
    p->saved = p->controls[3];
    p->reply_label = label(p->screen, 16, 445, 448, "", true);
    lv_obj_add_flag(p->reply_label, LV_OBJ_FLAG_CLICKABLE); lv_obj_set_height(p->reply_label, 28);
    lv_obj_add_event_cb(p->reply_label, show_details, LV_EVENT_CLICKED, NULL);
    p->keyboard = lv_keyboard_create(p->screen); lv_obj_set_align(p->keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(p->keyboard, 0, 288); lv_obj_set_size(p->keyboard, 480, 192);
    lv_keyboard_set_textarea(p->keyboard, p->query); lv_keyboard_set_mode(p->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(p->keyboard, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(p->keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(p->keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(p->keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_pinyin_dict_t *dict = get_sd_pinyin_dict();
    if (dict) {
        p->ime = lv_ime_pinyin_create(p->screen); lv_ime_pinyin_set_keyboard(p->ime, p->keyboard);
        lv_ime_pinyin_set_dict(p->ime, dict); lv_ime_pinyin_set_mode(p->ime, LV_IME_PINYIN_MODE_K26);
        lv_obj_clear_flag(p->ime, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_size(p->ime, 1, 1);
        p->candidates = lv_ime_pinyin_get_cand_panel(p->ime); lv_obj_set_parent(p->candidates, p->screen);
        lv_obj_set_align(p->candidates, LV_ALIGN_TOP_LEFT); lv_obj_set_pos(p->candidates, 0, 252); lv_obj_set_size(p->candidates, 480, 36);
        lv_obj_set_style_text_font(p->candidates, font(), LV_PART_ITEMS);
        lv_obj_set_style_text_color(p->candidates, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(p->candidates, lv_color_hex(UI_COLOR_SURFACE), 0);
    }
    lv_obj_add_event_cb(p->query, query_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(p->query, query_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(p->keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(p->keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    p->timer = lv_timer_create(poll, 350, NULL);
    edge_back_bind(p->screen, exit_page, NULL);
    lv_obj_add_event_cb(p->screen, screen_event, LV_EVENT_ALL, NULL);
    edit_query(false); ui_screen_open(p->screen); poll(NULL);
    if (!dict) notice("中文词库未就绪，可先输入英文或检查 SD 卡");
}
