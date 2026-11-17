#pragma once

// Optional local configuration: copy src/private_config.example.h to
// src/private_config.h. Omit it for an unprovisioned, shareable build.
#if !defined(BREEZE_GENERIC_BUILD) && defined(__has_include)
#  if __has_include("private_config.h")
#    include "private_config.h"
#  endif
#endif

#ifndef DEVICE_WIFI_SSID
#define DEVICE_WIFI_SSID ""
#endif
#ifndef DEVICE_WIFI_PASSWORD
#define DEVICE_WIFI_PASSWORD ""
#endif
// Weather/life endpoints and keys are imported from SD /config/weather.json into NVS.
