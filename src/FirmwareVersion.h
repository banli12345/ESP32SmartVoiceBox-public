#ifndef SMARTVOICEBOX_FIRMWARE_VERSION_H
#define SMARTVOICEBOX_FIRMWARE_VERSION_H
#include "esp_app_desc.h"
// Keep the compatibility name, but always report the running IDF application.
#define SMARTVOICEBOX_FIRMWARE_VERSION (esp_app_get_description()->version)
#endif
