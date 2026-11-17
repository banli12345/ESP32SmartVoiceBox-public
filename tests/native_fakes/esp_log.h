#pragma once
#include <cstdarg>
enum { ESP_LOG_NONE = 0 };
typedef int (*vprintf_like_t)(const char *, va_list);
inline void esp_log_level_set(const char *, int) {}
inline vprintf_like_t esp_log_set_vprintf(vprintf_like_t) { return nullptr; }
