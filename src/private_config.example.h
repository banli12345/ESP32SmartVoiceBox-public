#pragma once

// Copy to src/private_config.h (Git-ignored). Device WiFi settings in NVS
// take precedence over these optional bootstrap credentials.
#define DEVICE_WIFI_SSID ""
#define DEVICE_WIFI_PASSWORD ""

// Weather/life endpoints and keys use SD /config/weather.json -> NVS.
// scripts/configure_services.py generates both runtime JSON imports.

// AI is provisioned separately to NVS. Do not put a DeepSeek key here.
// See docs/DIRECT_AI.md and scripts/provision_ai.py.
