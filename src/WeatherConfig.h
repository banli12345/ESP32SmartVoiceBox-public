#pragma once
#include <stdbool.h>

#define WEATHER_DEFAULT_NOW_URL "http://api.seniverse.com/v3/weather/now.json"
#define WEATHER_DEFAULT_DAILY_URL "http://api.seniverse.com/v3/weather/daily.json"
#define WEATHER_DEFAULT_LIFE_URL "https://api.seniverse.com/v3/life/suggestion.json"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char api_key[96];
    char life_api_key[96];
    char now_url[192];
    char daily_url[192];
    char life_url[192];
} weather_api_config_t;
/* Boot only: after NVS and SD mount, before GUI/network workers start. */
void weather_config_init(void);
bool weather_config_get(weather_api_config_t *out);
bool weather_config_is_ready(void);
bool weather_config_imported(void);
const char *weather_config_status(void);
bool weather_config_validate(const weather_api_config_t *config);
/* Persist only. Web changes become active together after reboot. */
bool weather_config_store(const weather_api_config_t *config);
bool weather_config_read_saved(weather_api_config_t *config);
#ifdef __cplusplus
}
#endif
