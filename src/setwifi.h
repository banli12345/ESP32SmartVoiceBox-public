#ifndef SETWIFI_H
#define SETWIFI_H

#include <Arduino.h>

// 全局变量声明
extern char* weather;
extern char* temp;
extern char* updateTime;

// WiFi配置
extern const char* WIFI_SSID;
extern const char* WIFI_PASSWORD;

// 天气API配置
extern const char* WEATHER_API_KEY;
extern char WEATHER_CITY[32];
extern char WEATHER_CITY_DISPLAY[32];

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
#ifdef __cplusplus
}
#endif

// 供 C 文件调用的函数（C 链接）
#ifdef __cplusplus
extern "C" {
#endif
bool updateWeatherCity(const char* newCity);
bool fetchWeatherForecast(void);
bool syncNTPTime(void);
#ifdef __cplusplus
}
#endif

// C++ functions
String getBeijingTime();
bool connectWiFi();
void wifi_service_init(void);
void wifi_service_tick(void);
bool wifi_service_start_connect(const char *ssid, const char *password);
bool fetchWeatherData();
void cleanupWeatherData();
void saveWiFiCredentials(const char* ssid, const char* password);

// Offline cache: restore the last known clock/weather before networking, then
// overwrite it after a successful NTP/weather update.
bool loadCachedTime();
void saveCurrentTimeCache();
bool loadCachedWeather();
void saveCachedWeather();

#endif

