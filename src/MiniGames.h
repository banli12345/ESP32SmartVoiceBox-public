#ifndef MINI_GAMES_H
#define MINI_GAMES_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MINI_GAME_MINESWEEPER = 0,
    MINI_GAME_GOMOKU = 1,
    MINI_GAME_MEMORY = 2,
    MINI_GAME_TILE_MATCH = 3
} mini_game_kind_t;

void mini_games_open(lv_obj_t *return_screen, mini_game_kind_t kind, const lv_font_t *font);

#ifdef __cplusplus
}
#endif

#endif
