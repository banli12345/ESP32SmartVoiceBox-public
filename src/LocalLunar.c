#include "LocalLunar.h"
#include <stdio.h>
#include <string.h>

/* HKO conversion data is compiled into flash. No clock, network or SD access. */
#include "LocalLunarData.inc"

static const char *const months[] = {"", "正月", "二月", "三月", "四月", "五月", "六月", "七月", "八月", "九月", "十月", "冬月", "腊月"};
static const char *const days[] = {"", "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十", "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十", "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"};
static const char *const animals[] = {"鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪"};
static const char *const terms[] = {"小寒", "大寒", "立春", "雨水", "惊蛰", "春分", "清明", "谷雨", "立夏", "小满", "芒种", "夏至", "小暑", "大暑", "立秋", "处暑", "白露", "秋分", "寒露", "霜降", "立冬", "小雪", "大雪", "冬至"};

static int before_year(int year)
{
    --year;
    return year * 365 + year / 4 - year / 100 + year / 400;
}
static int month_days(int year, int month)
{
    static const int count[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    return count[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}
static int ordinal(int year, int month, int day)
{
    int result = before_year(year) - before_year(1901) + day - 1;
    for (int m = 1; m < month; ++m) result += month_days(year, m);
    return result;
}
static void convert(int offset, int *year, int *month, int *day, bool *leap)
{
    size_t lo = 0, hi = sizeof(lunar_month_starts) / sizeof(lunar_month_starts[0]);
    while (lo + 1 < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        if ((int)(lunar_month_starts[mid] & 0x1ffffU) <= offset) lo = mid;
        else hi = mid;
    }
    const uint32_t packed = lunar_month_starts[lo];
    *year = 1900 + ((packed >> 17) & 255U);
    *month = (packed >> 25) & 15U;
    *leap = (packed & (1U << 29)) != 0;
    *day = offset - (int)(packed & 0x1ffffU) + (lo == 0 ? 11 : 1);
}
static void add_festival(life_calendar_day_t *out, const char *name)
{
    if (!name || !name[0]) return;
    const size_t used = strlen(out->festival);
    snprintf(out->festival + used, sizeof(out->festival) - used, "%s%s", used ? " / " : "", name);
}

bool local_lunar_get(int year, int month, int day, life_calendar_day_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (year < 1901 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || day > month_days(year, month)) return false;
    int lunar_year, lunar_month, lunar_day;
    bool leap;
    const int offset = ordinal(year, month, day);
    convert(offset, &lunar_year, &lunar_month, &lunar_day, &leap);
    if (lunar_month < 1 || lunar_month > 12 || lunar_day < 1 || lunar_day > 30) return false;
    snprintf(out->date, sizeof(out->date), "%04d-%02d-%02d", year, month, day);
    snprintf(out->lunar_month, sizeof(out->lunar_month), "%s%s", leap ? "闰" : "", months[lunar_month]);
    snprintf(out->lunar_day, sizeof(out->lunar_day), "%s", days[lunar_day]);
    /* Civil zodiac changes on lunar New Year's Day, rather than Li Chun. */
    snprintf(out->zodiac, sizeof(out->zodiac), "%s", animals[(lunar_year - 4) % 12]);
    for (int i = (month - 1) * 2; i < month * 2; ++i)
        if (solar_term_days[year - 1901][i] == day) snprintf(out->solar_term, sizeof(out->solar_term), "%s", terms[i]);

    switch (month * 100 + day) {
    case 101: add_festival(out, "元旦"); break;
    case 214: add_festival(out, "情人节"); break;
    case 308: add_festival(out, "妇女节"); break;
    case 312: add_festival(out, "植树节"); break;
    case 501: add_festival(out, "劳动节"); break;
    case 504: add_festival(out, "青年节"); break;
    case 601: add_festival(out, "儿童节"); break;
    case 701: add_festival(out, "建党节"); break;
    case 801: add_festival(out, "建军节"); break;
    case 910: add_festival(out, "教师节"); break;
    case 1001: add_festival(out, "国庆节"); break;
    case 1224: add_festival(out, "平安夜"); break;
    case 1225: add_festival(out, "圣诞节"); break;
    }
    const int weekday = (offset + 2) % 7;
    if (month == 5 && weekday == 0 && day >= 8 && day <= 14) add_festival(out, "母亲节");
    if (month == 6 && weekday == 0 && day >= 15 && day <= 21) add_festival(out, "父亲节");
    if (!strcmp(out->solar_term, "清明")) add_festival(out, "清明节");
    if (!leap) {
        switch (lunar_month * 100 + lunar_day) {
        case 101: add_festival(out, "春节"); break;
        case 115: add_festival(out, "元宵节"); break;
        case 202: add_festival(out, "龙抬头"); break;
        case 505: add_festival(out, "端午节"); break;
        case 707: add_festival(out, "七夕"); break;
        case 715: add_festival(out, "中元节"); break;
        case 815: add_festival(out, "中秋节"); break;
        case 909: add_festival(out, "重阳节"); break;
        case 1208: add_festival(out, "腊八节"); break;
        }
        int next_year, next_month, next_day;
        bool next_leap;
        convert(offset + 1, &next_year, &next_month, &next_day, &next_leap);
        if (next_month == 1 && next_day == 1 && !next_leap) add_festival(out, "除夕");
    }
    return true;
}
