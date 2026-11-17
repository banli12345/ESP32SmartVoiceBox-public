#include "WordMatchUI.h"

#include "EdgeBack.h"
#include "UITheme.h"
#include "UiTransition.h"
#include "WordBook.h"

#include <stdio.h>
#include <string.h>

#include "nvs.h"

#define WORD_MATCH_FONT_PATH "S:/fonts/cet4_20.bin"
#define WORD_MATCH_PAIRS 5
#define WORD_MATCH_SCAN_MAX 100u
#define WORD_MATCH_WRONG_MS 650u

typedef struct word_match_page {
    lv_obj_t *screen;
    lv_obj_t *return_to;
    const lv_font_t *fallback;
    lv_font_t *zh_font;
    bool chinese;
    bool closing;
    bool delete_queued;
    bool freed;
    bool book_open;
    bool nvs_ready;
    bool warn_persist;
    bool round_complete;
    word_book_t book;
    uint32_t count;
    uint32_t cursor;
    uint32_t pending_cursor;
    int pairs;
    int matched;
    int en_sel;
    int zh_sel;
    bool lock;
    lv_timer_t *flash;
    lv_timer_t *note;
    lv_obj_t *en_btn[WORD_MATCH_PAIRS];
    lv_obj_t *zh_btn[WORD_MATCH_PAIRS];
    uint8_t en_pair[WORD_MATCH_PAIRS];
    uint8_t zh_pair[WORD_MATCH_PAIRS];
    bool en_done[WORD_MATCH_PAIRS];
    bool zh_done[WORD_MATCH_PAIRS];
    char english[WORD_MATCH_PAIRS][WORD_BOOK_EN_CAP];
    char chinese_text[WORD_MATCH_PAIRS][WORD_BOOK_ZH_CAP];
    lv_obj_t *status;
} word_match_page_t;

static word_match_page_t *active_page;

static const lv_font_t *fallback_of(const word_match_page_t *page)
{
    return page->fallback ? page->fallback : &lv_font_montserrat_20;
}

static bool glyph_ok(const lv_font_t *font, uint32_t letter)
{
    lv_font_glyph_dsc_t dsc;
    if (!font || !font->get_glyph_dsc) return false;
    memset(&dsc, 0, sizeof(dsc));
    if (!font->get_glyph_dsc(font, &dsc, letter, 0)) return false;
    if (dsc.is_placeholder) return false;
    return dsc.box_w > 0 && dsc.box_h > 0;
}

static void drop_timer(lv_timer_t **timer)
{
    if (timer && *timer) {
        lv_timer_del(*timer);
        *timer = NULL;
    }
}

static void close_book(word_match_page_t *page)
{
    if (!page || !page->book_open) return;
    word_book_close(&page->book);
    memset(&page->book, 0, sizeof(page->book));
    page->book_open = false;
}

static bool nvs_read_cursor(uint32_t *value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("word_match", NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) { *value = 0; return true; }
    if (err != ESP_OK) return false;
    err = nvs_get_u32(handle, "cursor", value);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *value = 0;
        return true;
    }
    return err == ESP_OK;

}

static bool nvs_write_cursor(uint32_t value)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("word_match", NVS_READWRITE, &handle);
    if (err != ESP_OK) return false;
    err = nvs_set_u32(handle, "cursor", value);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;

}

static const char *tr(const word_match_page_t *page, const char *zh, const char *en)
{
    return page->chinese ? zh : en;
}

static const lv_font_t *chrome_font(const word_match_page_t *page)
{
    /* 背 U+80CC: UI chrome stays on the fallback when the word font lacks it. */
    if (page->chinese && page->zh_font && glyph_ok(page->zh_font, 0x80CCu)) return page->zh_font;
    return fallback_of(page);
}

static void style_label(lv_obj_t *label, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
}

static lv_obj_t *add_label(lv_obj_t *parent, int x, int y, int w, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    style_label(label, font, color);
    lv_label_set_text(label, text);
    return label;
}

static void paint_card(lv_obj_t *button, int kind)
{
    uint32_t bg = UI_COLOR_SURFACE;
    uint32_t fg = UI_COLOR_TEXT;
    uint32_t border = UI_COLOR_BORDER;
    int width = 1;
    if (kind == 1) {
        bg = UI_COLOR_PANEL;
        border = UI_COLOR_ACCENT;
        width = 3;
    } else if (kind == 2) {
        bg = UI_COLOR_SUCCESS;
        fg = UI_COLOR_ON_ACCENT;
        border = UI_COLOR_SUCCESS;
    } else if (kind == 3) {
        bg = UI_COLOR_ERROR;
        fg = UI_COLOR_ON_ACCENT;
        border = UI_COLOR_ERROR;
    }
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(bg), LV_STATE_DISABLED);
    lv_obj_set_style_border_color(button, lv_color_hex(border), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(border), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(button, width, 0);
    lv_obj_set_style_border_width(button, width, LV_STATE_DISABLED);
    lv_obj_set_style_color_filter_opa(button, LV_OPA_TRANSP, LV_STATE_DISABLED);
    lv_obj_set_style_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) {
        lv_obj_set_style_text_color(label, lv_color_hex(fg), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(fg), LV_STATE_DISABLED);
    }
}

static void restyle_cards(word_match_page_t *page)
{
    for (int i = 0; i < page->pairs; ++i) {
        int en_kind = page->en_done[i] ? 2 : (page->en_sel == i ? (page->lock ? 3 : 1) : 0);
        int zh_kind = page->zh_done[i] ? 2 : (page->zh_sel == i ? (page->lock ? 3 : 1) : 0);
        if (page->en_btn[i]) paint_card(page->en_btn[i], en_kind);
        if (page->zh_btn[i]) paint_card(page->zh_btn[i], zh_kind);
    }
}

static void set_status(word_match_page_t *page, const char *text)
{
    if (page->status && text && strcmp(lv_label_get_text(page->status), text) != 0) lv_label_set_text(page->status, text);
}

static void progress_text(word_match_page_t *page, char *out, size_t out_size)
{
    if (page->round_complete) {
        snprintf(out, out_size, "%s %d/%d", tr(page, "本组完成", "Group done"), page->matched, page->pairs);
    } else if (page->matched > 0) {
        snprintf(out, out_size, "%s %d/%d", tr(page, "已匹配", "Matched"), page->matched, page->pairs);
    } else {
        snprintf(out, out_size, "%s", tr(page, "点左右卡片配对", "Tap one side, then the other"));
    }
}

static void show_progress(word_match_page_t *page)
{
    char text[64];
    progress_text(page, text, sizeof(text));
    set_status(page, text);
}

static void note_cb(lv_timer_t *timer)
{
    word_match_page_t *page = timer->user_data;
    if (!page || page->freed || page->note != timer) return;
    page->note = NULL;
    lv_timer_del(timer);
    if (!page->closing && page->status) show_progress(page);
}

static void show_note(word_match_page_t *page, const char *text)
{
    set_status(page, text);
    drop_timer(&page->note);
    page->note = lv_timer_create(note_cb, 1400, page);
    if (page->note) lv_timer_set_repeat_count(page->note, 1);
}

static void release_font_cb(void *font)
{
    if (font) lv_font_free((lv_font_t *)font);
}

static void retarget_fonts(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_20, 0);
    uint32_t count = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < count; ++i) retarget_fonts(lv_obj_get_child(obj, i));
}

static void release_font_later(word_match_page_t *page)
{
    lv_font_t *font = page->zh_font;
    page->zh_font = NULL;
    if (!font) return;
    if (page->screen && lv_obj_is_valid(page->screen)) retarget_fonts(page->screen);
    if (lv_async_call(release_font_cb, font) != LV_RES_OK) lv_font_free(font);
}

static bool load_font(word_match_page_t *page)
{
    if (page->zh_font) return true;
    lv_font_t *font = lv_font_load(WORD_MATCH_FONT_PATH);
    /* 中 U+4E2D stands in for a real vocabulary glyph, not the placeholder box. */
    if (!font || !glyph_ok(font, 0x4E2Du)) {
        if (font) lv_font_free(font);
        return false;
    }
    font->fallback = (lv_font_t *)fallback_of(page);
    page->zh_font = font;
    return true;
}

static bool open_book(word_match_page_t *page)
{
    if (page->book_open) return page->count > 0;
    memset(&page->book, 0, sizeof(page->book));
    if (!word_book_open(&page->book, WORD_BOOK_SD_PATH)) return false;
    page->book_open = true;
    page->count = page->book.count;
    if (page->count == 0) {
        close_book(page);
        return false;
    }
    uint32_t cursor = 0;
    page->nvs_ready = nvs_read_cursor(&cursor);
    if (!page->nvs_ready) {
        cursor = 0;
        page->warn_persist = true;
    }
    if (cursor >= page->count) cursor %= page->count;
    page->cursor = cursor;
    return true;
}

static void copy_field(char *dst, size_t dst_size, const char *src, size_t src_size)
{
    size_t n = src_size;
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    dst[dst_size - 1] = '\0';
}

static bool take_entry(word_match_page_t *page, const word_book_entry_t *entry, int taken)
{
    char english[WORD_BOOK_EN_CAP];
    char chinese[WORD_BOOK_ZH_CAP];
    copy_field(english, sizeof(english), entry->english, WORD_BOOK_EN_CAP);
    copy_field(chinese, sizeof(chinese), entry->chinese, WORD_BOOK_ZH_CAP);
    if (!english[0] || !chinese[0]) return false;
    for (int i = 0; i < taken; ++i) {
        if (strcmp(page->english[i], english) == 0 || strcmp(page->chinese_text[i], chinese) == 0) return false;
    }
    memcpy(page->english[taken], english, sizeof(english));
    memcpy(page->chinese_text[taken], chinese, sizeof(chinese));
    return true;
}

/* Stop before a duplicate meaning, so it begins the next group and is not lost. */
static int collect_round(word_match_page_t *page)
{
    uint32_t limit = page->count < WORD_MATCH_SCAN_MAX ? page->count : WORD_MATCH_SCAN_MAX;
    uint32_t start = page->cursor;
    if (start >= page->count) start %= page->count;
    uint32_t examined = 0;
    int taken = 0;
    while (taken < WORD_MATCH_PAIRS && examined < limit) {
        uint32_t index = start + examined;
        if (index >= page->count) break;
        word_book_entry_t entry;
        memset(&entry, 0, sizeof(entry));
        if (!word_book_read(&page->book, index, &entry)) return 0;
        if (!take_entry(page, &entry, taken)) break;
        ++examined;
        ++taken;
    }
    uint32_t next = start + examined;
    page->pending_cursor = next >= page->count ? 0 : next;
    return taken;
}

static void shuffle_ids(uint8_t *ids, int count)
{
    for (int i = count - 1; i > 0; --i) {
        int swap = (int)lv_rand(0, (uint32_t)i);
        uint8_t tmp = ids[i];
        ids[i] = ids[swap];
        ids[swap] = tmp;
    }
}

static void clear_wrong(lv_timer_t *timer)
{
    word_match_page_t *page = timer->user_data;
    if (!page || page->freed) return;
    if (page->flash == timer) page->flash = NULL;
    lv_timer_del(timer);
    if (page->closing) return;
    page->lock = false;
    page->en_sel = -1;
    page->zh_sel = -1;
    restyle_cards(page);
}

static void finish_round(word_match_page_t *page)
{
    page->round_complete = true;
    page->cursor = page->pending_cursor;
    show_progress(page);
    if (!nvs_write_cursor(page->cursor)) {
        page->nvs_ready = false;
        show_note(page, tr(page, "进度暂未保存", "Progress not saved"));
    } else {
        page->nvs_ready = true;
    }
}

static void judge(word_match_page_t *page)
{
    int english = page->en_sel;
    int chinese = page->zh_sel;
    if (english < 0 || chinese < 0) return;
    if (page->en_pair[english] == page->zh_pair[chinese]) {
        page->en_done[english] = true;
        page->zh_done[chinese] = true;
        lv_obj_add_state(page->en_btn[english], LV_STATE_DISABLED);
        lv_obj_add_state(page->zh_btn[chinese], LV_STATE_DISABLED);
        page->en_sel = -1;
        page->zh_sel = -1;
        page->matched++;
        restyle_cards(page);
        show_progress(page);
        if (page->pairs > 0 && page->matched >= page->pairs) finish_round(page);
        return;
    }
    page->lock = true;
    restyle_cards(page);
    drop_timer(&page->flash);
    page->flash = lv_timer_create(clear_wrong, WORD_MATCH_WRONG_MS, page);
    if (!page->flash) {
        page->lock = false;
        page->en_sel = -1;
        page->zh_sel = -1;
        restyle_cards(page);
    }
}

static void on_card(lv_event_t *event)
{
    word_match_page_t *page = active_page;
    if (!page || page->closing || page->lock || page->freed) return;
    unsigned packed = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    unsigned column = packed >> 4;
    unsigned row = packed & 0xFu;
    if (row >= (unsigned)page->pairs) return;
    if (column == 0) {
        if (page->en_done[row]) return;
        page->en_sel = page->en_sel == (int)row ? -1 : (int)row;
    } else {
        if (page->zh_done[row]) return;
        page->zh_sel = page->zh_sel == (int)row ? -1 : (int)row;
    }
    restyle_cards(page);
    if (page->en_sel >= 0 && page->zh_sel >= 0) judge(page);
}

static lv_obj_t *make_card(word_match_page_t *page, int column, int row, const char *text, const lv_font_t *font)
{
    lv_obj_t *button = lv_btn_create(page->screen);
    lv_obj_set_pos(button, column == 0 ? 28 : 248, 110 + row * 58);
    lv_obj_set_size(button, 204, 52);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 4, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_set_width(label, 196);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label, 0, 0);
    style_label(label, font, UI_COLOR_TEXT);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    paint_card(button, 0);
    lv_obj_add_event_cb(button, on_card, LV_EVENT_CLICKED, (void *)(uintptr_t)((column << 4) | row));
    return button;
}

static void place_columns(word_match_page_t *page)
{
    const lv_font_t *header_font = chrome_font(page);
    add_label(page->screen, 28, 82, 204, tr(page, "英文", "English"), header_font, UI_COLOR_TEXT_MUTED);
    add_label(page->screen, 248, 82, 204, tr(page, "中文", "Chinese"), page->zh_font ? page->zh_font : header_font, UI_COLOR_TEXT_MUTED);
    uint8_t english_order[WORD_MATCH_PAIRS];
    uint8_t chinese_order[WORD_MATCH_PAIRS];
    for (int i = 0; i < page->pairs; ++i) english_order[i] = chinese_order[i] = (uint8_t)i;
    shuffle_ids(english_order, page->pairs);
    shuffle_ids(chinese_order, page->pairs);
    for (int row = 0; row < page->pairs; ++row) {
        uint8_t english_id = english_order[row];
        uint8_t chinese_id = chinese_order[row];
        page->en_pair[row] = english_id;
        page->zh_pair[row] = chinese_id;
        page->en_done[row] = false;
        page->zh_done[row] = false;
        /* Montserrat 18 is not built; 20 is the English face that is linked. */
        page->en_btn[row] = make_card(page, 0, row, page->english[english_id], &lv_font_montserrat_20);
        page->zh_btn[row] = make_card(page, 1, row, page->chinese_text[chinese_id], page->zh_font);
    }
}

static void on_retry(lv_event_t *event);
static void on_next(lv_event_t *event);

static lv_obj_t *make_action(word_match_page_t *page, int x, int y, int w, const char *text, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(page->screen);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, 40);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_center(label);
    style_label(label, chrome_font(page), UI_COLOR_ON_ACCENT);
    lv_label_set_text(label, text);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, page);
    return button;
}

static void show_message(word_match_page_t *page, const char *message, bool retry)
{
    const lv_font_t *font = fallback_of(page);
    add_label(page->screen, 24, 16, 432, tr(page, "背单词", "Word match"), font, UI_COLOR_ACCENT_DARK);
    page->status = add_label(page->screen, 24, 150, 432, message, font, UI_COLOR_TEXT);
    if (retry) make_action(page, 24, 400, 160, tr(page, "重试", "Retry"), on_retry);
}

static void fill_screen(word_match_page_t *page)
{
    drop_timer(&page->flash);
    drop_timer(&page->note);
    page->lock = false;
    page->en_sel = -1;
    page->zh_sel = -1;
    page->matched = 0;
    page->pairs = 0;
    page->round_complete = false;
    page->status = NULL;
    for (int i = 0; i < WORD_MATCH_PAIRS; ++i) {
        page->en_btn[i] = NULL;
        page->zh_btn[i] = NULL;
        page->en_done[i] = false;
        page->zh_done[i] = false;
    }
    lv_obj_clean(page->screen);

    if (!load_font(page)) {
        char message[160];
        snprintf(message, sizeof(message), "%s\n%s",
                 tr(page, "找不到字体", "Font missing"), WORD_MATCH_FONT_PATH);
        show_message(page, message, true);
        return;
    }
    if (!open_book(page)) {
        char message[192];
        snprintf(message, sizeof(message), "%s\n%s",
                 tr(page, "找不到词库", "Word book missing"), WORD_BOOK_SD_PATH);
        show_message(page, message, true);
        return;
    }

    page->pairs = collect_round(page);
    const lv_font_t *font = chrome_font(page);
    add_label(page->screen, 24, 10, 432, tr(page, "背单词", "Word match"), font, UI_COLOR_ACCENT_DARK);
    char subtitle[64];
    snprintf(subtitle, sizeof(subtitle), "%s (%u)", tr(page, "四级词库", "CET-4"), (unsigned)page->count);
    add_label(page->screen, 24, 42, 432, subtitle, font, UI_COLOR_TEXT_MUTED);
    if (page->pairs <= 0) {
        page->status = add_label(page->screen, 24, 180, 432,
                                  tr(page, "没有可用词条\n/sdcard/words/cet4.bwv", "No usable entries\n/sdcard/words/cet4.bwv"),
                                  fallback_of(page), UI_COLOR_TEXT);
        make_action(page, 24, 412, 150, tr(page, "重试", "Retry"), on_retry);
        return;
    }
    place_columns(page);
    make_action(page, 24, 412, 150, tr(page, "换一组", "Next group"), on_next);
    page->status = add_label(page->screen, 186, 418, 270, "", chrome_font(page), UI_COLOR_TEXT_MUTED);
    if (page->warn_persist) {
        page->warn_persist = false;
        show_note(page, tr(page, "进度暂未保存", "Progress not saved"));
    } else {
        show_progress(page);
    }
}

static void on_retry(lv_event_t *event)
{
    word_match_page_t *page = lv_event_get_user_data(event);
    if (!page || page != active_page || page->closing || page->freed) return;
    close_book(page);
    if (page->zh_font) {
        lv_font_t *font = page->zh_font;
        page->zh_font = NULL;
        lv_obj_clean(page->screen);
        lv_font_free(font);
    }
    fill_screen(page);
}

static void on_next(lv_event_t *event)
{
    word_match_page_t *page = lv_event_get_user_data(event);
    if (!page || page != active_page || page->closing || page->freed) return;
    drop_timer(&page->flash);
    drop_timer(&page->note);
    page->lock = false;
    /* Skip does not write NVS and does not mark the group mastered. */
    if (!page->round_complete) page->cursor = page->pending_cursor;
    fill_screen(page);
}

static void delete_screen_cb(void *data)
{
    word_match_page_t *page = data;
    /* Mark it dispatched before DELETE: cancelling this running async would
     * free its LVGL callback record twice in LVGL 8.3. */
    page->delete_queued = false;
    if (lv_obj_is_valid(page->screen)) lv_obj_del(page->screen);
}

static void on_delete(lv_event_t *event)
{
    word_match_page_t *page = lv_event_get_user_data(event);
    if (!page || page->freed || lv_event_get_target(event) != page->screen) return;
    page->freed = true;
    if (page->delete_queued) lv_async_call_cancel(delete_screen_cb, page);
    drop_timer(&page->flash);
    drop_timer(&page->note);
    close_book(page);
    if (active_page == page) active_page = NULL;
    release_font_later(page);
    page->screen = NULL;
    lv_mem_free(page);
}

static void on_unloaded(lv_event_t *event)
{
    word_match_page_t *page = lv_event_get_user_data(event);
    if (!page || page->freed || lv_event_get_target(event) != page->screen || page->delete_queued) return;
    page->closing = true;
    drop_timer(&page->flash);
    drop_timer(&page->note);
    close_book(page);
    if (active_page == page) active_page = NULL;
    page->delete_queued = true;
    if (lv_async_call(delete_screen_cb, page) != LV_RES_OK) page->delete_queued = false;
}

static void on_back(lv_event_t *event)
{
    word_match_page_t *page = lv_event_get_user_data(event);
    if (!page || page->freed || page->closing) return;
    page->closing = true;
    drop_timer(&page->flash);
    drop_timer(&page->note);
    page->lock = false;
    close_book(page);
    if (active_page == page) active_page = NULL;
    /* Leave the screen alive so the 180 ms return transition can capture it. */
    ui_screen_back(page->return_to);
}

void word_match_ui_open(lv_obj_t *return_to, const lv_font_t *fallback_font, bool chinese)
{
    if (active_page && active_page->screen && lv_obj_is_valid(active_page->screen) && !active_page->closing) {
        ui_screen_open(active_page->screen);
        return;
    }
    word_match_page_t *page = lv_mem_alloc(sizeof(*page));
    if (!page) return;
    memset(page, 0, sizeof(*page));
    page->return_to = return_to;
    page->fallback = fallback_font ? fallback_font : &lv_font_montserrat_20;
    page->chinese = chinese;
    page->en_sel = -1;
    page->zh_sel = -1;
    page->screen = lv_obj_create(NULL);
    if (!page->screen) {
        lv_mem_free(page);
        return;
    }
    lv_obj_set_size(page->screen, 480, 480);
    lv_obj_set_style_pad_all(page->screen, 0, 0);
    lv_obj_set_style_border_width(page->screen, 0, 0);
    lv_obj_clear_flag(page->screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_background(page->screen);
    lv_obj_add_event_cb(page->screen, on_delete, LV_EVENT_DELETE, page);
    lv_obj_add_event_cb(page->screen, on_unloaded, LV_EVENT_SCREEN_UNLOADED, page);
    edge_back_bind(page->screen, on_back, page);
    active_page = page;
    fill_screen(page);
    ui_screen_open(page->screen);
}

lv_obj_t *word_match_ui_get_screen(void)
{
    return active_page ? active_page->screen : NULL;
}
