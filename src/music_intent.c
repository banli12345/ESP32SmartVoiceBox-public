#include "music_intent.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void trim(char *s)
{
    while (s[0] && isspace((unsigned char)s[0])) memmove(s, s + 1, strlen(s));
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static void remove_prefix(char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) == 0) { memmove(s, s + n, strlen(s + n) + 1); trim(s); }
}

music_intent_t music_intent_parse(const char *text)
{
    music_intent_t out = {0};
    out.index = -1;
    if (!text || !*text || strlen(text) >= sizeof(out.query)) return out;
    snprintf(out.query, sizeof(out.query), "%s", text);
    trim(out.query);
    out.force_online = strstr(text, "网易云") != NULL || strstr(text, "在线") != NULL || strstr(text, "联网") != NULL;
    const char *stop[] = {"停止", "停止播放", "停止音乐", "关闭音乐", "停止歌曲", "停止在线音乐", NULL};
    const char *pause[] = {"暂停", "暂停播放", "暂停音乐", "暂停歌曲", NULL};
    const char *resume[] = {"继续", "继续播放", "继续音乐", "恢复播放", NULL};
    for (int i = 0; stop[i]; ++i) if (!strcmp(out.query, stop[i])) { out.kind = MUSIC_INTENT_STOP; return out; }
    for (int i = 0; pause[i]; ++i) if (!strcmp(out.query, pause[i])) { out.kind = MUSIC_INTENT_PAUSE; return out; }
    for (int i = 0; resume[i]; ++i) if (!strcmp(out.query, resume[i])) { out.kind = MUSIC_INTENT_RESUME; return out; }
    if (!strcmp(out.query, "下一首") || !strcmp(out.query, "下一曲")) { out.kind = MUSIC_INTENT_NEXT; return out; }
    if (!strcmp(out.query, "上一首") || !strcmp(out.query, "上一曲")) { out.kind = MUSIC_INTENT_PREVIOUS; return out; }

    remove_prefix(out.query, "请");
    remove_prefix(out.query, "帮我");
    remove_prefix(out.query, "在线");
    remove_prefix(out.query, "联网");
    remove_prefix(out.query, "在");
    remove_prefix(out.query, "网易云音乐");
    remove_prefix(out.query, "网易云");
    remove_prefix(out.query, "在线");
    remove_prefix(out.query, "联网");
    const char *verbs[] = {"搜索歌曲", "搜索音乐", "搜歌曲", "搜音乐", "搜歌", "搜索", "播放歌曲", "播放音乐", "播放一首", "播放", "我想听", "想听", "听一首", "来一首", NULL};
    for (int i = 0; verbs[i]; ++i) {
        if (strncmp(out.query, verbs[i], strlen(verbs[i])) == 0) {
            out.kind = i < 6 ? MUSIC_INTENT_SEARCH : MUSIC_INTENT_PLAY;
            remove_prefix(out.query, verbs[i]);
            break;
        }
    }
    if (!out.kind) return out;
    char *suffix = strstr(out.query, "并播放");
    if (suffix && suffix[strlen("并播放")] == 0) { *suffix = 0; out.kind = MUSIC_INTENT_PLAY; }
    remove_prefix(out.query, "一下");
    remove_prefix(out.query, "一首");
    trim(out.query);
    const size_t n = strlen(out.query);
    if (n >= strlen("《》") && !strncmp(out.query, "《", strlen("《")) &&
        !strcmp(out.query + n - strlen("》"), "》")) {
        out.query[n - strlen("》")] = 0;
        memmove(out.query, out.query + strlen("《"), strlen(out.query + strlen("《")) + 1);
    }
    if (out.kind == MUSIC_INTENT_PLAY) {
        if (!strcmp(out.query, "下一首")) { out.kind = MUSIC_INTENT_NEXT; return out; }
        if (!strcmp(out.query, "上一首")) { out.kind = MUSIC_INTENT_PREVIOUS; return out; }
        const char *ordinals[] = {"第一首", "第二首", "第三首", "第四首", "第五首", "第六首", "第七首", "第八首", "第九首", "第十首", NULL};
        for (int i = 0; ordinals[i]; ++i) if (!strcmp(out.query, ordinals[i])) { out.kind = MUSIC_INTENT_SELECT; out.index = i; return out; }
        if (out.query[0] == 0 || !strcmp(out.query, "音乐") || !strcmp(out.query, "歌曲")) { out.kind = MUSIC_INTENT_NONE; return out; }
        if (!strncmp(out.query, "第", strlen("第"))) {
            char *end = NULL;
            long value = strtol(out.query + strlen("第"), &end, 10);
            if (end && !strcmp(end, "首") && value > 0 && value < 100) { out.kind = MUSIC_INTENT_SELECT; out.index = (int)value - 1; }
        }
    }
    if (!out.query[0]) out.kind = MUSIC_INTENT_NONE;
    return out;
}
