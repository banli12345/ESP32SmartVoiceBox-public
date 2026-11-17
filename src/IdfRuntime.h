#ifndef SMARTVOICEBOX_IDF_RUNTIME_H
#define SMARTVOICEBOX_IDF_RUNTIME_H

#include <stdint.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static inline uint32_t app_millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline uint32_t app_micros(void)
{
    return (uint32_t)esp_timer_get_time();
}

static inline void app_delay_ms(uint32_t milliseconds)
{
    if (milliseconds) {
        TickType_t ticks = pdMS_TO_TICKS(milliseconds);
        vTaskDelay(ticks ? ticks : 1);
    } else {
        taskYIELD();
    }
}

#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_uart_init(void);
void app_log_memory(const char *stage);
#ifdef __cplusplus
}
#endif

#endif
