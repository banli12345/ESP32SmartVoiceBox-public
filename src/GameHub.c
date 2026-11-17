#include "GameHub.h"
#include "MiniGames.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include "UITheme.h"

static lv_obj_t *screen, *desktop;
static const lv_font_t *text_font;
static lv_event_cb_t legacy_open[2];
static bool use_chinese;
static const char *tr(const char *cn, const char *en) { return use_chinese ? cn : en; }
static void back(lv_event_t *e) {(void)e; if (desktop && lv_obj_is_valid(desktop)) ui_screen_back(desktop);}
static void deleted(lv_event_t *e) {if (lv_event_get_target(e) == screen) {screen = desktop = NULL; legacy_open[0] = legacy_open[1] = NULL;}}
lv_obj_t *games_ui_get_screen(void) {return screen;}
static void open_game(lv_event_t *e)
{
    unsigned id = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    if (id < 2) { if (legacy_open[id]) legacy_open[id](e); }
    else if (id < 6) mini_games_open(screen, (mini_game_kind_t)(id - 2), text_font);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent); lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y); lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0); lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE); return obj;
}
void games_ui_open(lv_obj_t *return_to, const lv_font_t *font, bool chinese,
                   lv_event_cb_t open_2048, lv_event_cb_t open_balance)
{
    desktop = return_to; legacy_open[0] = open_2048; legacy_open[1] = open_balance;
    if (screen) {ui_screen_open(screen); return;}
    text_font = font ? font : &lv_font_montserrat_20; use_chinese = chinese;
    screen = lv_obj_create(NULL); lv_obj_set_size(screen, 480, 480);
    lv_obj_set_style_pad_all(screen, 0, 0); lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE); ui_theme_apply_background(screen);
    edge_back_bind(screen, back, NULL); lv_obj_add_event_cb(screen, deleted, LV_EVENT_DELETE, NULL);
    label(screen, tr("游戏", "Games"), 24, 12, 300, text_font, UI_COLOR_ACCENT_DARK);
    label(screen, tr("离线畅玩  侧边返回", "Play offline. Swipe an edge to return."), 24, 42, 432, text_font, UI_COLOR_TEXT_MUTED);
    const char *names[] = {"2048", tr("平衡球", "Balance"), tr("扫雷", "Mines"), tr("五子棋", "Gomoku"),
                           tr("记忆翻牌", "Memory"), tr("叠牌三消", "Stacks")};
    const char *hints[] = {tr("滑动合并", "Slide to merge"), tr("转动设备", "Tilt to roll"), tr("翻开与插旗", "Reveal or flag"),
                           tr("与电脑对弈", "Play against CPU"), tr("寻找相同牌", "Find the pairs"),
                           tr("点上层入槽", "Free tiles to tray")};
    const char *badges[] = {"2048", "B", "*", "5", "1 1", "3"};
    const uint32_t colors[] = {0xF4DFB5, 0xD4E8C9, 0xD7E8ED, 0xC5DDB0, 0xF1DED4, 0xE4F0C8};
    for (unsigned i = 0; i < 6; ++i) {
        lv_obj_t *card = lv_btn_create(screen); lv_obj_set_pos(card, 20 + (i % 2) * 230, 76 + (i / 2) * 118); lv_obj_set_size(card, 210, 108);
        lv_obj_set_style_pad_all(card, 0, 0); lv_obj_set_style_radius(card, 20, 0); lv_obj_set_style_shadow_width(card, 0, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0); lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(card, 1, 0); lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_event_cb(card, open_game, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        lv_obj_t *badge = lv_obj_create(card); lv_obj_set_pos(badge, 12, 26); lv_obj_set_size(badge, 56, 56);
        lv_obj_set_style_pad_all(badge, 0, 0); lv_obj_set_style_radius(badge, 15, 0); lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_bg_color(badge, lv_color_hex(colors[i]), 0); lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *symbol = lv_label_create(badge); lv_label_set_text(symbol, badges[i]); lv_obj_set_style_text_font(symbol, i == 0 ? &lv_font_montserrat_20 : &lv_font_montserrat_28, 0); lv_obj_set_style_text_color(symbol, lv_color_hex(UI_COLOR_ACCENT_DARK), 0); lv_obj_center(symbol);
        label(card, names[i], 80, 24, 122, text_font, UI_COLOR_TEXT);
        label(card, hints[i], 80, 58, 122, text_font, UI_COLOR_TEXT_MUTED);
    }
    label(screen, tr("点击开始  随时返回游戏列表", "Tap a game to start."), 24, 440, 432, text_font, UI_COLOR_TEXT_MUTED);
    ui_screen_open(screen);
}
