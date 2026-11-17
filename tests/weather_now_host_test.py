"""Parse production now.json handling, URL encoding, and NVS code cache. No network or key."""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / ".tools" / "weather-city-music-20261117" / "host-now"
SDK = Path("C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON")


def grab(source, name):
    match = re.search(r"^(?:static |extern \"C\" )?[^\n]+?\b" + name + r"\s*\([^;]*\)\s*\{", source, re.M)
    assert match, name
    end = match.end()
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end].replace('extern "C" ', "")


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define WEATHER_FORECAST_DAYS 3
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int nvs_handle_t;
typedef int esp_err_t;
static const char *TAG = "setwifi";
static const char *OFFLINE_CACHE_NAMESPACE = "offline";
#include "WeatherConfig.h"
static bool config_ready=true;
static const char *now_endpoint="http://api.seniverse.com/v3/weather/now.json";
static const char *daily_endpoint="http://api.seniverse.com/v3/weather/daily.json";
bool weather_config_get(weather_api_config_t*out){
 memset(out,0,sizeof(*out));if(!config_ready)return false;
 strcpy(out->api_key,"unit key/a");strcpy(out->now_url,now_endpoint);strcpy(out->daily_url,daily_endpoint);return true;
}
static char WEATHER_CITY[32] = "nanning";
static char WEATHER_CITY_DISPLAY[96] = "old";
static char *weather, *temp, *updateTime;
static char WEATHER_FORECAST_DATE[3][12];
static char WEATHER_FORECAST_DAY_TEXT[3][32];
static char WEATHER_FORECAST_NIGHT_TEXT[3][32];
static char WEATHER_FORECAST_HIGH[3][8];
static char WEATHER_FORECAST_LOW[3][8];
static unsigned char WEATHER_FORECAST_COUNT;
static int s_weather_code = -1;
struct wifi_refresh_result {
    unsigned generation; char city[32]; char city_display[96]; char location[96];
    char weather[96]; char temp[32]; char update_time[48]; char status_message[48];
    int weather_code; int http_status;
    char forecast_date[3][12]; char forecast_day_text[3][32]; char forecast_night_text[3][32];
    char forecast_high[3][8]; char forecast_low[3][8]; unsigned char forecast_count;
    int ntp_ok, weather_ok, forecast_ok;
};
static char seen[12][600];
static int seen_count, http_status = 200;
static const char *http_body = "";
static int nvs_flash_init(void) { return ESP_OK; }
static char keys[32][16];
static char values[32][160];
static int key_count;
static int find_key(const char *key) {
    for (int i = 0; i < key_count; ++i) if (!strcmp(keys[i], key)) return i;
    return -1;
}
static int nvs_open(const char *ns, int mode, nvs_handle_t *out) {
    (void)mode; assert(!strcmp(ns, "offline")); *out = 1; return ESP_OK;
}
static int nvs_set_str(nvs_handle_t, const char *key, const char *value) {
    int slot = find_key(key);
    if (slot < 0) { slot = key_count++; assert(slot < 32); snprintf(keys[slot], 16, "%s", key); }
    assert(strlen(value) < 160);
    snprintf(values[slot], 160, "%s", value);
    return ESP_OK;
}
static unsigned char bytes[8];
static int have_byte[8];
static int nvs_set_u8(nvs_handle_t, const char *key, unsigned char value) {
    assert(!strcmp(key, "fcount")); bytes[0] = value; have_byte[0] = 1; return ESP_OK;
}
static int nvs_get_u8(nvs_handle_t, const char *key, unsigned char *out) {
    (void)key; if (!have_byte[0]) return ESP_ERR_NVS_NOT_FOUND; *out = bytes[0]; return ESP_OK;
}
static int nvs_get_str(nvs_handle_t, const char *key, char *buf, size_t *length) {
    int slot = find_key(key);
    if (slot < 0) return ESP_ERR_NVS_NOT_FOUND;
    size_t need = strlen(values[slot]) + 1;
    if (!buf) { *length = need; return ESP_OK; }
    if (*length < need) return 1;
    memcpy(buf, values[slot], need);
    *length = need;
    return ESP_OK;
}
static int nvs_commit(nvs_handle_t) { return ESP_OK; }
static void nvs_close(nvs_handle_t) {}
static char *http_get_bounded(const char *url, size_t limit, int *status) {
    (void)limit;
    assert(seen_count < 12);
    assert(strstr(url, "/v3/weather/now.json") || strstr(url, "/v3/weather/daily.json") || strstr(url,"https://weather.example.test:8443/custom"));
    assert(!strstr(url, "station"));
    assert(!strstr(url, "unit key"));
    assert(!strstr(url, " "));
    snprintf(seen[seen_count], sizeof seen[0], "%s", url);
    ++seen_count;
    if (status) *status = http_status;
    if (!http_body) return NULL;
    char *copy = (char *)malloc(strlen(http_body) + 1);
    memcpy(copy, http_body, strlen(http_body) + 1);
    return copy;
}
static void cleanupWeatherData(void) { free(weather); free(temp); free(updateTime); weather = temp = updateTime = NULL; }
'''

CASES = r'''
static void reset_nvs(void) { key_count = 0; have_byte[0] = 0; }
int main(void) {
    cJSON *bad_code = cJSON_CreateNumber(1e100);
    assert(weather_code_from_item(bad_code) == -1);cJSON_Delete(bad_code);
    bad_code = cJSON_CreateNumber(0.5);
    assert(weather_code_from_item(bad_code) == -1);cJSON_Delete(bad_code);
    char url[160];
    char tiny[48];
    assert(!weather_build_url(tiny, sizeof tiny, "http://api.seniverse.com/v3/weather/now.json", "k", "abc", "&language=zh-Hans&unit=c"));
    assert(!weather_build_url(url, 80, "http://api.seniverse.com/v3/weather/now.json", "unit key/a", "A B", "&language=zh-Hans&unit=c"));
    assert(url[0] == '\0');
    assert(weather_build_url(url, sizeof url, "http://api.seniverse.com/v3/weather/now.json", "unit key/a", "A/B", "&x=1"));
    assert(strcmp(url, "http://api.seniverse.com/v3/weather/now.json?key=unit%20key%2Fa&location=A%2FB&x=1") == 0);

    wifi_refresh_result out;
    memset(&out, 0, sizeof out);
    http_body = "{\"results\":[{\"location\":{\"name\":\"\\u4e94\\u901a\\u6865\"},\"now\":{\"text\":\"\\u6674\",\"code\":\"0\",\"temperature\":\"26\"},\"last_update\":\"2026-11-17T10:00:00+08:00\"}]}";
    assert(fetch_weather_now_into("WM1Z63UKTHX2", &out));
    assert(out.weather_code == 0 && !strcmp(out.weather, "\xe6\x99\xb4") && !strcmp(out.temp, "26"));
    assert(!strcmp(out.location, "\xe4\xba\x94\xe9\x80\x9a\xe6\xa1\xa5") && out.status_message[0] == '\0');
    assert(strstr(seen[0], "/v3/weather/now.json?key=unit%20key%2Fa&location=WM1Z63UKTHX2"));

    http_body = "{\"results\":[{\"location\":{\"name\":\"x\"},\"now\":{\"text\":\"unknown\",\"code\":\"99\",\"temperature\":\"1\"},\"last_update\":\"t\"}]}";
    seen_count = 0;
    assert(fetch_weather_now_into("nanning", &out) && out.weather_code == 99 && out.status_message[0] == '\0');
    http_body = "{\"results\":[{\"now\":{\"text\":\"rain\",\"code\":38,\"temperature\":7},\"last_update\":\"t\"}]}";
    assert(fetch_weather_now_into("nanning", &out) && out.weather_code == 38 && !strcmp(out.temp, "7"));
    http_body = "{\"results\":[{\"now\":{\"text\":\"rain\",\"temperature\":\"7\"},\"last_update\":\"t\"}]}";
    assert(fetch_weather_now_into("nanning", &out) && out.weather_code == -1);
    http_body = "{\"results\":[{\"now\":{\"text\":\"rain\",\"code\":\"100\",\"temperature\":\"7\"}}]}";
    assert(fetch_weather_now_into("nanning", &out) && out.weather_code == -1);
    http_body = "{\"results\":[{\"now\":{\"text\":\"rain\",\"code\":\"abc\",\"temperature\":\"7\"}}]}";
    assert(fetch_weather_now_into("nanning", &out) && out.weather_code == -1);

    http_status = 403; http_body = "{\"status\":\"AP010006\"}";
    assert(!fetch_weather_now_into("WM1Z63UKTHX2", &out));
    assert(!strcmp(out.status_message, "\xe5\xa4\xa9\xe6\xb0\x94\xe6\x8e\xa5\xe5\x8f\xa3\xe6\x97\xa0\xe6\x9d\x83\xe9\x99\x90"));
    assert(out.weather_code == -1);
    http_status = -1; http_body = NULL;
    assert(!fetch_weather_now_into("nanning", &out));
    assert(!strcmp(out.status_message, "\xe7\xbd\x91\xe7\xbb\x9c\xe5\xa4\xb1\xe8\xb4\xa5"));
    http_status = 500; http_body = "{}";
    assert(!fetch_weather_now_into("nanning", &out));
    assert(!strcmp(out.status_message, "\xe5\xa4\xa9\xe6\xb0\x94\xe8\xaf\xb7\xe6\xb1\x82\xe5\xa4\xb1\xe8\xb4\xa5"));

    http_status = 200;
    http_body = "{\"results\":[{\"daily\":[{\"date\":\"2026-11-17\",\"text_day\":\"\\u6674\",\"text_night\":\"\\u4e91\",\"high\":\"20\",\"low\":\"10\"}]}]}";
    seen_count = 0;
    assert(fetch_forecast_into("A B", &out) && out.forecast_count == 1);
    assert(strstr(seen[0], "/v3/weather/daily.json?key=unit%20key%2Fa&location=A%20B"));
    assert(!strstr(seen[0], "station"));

    now_endpoint="https://weather.example.test:8443/custom-now";
    daily_endpoint="https://weather.example.test:8443/custom-daily";
    seen_count=0;
    http_body="{\"results\":[{\"now\":{\"text\":\"sunny\",\"temperature\":\"26\"}}]}";
    assert(fetch_weather_now_into("nanning",&out));
    assert(strstr(seen[0],"https://weather.example.test:8443/custom-now?key=")==seen[0]);
    http_body="{\"results\":[{\"daily\":[{\"date\":\"2026-11-17\"}]}]}";
    assert(fetch_forecast_into("nanning",&out));
    assert(strstr(seen[1],"https://weather.example.test:8443/custom-daily?key=")==seen[1]);
    config_ready=false;int before=seen_count;
    assert(!fetch_weather_now_into("nanning",&out)&&strstr(out.status_message,"配置"));
    assert(!fetch_forecast_into("nanning",&out)&&seen_count==before);
    config_ready=true;
    reset_nvs();
    strcpy(WEATHER_CITY, "WM1Z63UKTHX2");
    strcpy(WEATHER_CITY_DISPLAY, "\xe7\xa7\xaf\xe7\x9f\xb3\xe5\xb1\xb1");
    weather = strdup("\xe6\x99\xb4"); temp = strdup("26"); updateTime = strdup("t");
    s_weather_code = 99; WEATHER_FORECAST_COUNT = 0;
    saveCachedWeather();
    s_weather_code = 3; strcpy(WEATHER_CITY_DISPLAY, "x"); cleanupWeatherData();
    assert(loadCachedWeather());
    assert(s_weather_code == 99 && !strcmp(WEATHER_CITY, "WM1Z63UKTHX2"));
    assert(!strcmp(WEATHER_CITY_DISPLAY, "\xe7\xa7\xaf\xe7\x9f\xb3\xe5\xb1\xb1") && !strcmp(weather, "\xe6\x99\xb4"));

    int code_slot = -1;
    for (int i = 0; i < key_count; ++i) if (!strcmp(keys[i], "wcode")) code_slot = i;
    assert(code_slot >= 0 && !strcmp(values[code_slot], "99"));
    values[code_slot][0] = '\0';
    s_weather_code = 4;
    assert(loadCachedWeather());
    assert(s_weather_code == -1 && !strcmp(weather, "\xe6\x99\xb4"));
    strcpy(values[code_slot], "0");
    assert(loadCachedWeather() && s_weather_code == 0);
    strcpy(values[code_slot], "39");
    assert(loadCachedWeather() && s_weather_code == -1);
    s_weather_code = 2147483647; saveCachedWeather();
    assert(values[code_slot][0] == '\0');
    cleanupWeatherData();
    printf("Weather now PASS: encoded now/daily URLs, codes 0/38/99/missing, 403/network, old NVS\n");
    return 0;
}
'''


def main():
    cc = shutil.which("cl") or shutil.which("g++") or shutil.which("c++")
    assert cc, "C++ compiler required"
    assert SDK.exists(), "cJSON SDK path missing"
    text = (ROOT / "src" / "setwifi.cpp").read_text(encoding="utf-8")
    assert "station/now" not in text
    names = ["copy_text", "weather_append", "weather_append_pct", "weather_build_url",
             "weather_status_for", "weather_parse_saved_code", "weather_code_from_item",
             "json_copy", "fetch_weather_now_into", "fetch_forecast_into",
             "nvs_ready", "nvs_copy", "nvs_dup", "save_weather_nvs", "saveCachedWeather",
             "loadCachedWeather"]
    parts = [HARNESS]
    for name in names:
        parts.append(grab(text, name))
    OUT.mkdir(parents=True, exist_ok=True)
    unit = OUT / "now.cpp"
    unit.write_text("\n".join(parts) + CASES, encoding="utf-8")
    exe = OUT / "now.exe"
    if Path(cc).stem.lower() == "cl":
        cmd = [cc, "/nologo", "/std:c++17", "/EHsc", "/utf-8", "/I" + str(SDK), "/I"+str(ROOT/"src"),
               str(unit), str(SDK / "cJSON.c"), "/Fe:" + str(exe), "/Fo" + str(OUT) + "\\"]
    else:
        cmd = [cc, "-std=c++17", "-I" + str(SDK), "-I"+str(ROOT/"src"), str(unit), str(SDK / "cJSON.c"), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True, timeout=30)


if __name__ == "__main__":
    main()
