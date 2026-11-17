#include "IdfRuntime.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include <stdio.h>

void app_log_memory(const char *stage)
{
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    printf("[MEM] %s internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
           stage, (unsigned)heap_caps_get_free_size(internal),
           (unsigned)heap_caps_get_largest_free_block(internal),
           (unsigned)heap_caps_get_free_size(external),
           (unsigned)heap_caps_get_largest_free_block(external));
}

esp_err_t app_uart_init(void)
{
    uart_config_t config = {};
    config.baud_rate = 115200;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t result = uart_param_config(UART_NUM_0, &config);
    if (result != ESP_OK) return result;
    result = uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (result != ESP_OK) return result;
    if (!uart_is_driver_installed(UART_NUM_0)) {
        result = uart_driver_install(UART_NUM_0, 8192, 0, 0, nullptr, 0);
        if (result != ESP_OK) return result;
    }
    uart_vfs_dev_use_driver(UART_NUM_0);
    return ESP_OK;
}
