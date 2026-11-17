#ifndef QUICK_SETTINGS_SERVICE_H
#define QUICK_SETTINGS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QUICK_RADIO_WIFI,
    QUICK_RADIO_BLUETOOTH,
    QUICK_RADIO_HOTSPOT,
    QUICK_RADIO_COUNT
} quick_radio_t;

typedef struct {
    bool enabled;
    bool busy;
    bool connected;
    bool failed;
    char detail[96];
} quick_radio_state_t;

typedef struct {
    char ssid[33];
    char password[65];
    char ip[16];
    uint8_t clients;
    bool internet_available;
} quick_hotspot_info_t;

// UI requests return promptly; native driver work runs from AppServices.
bool quick_radio_request(quick_radio_t radio, bool enabled);
void quick_radio_get(quick_radio_t radio, quick_radio_state_t *state);
void quick_hotspot_get_info(quick_hotspot_info_t *info);
const char *quick_bluetooth_get_name(void);
// Call only from AppServices, once per polling iteration.
void quick_settings_service_poll(void);

#ifdef __cplusplus
}
#endif
#endif
