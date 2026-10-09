#include "BrowserUI.h"
#include "WebBrowser.h"
#include "UITheme.h"
#include <stdio.h>
#include <string.h>

#define HISTORY_COUNT 5
typedef struct {
    lv_obj_t *screen, *return_screen, *address, *keyboard, *status, *title, *content;
    lv_obj_t *open, *back, *refresh, *links, *cancel, *previous, *next, *page_label;
    lv_timer_t *timer;
    const lv_font_t *font;
    bool chinese, show_links, cancelling;
    unsigned page;
    int history_count, history_position;
    uint32_t displayed_revision;
    char history[HISTORY_COUNT][BROWSER_URL_CAP];
    char page_text[BROWSER_PAGE_BYTES + 1];
    web_browser_view_t view;
} browser_ui_t;
static browser_ui_t *ui;

static void poll_browser(lv_timer_t *timer);
static bool navigate(const char *url, bool add_history);
static const char *tr(const char *cn, const char *en) { return ui->chinese ? cn : en; }
static void enabled(lv_obj_t *object, bool value)
{
    if (value) lv_obj_clear_state(object, LV_STATE_DISABLED);
    else lv_obj_add_state(object, LV_STATE_DISABLED);
}
static void set_label(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static lv_obj_t *button(lv_obj_t *parent, int x, int y, int width, int height,
                        const char *text, lv_event_cb_t callback, void *data)
{
    lv_obj_t *object = lv_btn_create(parent);
    lv_obj_set_pos(object, x, y); lv_obj_set_size(object, width, height);
    lv_obj_set_style_radius(object, 8, 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_DISABLED), LV_STATE_DISABLED);
    lv_obj_set_style_pad_all(object, 4, 0);
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, data);
    lv_obj_t *label = lv_label_create(object);
    lv_obj_set_style_text_font(label, ui->font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT_DISABLED), LV_STATE_DISABLED);
    lv_label_set_text(label, text); lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    return object;
}
static void body_text(const char *text)
{
    lv_obj_clean(ui->content);
    lv_obj_t *label = lv_label_create(ui->content);
    lv_obj_set_width(label, 424);
    lv_obj_set_style_text_font(label, ui->font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_line_space(label, 3, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    lv_obj_scroll_to_y(ui->content, 0, LV_ANIM_OFF);
}
static void link_clicked(lv_event_t *event)
{
    if (!ui) return;
    browser_link_t link;
    if (web_browser_copy_link((unsigned)(uintptr_t)lv_event_get_user_data(event), &link)) navigate(link.url, true);
}
static void render_content(void)
{
    if (ui->view.state != BROWSER_READY) return;
    if (ui->show_links) {
        lv_obj_clean(ui->content);
        for (unsigned i = 0; i < ui->view.link_count; ++i) {
            browser_link_t link;
            if (!web_browser_copy_link(i, &link)) continue;
            lv_obj_t *object = button(ui->content, 0, (int)i * 52, 424, 46, link.text,
                                      link_clicked, (void *)(uintptr_t)i);
            lv_obj_t *label = lv_obj_get_child(object, 0);
            lv_obj_set_width(label, 408);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }
        if (!ui->view.link_count) body_text(tr("此页没有可跳转的链接", "No links on this page."));
        lv_label_set_text(ui->page_label, tr("网页链接", "Links"));
    } else {
        if (web_browser_copy_page(ui->page, ui->page_text, sizeof(ui->page_text))) {
            body_text(ui->page_text[0] ? ui->page_text :
                      (ui->view.truncated ? tr("网页内容已截断，尚未读到正文。", "Download limit reached before readable text.") :
                       tr("网页没有可显示的正文，可能需要运行脚本。", "No readable text. This page may require JavaScript.")));
        }
        lv_label_set_text_fmt(ui->page_label, "%u / %u", ui->page + 1, ui->view.page_count);
    }
    lv_label_set_text(lv_obj_get_child(ui->links, 0), ui->show_links ? tr("正文", "Text") : tr("链接", "Links"));
    enabled(ui->previous, !ui->show_links && ui->page > 0);
    enabled(ui->next, !ui->show_links && ui->page + 1 < ui->view.page_count);
    lv_obj_scroll_to_y(ui->content, 0, LV_ANIM_OFF);
}
static bool navigate(const char *url, bool add_history)
{
    char normalized[BROWSER_URL_CAP];
    const bool valid = browser_url_normalize(url, normalized, sizeof(normalized));
    lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    const bool started = web_browser_start(url);
    if (started) {
        if (add_history && valid && (ui->history_position < 0 || strcmp(ui->history[ui->history_position], normalized))) {
            ui->history_count = ui->history_position + 1;
            if (ui->history_count == HISTORY_COUNT) {
                memmove(ui->history, ui->history + 1, sizeof(ui->history[0]) * (HISTORY_COUNT - 1));
                --ui->history_count;
            }
            strcpy(ui->history[ui->history_count++], normalized);
            ui->history_position = ui->history_count - 1;
        }
        ui->page = 0; ui->show_links = false; ui->cancelling = false;
        body_text(tr("正在打开网页...", "Opening page..."));
        set_label(ui->title, "");
        lv_label_set_text(ui->page_label, "--");
    }
    poll_browser(NULL);
    return started;
}
static void open_clicked(lv_event_t *event) { (void)event; if (ui) navigate(lv_textarea_get_text(ui->address), true); }
static void back_clicked(lv_event_t *event)
{
    (void)event;
    if (!ui || ui->view.busy || ui->history_position <= 0) return;
    int old_position = ui->history_position--;
    if (!navigate(ui->history[ui->history_position], false)) ui->history_position = old_position;
}
static void refresh_clicked(lv_event_t *event)
{
    (void)event;
    if (ui) navigate(ui->history_position >= 0 ? ui->history[ui->history_position] : lv_textarea_get_text(ui->address), false);
}
static void links_clicked(lv_event_t *event) { (void)event; if (ui) { ui->show_links = !ui->show_links; render_content(); } }
static void cancel_clicked(lv_event_t *event)
{
    (void)event;
    if (ui) { ui->cancelling = true; web_browser_cancel(); set_label(ui->status, tr("正在取消...", "Cancelling...")); }
}
static void page_clicked(lv_event_t *event)
{
    if (!ui) return;
    if ((intptr_t)lv_event_get_user_data(event) < 0) { if (ui->page) --ui->page; }
    else if (ui->page + 1 < ui->view.page_count) ++ui->page;
    render_content();
}
static void address_event(lv_event_t *event)
{
    if (!ui) return;
    if (lv_event_get_code(event) == LV_EVENT_DEFOCUSED) lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    else if (!ui->view.busy) { lv_obj_clear_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(ui->keyboard); }
}
static void keyboard_event(lv_event_t *event)
{
    if (!ui) return;
    if (lv_event_get_code(event) == LV_EVENT_READY) open_clicked(event);
    else if (lv_event_get_code(event) == LV_EVENT_CANCEL) lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
}
static void exit_clicked(lv_event_t *event)
{
    (void)event;
    if (!ui) return;
    browser_ui_t *old = ui;
    if (old->timer) { lv_timer_del(old->timer); old->timer = NULL; }
    lv_scr_load(old->return_screen);
    web_browser_release();
    ui = NULL;
    lv_obj_del_async(old->screen);
    lv_mem_free(old);
}
static void screen_event(lv_event_t *event)
{
    if (!ui) return;
    if (lv_event_get_code(event) == LV_EVENT_SCREEN_UNLOADED) {
        if (ui->timer) lv_timer_pause(ui->timer);
        web_browser_release();
    } else if (lv_event_get_code(event) == LV_EVENT_SCREEN_LOADED) {
        if (ui->timer) { lv_timer_reset(ui->timer); lv_timer_resume(ui->timer); }
        poll_browser(NULL);
    }
}
static void poll_browser(lv_timer_t *timer)
{
    (void)timer;
    if (!ui || lv_scr_act() != ui->screen) return;
    web_browser_get_view(&ui->view);
    const bool ready = ui->view.state == BROWSER_READY;
    enabled(ui->open, !ui->view.busy);
    enabled(ui->refresh, !ui->view.busy);
    enabled(ui->back, !ui->view.busy && ui->history_position > 0);
    enabled(ui->links, ready && !ui->view.busy);
    enabled(ui->cancel, ui->view.busy);
    enabled(ui->previous, ready && !ui->show_links && ui->page > 0);
    enabled(ui->next, ready && !ui->show_links && ui->page + 1 < ui->view.page_count);
    if (ui->view.state == BROWSER_LOADING) {
        char text[96];
        snprintf(text, sizeof(text), tr("正在读取网页... %lu KiB", "Loading... %lu KiB"), (unsigned long)(ui->view.received_bytes / 1024));
        if (ui->cancelling) snprintf(text, sizeof(text), "%s", tr("正在取消...", "Cancelling..."));
        set_label(ui->status, text);
    }
    if (ui->displayed_revision == ui->view.revision) return;
    ui->displayed_revision = ui->view.revision;
    if (ready) {
        if (ui->history_position >= 0) strcpy(ui->history[ui->history_position], ui->view.url);
        lv_textarea_set_text(ui->address, ui->view.url);
        set_label(ui->title, ui->view.title[0] ? ui->view.title : tr("网页正文", "Page text"));
        set_label(ui->status, ui->view.truncated ? tr("已加载，页面内容已截断", "Loaded (partial page)") : tr("已加载，可滚动阅读", "Loaded. Scroll to read."));
        render_content();
    } else if (ui->view.state == BROWSER_ERROR) {
        set_label(ui->title, "");
        set_label(ui->status, tr("网页加载失败", "Loading failed"));
        body_text(ui->chinese ? ui->view.error : "Cannot open page. Check URL, WiFi and device time.");
        lv_label_set_text(ui->page_label, "--");
    } else if (ui->view.state == BROWSER_IDLE) {
        set_label(ui->status, ui->view.busy ? tr("正在结束上一次加载...", "Stopping previous request...") : tr("支持 UTF-8 文字网页", "UTF-8 text pages"));
        body_text(tr("输入网址后点击打开。\n\n正文可以上下滚动，也可使用底部按钮翻页。点击链接可查看并打开本页的网址。", "Enter a URL and tap Open.\n\nScroll to read, use page buttons, or tap Links to navigate."));
        lv_label_set_text(ui->page_label, "--");
    }
}

void browser_ui_open(lv_obj_t *return_screen, const lv_font_t *font, bool chinese,
                     browser_back_button_builder_t build_back_button)
{
    if (ui) { lv_scr_load(ui->screen); return; }
    ui = lv_mem_alloc(sizeof(*ui));
    if (!ui) return;
    memset(ui, 0, sizeof(*ui));
    ui->return_screen = return_screen; ui->font = font; ui->chinese = chinese;
    ui->history_position = -1; ui->displayed_revision = UINT32_MAX;
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, 480, 480); lv_obj_set_style_pad_all(ui->screen, 0, 0);
    lv_obj_set_style_border_width(ui->screen, 0, 0);
    ui_theme_apply_background(ui->screen);
    lv_obj_set_style_text_color(ui->screen, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_clear_flag(ui->screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *header = lv_obj_create(ui->screen);
    lv_obj_set_pos(header, 0, 0); lv_obj_set_size(header, 480, 50);
    lv_obj_set_style_pad_all(header, 0, 0); lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *name = lv_label_create(header);
    lv_obj_set_style_text_font(name, font, 0); lv_label_set_text(name, tr("浏览器", "Browser"));
    lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_ACCENT_DARK), 0); lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);
    build_back_button(header, LV_ALIGN_RIGHT_MID, -8, exit_clicked);
    ui->address = lv_textarea_create(ui->screen);
    lv_obj_set_pos(ui->address, 12, 58); lv_obj_set_size(ui->address, 358, 44);
    lv_textarea_set_one_line(ui->address, true); lv_textarea_set_max_length(ui->address, BROWSER_URL_CAP - 1);
    lv_textarea_set_text(ui->address, "https://example.com");
    lv_obj_set_style_text_font(ui->address, &lv_font_montserrat_16, 0);
    lv_obj_set_style_bg_color(ui->address, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(ui->address, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_color(ui->address, lv_color_hex(UI_COLOR_BORDER), 0);
    ui->open = button(ui->screen, 380, 58, 88, 44, tr("打开", "Open"), open_clicked, NULL);
    ui->back = button(ui->screen, 12, 108, 108, 38, tr("后退", "Back"), back_clicked, NULL);
    ui->refresh = button(ui->screen, 128, 108, 108, 38, tr("刷新", "Reload"), refresh_clicked, NULL);
    ui->links = button(ui->screen, 244, 108, 108, 38, tr("链接", "Links"), links_clicked, NULL);
    ui->cancel = button(ui->screen, 360, 108, 108, 38, tr("取消", "Cancel"), cancel_clicked, NULL);
    ui->status = lv_label_create(ui->screen); lv_obj_set_pos(ui->status, 14, 152); lv_obj_set_width(ui->status, 452);
    lv_obj_set_style_text_font(ui->status, font, 0); lv_obj_set_style_text_color(ui->status, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    ui->title = lv_label_create(ui->screen); lv_obj_set_pos(ui->title, 14, 180); lv_obj_set_size(ui->title, 452, 27);
    lv_obj_set_style_text_font(ui->title, font, 0); lv_obj_set_style_text_color(ui->title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_long_mode(ui->title, LV_LABEL_LONG_DOT);
    ui->content = lv_obj_create(ui->screen); lv_obj_set_pos(ui->content, 12, 214); lv_obj_set_size(ui->content, 456, 210);
    lv_obj_set_style_pad_all(ui->content, 8, 0); lv_obj_set_style_border_width(ui->content, 0, 0);
    lv_obj_set_style_bg_color(ui->content, lv_color_hex(UI_COLOR_SURFACE), 0); lv_obj_set_scroll_dir(ui->content, LV_DIR_VER);
    ui->previous = button(ui->screen, 12, 432, 120, 40, tr("上一页", "Previous"), page_clicked, (void *)(intptr_t)-1);
    ui->next = button(ui->screen, 348, 432, 120, 40, tr("下一页", "Next"), page_clicked, (void *)(intptr_t)1);
    ui->page_label = lv_label_create(ui->screen); lv_obj_set_pos(ui->page_label, 138, 441); lv_obj_set_width(ui->page_label, 204);
    lv_obj_set_style_text_font(ui->page_label, font, 0); lv_obj_set_style_text_align(ui->page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(ui->page_label, lv_color_hex(UI_COLOR_TEXT), 0);
    ui->keyboard = lv_keyboard_create(ui->screen); lv_obj_set_size(ui->keyboard, 480, 216);
    lv_obj_align(ui->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0); lv_keyboard_set_textarea(ui->keyboard, ui->address);
    lv_keyboard_set_mode(ui->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_set_style_bg_color(ui->keyboard, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(ui->keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ui->keyboard, lv_color_hex(UI_COLOR_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ui->keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(ui->keyboard, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_add_flag(ui->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(ui->address, address_event, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ui->address, address_event, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui->address, address_event, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(ui->keyboard, keyboard_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(ui->keyboard, keyboard_event, LV_EVENT_CANCEL, NULL);
    ui->timer = lv_timer_create(poll_browser, 150, NULL);
    lv_mem_monitor_t memory;
    lv_mem_monitor(&memory);
    printf("[BROWSER_UI] created ui_bytes=%u pool_free=%u pool_largest=%u\n",
           (unsigned)sizeof(*ui), (unsigned)memory.free_size, (unsigned)memory.free_biggest_size);
    lv_obj_add_event_cb(ui->screen, screen_event, LV_EVENT_ALL, NULL);
    lv_scr_load(ui->screen);
}
