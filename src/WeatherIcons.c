#include "WeatherIcons.h"
#include <string.h>
#include <ctype.h>

const lv_img_dsc_t *weather_icon_for_code(int code)
{
    return &weather_icon_descriptors[code >= 0 && code <= 38 ? code : 39];
}

int weather_icon_legacy_code(const char *condition)
{
    if (!condition || !*condition) return 99;
    /* Only old NVS records lack a code. Prefer the API code for all new data. */
    static const struct { const char *text; int code; } aliases[] = {
        {"雷阵雨伴有冰雹",12},{"强沙尘暴",29},{"特大暴雨",18},{"大暴雨",17},
        {"晴间多云",5},{"大部多云",7},{"雨夹雪",20},{"雷阵雨",11},
        {"阵雨",10},{"小雨",13},{"中雨",14},{"大雨",15},{"暴雨",16},{"冻雨",19},
        {"阵雪",21},{"小雪",22},{"中雪",23},{"大雪",24},{"暴雪",25},
        {"浮尘",26},{"扬沙",27},{"沙尘暴",28},{"龙卷风",36},{"热带风暴",35},
        {"飓风",34},{"大风",33},{"多云",4},{"阴",9},{"雾",30},{"霾",31},
        {"风",32},{"冷",37},{"热",38},{"晴",0},{"雪",22},{"雨",13}
    };
    for (unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);++i)
        if (strstr(condition,aliases[i].text)) return aliases[i].code;
    char english[96];size_t n=0;
    while (condition[n] && n+1<sizeof(english)) {english[n]=(char)tolower((unsigned char)condition[n]);++n;}
    english[n]=0;
    if(strstr(english,"thunder"))return 11;
    if(strstr(english,"snow"))return 22;
    if(strstr(english,"rain"))return 13;
    if(strstr(english,"overcast"))return 9;
    if(strstr(english,"cloud"))return 4;
    if(strstr(english,"sunny")||strstr(english,"clear"))return 0;
    return 99;
}
