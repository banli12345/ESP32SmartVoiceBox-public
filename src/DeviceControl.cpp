#include "DeviceControl.h"

#include "driver/uart.h"
#include "esp_system.h"
#include <stdio.h>

void device_soft_restart(void)
{
    puts("[DEVICE] Soft restart requested");
    fflush(stdout);
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(200));
    esp_restart();
}
