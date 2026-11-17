#include "MiniGames.h"
#include "EdgeBack.h"
#include "UITheme.h"
#include "UiTransition.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINE_N 8
#define MINE_CELLS 64
#define MINE_COUNT 10
#define MINE_SAFE 54
#define GOMOKU_N 11
#define GOMOKU_CELLS (GOMOKU_N * GOMOKU_N)
#define MEM_N 16
#define MEM_PAIRS 8
#define TILE_N 100
#define TILE_NORMAL 99
#define TILE_KINDS 6
#define TILE_SLOT 7
#define TILE_REWARD 255
#define TILE_W 56
#define TILE_H 52
#define CELL_MAX GOMOKU_CELLS

typedef struct mini_page {
    lv_obj_t *screen;
    lv_obj_t *return_screen;
    lv_obj_t *title;
    lv_obj_t *hint;
    lv_obj_t *status;
    lv_obj_t *restart;
    lv_obj_t *mode;
    lv_obj_t *cells[CELL_MAX];
    lv_obj_t *slots[TILE_SLOT];
    const lv_font_t *font;
    mini_game_kind_t kind;
    lv_timer_t *timer;
    lv_timer_t *ai_timer;
    lv_timer_t *flip_timer;
    uint8_t mine[MINE_CELLS];
    uint8_t open[MINE_CELLS];
    uint8_t flag[MINE_CELLS];
    uint8_t adj[MINE_CELLS];
    char mine_text[MINE_CELLS][2];
    bool placed;
    bool over;
    bool won;
    bool mark_mode;
    bool timing;
    uint32_t seconds;
    uint32_t elapsed_ms;
    uint32_t last_tick;
    uint8_t flags;
    int8_t board[GOMOKU_CELLS];
    bool ai_pending;
    uint8_t deck[MEM_N];
    uint8_t face[MEM_N];
    int8_t first;
    int8_t second;
    uint8_t pairs;
    uint16_t turns;
    bool lock;
    uint8_t tile_kind[TILE_N];
    uint8_t tile_layer[TILE_N];
    uint8_t tile_alive[TILE_N];
    int16_t tile_x[TILE_N];
    int16_t tile_y[TILE_N];
    int8_t slot[TILE_SLOT];
    uint8_t slot_n;
    uint8_t tiles_left;
    uint8_t tile_order[TILE_N];
} mini_page_t;

static mini_page_t *active_page;

static const lv_font_t *ui_font(const mini_page_t *page)
{
    return page->font ? page->font : &lv_font_montserrat_20;
}

static void style_text(lv_obj_t *obj, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int w, const char *text, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    style_text(label, font, UI_COLOR_TEXT);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
                             const lv_font_t *font, lv_event_cb_t cb, void *user, lv_event_code_t code)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_obj_center(label);
    style_text(label, font, UI_COLOR_TEXT);
    lv_label_set_text(label, text);
    if (cb) lv_obj_add_event_cb(button, cb, code, user);
    return button;
}

static void set_status(mini_page_t *page, const char *text)
{
    if (strcmp(lv_label_get_text(page->status), text) != 0) lv_label_set_text(page->status, text);
}

static void drop_timer(lv_timer_t **timer)
{
    if (*timer) {
        lv_timer_del(*timer);
        *timer = NULL;
    }
}

static bool page_visible(const mini_page_t *page)
{
    return page && active_page == page && lv_scr_act() == page->screen;
}

static void update_elapsed(mini_page_t *page)
{
    if (!page->timing) return;
    uint32_t now = lv_tick_get();
    page->elapsed_ms += now - page->last_tick;
    page->last_tick = now;
    page->seconds = page->elapsed_ms / 1000;
}

static void screen_event(lv_event_t *event)
{
    mini_page_t *page = lv_event_get_user_data(event);
    if (!page || active_page != page) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_SCREEN_UNLOADED) {
        update_elapsed(page);
        if (page->timer) lv_timer_pause(page->timer);
        if (page->ai_timer) lv_timer_pause(page->ai_timer);
        if (page->flip_timer) lv_timer_pause(page->flip_timer);
    } else if (code == LV_EVENT_SCREEN_LOADED) {
        page->last_tick = lv_tick_get();
        if (page->timer && page->timing && !page->over) {lv_timer_reset(page->timer); lv_timer_resume(page->timer);}
        if (page->ai_timer) lv_timer_resume(page->ai_timer);
        if (page->flip_timer) lv_timer_resume(page->flip_timer);
    }
}

static void page_delete(lv_event_t *event)
{
    mini_page_t *page = lv_event_get_user_data(event);
    if (!page || lv_event_get_target(event) != page->screen) return;
    if (active_page == page) active_page = NULL;
    drop_timer(&page->timer);
    drop_timer(&page->ai_timer);
    drop_timer(&page->flip_timer);
    lv_mem_free(page);
}

static void close_page(mini_page_t *page)
{
    if (!page) return;
    if (active_page == page) active_page = NULL;
    drop_timer(&page->timer);
    drop_timer(&page->ai_timer);
    drop_timer(&page->flip_timer);
    page->ai_pending = false;
    page->lock = false;
    lv_obj_t *back = page->return_screen;
    lv_obj_del_async(page->screen);
    ui_screen_back(back);
}

static void exit_page(lv_event_t *event)
{
    (void)event;
    if (active_page) close_page(active_page);
}

static void mines_status(mini_page_t *page)
{
    char text[48];
    if (!page->placed) snprintf(text, sizeof(text), "等待首击  雷%d  旗%d", MINE_COUNT, page->flags);
    else if (page->won) snprintf(text, sizeof(text), "胜利  雷%d  旗%d  %u秒", MINE_COUNT, page->flags, (unsigned)page->seconds);
    else if (page->over) snprintf(text, sizeof(text), "失败  雷%d  旗%d  %u秒", MINE_COUNT, page->flags, (unsigned)page->seconds);
    else snprintf(text, sizeof(text), "雷%d  旗%d  %u秒", MINE_COUNT, page->flags, (unsigned)page->seconds);
    set_status(page, text);
}

static void refresh_mines(mini_page_t *page)
{
    mines_status(page);
    lv_label_set_text(lv_obj_get_child(page->mode, 0), page->mark_mode ? "挖开" : "标记");
    for (int i = 0; i < MINE_CELLS; ++i) {
        const char *face = "?";
        uint32_t bg = UI_COLOR_PANEL;
        if (page->open[i]) {
            bg = page->mine[i] ? UI_COLOR_ERROR : UI_COLOR_SURFACE;
            if (page->mine[i]) face = "*";
            else if (page->adj[i] == 0) face = "";
            else {
                page->mine_text[i][0] = (char)('0' + page->adj[i]);
                page->mine_text[i][1] = '\0';
                face = page->mine_text[i];
            }
        } else if (page->over && page->mine[i]) {
            face = "*";
            bg = UI_COLOR_ERROR;
        } else if (page->flag[i]) {
            face = "F";
            bg = UI_COLOR_WARNING;
        }
        lv_obj_t *label = lv_obj_get_child(page->cells[i], 0);
        if (strcmp(lv_label_get_text(label), face) != 0) lv_label_set_text(label, face);
        lv_obj_set_style_bg_color(page->cells[i], lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(label, lv_color_hex((page->flag[i] || (page->over && page->mine[i])) ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT), 0);
    }
}

static void place_mines(mini_page_t *page, int safe)
{
    int sx = safe % MINE_N;
    int sy = safe / MINE_N;
    int placed = 0;
    int guard = 0;
    while (placed < MINE_COUNT && guard < 512) {
        int i = (int)lv_rand(0, MINE_CELLS - 1);
        int x = i % MINE_N;
        int y = i / MINE_N;
        ++guard;
        if (page->mine[i]) continue;
        if (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1) continue;
        page->mine[i] = 1;
        ++placed;
    }
    for (int i = 0; placed < MINE_COUNT && i < MINE_CELLS; ++i) {
        int x = i % MINE_N;
        int y = i / MINE_N;
        if (page->mine[i]) continue;
        if (x >= sx - 1 && x <= sx + 1 && y >= sy - 1 && y <= sy + 1) continue;
        page->mine[i] = 1;
        ++placed;
    }
    for (int i = 0; i < MINE_CELLS; ++i) {
        if (page->mine[i]) {
            page->adj[i] = 0;
            continue;
        }
        int x = i % MINE_N;
        int y = i / MINE_N;
        uint8_t n = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= MINE_N || ny >= MINE_N) continue;
                n = (uint8_t)(n + page->mine[ny * MINE_N + nx]);
            }
        }
        page->adj[i] = n;
    }
}

static void dig_cell(mini_page_t *page, int start)
{
    if (start < 0 || start >= MINE_CELLS || page->over || page->open[start] || page->flag[start]) return;
    update_elapsed(page);
    if (!page->placed) {
        place_mines(page, start);
        page->placed = true;
        page->timing = true;
        page->last_tick = lv_tick_get();
        if (page->timer) {lv_timer_reset(page->timer); lv_timer_resume(page->timer);}
    }
    if (page->mine[start]) {
        page->open[start] = 1;
        page->over = true;
        page->won = false;
        page->timing = false;
        if (page->timer) lv_timer_pause(page->timer);
        refresh_mines(page);
        return;
    }
    uint8_t queue[MINE_CELLS];
    int head = 0;
    int tail = 0;
    queue[tail++] = (uint8_t)start;
    page->open[start] = 1;
    while (head < tail) {
        int cell = queue[head++];
        if (page->adj[cell] != 0) continue;
        int x = cell % MINE_N;
        int y = cell / MINE_N;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= MINE_N || ny >= MINE_N) continue;
                int next = ny * MINE_N + nx;
                if (page->open[next] || page->flag[next] || page->mine[next]) continue;
                page->open[next] = 1;
                if (tail < MINE_CELLS) queue[tail++] = (uint8_t)next;
            }
        }
    }
    int safe = 0;
    for (int i = 0; i < MINE_CELLS; ++i) if (page->open[i] && !page->mine[i]) ++safe;
    if (safe >= MINE_SAFE) {
        page->over = true;
        page->won = true;
        page->timing = false;
        if (page->timer) lv_timer_pause(page->timer);
    }
    refresh_mines(page);
}

static void toggle_flag(mini_page_t *page, int index)
{
    if (index < 0 || index >= MINE_CELLS || page->over || page->open[index]) return;
    if (page->flag[index]) {
        page->flag[index] = 0;
        if (page->flags) --page->flags;
    } else if (page->flags < MINE_COUNT) {
        page->flag[index] = 1;
        ++page->flags;
    }
    refresh_mines(page);
}

static void mine_event(lv_event_t *event)
{
    mini_page_t *page = active_page;
    if (!page_visible(page) || page->kind != MINI_GAME_MINESWEEPER) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= MINE_CELLS) return;
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_LONG_PRESSED) toggle_flag(page, index);
    else if (code == LV_EVENT_SHORT_CLICKED) {
        if (page->mark_mode) toggle_flag(page, index);
        else dig_cell(page, index);
    }
}

static void mode_event(lv_event_t *event)
{
    (void)event;
    if (!page_visible(active_page) || active_page->kind != MINI_GAME_MINESWEEPER || active_page->over) return;
    active_page->mark_mode = !active_page->mark_mode;
    refresh_mines(active_page);
}

static void reset_mines(mini_page_t *page)
{
    memset(page->mine, 0, sizeof(page->mine));
    memset(page->open, 0, sizeof(page->open));
    memset(page->flag, 0, sizeof(page->flag));
    memset(page->adj, 0, sizeof(page->adj));
    page->placed = false;
    page->over = false;
    page->won = false;
    page->mark_mode = false;
    page->timing = false;
    page->seconds = 0;
    page->elapsed_ms = 0;
    page->last_tick = lv_tick_get();
    if (page->timer) lv_timer_pause(page->timer);
    page->flags = 0;
    refresh_mines(page);
}

static int gomoku_run(const int8_t *board, int x, int y, int dx, int dy, int who)
{
    int n = 0;
    for (int k = 1; k < 5; ++k) {
        int nx = x + dx * k;
        int ny = y + dy * k;
        if (nx < 0 || ny < 0 || nx >= GOMOKU_N || ny >= GOMOKU_N) break;
        if (board[ny * GOMOKU_N + nx] != who) break;
        ++n;
    }
    return n;
}

static int gomoku_winner(const int8_t *board)
{
    static const int dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    for (int y = 0; y < GOMOKU_N; ++y) {
        for (int x = 0; x < GOMOKU_N; ++x) {
            int who = board[y * GOMOKU_N + x];
            if (!who) continue;
            for (int d = 0; d < 4; ++d) {
                int a = gomoku_run(board, x, y, dir[d][0], dir[d][1], who);
                int b = gomoku_run(board, x, y, -dir[d][0], -dir[d][1], who);
                if (a + b + 1 >= 5) return who;
            }
        }
    }
    return 0;
}

static bool gomoku_full(const int8_t *board)
{
    for (int i = 0; i < GOMOKU_CELLS; ++i) if (!board[i]) return false;
    return true;
}

static bool gomoku_wins_at(int8_t *board, int cell, int who)
{
    if (cell < 0 || cell >= GOMOKU_CELLS || board[cell]) return false;
    static const int dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    int x = cell % GOMOKU_N, y = cell / GOMOKU_N;
    for (unsigned d = 0; d < 4; ++d) {
        if (1 + gomoku_run(board, x, y, dir[d][0], dir[d][1], who) +
            gomoku_run(board, x, y, -dir[d][0], -dir[d][1], who) >= 5) return true;
    }
    return false;
}

static int gomoku_score_cell(const int8_t *board, int cell, int who)
{
    int x = cell % GOMOKU_N;
    int y = cell / GOMOKU_N;
    static const int dir[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
    int score = 6 - abs(x - GOMOKU_N / 2) - abs(y - GOMOKU_N / 2);
    for (int d = 0; d < 4; ++d) {
        int a = gomoku_run(board, x, y, dir[d][0], dir[d][1], who);
        int b = gomoku_run(board, x, y, -dir[d][0], -dir[d][1], who);
        int n = a + b;
        if (n >= 3) score += 40;
        else if (n == 2) score += 12;
        else if (n == 1) score += 3;
    }
    return score;
}

static int gomoku_choose(int8_t *board)
{
    for (int i = 0; i < GOMOKU_CELLS; ++i) if (gomoku_wins_at(board, i, 2)) return i;
    for (int i = 0; i < GOMOKU_CELLS; ++i) if (gomoku_wins_at(board, i, 1)) return i;
    int best = -1;
    int best_score = -1000;
    for (int i = 0; i < GOMOKU_CELLS; ++i) {
        if (board[i]) continue;
        int score = gomoku_score_cell(board, i, 2) + gomoku_score_cell(board, i, 1) / 2;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static void refresh_gomoku(mini_page_t *page)
{
    for (int i = 0; i < GOMOKU_CELLS; ++i) {
        lv_obj_t *stone = page->cells[i];
        uint32_t bg = UI_COLOR_PANEL;
        if (page->board[i] == 1) bg = 0x1C2C18;
        else if (page->board[i] == 2) bg = 0xF7F4E8;
        lv_obj_set_style_bg_color(stone, lv_color_hex(bg), 0);
        lv_obj_set_style_bg_opa(stone, page->board[i] ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(stone, lv_color_hex(page->board[i] == 2 ? 0x3A5A2C : 0x4A6A38), 0);
        lv_obj_set_style_border_width(stone, page->board[i] ? 1 : 0, 0);
        lv_obj_set_style_radius(stone, LV_RADIUS_CIRCLE, 0);
        lv_label_set_text(lv_obj_get_child(stone, 0), "");
    }
    int winner = gomoku_winner(page->board);
    if (winner == 1) set_status(page, "你赢了  五连");
    else if (winner == 2) set_status(page, "电脑赢了");
    else if (page->over || gomoku_full(page->board)) set_status(page, "平局");
    else if (page->ai_pending) set_status(page, "电脑思考");
    else set_status(page, "轮到你  黑棋");
}

static void finish_gomoku(mini_page_t *page)
{
    if (gomoku_winner(page->board) || gomoku_full(page->board)) {
        page->over = true;
        page->ai_pending = false;
        drop_timer(&page->ai_timer);
    }
    refresh_gomoku(page);
}

static void ai_fire(lv_timer_t *timer)
{
    mini_page_t *page = active_page;
    if (!page || (timer && page->ai_timer != timer)) return;
    page->ai_timer = NULL;
    page->ai_pending = false;
    if (!page_visible(page) || page->kind != MINI_GAME_GOMOKU || page->over) return;
    int cell = gomoku_choose(page->board);
    if (cell >= 0 && !page->board[cell]) page->board[cell] = 2;
    finish_gomoku(page);
}

static void schedule_ai(mini_page_t *page)
{
    drop_timer(&page->ai_timer);
    page->ai_pending = true;
    refresh_gomoku(page);
    page->ai_timer = lv_timer_create(ai_fire, 400, NULL);
    if (page->ai_timer) lv_timer_set_repeat_count(page->ai_timer, 1);
    else ai_fire(NULL);
}

static void gomoku_event(lv_event_t *event)
{
    mini_page_t *page = active_page;
    if (!page_visible(page) || page->kind != MINI_GAME_GOMOKU) return;
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
    if (page->over || page->ai_pending) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= GOMOKU_CELLS || page->board[index]) return;
    page->board[index] = 1;
    finish_gomoku(page);
    if (!page->over) schedule_ai(page);
}

static void reset_gomoku(mini_page_t *page)
{
    drop_timer(&page->ai_timer);
    page->ai_pending = false;
    page->over = false;
    memset(page->board, 0, sizeof(page->board));
    refresh_gomoku(page);
}

static void refresh_memory(mini_page_t *page)
{
    static const char *names[] = {"1", "2", "3", "4", "5", "6", "7", "8"};
    char text[40];
    if (page->pairs >= MEM_PAIRS) snprintf(text, sizeof(text), "胜利  回合%u  配对%d/8", (unsigned)page->turns, page->pairs);
    else snprintf(text, sizeof(text), "回合%u  配对%d/8", (unsigned)page->turns, page->pairs);
    set_status(page, text);
    for (int i = 0; i < MEM_N; ++i) {
        bool show = page->face[i] != 0;
        const char *face = show ? names[page->deck[i]] : "?";
        lv_obj_t *label = lv_obj_get_child(page->cells[i], 0);
        if (strcmp(lv_label_get_text(label), face) != 0) lv_label_set_text(label, face);
        uint32_t bg = page->face[i] == 2 ? UI_COLOR_SUCCESS : show ? UI_COLOR_SURFACE : UI_COLOR_PANEL;
        lv_obj_set_style_bg_color(page->cells[i], lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(page->face[i] == 2 ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT), 0);
    }
}

static void memory_close(lv_timer_t *timer)
{
    mini_page_t *page = active_page;
    if (!page || page->flip_timer != timer) return;
    page->flip_timer = NULL;
    page->lock = false;
    if (page->kind != MINI_GAME_MEMORY) return;
    if (page->first >= 0 && page->face[page->first] == 1) page->face[page->first] = 0;
    if (page->second >= 0 && page->face[page->second] == 1) page->face[page->second] = 0;
    page->first = -1;
    page->second = -1;
    refresh_memory(page);
}

static void shuffle_memory(mini_page_t *page)
{
    for (int i = 0; i < MEM_N; ++i) page->deck[i] = (uint8_t)(i / 2);
    for (int i = MEM_N - 1; i > 0; --i) {
        int j = (int)lv_rand(0, (uint32_t)i);
        uint8_t swap = page->deck[i];
        page->deck[i] = page->deck[j];
        page->deck[j] = swap;
    }
}

static void reset_memory(mini_page_t *page)
{
    drop_timer(&page->flip_timer);
    shuffle_memory(page);
    memset(page->face, 0, sizeof(page->face));
    page->first = -1;
    page->second = -1;
    page->pairs = 0;
    page->turns = 0;
    page->lock = false;
    page->over = false;
    refresh_memory(page);
}

static void memory_event(lv_event_t *event)
{
    mini_page_t *page = active_page;
    if (!page_visible(page) || page->kind != MINI_GAME_MEMORY) return;
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
    if (page->lock || page->over) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= MEM_N || page->face[index] != 0) return;
    page->face[index] = 1;
    if (page->first < 0) {
        page->first = (int8_t)index;
        refresh_memory(page);
        return;
    }
    if (page->first == index) return;
    page->second = (int8_t)index;
    ++page->turns;
    if (page->deck[page->first] == page->deck[index]) {
        page->face[page->first] = 2;
        page->face[index] = 2;
        page->first = -1;
        page->second = -1;
        ++page->pairs;
        if (page->pairs >= MEM_PAIRS) page->over = true;
    } else {
        page->lock = true;
        drop_timer(&page->flip_timer);
        page->flip_timer = lv_timer_create(memory_close, 650, NULL);
        if (page->flip_timer) lv_timer_set_repeat_count(page->flip_timer, 1);
        else memory_close(NULL);
    }
    refresh_memory(page);
}

static const uint32_t TILE_COLOR[TILE_KINDS] = {0x7CB342, 0x43A047, 0x8D6E63, 0x26A69A, 0x9CCC65, 0x5D9C59};
static const char *TILE_MARK[TILE_KINDS] = {"O", "+", "#", "=", "/", "*"};

static bool tile_is_reward(const mini_page_t *page, int index)
{
    return page->tile_kind[index] == TILE_REWARD;
}

static bool tile_overlap(const mini_page_t *page, int a, int b)
{
    return abs(page->tile_x[a] - page->tile_x[b]) < TILE_W && abs(page->tile_y[a] - page->tile_y[b]) < TILE_H;
}

static bool tile_is_free(const mini_page_t *page, int index)
{
    if (!page->tile_alive[index]) return false;
    for (int i = 0; i < TILE_N; ++i) {
        if (i == index || !page->tile_alive[i]) continue;
        if (page->tile_layer[i] > page->tile_layer[index] && tile_overlap(page, i, index)) return false;
    }
    return true;
}

/* Origins stay inside the board. Right edge is x+56 <= 464, bottom is y+52 < 418. */
#define TILE_X0 16
#define TILE_X1 408
#define TILE_Y0 118
#define TILE_Y1 352

static bool tile_free_alive(const mini_page_t *page, const uint8_t *alive, int index);

static bool tile_near(int x, int y, int ox, int oy)
{
    int dx = x - ox;
    int dy = y - oy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx < 12 && dy < 10;
}

static void tile_layout(mini_page_t *page)
{
    /* Depth equals index. refresh_tiles paints 0..99 and raises each object,
       so a higher index is both drawn later and the only one allowed to block. */
    const int span_x = TILE_X1 - TILE_X0;
    const int span_y = TILE_Y1 - TILE_Y0;
    for (int round = 0; round < 6; ++round) {
        for (int i = 0; i < TILE_N; ++i) page->tile_layer[i] = (uint8_t)i;
        for (int i = 0; i < TILE_N; ++i) {
            const bool cap = i >= TILE_N - 16;
            bool placed = false;
            for (int attempt = 0; attempt < 48 && !placed; ++attempt) {
                int x = TILE_X0 + (int)lv_rand(0, (uint32_t)span_x);
                int y = TILE_Y0 + (int)lv_rand(0, (uint32_t)span_y);
                bool ok = true;
                for (int j = 0; j < i && ok; ++j) {
                    int dx = x - page->tile_x[j];
                    int dy = y - page->tile_y[j];
                    if (dx < 0) dx = -dx;
                    if (dy < 0) dy = -dy;
                    if (tile_near(x, y, page->tile_x[j], page->tile_y[j])) ok = false;
                    else if (cap && j >= TILE_N - 16 && dx < TILE_W && dy < TILE_H) ok = false;
                }
                if (!ok) continue;
                page->tile_x[i] = (int16_t)x;
                page->tile_y[i] = (int16_t)y;
                placed = true;
            }
            if (placed) continue;
            int best = -1, bx = TILE_X0, by = TILE_Y0;
            for (int gy = 0; gy <= span_y; gy += 18) {
                for (int gx = 0; gx <= span_x; gx += 20) {
                    int x = TILE_X0 + gx;
                    int y = TILE_Y0 + gy;
                    if (x > TILE_X1) x = TILE_X1;
                    if (y > TILE_Y1) y = TILE_Y1;
                    int score = 100000;
                    bool blocked = false;
                    for (int j = 0; j < i; ++j) {
                        int dx = x - page->tile_x[j];
                        int dy = y - page->tile_y[j];
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;
                        int box = dx + dy;
                        if (box < score) score = box;
                        if (tile_near(x, y, page->tile_x[j], page->tile_y[j])) blocked = true;
                        if (cap && j >= TILE_N - 16 && dx < TILE_W && dy < TILE_H) blocked = true;
                    }
                    if (blocked) continue;
                    score += (i * 3 + round) % 7;
                    if (score > best) { best = score; bx = x; by = y; }
                }
            }
            if (best < 0) {
                bx = TILE_X0 + ((i * 37 + round * 13) % (span_x + 1));
                by = TILE_Y0 + ((i * 19 + round * 11) % (span_y + 1));
            }
            page->tile_x[i] = (int16_t)bx;
            page->tile_y[i] = (int16_t)by;
        }
        int free_n = 0, overlaps = 0, minx = TILE_X1, maxx = TILE_X0, miny = TILE_Y1, maxy = TILE_Y0;
        uint8_t alive[TILE_N];
        memset(alive, 1, sizeof(alive));
        for (int i = 0; i < TILE_N; ++i) {
            if (tile_free_alive(page, alive, i)) ++free_n;
            if (page->tile_x[i] < minx) minx = page->tile_x[i];
            if (page->tile_x[i] > maxx) maxx = page->tile_x[i];
            if (page->tile_y[i] < miny) miny = page->tile_y[i];
            if (page->tile_y[i] > maxy) maxy = page->tile_y[i];
        }
        for (int i = 0; i < TILE_N; ++i) {
            for (int j = i + 1; j < TILE_N; ++j) {
                if (tile_overlap(page, i, j)) ++overlaps;
            }
        }
        if (free_n >= 8 && overlaps >= 60 && (maxx - minx) >= 280 && (maxy - miny) >= 140) return;
    }
}

static bool tile_free_alive(const mini_page_t *page, const uint8_t *alive, int index)
{
    if (!alive[index]) return false;
    for (int i = 0; i < TILE_N; ++i) {
        if (i == index || !alive[i]) continue;
        if (page->tile_layer[i] > page->tile_layer[index] && tile_overlap(page, i, index)) return false;
    }
    return true;
}

static void deal_tiles(mini_page_t *page)
{
    tile_layout(page);
    uint8_t alive[TILE_N];
    memset(alive, 1, sizeof(alive));
    /* 33 triples: three kinds get 6 groups (18) and three get 5 groups (15). */
    uint8_t kinds[TILE_KINDS];
    for (int i = 0; i < TILE_KINDS; ++i) kinds[i] = (uint8_t)i;
    for (int i = TILE_KINDS - 1; i > 0; --i) {
        int j = (int)lv_rand(0, i); uint8_t swap = kinds[i]; kinds[i] = kinds[j]; kinds[j] = swap;
    }
    uint8_t group_kind[TILE_NORMAL / 3];
    int groups = 0;
    for (int k = 0; k < TILE_KINDS; ++k) {
        int n = k < 3 ? 6 : 5;
        for (int g = 0; g < n; ++g) group_kind[groups++] = kinds[k];
    }
    for (int i = groups - 1; i > 0; --i) {
        int j = (int)lv_rand(0, i);
        uint8_t swap = group_kind[i]; group_kind[i] = group_kind[j]; group_kind[j] = swap;
    }
    int reward_at = (int)lv_rand(0, TILE_N - 1);
    int normal_i = 0;
    /* Removal order only picks a tile that is free now. Reward is skipped
       when grouping, so every three successive normal tiles match. */
    for (int order = 0; order < TILE_N; ++order) {
        int free_list[TILE_N];
        int nf = 0;
        for (int i = 0; i < TILE_N; ++i) if (tile_free_alive(page, alive, i)) free_list[nf++] = i;
        if (!nf) break;
        int idx = free_list[lv_rand(0, nf - 1)];
        if (order == reward_at) page->tile_kind[idx] = TILE_REWARD;
        else page->tile_kind[idx] = group_kind[normal_i++ / 3];
        page->tile_order[order] = (uint8_t)idx;
        alive[idx] = 0;
    }
    for (int i = 0; i < TILE_N; ++i) page->tile_alive[i] = 1;
    page->tiles_left = TILE_N;
}

static void compact_slot(mini_page_t *page)
{
    int write = 0;
    for (int i = 0; i < page->slot_n; ++i) {
        if (page->slot[i] >= 0) page->slot[write++] = page->slot[i];
    }
    page->slot_n = (uint8_t)write;
    for (int i = write; i < TILE_SLOT; ++i) page->slot[i] = -1;
}

static void clear_three(mini_page_t *page)
{
    for (int kind = 0; kind < TILE_KINDS; ++kind) {
        int hit[3];
        int n = 0;
        for (int i = 0; i < page->slot_n && n < 3; ++i) {
            int t = page->slot[i];
            if (t >= 0 && page->tile_kind[t] == kind) hit[n++] = i;
        }
        if (n == 3) {
            for (int k = 0; k < 3; ++k) page->slot[hit[k]] = -1;
            compact_slot(page);
            return;
        }
    }
}

static void refresh_tiles(mini_page_t *page)
{
    char text[64];
    if (page->won) snprintf(text, sizeof(text), "胜利  全部清空");
    else if (page->over) snprintf(text, sizeof(text), "失败  槽满");
    else snprintf(text, sizeof(text), "剩余%d  槽%d/7", page->tiles_left, page->slot_n);
    set_status(page, text);
    for (int i = 0; i < TILE_N; ++i) {
        lv_obj_t *cell = page->cells[i];
        if (!page->tile_alive[i]) {
            lv_obj_add_flag(cell, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(cell, page->tile_x[i], page->tile_y[i]);
        bool free = tile_is_free(page, i);
        lv_obj_t *mark = lv_obj_get_child(cell, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(cell, LV_OPA_COVER, 0);
        if (tile_is_reward(page, i)) {
            lv_obj_set_style_bg_color(cell, lv_color_hex(free ? 0xE6A817 : 0xD8E0C9), 0);
            lv_obj_set_style_text_font(mark, ui_font(page), 0);
            lv_label_set_text(mark, "奖");
        } else {
            uint8_t kind = page->tile_kind[i];
            lv_obj_set_style_bg_color(cell, lv_color_hex(free ? TILE_COLOR[kind] : 0xD8E0C9), 0);
            lv_obj_set_style_text_font(mark, &lv_font_montserrat_20, 0);
            lv_label_set_text(mark, TILE_MARK[kind]);
        }
        lv_obj_set_style_text_color(mark, lv_color_hex(free ? UI_COLOR_ON_ACCENT : UI_COLOR_TEXT_MUTED), 0);
        lv_obj_move_foreground(cell);
    }
    for (int i = 0; i < TILE_SLOT; ++i) {
        if (i < page->slot_n && page->slot[i] >= 0) {
            uint8_t kind = page->tile_kind[page->slot[i]];
            lv_label_set_text(lv_obj_get_child(page->slots[i], 0), TILE_MARK[kind]);
            lv_obj_set_style_bg_color(page->slots[i], lv_color_hex(TILE_COLOR[kind]), 0);
        } else {
            lv_label_set_text(lv_obj_get_child(page->slots[i], 0), "");
            lv_obj_set_style_bg_color(page->slots[i], lv_color_hex(UI_COLOR_SURFACE), 0);
        }
    }
}

static void reset_tiles(mini_page_t *page)
{
    page->over = false;
    page->won = false;
    page->slot_n = 0;
    for (int i = 0; i < TILE_SLOT; ++i) page->slot[i] = -1;
    deal_tiles(page);
    refresh_tiles(page);
}

static void tile_event(lv_event_t *event)
{
    mini_page_t *page = active_page;
    if (!page_visible(page) || page->kind != MINI_GAME_TILE_MATCH) return;
    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED) return;
    if (page->over) return;
    int index = (int)(uintptr_t)lv_event_get_user_data(event);
    if (index < 0 || index >= TILE_N || !tile_is_free(page, index)) return;
    if (tile_is_reward(page, index)) {
        page->tile_alive[index] = 0;
        if (page->tiles_left) --page->tiles_left;
        if (page->tiles_left == 0 && page->slot_n == 0) {
            page->over = true;
            page->won = true;
        }
        refresh_tiles(page);
        return;
    }
    if (page->slot_n >= TILE_SLOT) return;
    page->tile_alive[index] = 0;
    page->slot[page->slot_n++] = (int8_t)index;
    --page->tiles_left;
    clear_three(page);
    if (page->tiles_left == 0 && page->slot_n == 0) {
        page->over = true;
        page->won = true;
    } else if (page->slot_n >= TILE_SLOT) {
        page->over = true;
        page->won = false;
    }
    refresh_tiles(page);
}

static void restart_event(lv_event_t *event)
{
    (void)event;
    mini_page_t *page = active_page;
    if (!page_visible(page)) return;
    if (page->kind == MINI_GAME_MINESWEEPER) reset_mines(page);
    else if (page->kind == MINI_GAME_GOMOKU) reset_gomoku(page);
    else if (page->kind == MINI_GAME_MEMORY) reset_memory(page);
    else reset_tiles(page);
}

static void clock_event(lv_timer_t *timer)
{
    (void)timer;
    mini_page_t *page = active_page;
    if (!page_visible(page) || page->kind != MINI_GAME_MINESWEEPER || !page->timing || page->over) return;
    uint32_t previous = page->seconds;
    update_elapsed(page);
    if (page->seconds != previous) mines_status(page);
}

static void build_mines(mini_page_t *page)
{
    const lv_font_t *font = ui_font(page);
    page->title = make_label(page->screen, 28, 8, 300, "扫雷", font);
    page->hint = make_label(page->screen, 28, 36, 424, "点按翻开，长按或点标记插旗", font);
    page->status = make_label(page->screen, 28, 64, 280, "", font);
    page->restart = make_button(page->screen, 360, 58, 92, 34, "重开", font, restart_event, NULL, LV_EVENT_SHORT_CLICKED);
    page->mode = make_button(page->screen, 28, 96, 92, 34, "标记", font, mode_event, NULL, LV_EVENT_SHORT_CLICKED);
    const int pitch = 40;
    const int origin_x = (480 - pitch * MINE_N) / 2;
    const int origin_y = 138;
    for (int i = 0; i < MINE_CELLS; ++i) {
        int x = origin_x + (i % MINE_N) * pitch;
        int y = origin_y + (i / MINE_N) * pitch;
        page->cells[i] = make_button(page->screen, x, y, 38, 38, "?", &lv_font_montserrat_20, mine_event,
                                     (void *)(uintptr_t)i, LV_EVENT_SHORT_CLICKED);
        lv_obj_add_event_cb(page->cells[i], mine_event, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)i);
    }
    reset_mines(page);
}

static void gomoku_grid(lv_event_t *event)
{
    lv_obj_t *board = lv_event_get_target(event);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_area_t a; lv_obj_get_coords(board, &a);
    lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0x91A47A); line.width = 1;
    for (int i = 0; i < GOMOKU_N; ++i) {
        lv_point_t p = {a.x1 + 25 + i * 32, a.y1 + 25};
        lv_point_t q = {p.x, a.y1 + 345}; lv_draw_line(ctx, &line, &p, &q);
        p.x = a.x1 + 25; p.y = a.y1 + 25 + i * 32;
        q.x = a.x1 + 345; q.y = p.y; lv_draw_line(ctx, &line, &p, &q);
    }
    static const uint8_t stars[5][2] = {{2,2},{8,2},{5,5},{2,8},{8,8}};
    lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot);
    dot.bg_color = line.color; dot.radius = LV_RADIUS_CIRCLE; dot.bg_opa = LV_OPA_COVER;
    for (unsigned i = 0; i < 5; ++i) {
        int x = a.x1 + 25 + stars[i][0] * 32, y = a.y1 + 25 + stars[i][1] * 32;
        lv_area_t point = {x - 2, y - 2, x + 2, y + 2}; lv_draw_rect(ctx, &dot, &point);
    }
}

static void build_gomoku(mini_page_t *page)
{
    const lv_font_t *font = ui_font(page);
    page->title = make_label(page->screen, 28, 8, 300, "五子棋", font);
    page->hint = make_label(page->screen, 28, 36, 424, "黑棋先手，四方向五连获胜", font);
    page->status = make_label(page->screen, 28, 64, 280, "", font);
    page->restart = make_button(page->screen, 360, 58, 92, 34, "重开", font, restart_event, NULL, LV_EVENT_SHORT_CLICKED);
    const int pitch = 32;
    const int origin_x = (480 - pitch * GOMOKU_N) / 2;
    const int origin_y = 108;
    lv_obj_t *grid = lv_obj_create(page->screen);
    lv_obj_set_pos(grid, origin_x - 10, origin_y - 10); lv_obj_set_size(grid, 370, 370);
    lv_obj_set_style_bg_color(grid, lv_color_hex(0xEDF1DC), 0);
    lv_obj_set_style_border_color(grid, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(grid, 1, 0); lv_obj_set_style_radius(grid, 14, 0);
    lv_obj_set_style_pad_all(grid, 0, 0); lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(grid, gomoku_grid, LV_EVENT_DRAW_MAIN, NULL);
    for (int i = 0; i < GOMOKU_CELLS; ++i) {
        int x = origin_x + (i % GOMOKU_N) * pitch;
        int y = origin_y + (i / GOMOKU_N) * pitch;
        page->cells[i] = make_button(page->screen, x, y, 30, 30, "", &lv_font_montserrat_14, gomoku_event,
                                     (void *)(uintptr_t)i, LV_EVENT_SHORT_CLICKED);
        lv_obj_set_style_radius(page->cells[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(page->cells[i], 0, 0);
    }
    reset_gomoku(page);
}

static void build_memory(mini_page_t *page)
{
    const lv_font_t *font = ui_font(page);
    page->title = make_label(page->screen, 28, 8, 300, "记忆翻牌", font);
    page->hint = make_label(page->screen, 28, 36, 424, "一次翻两张，相同保持，不同收起", font);
    page->status = make_label(page->screen, 28, 68, 280, "", font);
    page->restart = make_button(page->screen, 360, 60, 92, 36, "重开", font, restart_event, NULL, LV_EVENT_SHORT_CLICKED);
    const int pitch = 78;
    const int origin_x = (480 - pitch * 4) / 2;
    const int origin_y = 118;
    for (int i = 0; i < MEM_N; ++i) {
        int x = origin_x + (i % 4) * pitch;
        int y = origin_y + (i / 4) * pitch;
        page->cells[i] = make_button(page->screen, x, y, 72, 72, "?", &lv_font_montserrat_32, memory_event,
                                     (void *)(uintptr_t)i, LV_EVENT_SHORT_CLICKED);
    }
    reset_memory(page);
}

static void build_tiles(mini_page_t *page)
{
    const lv_font_t *font = ui_font(page);
    page->title = make_label(page->screen, 24, 6, 320, "叠牌三消", font);
    page->hint = make_label(page->screen, 24, 30, 432, "点未遮挡的牌入7格，三张相同消除。金色奖牌直接收走，不占槽。", font);
    page->status = make_label(page->screen, 24, 84, 280, "", font);
    page->restart = make_button(page->screen, 360, 78, 92, 34, "重开", font, restart_event, NULL, LV_EVENT_SHORT_CLICKED);
    for (int i = 0; i < TILE_N; ++i) {
        page->cells[i] = make_button(page->screen, 0, 0, 56, 52, "O", &lv_font_montserrat_20, tile_event,
                                     (void *)(uintptr_t)i, LV_EVENT_SHORT_CLICKED);
        lv_obj_set_style_radius(page->cells[i], 10, 0);
    }
    for (int i = 0; i < TILE_SLOT; ++i) {
        page->slots[i] = make_button(page->screen, 16 + i * 66, 418, 60, 48, "", &lv_font_montserrat_20, NULL, NULL, LV_EVENT_SHORT_CLICKED);
        lv_obj_clear_flag(page->slots[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(page->slots[i], 8, 0);
    }
    reset_tiles(page);
}

void mini_games_open(lv_obj_t *return_screen, mini_game_kind_t kind, const lv_font_t *font)
{
    if (!return_screen || !lv_obj_is_valid(return_screen)) return;
    if ((unsigned)kind > MINI_GAME_TILE_MATCH) return;
    if (active_page) {
        if (active_page->kind == kind) {
            ui_screen_open(active_page->screen);
            return;
        }
        close_page(active_page);
    }
    mini_page_t *page = lv_mem_alloc(sizeof(*page));
    if (!page) return;
    memset(page, 0, sizeof(*page));
    active_page = page;
    page->return_screen = return_screen;
    page->kind = kind;
    page->font = font;
    page->first = -1;
    page->second = -1;
    page->screen = lv_obj_create(NULL);
    ui_theme_apply_background(page->screen);
    lv_obj_set_style_pad_all(page->screen, 0, 0);
    lv_obj_set_style_border_width(page->screen, 0, 0);
    lv_obj_clear_flag(page->screen, LV_OBJ_FLAG_SCROLLABLE);
    if (kind == MINI_GAME_MINESWEEPER) build_mines(page);
    else if (kind == MINI_GAME_GOMOKU) build_gomoku(page);
    else if (kind == MINI_GAME_MEMORY) build_memory(page);
    else build_tiles(page);
    if (kind == MINI_GAME_MINESWEEPER) {
        page->timer = lv_timer_create(clock_event, 200, NULL);
        if (page->timer) lv_timer_pause(page->timer);
    }
    lv_obj_add_event_cb(page->screen, screen_event, LV_EVENT_ALL, page);
    lv_obj_add_event_cb(page->screen, page_delete, LV_EVENT_DELETE, page);
    edge_back_bind(page->screen, exit_page, NULL);
    ui_screen_open(page->screen);
}
