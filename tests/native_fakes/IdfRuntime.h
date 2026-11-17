#pragma once
#include <stdint.h>
inline uint32_t host_milliseconds = 0;
inline uint32_t app_millis(void) { return host_milliseconds; }
inline uint32_t app_micros(void) { return host_milliseconds * 1000u; }
inline void app_delay_ms(uint32_t milliseconds) { host_milliseconds += milliseconds; }
inline int app_uart_init(void) { return 0; }
