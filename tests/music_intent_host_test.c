#include "music_intent.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void query(const char *text, music_intent_kind_t kind, const char *expected, int forced)
{
    music_intent_t result = music_intent_parse(text);
    assert(result.kind == kind);
    assert(!strcmp(result.query, expected));
    assert(result.force_online == forced);
}
int main(void)
{
    query("搜歌 茉莉花", MUSIC_INTENT_SEARCH, "茉莉花", 0);
    query("搜索歌曲 茉莉花 贝乐虎", MUSIC_INTENT_SEARCH, "茉莉花 贝乐虎", 0);
    query("搜索《茉莉花》并播放", MUSIC_INTENT_PLAY, "茉莉花", 0);
    query("在网易云音乐播放 茉莉花", MUSIC_INTENT_PLAY, "茉莉花", 1);
    query("请帮我播放《茉莉花》", MUSIC_INTENT_PLAY, "茉莉花", 0);
    query("我想听茉莉花", MUSIC_INTENT_PLAY, "茉莉花", 0);
    query("在线播放茉莉花", MUSIC_INTENT_PLAY, "茉莉花", 1);
    query("请在线搜索茉莉花", MUSIC_INTENT_SEARCH, "茉莉花", 1);
    assert(music_intent_parse("播放下一首").kind == MUSIC_INTENT_NEXT);
    assert(music_intent_parse("播放第2首").index == 1);
    assert(music_intent_parse("播放第二首").kind == MUSIC_INTENT_SELECT);
    assert(music_intent_parse("播放第六首").index == 5);
    assert(music_intent_parse("播放第10首").index == 9);
    assert(music_intent_parse("停止").kind == MUSIC_INTENT_STOP);
    assert(music_intent_parse("暂停播放").kind == MUSIC_INTENT_PAUSE);
    assert(music_intent_parse("继续播放").kind == MUSIC_INTENT_RESUME);
    assert(music_intent_parse("下一首").kind == MUSIC_INTENT_NEXT);
    assert(music_intent_parse("上一首").kind == MUSIC_INTENT_PREVIOUS);
    assert(music_intent_parse("明天北京天气").kind == MUSIC_INTENT_NONE);
    assert(music_intent_parse("你好").kind == MUSIC_INTENT_NONE);
    assert(music_intent_parse("播放音乐").kind == MUSIC_INTENT_NONE);
    assert(music_intent_parse("播放").kind == MUSIC_INTENT_NONE);
    assert(music_intent_parse("搜索 ").kind == MUSIC_INTENT_NONE);
    assert(music_intent_parse(NULL).kind == MUSIC_INTENT_NONE);
    char too_long[300]; memset(too_long, 'x', sizeof(too_long)); too_long[299] = 0;
    assert(music_intent_parse(too_long).kind == MUSIC_INTENT_NONE);
    puts("music intent query/control/boundary checks passed");
    return 0;
}
