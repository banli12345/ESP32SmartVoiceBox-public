#include "AlarmIntent.h"

#include <ctype.h>
#include <string.h>

static alarm_intent_t blank(alarm_intent_action_t action) {
    alarm_intent_t out;
    memset(&out, 0, sizeof out);
    out.action = action;
    out.day_offset = -1;
    return out;
}

static int utf8_len(const char *s) {
    const unsigned char c = (unsigned char)s[0];
    int n = 1;
    if (c < 0x80) n = 1;
    else if ((c & 0xE0) == 0xC0) n = 2;
    else if ((c & 0xF0) == 0xE0) n = 3;
    else if ((c & 0xF8) == 0xF0) n = 4;
    else return -1;
    for (int i = 1; i < n; ++i) {
        if ((s[i] & 0xC0) != 0x80) return -1;
    }
    return n;
}

static int starts(const char *s, const char *lit) {
    size_t n = strlen(lit);
    return strncmp(s, lit, n) == 0 ? (int)n : 0;
}

static int eq_span(const char *s, const char *lit) {
    size_t n = strlen(lit);
    return strncmp(s, lit, n) == 0 && s[n] == '\0';
}

/* Copy a bounded UTF-8 command: drop whitespace, fold fullwidth digits and colon. */
static int normalize(const char *text, char *dst, int cap) {
    int w = 0;
    for (const char *p = text; *p;) {
        int n = utf8_len(p);
        if (n < 0) return -1;
        unsigned char c = (unsigned char)p[0];
        int skip = 0;
        char folded = 0;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') skip = 1;
        else if (n == 3 && (unsigned char)p[0] == 0xE3 && (unsigned char)p[1] == 0x80 &&
                 (unsigned char)p[2] == 0x80) skip = 1;
        else if (n == 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBC) {
            unsigned char t = (unsigned char)p[2];
            if (t >= 0x90 && t <= 0x99) folded = (char)('0' + (t - 0x90));
            else if (t == 0x9A) folded = ':';
        }
        if (!skip) {
            if (w + (folded ? 1 : n) >= cap) return -1;
            if (folded) dst[w++] = folded;
            else {
                memcpy(dst + w, p, (size_t)n);
                w += n;
            }
        }
        p += n;
    }
    dst[w] = '\0';
    return w;
}

static int cn_digit(const char *s, int *len) {
    static const char *name[] = {"零", "一", "二", "两", "三", "四", "五", "六", "七", "八", "九"};
    static const int val[] = {0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9};
    for (int i = 0; i < 11; ++i) {
        int n = starts(s, name[i]);
        if (n) {
            *len = n;
            return val[i];
        }
    }
    return -1;
}

/* 0..99: 零, 十, 十一, 二十, 二十三, 三十, 四十五, 六十, 两. */
static int read_cn(const char *s, int *len) {
    int one = 0;
    if (starts(s, "十")) {
        int used = 3;
        int ol = 0;
        int ones = cn_digit(s + 3, &ol);
        if (ones >= 0) {
            *len = 3 + ol;
            return 10 + ones;
        }
        *len = used;
        return 10;
    }
    int dl = 0;
    int d = cn_digit(s, &dl);
    if (d < 0) return -1;
    if (starts(s + dl, "十")) {
        int used = dl + 3;
        int ol = 0;
        int ones = cn_digit(s + used, &ol);
        if (ones >= 0) {
            *len = used + ol;
            return d * 10 + ones;
        }
        *len = used;
        return d * 10;
    }
    (void)one;
    *len = dl;
    return d;
}

static int read_number(const char *s, int max_digits, int *len) {
    *len = 0;
    if ((unsigned char)s[0] < 0x80 && isdigit((unsigned char)s[0])) {
        int v = 0;
        int c = 0;
        while (isdigit((unsigned char)s[c])) {
            if (c >= max_digits) return -1;
            v = v * 10 + (s[c] - '0');
            ++c;
        }
        *len = c;
        return v;
    }
    return read_cn(s, len);
}

static int weekday_at(const char *s, int *bit) {
    int pre = starts(s, "星期");
    if (!pre) pre = starts(s, "礼拜");
    if (!pre) pre = starts(s, "周");
    if (!pre) return 0;
    const char *p = s + pre;
    static const char *nm[] = {"一", "二", "三", "四", "五", "六", "日", "天"};
    static const int bt[] = {0, 1, 2, 3, 4, 5, 6, 6};
    for (int i = 0; i < 8; ++i) {
        int n = starts(p, nm[i]);
        if (n) {
            *bit = bt[i];
            return pre + n;
        }
    }
    return 0;
}

static int skip_day_sep(const char *s) {
    int n = starts(s, "以及");
    if (n) return n;
    n = starts(s, "和");
    if (!n) n = starts(s, "与");
    if (!n) n = starts(s, "跟");
    if (!n) n = starts(s, "及");
    if (!n) n = starts(s, "、");
    if (!n) n = starts(s, "，");
    if (n) return n;
    if (*s == ',' || *s == '/' ) return 1;
    return 0;
}

static int match_weekdays(const char *s, uint8_t *mask, int *bad) {
    const char *p = s;
    int peek = 0;
    if (starts(p, "每")) {
        if (!weekday_at(p + 3, &peek)) return 0;
        p += 3;
    }
    uint8_t m = 0;
    int any = 0;
    for (;;) {
        int bit = 0;
        int n = weekday_at(p, &bit);
        if (!n) break;
        p += n;
        int to = starts(p, "到");
        if (!to) to = starts(p, "至");
        if (to) {
            int bit2 = 0;
            int n2 = weekday_at(p + to, &bit2);
            if (!n2 || bit2 < bit) {
                *bad = 1;
                if (n2) p += to + n2;
                else p += to;
            } else {
                for (int b = bit; b <= bit2; ++b) m = (uint8_t)(m | (uint8_t)(1u << b));
                p += to + n2;
            }
        } else {
            m = (uint8_t)(m | (uint8_t)(1u << bit));
        }
        any = 1;
        int sep = skip_day_sep(p);
        if (sep && weekday_at(p + sep, &bit)) p += sep;
        else break;
    }
    if (!any) return 0;
    *mask = m;
    return (int)(p - s);
}

static int apply_period(int period, int hour, int minute, int *out_h) {
    if (minute < 0 || minute > 59 || hour < 0) return 0;
    if (period == 0) {
        if (hour > 23) return 0;
        *out_h = hour;
        return 1;
    }
    /* 1 上午 2 早上/早晨/清晨 3 中午 4 下午/傍晚 5 晚上 6 凌晨 */
    if (period == 1 || period == 2) {
        if (hour < 1 || hour > 11) return 0;
        *out_h = hour;
        return 1;
    }
    if (period == 3) {
        if (hour != 12) return 0;
        *out_h = 12;
        return 1;
    }
    if (period == 4) {
        if (hour == 12) {
            *out_h = 12;
            return 1;
        }
        if (hour < 1 || hour > 11) return 0;
        *out_h = hour + 12;
        return 1;
    }
    if (period == 5) {
        if (hour == 12) {
            *out_h = 0;
            return 1;
        }
        if (hour < 1 || hour > 11) return 0;
        *out_h = hour + 12;
        return 1;
    }
    if (period == 6) {
        if (hour == 12) {
            *out_h = 0;
            return 1;
        }
        if (hour < 0 || hour > 5) return 0;
        *out_h = hour;
        return 1;
    }
    return 0;
}

static int period_at(const char *s) {
    if (starts(s, "上午")) return 1;
    if (starts(s, "早上") || starts(s, "早晨") || starts(s, "清晨")) return 2;
    if (starts(s, "中午")) return 3;
    if (starts(s, "下午") || starts(s, "傍晚")) return 4;
    if (starts(s, "晚上")) return 5;
    if (starts(s, "凌晨")) return 6;
    return 0;
}

static int period_len(const char *s) {
    if (starts(s, "上午") || starts(s, "早上") || starts(s, "早晨") || starts(s, "清晨") ||
        starts(s, "中午") || starts(s, "下午") || starts(s, "傍晚") || starts(s, "晚上") ||
        starts(s, "凌晨")) return 6;
    return 0;
}

/* Longest clock at s. Returns bytes, or 0. *bad=1 if the clock is illegal. */
static int match_time(const char *s, int *hour, int *minute, int *bad) {
    int per = period_at(s);
    const char *p = s + (per ? period_len(s) : 0);
    const char *base = p;
    int hlen = 0;
    int hour_n = read_number(p, 2, &hlen);
    if (hour_n < 0) return 0;
    p += hlen;
    int ah = 0, am = 0, ok = 0;
    int span = 0;
    if (*p == ':') {
        if (!isdigit((unsigned char)p[1]) || !isdigit((unsigned char)p[2]) ||
            isdigit((unsigned char)p[3])) return 0;
        am = (p[1] - '0') * 10 + (p[2] - '0');
        ok = apply_period(per, hour_n, am, &ah);
        span = (int)((p + 3) - s);
        if (!ok) *bad = 1;
        else {
            *hour = ah;
            *minute = am;
        }
        return span;
    }
    if (!starts(p, "点")) return 0;
    p += 3;
    if (starts(p, "半")) {
        p += 3;
        ok = apply_period(per, hour_n, 30, &ah);
        span = (int)(p - s);
        if (!ok) *bad = 1;
        else {
            *hour = ah;
            *minute = 30;
        }
        return span;
    }
    int mlen = 0;
    const char *after_dian = p;
    int minute_n = read_number(p, 2, &mlen);
    if (minute_n >= 0) {
        const char *q = p + mlen;
        if (starts(q, "分钟")) {
            /* Duration, not "点" + minute-of-hour. Fall through to plain 点 if that stands. */
            minute_n = -1;
        } else {
            if (starts(q, "分")) q += 3;
            ok = apply_period(per, hour_n, minute_n, &ah);
            span = (int)(q - s);
            if (!ok) *bad = 1;
            else {
                *hour = ah;
                *minute = minute_n;
            }
            return span;
        }
    }
    (void)base;
    (void)after_dian;
    ok = apply_period(per, hour_n, 0, &ah);
    span = (int)(after_dian - s);
    if (!ok) *bad = 1;
    else {
        *hour = ah;
        *minute = 0;
    }
    return span;
}

static int match_ring(const char *s, int *minutes, int *bad) {
    static const char *px[] = {"铃声持续", "响铃持续", "持续响铃", "持续响", "响铃", "铃声", "响"};
    int best = 0;
    int best_min = 0;
    int best_bad = 0;
    for (int i = 0; i < 7; ++i) {
        int n = starts(s, px[i]);
        if (!n) continue;
        int nl = 0;
        int v = read_number(s + n, 3, &nl);
        if (v < 0) continue;
        int m = starts(s + n + nl, "分钟");
        if (!m) continue;
        if (starts(s + n + nl + m, "后")) continue;
        int total = n + nl + m;
        if (total > best) {
            best = total;
            best_min = v;
            best_bad = (v < 1 || v > 60);
        }
    }
    if (!best) return 0;
    *minutes = best_min;
    if (best_bad) *bad = 1;
    return best;
}

static int match_delay(const char *s, uint32_t *minutes, int *bad) {
    int nl = 0;
    int v = read_number(s, 5, &nl);
    if (v < 0) return 0;
    int n = starts(s + nl, "分钟后");
    if (!n) return 0;
    const char *p = s + nl + n;
    int of = starts(p, "的");
    if (of) p += of;
    int t = starts(p, "提醒我");
    if (!t) t = starts(p, "叫我");
    if (!t) t = starts(p, "闹钟");
    if (!t) return 0;
    p += t;
    int once = starts(p, "一下");
    if (once) p += once;
    *minutes = (uint32_t)v;
    if (v < 1 || v > 10080) *bad = 1;
    return (int)(p - s);
}

static int filler_at(const char *s) {
    static const char *f[] = {"帮我", "给我", "一下", "一个", "闹钟", "提醒我", "叫我",
                              "请", "的", "吧", "呢", "啊", "呀", "想", "要", "把", "将",
                              "我", "你", "个", "了", "再", "就", "在", "嘛"};
    int best = 0;
    for (int i = 0; i < 25; ++i) {
        int n = starts(s, f[i]);
        if (n > best) best = n;
    }
    if (starts(s, "。") || starts(s, "！") || starts(s, "、") || starts(s, "，")) {
        int n = 3;
        if (starts(s, "。") || starts(s, "！") || starts(s, "、") || starts(s, "，")) n = 3;
        if (n > best) best = n;
    }
    if (*s == ',' || *s == '.' || *s == '!' || *s == ';' || *s == ' ') return 1;
    return best;
}

static int has_lit(const char *s, const char *lit) {
    return strstr(s, lit) != NULL;
}

static int related(const char *s) {
    if (has_lit(s, "闹钟") || has_lit(s, "响铃") || has_lit(s, "铃声") || has_lit(s, "提醒我") ||
        has_lit(s, "叫我") || has_lit(s, "设置") || has_lit(s, "设定") || has_lit(s, "分钟后") ||
        has_lit(s, "关闭") || has_lit(s, "关掉") || has_lit(s, "取消") || has_lit(s, "停止") ||
        has_lit(s, "查看") || has_lit(s, "查询") || has_lit(s, "看看")) return 1;
    return 0;
}

static int full_of(const char *s, const char **tok, int ntok) {
    const char *p = s;
    while (*p) {
        int best = 0;
        for (int i = 0; i < ntok; ++i) {
            int n = starts(p, tok[i]);
            if (n > best) best = n;
        }
        if (!best) return 0;
        p += best;
    }
    return 1;
}

static int is_query(const char *s) {
    if (has_lit(s, "点") || has_lit(s, ":") || has_lit(s, "分钟") || has_lit(s, "设置") ||
        has_lit(s, "设定") || has_lit(s, "关闭") || has_lit(s, "关掉") || has_lit(s, "取消") ||
        has_lit(s, "停止") || has_lit(s, "每天") || has_lit(s, "每周")) return 0;
    int core = has_lit(s, "查看") || has_lit(s, "看看") || has_lit(s, "查询") ||
               has_lit(s, "还有多久") || has_lit(s, "我的闹钟");
    if (!core || !has_lit(s, "闹钟")) return 0;
    static const char *tok[] = {"闹钟还有多久", "还有多久", "我的闹钟", "查看", "看看", "查询",
                                "闹钟", "请", "帮我", "给我", "一下", "的", "吧", "呢", "啊", "呀",
                                "吗", "？", "?", "我", "你"};
    return full_of(s, tok, 21);
}

static int is_disable(const char *s) {
    if (!(has_lit(s, "关闭") || has_lit(s, "关掉") || has_lit(s, "取消")) || !has_lit(s, "闹钟")) return 0;
    if (has_lit(s, "点") || has_lit(s, ":") || has_lit(s, "分钟") || has_lit(s, "设置") ||
        has_lit(s, "每天") || has_lit(s, "周") || has_lit(s, "星期") || has_lit(s, "礼拜") ||
        has_lit(s, "明天") || has_lit(s, "今天") || has_lit(s, "后天") || has_lit(s, "响")) return 0;
    static const char *tok[] = {"关闭", "关掉", "取消", "我的闹钟", "闹钟", "请", "帮我", "给我",
                                "一下", "的", "吧", "呢", "啊", "呀", "把", "将", "我", "你", "了"};
    return full_of(s, tok, 19);
}

static int is_stop(const char *s) {
    int hit = has_lit(s, "停止响铃") || has_lit(s, "停止闹钟") || has_lit(s, "别响了") ||
              has_lit(s, "别再响了");
    if (!hit) return 0;
    if (has_lit(s, "设置") || has_lit(s, "点") || has_lit(s, ":") || has_lit(s, "分钟") ||
        has_lit(s, "每天") || has_lit(s, "明天")) return 0;
    static const char *tok[] = {"别再响了", "别响了", "停止响铃", "停止闹钟", "请", "帮我", "一下",
                                "的", "吧", "呢", "啊", "呀", "我", "你"};
    return full_of(s, tok, 14);
}

static int looks_english_alarm(const char *s) {
    for (const char *p = s; *p; ++p) {
        if ((unsigned char)*p >= 128) return 0;
    }
    return 1;
}

static void lower_copy(const char *s, char *d, int cap) {
    int w = 0;
    int sp = 1;
    for (const char *p = s; *p && w + 1 < cap; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == ' ' || c == '\t') {
            if (!sp && w + 1 < cap) {
                d[w++] = ' ';
                sp = 1;
            }
            continue;
        }
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        d[w++] = (char)c;
        sp = 0;
    }
    if (w && d[w - 1] == ' ') --w;
    d[w] = '\0';
}

static int english_intent(const char *spaced) {
    const char *s = spaced;
    if (starts(s, "please ")) s += 7;
    static const char *query[] = {"query alarm", "query alarms", "query my alarm", "query my alarms",
                                  "query the alarm", "query the alarms", "show alarm", "show alarms",
                                  "show my alarm", "show my alarms", "show the alarm", "list alarm",
                                  "list alarms", "list my alarms", "check alarm", "check alarms",
                                  "view alarm", "view alarms", "my alarm", "my alarms"};
    static const char *disable[] = {"cancel alarm", "cancel alarms", "cancel my alarm", "cancel the alarm",
                                    "disable alarm", "disable alarms", "disable my alarm",
                                    "turn off alarm", "turn off alarms", "turn off my alarm",
                                    "turn off the alarm"};
    static const char *stop[] = {"stop alarm", "stop alarms", "stop the alarm", "stop my alarm",
                                 "stop ringing", "stop the ringing"};
    for (int i = 0; i < 20; ++i) if (eq_span(s, query[i])) return ALARM_INTENT_QUERY;
    for (int i = 0; i < 11; ++i) if (eq_span(s, disable[i])) return ALARM_INTENT_DISABLE;
    for (int i = 0; i < 6; ++i) if (eq_span(s, stop[i])) return ALARM_INTENT_STOP;
    return ALARM_INTENT_NONE;
}

static int verb_at(const char *s) {
    int n = starts(s, "设置");
    if (n) return n;
    n = starts(s, "设定");
    if (n) return n;
    n = starts(s, "设一个");
    if (n) return n;
    n = starts(s, "设个");
    if (n) return n;
    n = starts(s, "定一个");
    if (n) return n;
    n = starts(s, "定个");
    if (n) return n;
    return 0;
}

/* SET_OK, SET_BAD, SET_NO */
enum { SET_NO = 0, SET_OK = 1, SET_BAD = 2 };

static int try_set(const char *s, alarm_intent_t *out) {
    int verb = 0, noun = 0, got_rep = 0, got_time = 0, got_ring = 0, got_delay = 0;
    int explicit_day = 0, bad = 0, op = 0;
    int hour = 0, minute = 0, ring = 0, day_off = -1;
    uint8_t mask = 0;
    uint32_t delay = 0;
    const char *p = s;
    while (*p) {
        int n = 0;
        int bitdump = 0;
        if ((n = starts(p, "大后天"))) {
            bad = 1;
            op = 1;
            p += n;
            continue;
        }
        if ((n = starts(p, "今天")) || (n = starts(p, "明天")) || (n = starts(p, "后天"))) {
            int off = starts(p, "今天") ? 0 : starts(p, "明天") ? 1 : 2;
            if (explicit_day) bad = 1;
            explicit_day = 1;
            day_off = off;
            op = 1;
            p += n;
            continue;
        }
        if ((n = starts(p, "每个工作日")) || (n = starts(p, "工作日")) || (n = starts(p, "每天")) ||
            (n = starts(p, "每日")) || (n = starts(p, "天天"))) {
            if (got_rep) bad = 1;
            got_rep = 1;
            mask = (uint8_t)(starts(p, "工作日") || starts(p, "每个工作日") ? 31 : 127);
            if (starts(p, "每个工作日")) n = starts(p, "每个工作日");
            op = 1;
            p += n;
            continue;
        }
        {
            uint8_t wm = 0;
            int wbad = 0;
            n = match_weekdays(p, &wm, &wbad);
            if (n) {
                if (got_rep || wbad) bad = 1;
                got_rep = 1;
                mask = wm;
                op = 1;
                p += n;
                continue;
            }
        }
        {
            uint32_t dm = 0;
            int db = 0;
            n = match_delay(p, &dm, &db);
            if (n) {
                if (got_delay) bad = 1;
                got_delay = 1;
                delay = dm;
                if (db) bad = 1;
                op = 1;
                p += n;
                continue;
            }
        }
        {
            int rm = 0, rb = 0;
            n = match_ring(p, &rm, &rb);
            if (n) {
                if (got_ring) bad = 1;
                got_ring = 1;
                ring = rm;
                if (rb) bad = 1;
                op = 1;
                p += n;
                continue;
            }
        }
        {
            int hh = 0, mm = 0, tb = 0;
            n = match_time(p, &hh, &mm, &tb);
            if (n) {
                if (got_time) bad = 1;
                got_time = 1;
                hour = hh;
                minute = mm;
                if (tb) bad = 1;
                op = 1;
                p += n;
                continue;
            }
        }
        if ((n = verb_at(p))) {
            verb = 1;
            op = 1;
            p += n;
            continue;
        }
        if ((n = starts(p, "提醒我")) || (n = starts(p, "叫我"))) {
            op = 1;
            p += n;
            continue;
        }
        if ((n = starts(p, "闹钟"))) {
            noun = 1;
            op = 1;
            p += n;
            continue;
        }
        /* A bare period word is not its own token; time owns it. Leftover period is unreliable. */
        if (period_at(p)) return op ? SET_BAD : SET_NO;
        n = filler_at(p);
        if (n) {
            /* Noun/remind already handled above. filler_at also lists them; skip those. */
            if (starts(p, "闹钟") || starts(p, "提醒我") || starts(p, "叫我")) {
                /* unreachable */
            }
            p += n;
            continue;
        }
        (void)bitdump;
        return op ? SET_BAD : SET_NO;
    }
    if (bad) return SET_BAD;
    if (got_delay) {
        /* match_delay already required 提醒我 / 叫我 / 闹钟. */
        if (got_time || got_rep || explicit_day || got_ring) return SET_BAD;
        if (delay < 1 || delay > 10080) return SET_BAD;
        out->action = ALARM_INTENT_SET;
        out->hour = 0;
        out->minute = 0;
        out->weekdays = 0;
        out->ring_minutes = 0;
        out->day_offset = -1;
        out->delay_minutes = delay;
        return SET_OK;
    }
    if (!got_time) {
        if (verb || got_rep || got_ring || explicit_day || noun) return SET_BAD;
        return SET_NO;
    }
    if (explicit_day && got_rep) return SET_BAD;
    if (got_ring && (ring < 1 || ring > 60)) return SET_BAD;
    if (hour > 23 || minute > 59) return SET_BAD;
    int allow = verb || (got_rep && (noun || got_ring));
    if (!allow) {
        if (noun || got_rep || got_ring || verb) return SET_BAD;
        return SET_NO;
    }
    out->action = ALARM_INTENT_SET;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;
    out->weekdays = got_rep ? mask : 0;
    out->ring_minutes = got_ring ? (uint8_t)ring : 0;
    out->day_offset = (int8_t)day_off;
    out->delay_minutes = 0;
    return SET_OK;
}

alarm_intent_t alarm_intent_parse(const char *text) {
    if (!text || !text[0]) return blank(ALARM_INTENT_NONE);
    char raw[512];
    int n = normalize(text, raw, (int)sizeof raw);
    if (n < 0) return blank(ALARM_INTENT_INVALID);
    if (n == 0) return blank(ALARM_INTENT_NONE);

    if (looks_english_alarm(text)) {
        char spaced[512];
        lower_copy(text, spaced, (int)sizeof spaced);
        return blank((alarm_intent_action_t)english_intent(spaced));
    }

    if (has_lit(raw, "怎么") || has_lit(raw, "如何") || has_lit(raw, "怎样") || has_lit(raw, "教我") ||
        has_lit(raw, "为什么") || has_lit(raw, "是什么") || has_lit(raw, "什么是") || has_lit(raw, "故事") ||
        has_lit(raw, "从前") || has_lit(raw, "梦见") || has_lit(raw, "小说") || has_lit(raw, "比如") ||
        has_lit(raw, "例如") || has_lit(raw, "假如") || has_lit(raw, "如果") || has_lit(raw, "要是") ||
        has_lit(raw, "假设")) {
        return blank(ALARM_INTENT_NONE);
    }
    if (has_lit(raw, "吗") || has_lit(raw, "？") || has_lit(raw, "?")) {
        if (is_query(raw)) return blank(ALARM_INTENT_QUERY);
        return blank(ALARM_INTENT_NONE);
    }
    if (has_lit(raw, "不")) {
        return blank(related(raw) ? ALARM_INTENT_INVALID : ALARM_INTENT_NONE);
    }
    if (has_lit(raw, "两个") || has_lit(raw, "三个") || has_lit(raw, "几个") || has_lit(raw, "多个") ||
        has_lit(raw, "两只") || has_lit(raw, "2个") || has_lit(raw, "3个")) {
        return blank(related(raw) ? ALARM_INTENT_INVALID : ALARM_INTENT_NONE);
    }
    if (has_lit(raw, "大约") || has_lit(raw, "大概") || has_lit(raw, "左右") || has_lit(raw, "可能") ||
        has_lit(raw, "或者") || has_lit(raw, "或")) {
        return blank(related(raw) ? ALARM_INTENT_INVALID : ALARM_INTENT_NONE);
    }
    if (has_lit(raw, "别")) {
        if (is_stop(raw)) return blank(ALARM_INTENT_STOP);
        return blank(related(raw) ? ALARM_INTENT_INVALID : ALARM_INTENT_NONE);
    }
    if (is_stop(raw)) return blank(ALARM_INTENT_STOP);
    if (is_disable(raw)) return blank(ALARM_INTENT_DISABLE);
    if (is_query(raw)) return blank(ALARM_INTENT_QUERY);

    alarm_intent_t parsed = blank(ALARM_INTENT_NONE);
    int st = try_set(raw, &parsed);
    if (st == SET_OK) return parsed;
    if (st == SET_BAD) return blank(ALARM_INTENT_INVALID);
    if (has_lit(raw, "分钟后") || has_lit(raw, "设置") || has_lit(raw, "设定") || has_lit(raw, "设个") ||
        has_lit(raw, "设一个") || has_lit(raw, "定个") || has_lit(raw, "定一个") || has_lit(raw, "查看") ||
        has_lit(raw, "关闭") || has_lit(raw, "取消") || has_lit(raw, "停止")) {
        return blank(ALARM_INTENT_INVALID);
    }
    return blank(ALARM_INTENT_NONE);
}
