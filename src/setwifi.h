#ifndef SETWIFI_H
#define SETWIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <string>
#endif

extern char *weather;
extern char *temp;
extern char *updateTime;

extern const char *WIFI_SSID;
extern const char *WIFI_PASSWORD;

extern char WEATHER_CITY[32];
/* Short Chinese label. Separate from the 32-byte query id so long county names fit. */
extern char WEATHER_CITY_DISPLAY[96];

#define WEATHER_FORECAST_DAYS 3
#ifdef __cplusplus
extern "C" {
#endif
extern char WEATHER_FORECAST_DATE[WEATHER_FORECAST_DAYS][12];
extern char WEATHER_FORECAST_DAY_TEXT[WEATHER_FORECAST_DAYS][32];
extern char WEATHER_FORECAST_NIGHT_TEXT[WEATHER_FORECAST_DAYS][32];
extern char WEATHER_FORECAST_HIGH[WEATHER_FORECAST_DAYS][8];
extern char WEATHER_FORECAST_LOW[WEATHER_FORECAST_DAYS][8];
extern uint8_t WEATHER_FORECAST_COUNT;

// GUI-thread request: returns acceptance; cached weather stays until a result arrives.
bool updateWeatherCity(const char *newCity);
/* Empty after a successful change. Otherwise a short UI string such as 城市重名，请加省市. */
const char *getWeatherCityError(void);
/* Provider code 0..38 or 99. -1 means the current cache has no code (old NVS). */
int get_weather_code(void);
/* Empty when the last published observation is usable. 403 and network failures differ. */
const char *get_weather_status_message(void);
bool fetchWeatherForecast(void);
bool syncNTPTime(void);

bool get_wifi_connected(void);
// String snapshots below are UI-task-only; radio workers read their own state.
// Fixed buffers avoid GCC TLS reserving these arrays on every FreeRTOS task stack.
const char *get_wifi_connected_ssid(void);
int do_wifi_scan(void);
int get_wifi_scan_count(void);
const char *get_wifi_ssid(int index);
bool do_wifi_connect(const char *ssid, const char *password);
bool do_wifi_connect_saved(const char *ssid);
bool wifi_network_is_saved(const char *ssid);
bool wifi_scan_is_busy(void);
uint32_t wifi_scan_revision(void);
bool wifi_connection_is_busy(void);
const char *wifi_connection_target(void);
bool wifi_scan_network_is_open(int index);
bool wifi_last_connection_failed(void);
const char *get_wifi_status(void);

// Quick-settings radio. Request state is non-blocking; apply/poll does the driver work.
void wifi_driver_lock(void);
void wifi_driver_unlock(void);
void wifi_radio_note_desired(bool enabled);
bool wifi_radio_apply(bool enabled);
bool wifi_radio_set_enabled(bool enabled);
bool wifi_radio_get_enabled(void);
bool wifi_hotspot_apply(bool enabled);
/* Runtime configuration only; HotspotName coordinates persistence/rollback. */
bool wifi_hotspot_apply_credentials(const char *name, const char *password);
bool wifi_hotspot_apply_name(const char *name);
bool wifi_hotspot_set_enabled(bool enabled);
bool wifi_hotspot_get_enabled(void);
bool wifi_hotspot_internet_available(void);
void wifi_hotspot_maintain_locked(void);
void wifi_hotspot_copy_info(char *ssid, size_t ssid_len, char *password, size_t password_len,
                            char *ip, size_t ip_len, uint8_t *clients, bool *internet_available);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
std::string getBeijingTime();
bool connectWiFi();
bool wifi_service_init(void);
// GUI-thread poll; NTP/HTTP/NVS weather work is handled by one background task.
void wifi_service_tick(bool allow_refresh = true);
// AppServices poll: STA join, scan results, and profile persistence.
void wifi_service_poll(void);
bool wifi_service_start_connect(const char *ssid, const char *password);
bool fetchWeatherData();
void cleanupWeatherData();
void saveWiFiCredentials(const char *ssid, const char *password);

// Offline cache: restore the last known clock/weather before networking, then
// overwrite it after a successful NTP/weather update.
bool loadCachedTime();
void saveCurrentTimeCache();
bool loadCachedWeather();
void saveCachedWeather();
#endif

#endif
