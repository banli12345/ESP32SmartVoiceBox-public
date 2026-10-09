#include "setwifi.h"
#include "ui/custom/custom.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#if __has_include("private_config.h")
#include "private_config.h"
#endif
#ifndef SMARTVOICEBOX_WEATHER_API_KEY
#define SMARTVOICEBOX_WEATHER_API_KEY ""
#endif

// 配置信息
// Keep credentials in mutable buffers so they can be loaded from NVS at boot.
static char wifi_ssid_storage[33] = {};
static char wifi_password_storage[64] = {};
const char* WIFI_SSID = wifi_ssid_storage;
const char* WIFI_PASSWORD = wifi_password_storage;
const char* WEATHER_API_KEY = SMARTVOICEBOX_WEATHER_API_KEY;
char WEATHER_CITY[32] = "nanning";//城市（拼音，可修改）
char WEATHER_CITY_DISPLAY[32] = "南宁";//UI显示的城市名

extern "C" {
char WEATHER_FORECAST_DATE[WEATHER_FORECAST_DAYS][12] = {};
char WEATHER_FORECAST_DAY_TEXT[WEATHER_FORECAST_DAYS][32] = {};
char WEATHER_FORECAST_NIGHT_TEXT[WEATHER_FORECAST_DAYS][32] = {};
char WEATHER_FORECAST_HIGH[WEATHER_FORECAST_DAYS][8] = {};
char WEATHER_FORECAST_LOW[WEATHER_FORECAST_DAYS][8] = {};
uint8_t WEATHER_FORECAST_COUNT = 0;
}

// Keep the JSON arena out of the 4 KB LVGL task stack. City changes run from
// an LVGL event callback, and a 2 KB local StaticJsonDocument can overflow it.
static StaticJsonDocument<2048> weatherForecastDoc;

// ==================== 拼音→中文城市对照表 ====================
// 即使API失败也能显示中文城市名
typedef struct {
    const char* pinyin;
    const char* chinese;
} CityEntry;

static const CityEntry city_table[] = {
    // 直辖市
    {"beijing",     "北京"},
    {"shanghai",    "上海"},
    {"tianjin",     "天津"},
    {"chongqing",   "重庆"},
    // 省会城市
    {"haerbin",     "哈尔滨"},
    {"changchun",   "长春"},
    {"shenyang",    "沈阳"},
    {"hohhot",      "呼和浩特"},
    {"shijiazhuang","石家庄"},
    {"taiyuan",     "太原"},
    {"jinan",       "济南"},
    {"zhengzhou",   "郑州"},
    {"nanjing",     "南京"},
    {"hefei",       "合肥"},
    {"wuhan",       "武汉"},
    {"hangzhou",    "杭州"},
    {"nanchang",    "南昌"},
    {"fuzhou",      "福州"},
    {"guangzhou",   "广州"},
    {"nanning",     "南宁"},
    {"haikou",      "海口"},
    {"kunming",     "昆明"},
    {"guiyang",     "贵阳"},
    {"chengdu",     "成都"},
    {"changsha",    "长沙"},
    {"xian",        "西安"},
    {"lanzhou",     "兰州"},
    {"xining",      "西宁"},
    {"yinchuan",    "银川"},
    {"wulumuqi",    "乌鲁木齐"},
    {"lasa",        "拉萨"},
    // 常见大城市
    {"shenzhen",    "深圳"},
    {"xiamen",      "厦门"},
    {"qingdao",     "青岛"},
    {"dalian",      "大连"},
    {"suzhou",      "苏州"},
    {"wuxi",        "无锡"},
    {"ningbo",      "宁波"},
    {"dongguan",    "东莞"},
    {"foshan",      "佛山"},
    {"zhuhai",      "珠海"},
    {"guilin",      "桂林"},
    {"sanya",       "三亚"},
    {"lhasa",       "拉萨"},
    // 常见拼写变体
    {"beijing",     "北京"},
    {"harbin",      "哈尔滨"},
    {"urumqi",      "乌鲁木齐"},
    {"urumchi",     "乌鲁木齐"},
    {NULL, NULL}  // 结束标记
};

static const char* pinyin_to_chinese(const char* pinyin) {
    if (!pinyin) return NULL;
    for (int i = 0; city_table[i].pinyin != NULL; i++) {
        if (strcasecmp(pinyin, city_table[i].pinyin) == 0) {
            return city_table[i].chinese;
        }
    }
    return NULL;  // 未找到匹配
}

// NTP 服务器（多个备选，提高可靠性）
const char* ntpServer1 = "ntp.aliyun.com";
const char* ntpServer2 = "ntp.ntsc.ac.cn";
const char* ntpServer3 = "pool.ntp.org";
const long gmtOffset_sec = 8 * 3600;  // UTC+8 北京时间
const int daylightOffset_sec = 0;

// 全局变量
char* weather = nullptr;
char* temp = nullptr;
char* updateTime = nullptr;

static const char* OFFLINE_CACHE_NAMESPACE = "offline";
static const uint64_t MIN_VALID_EPOCH = 1000000000ULL;

WiFiClient client;
HTTPClient http;

static void copyWiFiCredential(char* destination, size_t destinationSize, const char* value) {
  if (!destination || destinationSize == 0) return;
  if (!value) value = "";
  strncpy(destination, value, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

bool loadCachedTime() {
  Preferences preferences;
  if (!preferences.begin(OFFLINE_CACHE_NAMESPACE, true)) return false;

  const uint64_t savedEpoch = preferences.getULong64("epoch", 0);
  preferences.end();
  if (savedEpoch < MIN_VALID_EPOCH) return false;

  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(savedEpoch);
  settimeofday(&tv, nullptr);
  Serial.printf("Loaded cached time: %llu\n",
                static_cast<unsigned long long>(savedEpoch));
  return true;
}

void saveCurrentTimeCache() {
  const time_t now = time(nullptr);
  if (now < static_cast<time_t>(MIN_VALID_EPOCH)) return;

  Preferences preferences;
  if (!preferences.begin(OFFLINE_CACHE_NAMESPACE, false)) return;
  preferences.putULong64("epoch", static_cast<uint64_t>(now));
  preferences.end();
}

static bool loadSavedWiFiCredentials() {
  Preferences preferences;
  if (!preferences.begin("wifi", true)) {
    Serial.println("Unable to open WiFi preferences");
    return false;
  }

  String savedSsid = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();

  if (savedSsid.length() == 0 || savedSsid.length() > 32 || savedPassword.length() > 63) {
    return false;
  }

  copyWiFiCredential(wifi_ssid_storage, sizeof(wifi_ssid_storage), savedSsid.c_str());
  copyWiFiCredential(wifi_password_storage, sizeof(wifi_password_storage), savedPassword.c_str());
  return true;
}

void saveWiFiCredentials(const char* ssid, const char* password) {
  if (!ssid || strlen(ssid) == 0 || strlen(ssid) > 32 ||
      !password || strlen(password) > 63) {
    return;
  }

  // Avoid overlapping copies when the caller passes the currently active buffers.
  if (ssid != wifi_ssid_storage) {
    copyWiFiCredential(wifi_ssid_storage, sizeof(wifi_ssid_storage), ssid);
  }
  if (password != wifi_password_storage) {
    copyWiFiCredential(wifi_password_storage, sizeof(wifi_password_storage), password);
  }

  Preferences preferences;
  if (!preferences.begin("wifi", false)) {
    Serial.println("Unable to open WiFi preferences for writing");
    return;
  }
  preferences.putString("ssid", wifi_ssid_storage);
  preferences.putString("password", wifi_password_storage);
  preferences.end();
  Serial.printf("Saved WiFi credentials for SSID: %s\n", wifi_ssid_storage);
}

static bool waitForNtpTime(struct tm* timeinfo, uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (millis() - started < timeout_ms) {
    // Arduino's default getLocalTime timeout is 5 seconds per call. Use a
    // short poll so retries cannot accidentally turn into a minutes-long wait.
    if (getLocalTime(timeinfo, 100)) return true;
    delay(150);
  }
  return false;
}

bool connectWiFi() {
  const bool loaded = loadSavedWiFiCredentials();
  Serial.print("连接WiFi: ");
  Serial.println(WIFI_SSID);
  Serial.println(loaded ? "Using saved WiFi credentials" : "Using default WiFi credentials");
  
  // Let the Arduino WiFi layer restore the association after a transient AP
  // drop. main.cpp still detects the recovery and refreshes application data.
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();
  const unsigned long timeout = 30000; // 30秒超时
  
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > timeout) {
      Serial.println("\nWiFi连接超时!");
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi连接成功!");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());

  // Keep the credentials used by this successful connection for the next boot.
  saveWiFiCredentials(WIFI_SSID, WIFI_PASSWORD);

  // 配置 NTP 时间（多服务器备选）
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  struct tm tm_buf;
  Serial.print("Waiting for NTP");
  const bool ntp_ok = waitForNtpTime(&tm_buf, 5000);
  if (ntp_ok) {
    Serial.printf("NTP synced: %d-%02d-%02d %02d:%02d:%02d\n",
      tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
      tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    saveCurrentTimeCache();
  } else {
    Serial.println("\nNTP not ready yet; continuing startup");
  }

  return true;
}

// 供外部调用：重新同步时间（换网后调用）
bool syncNTPTime(void) {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  struct tm tm_buf;
  const bool synced = waitForNtpTime(&tm_buf, 5000);
  if (synced) saveCurrentTimeCache();
  return synced;
}

void cleanupWeatherData() {
  if (weather) {
    free(weather);
    weather = nullptr;
  }
  if (temp) {
    free(temp);
    temp = nullptr;
  }
  if (updateTime) {
    free(updateTime);
    updateTime = nullptr;
  }
}

bool loadCachedWeather() {
  Preferences preferences;
  if (!preferences.begin(OFFLINE_CACHE_NAMESPACE, true)) return false;

  String savedCity = preferences.getString("city", "");
  String savedCityDisplay = preferences.getString("city_cn", "");
  String savedWeather = preferences.getString("weather", "");
  String savedTemp = preferences.getString("temp", "");
  String savedUpdateTime = preferences.getString("updated", "");
  const uint8_t savedForecastCount = preferences.getUChar("fcount", 0);
  String savedDates[WEATHER_FORECAST_DAYS];
  String savedDayText[WEATHER_FORECAST_DAYS];
  String savedNightText[WEATHER_FORECAST_DAYS];
  String savedHigh[WEATHER_FORECAST_DAYS];
  String savedLow[WEATHER_FORECAST_DAYS];
  for (uint8_t i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "fdate%u", i);
    savedDates[i] = preferences.getString(key, "");
    snprintf(key, sizeof(key), "fday%u", i);
    savedDayText[i] = preferences.getString(key, "");
    snprintf(key, sizeof(key), "fnight%u", i);
    savedNightText[i] = preferences.getString(key, "");
    snprintf(key, sizeof(key), "fhigh%u", i);
    savedHigh[i] = preferences.getString(key, "");
    snprintf(key, sizeof(key), "flow%u", i);
    savedLow[i] = preferences.getString(key, "");
  }
  preferences.end();

  if (savedCity.length() > 0) {
    copyWiFiCredential(WEATHER_CITY, sizeof(WEATHER_CITY), savedCity.c_str());
  }
  if (savedCityDisplay.length() > 0) {
    copyWiFiCredential(WEATHER_CITY_DISPLAY, sizeof(WEATHER_CITY_DISPLAY),
                       savedCityDisplay.c_str());
  }

  cleanupWeatherData();
  if (savedWeather.length() > 0) weather = strdup(savedWeather.c_str());
  if (savedTemp.length() > 0) temp = strdup(savedTemp.c_str());
  if (savedUpdateTime.length() > 0) updateTime = strdup(savedUpdateTime.c_str());

  WEATHER_FORECAST_COUNT = savedForecastCount > WEATHER_FORECAST_DAYS
                              ? WEATHER_FORECAST_DAYS : savedForecastCount;
  for (uint8_t i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
    snprintf(WEATHER_FORECAST_DATE[i], sizeof(WEATHER_FORECAST_DATE[i]), "%s",
             savedDates[i].c_str());
    snprintf(WEATHER_FORECAST_DAY_TEXT[i], sizeof(WEATHER_FORECAST_DAY_TEXT[i]), "%s",
             savedDayText[i].c_str());
    snprintf(WEATHER_FORECAST_NIGHT_TEXT[i], sizeof(WEATHER_FORECAST_NIGHT_TEXT[i]), "%s",
             savedNightText[i].c_str());
    snprintf(WEATHER_FORECAST_HIGH[i], sizeof(WEATHER_FORECAST_HIGH[i]), "%s",
             savedHigh[i].c_str());
    snprintf(WEATHER_FORECAST_LOW[i], sizeof(WEATHER_FORECAST_LOW[i]), "%s",
             savedLow[i].c_str());
  }

  const bool loaded = weather != nullptr || temp != nullptr || WEATHER_FORECAST_COUNT > 0;
  if (loaded) {
    Serial.printf("Loaded cached weather: %s, %s C\n",
                  weather ? weather : "--", temp ? temp : "--");
  }
  return loaded;
}

void saveCachedWeather() {
  if (!weather && !temp && WEATHER_FORECAST_COUNT == 0) return;

  Preferences preferences;
  if (!preferences.begin(OFFLINE_CACHE_NAMESPACE, false)) return;
  preferences.putString("city", WEATHER_CITY);
  preferences.putString("city_cn", WEATHER_CITY_DISPLAY);
  preferences.putString("weather", weather ? weather : "");
  preferences.putString("temp", temp ? temp : "");
  preferences.putString("updated", updateTime ? updateTime : "");
  preferences.putUChar("fcount", WEATHER_FORECAST_COUNT);
  for (uint8_t i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "fdate%u", i);
    preferences.putString(key, WEATHER_FORECAST_DATE[i]);
    snprintf(key, sizeof(key), "fday%u", i);
    preferences.putString(key, WEATHER_FORECAST_DAY_TEXT[i]);
    snprintf(key, sizeof(key), "fnight%u", i);
    preferences.putString(key, WEATHER_FORECAST_NIGHT_TEXT[i]);
    snprintf(key, sizeof(key), "fhigh%u", i);
    preferences.putString(key, WEATHER_FORECAST_HIGH[i]);
    snprintf(key, sizeof(key), "flow%u", i);
    preferences.putString(key, WEATHER_FORECAST_LOW[i]);
  }
  preferences.end();
  Serial.println("Saved weather cache");
}

bool fetchWeatherData() {
  // 清理旧连接
  client.stop();

  //知心天气HTTP的地址
  String url = "http://api.seniverse.com/v3/weather/now.json?key=";
  url += WEATHER_API_KEY;
  url += "&location=";
  url += WEATHER_CITY;
  url += "&language=zh-Hans&unit=c";  // zh-Hans=中文天气

  if (!http.begin(client, url)) {
    Serial.println("无法初始化HTTP连接");
    return false;
  }

  int httpCode = http.GET();
  bool success = false;

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("获取到天气数据:");

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
      Serial.print("JSON解析失败: ");
      Serial.println(error.f_str());
    } else {
      JsonObject result = doc["results"][0];
      JsonObject now = result["now"];
      
      // 清理旧数据
      cleanupWeatherData();
      
      // 获取天气数据
      const char* locationText = result["location"]["name"];
      const char* weatherText = now["text"];
      const char* tempText = now["temperature"];
      const char* updateTimeText = result["last_update"];

      /* 只有当本地对照表没匹配(显示名仍是拼音)时才用API的城市名覆盖 */
      if (locationText && strcmp(WEATHER_CITY_DISPLAY, WEATHER_CITY) == 0) {
        strncpy(WEATHER_CITY_DISPLAY, locationText, sizeof(WEATHER_CITY_DISPLAY) - 1);
        WEATHER_CITY_DISPLAY[sizeof(WEATHER_CITY_DISPLAY) - 1] = '\0';
      }
      
      if (weatherText) weather = strdup(weatherText);
      if (tempText) temp = strdup(tempText);
      if (updateTimeText) updateTime = strdup(updateTimeText);
      
      // 检查内存分配
      if ((weatherText && !weather) || (tempText && !temp) || (updateTimeText && !updateTime)) {
        Serial.println("内存分配失败!");
        cleanupWeatherData();
      } else {
        success = true;
        
        Serial.println("\n===== 当前天气 =====");
        Serial.print("城市: ");
        Serial.print("天气状况: ");
        Serial.println(weather ? weather : "N/A");
        Serial.print("温度: ");
        Serial.println(temp ? temp : "N/A");
        Serial.print("更新时间: ");
        Serial.println(updateTime ? updateTime : "N/A");
        Serial.print("北京时间: ");
        Serial.println(getBeijingTime());
        Serial.println("====================");
      }
    }
  } else {
    Serial.print("HTTP请求失败，错误代码: ");
    Serial.println(httpCode);
  }

  http.end();
  return success;
}

bool fetchWeatherForecast() {
  WEATHER_FORECAST_COUNT = 0;
  memset(WEATHER_FORECAST_DATE, 0, sizeof(WEATHER_FORECAST_DATE));
  memset(WEATHER_FORECAST_DAY_TEXT, 0, sizeof(WEATHER_FORECAST_DAY_TEXT));
  memset(WEATHER_FORECAST_NIGHT_TEXT, 0, sizeof(WEATHER_FORECAST_NIGHT_TEXT));
  memset(WEATHER_FORECAST_HIGH, 0, sizeof(WEATHER_FORECAST_HIGH));
  memset(WEATHER_FORECAST_LOW, 0, sizeof(WEATHER_FORECAST_LOW));

  client.stop();
  String url = "http://api.seniverse.com/v3/weather/daily.json?key=";
  url += WEATHER_API_KEY;
  url += "&location=";
  url += WEATHER_CITY;
  url += "&language=zh-Hans&unit=c&start=0&days=3";

  if (!http.begin(client, url)) {
    Serial.println("Weather forecast: unable to initialize HTTP");
    return false;
  }

  bool success = false;
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    weatherForecastDoc.clear();
    DeserializationError error = deserializeJson(weatherForecastDoc, payload);
    if (!error) {
      JsonArray daily = weatherForecastDoc["results"][0]["daily"].as<JsonArray>();
      for (JsonObject day : daily) {
        if (WEATHER_FORECAST_COUNT >= WEATHER_FORECAST_DAYS) break;
        const uint8_t i = WEATHER_FORECAST_COUNT;
        const char* date = day["date"] | "";
        const char* dayText = day["text_day"] | "--";
        const char* nightText = day["text_night"] | "--";
        const char* high = day["high"] | "--";
        const char* low = day["low"] | "--";
        snprintf(WEATHER_FORECAST_DATE[i], sizeof(WEATHER_FORECAST_DATE[i]), "%s", date);
        snprintf(WEATHER_FORECAST_DAY_TEXT[i], sizeof(WEATHER_FORECAST_DAY_TEXT[i]), "%s", dayText);
        snprintf(WEATHER_FORECAST_NIGHT_TEXT[i], sizeof(WEATHER_FORECAST_NIGHT_TEXT[i]), "%s", nightText);
        snprintf(WEATHER_FORECAST_HIGH[i], sizeof(WEATHER_FORECAST_HIGH[i]), "%s", high);
        snprintf(WEATHER_FORECAST_LOW[i], sizeof(WEATHER_FORECAST_LOW[i]), "%s", low);
        WEATHER_FORECAST_COUNT++;
      }
      success = WEATHER_FORECAST_COUNT > 0;
      Serial.printf("Weather forecast: loaded %u day(s) for %s\n",
                    WEATHER_FORECAST_COUNT, WEATHER_CITY_DISPLAY);
    } else {
      Serial.print("Weather forecast JSON parse failed: ");
      Serial.println(error.f_str());
    }
  } else {
    Serial.printf("Weather forecast HTTP request failed: %d\n", httpCode);
  }
  http.end();
  return success;
}

// 更新城市并重新获取天气
bool updateWeatherCity(const char* newCity) {
    if (!newCity || strlen(newCity) == 0) return false;
    strncpy(WEATHER_CITY, newCity, sizeof(WEATHER_CITY) - 1);
    WEATHER_CITY[sizeof(WEATHER_CITY) - 1] = '\0';

    // 1. 先查本地对照表，提前设置中文显示名
    const char* cn = pinyin_to_chinese(newCity);
    if (cn) {
        strncpy(WEATHER_CITY_DISPLAY, cn, sizeof(WEATHER_CITY_DISPLAY) - 1);
    } else {
        // 没找到对照，先用拼音顶替
        strncpy(WEATHER_CITY_DISPLAY, newCity, sizeof(WEATHER_CITY_DISPLAY) - 1);
    }
    WEATHER_CITY_DISPLAY[sizeof(WEATHER_CITY_DISPLAY) - 1] = '\0';
    Serial.printf("City changed to: %s (%s)\n", WEATHER_CITY, WEATHER_CITY_DISPLAY);

    // 2. 尝试从 API 获取天气和中文城市名
    bool ok = fetchWeatherData();
    fetchWeatherForecast();
    if (ok) saveCachedWeather();
    // 注意：fetchWeatherData 成功时会用 API 返回的中文名覆盖 WEATHER_CITY_DISPLAY
    // 如果失败，WEATHER_CITY_DISPLAY 保持本地对照表的值
    return ok;
}

//获取时间
String getBeijingTime() {
  struct tm timeinfo;
  char timeString[50];

  if (getLocalTime(&timeinfo)) {
    strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
    return String(timeString);
  } else {
    return "获取时间失败";
  }
}

static bool service_connecting = false;
static uint32_t service_deadline = 0;
static char service_ssid[33] = {};
static char service_password[64] = {};
static bool service_was_connected = false;
static bool service_refresh_pending = false;
static uint32_t service_next_retry = 0;
static bool service_restore_pending = false;

bool wifi_service_start_connect(const char *ssid, const char *password) {
  if (!ssid || !*ssid || !password || service_connecting) return false;
  service_restore_pending = false;
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.disconnect(false, false);
  strlcpy(service_ssid, ssid, sizeof(service_ssid));
  strlcpy(service_password, password, sizeof(service_password));
  service_connecting = true; service_deadline = millis() + 30000UL;
  WiFi.begin(ssid, password); return true;
}
void wifi_service_init(void) {
  service_was_connected = WiFi.status() == WL_CONNECTED;
  service_refresh_pending = !service_was_connected;
  service_restore_pending = !service_was_connected && loadSavedWiFiCredentials();
  // Start the UI and its tasks before allocating the WiFi driver's buffers.
  service_next_retry = millis() + (service_restore_pending ? 1000UL : 10000UL);
}
void wifi_service_tick(void) {
  const uint32_t now = millis();
  if (service_restore_pending) {
    if (WiFi.status() == WL_CONNECTED || service_connecting) {
      service_restore_pending = false;
    } else if ((int32_t)(now - service_next_retry) >= 0) {
      service_restore_pending = false;
      Serial.println("[WIFI] Restoring saved network");
      wifi_service_start_connect(WIFI_SSID, WIFI_PASSWORD);
    }
  }
  if (service_connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      service_connecting = false;
      saveWiFiCredentials(service_ssid, service_password);
      service_refresh_pending = true;
      service_next_retry = now;
    } else if ((int32_t)(now - service_deadline) >= 0) {
      service_connecting = false;
    }
  }

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    service_was_connected = false;
    if (!service_connecting && (int32_t)(now - service_next_retry) >= 0) {
      WiFi.reconnect();
      service_next_retry = now + 10000UL;
    }
    return;
  }
  if (!service_was_connected) {
    service_was_connected = true;
    Serial.println("[WIFI] Connected");
    service_refresh_pending = true;
    service_next_retry = now;
  }
  if (!service_refresh_pending) return;

  // Failed refreshes must wait for their retry deadline instead of running
  // blocking NTP/HTTP requests on every iteration of the LVGL task.
  if ((int32_t)(now - service_next_retry) < 0) return;
  const bool ntp_ok = syncNTPTime();
  const bool weather_ok = fetchWeatherData();
  const bool forecast_ok = weather_ok && fetchWeatherForecast();
  if (weather_ok) {
    saveCachedWeather();
    set_weather(weather, temp);
  }
  service_refresh_pending = !ntp_ok || !weather_ok || !forecast_ok;
  service_next_retry = millis() + (service_refresh_pending ? 30000UL : 3600000UL);
}
