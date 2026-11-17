#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* AI worker only; local alarm commands work without cloud or WiFi. */
bool alarm_ai_handle(const char *message, char *reply, size_t capacity);
#ifdef __cplusplus
}
#endif
