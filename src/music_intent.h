#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { MUSIC_INTENT_NONE, MUSIC_INTENT_SEARCH, MUSIC_INTENT_PLAY,
    MUSIC_INTENT_SELECT, MUSIC_INTENT_STOP, MUSIC_INTENT_PAUSE,
    MUSIC_INTENT_RESUME, MUSIC_INTENT_NEXT, MUSIC_INTENT_PREVIOUS } music_intent_kind_t;
typedef struct { music_intent_kind_t kind; int index; bool force_online; char query[256]; } music_intent_t;
music_intent_t music_intent_parse(const char *text);
#ifdef __cplusplus
}
#endif
