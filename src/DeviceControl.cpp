#include "DeviceControl.h"

#include <Arduino.h>

void device_soft_restart(void)
{
    Serial.println("[DEVICE] Soft restart requested");
    Serial.flush();
    ESP.restart();
}
