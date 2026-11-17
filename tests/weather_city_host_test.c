#include "WeatherCity.h"

#include <stdio.h>
#include <string.h>

static int failed;

static void expect(int cond, const char *label)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", label);
        failed = 1;
    }
}

int main(void)
{
    char query[96];
    char id[16];
    char name[96];
    const char *long_name =
        "\xe7\x94\x98\xe8\x82\x83\xe7\x9c\x81/\xe4\xb8\xb4\xe5\xa4\x8f\xe5\x9b\x9e\xe6\x97\x8f"
        "\xe8\x87\xaa\xe6\xb2\xbb\xe5\xb7\x9e/\xe7\xa7\xaf\xe7\x9f\xb3\xe5\xb1\xb1\xe4\xbf\x9d"
        "\xe5\xae\x89\xe6\x97\x8f\xe4\xb8\x9c\xe4\xb9\xa1\xe6\x97\x8f\xe6\x92\x92\xe6\x8b\x89"
        "\xe6\x97\x8f\xe8\x87\xaa\xe6\xb2\xbb\xe5\x8e\xbf";

    expect(weather_city_check_input("  \xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5  ", query, sizeof query) == 0, "trim");
    expect(strcmp(query, "\xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5") == 0, "trimmed text");
    expect(weather_city_lookup(query, id, sizeof id, name, sizeof name) == 0, "wutongqiao");
    expect(strcmp(id, "WM1Z63UKTHX2") == 0 && strcmp(name, "\xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5") == 0, "wutongqiao id");

    expect(weather_city_lookup("\xe5\x9b\x9b\xe5\xb7\x9d\xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5", id, sizeof id, name, sizeof name) == 0, "province short");
    expect(strcmp(id, "WM1Z63UKTHX2") == 0, "province id");
    expect(weather_city_lookup("\xe5\x9b\x9b\xe5\xb7\x9d/\xe4\xb9\x90\xe5\xb1\xb1/\xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5", id, sizeof id, name, sizeof name) == 0, "slash path");
    expect(strcmp(id, "WM1Z63UKTHX2") == 0, "slash id");

    expect(weather_city_lookup("\xe9\x99\x95\xe8\xa5\xbf\xe8\xa5\xbf\xe5\xae\x89\xe9\x84\xa0\xe9\x82\x91", id, sizeof id, name, sizeof name) == 0, "huyi");
    expect(strcmp(id, "WQJ1ZR8NGKTP") == 0 && strcmp(name, "\xe9\x84\xa0\xe9\x82\x91") == 0, "huyi id");
    expect(weather_city_lookup("\xe5\x8c\x97\xe4\xba\xac\xe5\xb8\x82\xe6\x9c\x9d\xe9\x98\xb3\xe5\x8c\xba", id, sizeof id, name, sizeof name) == 0, "chaoyang full");
    expect(strcmp(id, "WX4G17JWZEK7") == 0 && strcmp(name, "\xe6\x9c\x9d\xe9\x98\xb3") == 0, "chaoyang id");
    expect(weather_city_lookup("\xe5\x8c\x97\xe4\xba\xac", id, sizeof id, name, sizeof name) == 0, "beijing");
    expect(strcmp(id, "WX4FBXXFKE4F") == 0 && strcmp(name, "\xe5\x8c\x97\xe4\xba\xac") == 0, "beijing id");

    expect(weather_city_lookup("\xe6\x9c\x9d\xe9\x98\xb3", id, sizeof id, name, sizeof name) == 1, "ambiguous chaoyang");
    expect(weather_city_lookup("\xe8\xa5\xbf\xe5\xae\x89", id, sizeof id, name, sizeof name) == 1, "ambiguous xian");
    expect(weather_city_lookup("\xe8\xa5\xbf\xe5\xae\x89\xe5\xb8\x82", id, sizeof id, name, sizeof name) == 0, "xian city");
    expect(strcmp(id, "WQJ6YY8MHZP0") == 0, "xian id");

    expect(weather_city_check_input(long_name, query, sizeof query) == 0, "long input");
    expect(weather_city_lookup(query, id, sizeof id, name, sizeof name) == 0, "long match");
    expect(strcmp(id, "WQ35K6M0DDYK") == 0 && strcmp(name, "\xe7\xa7\xaf\xe7\x9f\xb3\xe5\xb1\xb1") == 0, "long label");
    expect(strlen(name) < sizeof name && name[strlen(name)] == '\0', "label terminated");

    expect(weather_city_check_input("\xff", query, sizeof query) == 3, "bad utf8");
    expect(weather_city_check_input("\xe4\xb8", query, sizeof query) == 3, "truncated utf8");
    expect(weather_city_check_input("\xe5\x8c\x97\x01\xe4\xba\xac", query, sizeof query) == 3, "control");
    expect(weather_city_check_input("\xe5\x8c\x97\xe4\xba\xac&x=1", query, sizeof query) == 3, "injection");
    expect(weather_city_check_input("nanning&x=1", query, sizeof query) == 3, "ascii injection");
    expect(weather_city_check_input("   ", query, sizeof query) == 3, "blank");
    expect(weather_city_check_input(NULL, query, sizeof query) == 3, "null");

    expect(weather_city_check_input("  nanning ", query, sizeof query) == 0, "legacy trim");
    expect(weather_city_is_legacy_token(query) && strcmp(query, "nanning") == 0, "legacy pinyin");
    expect(weather_city_lookup("nanning", id, sizeof id, name, sizeof name) == 2, "pinyin not rewritten");
    expect(weather_city_is_legacy_token("Beijing") && weather_city_is_legacy_token("WM1Z63UKTHX2"), "english and id");
    expect(!weather_city_is_legacy_token("\xe5\x8c\x97\xe4\xba\xac"), "chinese is not legacy");
    expect(!weather_city_is_legacy_token("abcdefghijklmnopqrstuvwxyzabcdef"), "31 is the max token");
    expect(weather_city_lookup("\xe4\xb8\x8d\xe5\xad\x98\xe5\x9c\xa8\xe7\x9a\x84\xe5\x8e\xbf", id, sizeof id, name, sizeof name) == 2, "missing");

    if (failed) return 1;
    printf("WeatherCity PASS: province/full/ambiguous/longest/invalid/legacy\n");
    return 0;
}
