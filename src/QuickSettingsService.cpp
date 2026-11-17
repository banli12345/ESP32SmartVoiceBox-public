#include "QuickSettingsService.h"

#include <stdio.h>
#include <string.h>

#ifdef QUICK_RADIO_HOST_TEST

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))

extern "C" bool quick_wifi_driver_set(bool enabled, bool *connected, char *detail, size_t detail_len);
extern "C" bool quick_hotspot_driver_set(bool enabled, bool *connected, char *detail, size_t detail_len);
extern "C" bool quick_bluetooth_driver_set(bool enabled, bool *connected, char *detail, size_t detail_len);
extern "C" bool quick_wifi_hw_enabled(void);
extern "C" bool quick_hotspot_hw_enabled(void);
extern "C" bool quick_bluetooth_hw_enabled(void);
extern "C" bool quick_wifi_hw_connected(void);
extern "C" bool quick_bluetooth_hw_connected(void);
extern "C" void quick_hotspot_hw_info(quick_hotspot_info_t *info);
extern "C" void wifi_radio_note_desired(bool enabled);
extern "C" void wifi_driver_lock(void);
extern "C" void wifi_driver_unlock(void);

#else

#include "QuickBluetooth.h"
#include "IdfRuntime.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "setwifi.h"

extern "C" void quick_radio_adopt(quick_radio_t radio, bool enabled);

#endif

struct quick_slot {
    bool desired;
    bool enabled;
    bool busy;
    bool connected;
    bool failed;
    char detail[96];
    uint32_t request_seq;
    uint32_t applied_seq;
};

static quick_slot quick_slots[QUICK_RADIO_COUNT];
static portMUX_TYPE quick_mux = portMUX_INITIALIZER_UNLOCKED;
static bool quick_slots_ready = false;

#ifndef QUICK_RADIO_HOST_TEST
static bool wifi_apply(bool enabled, bool *connected, char *detail, size_t detail_len)
{
    const bool ok = wifi_radio_apply(enabled);
    const bool link = ok && enabled && get_wifi_connected();
    if (connected) *connected = link;
    if (detail && detail_len) {
        snprintf(detail, detail_len, ok ? (enabled ? (link ? "Connected" : "On") : "Off") : "WiFi failed");
    }
    return ok;
}

static bool hotspot_apply(bool enabled, bool *connected, char *detail, size_t detail_len)
{
    const bool ok = wifi_hotspot_apply(enabled);
    const bool up = ok && enabled && wifi_hotspot_get_enabled();
    if (connected) *connected = up;
    if (detail && detail_len) {
        if (!ok) snprintf(detail, detail_len, "Hotspot failed");
        else if (!enabled) snprintf(detail, detail_len, "Off");
        else snprintf(detail, detail_len, wifi_hotspot_internet_available() ? "Sharing" : "No upstream");
    }
    return ok;
}

static bool bluetooth_apply(bool enabled, bool *connected, char *detail, size_t detail_len)
{
    return quick_bluetooth_driver_set(enabled, connected, detail, detail_len);
}
#endif

static void quick_slots_init(void)
{
    portENTER_CRITICAL(&quick_mux);
    if (quick_slots_ready) {
        portEXIT_CRITICAL(&quick_mux);
        return;
    }
    memset(quick_slots, 0, sizeof(quick_slots));
    quick_slots[QUICK_RADIO_WIFI].desired = true;
    quick_slots[QUICK_RADIO_WIFI].request_seq = 1;
    quick_slots[QUICK_RADIO_WIFI].busy = true;
    snprintf(quick_slots[QUICK_RADIO_WIFI].detail, sizeof(quick_slots[QUICK_RADIO_WIFI].detail), "Starting");
    snprintf(quick_slots[QUICK_RADIO_BLUETOOTH].detail, sizeof(quick_slots[QUICK_RADIO_BLUETOOTH].detail), "Off");
    snprintf(quick_slots[QUICK_RADIO_HOTSPOT].detail, sizeof(quick_slots[QUICK_RADIO_HOTSPOT].detail), "Off");
    quick_slots_ready = true;
    portEXIT_CRITICAL(&quick_mux);
}

typedef bool (*quick_apply_fn)(bool enabled, bool *connected, char *detail, size_t detail_len);

static void quick_drive_slot(quick_radio_t radio, quick_apply_fn apply, bool (*hw_enabled)(void), bool (*hw_connected)(void))
{
    uint32_t seq = 0;
    bool target = false;
    portENTER_CRITICAL(&quick_mux);
    const bool pending = quick_slots[radio].request_seq != quick_slots[radio].applied_seq;
    seq = quick_slots[radio].request_seq;
    target = quick_slots[radio].desired;
    if (pending) quick_slots[radio].busy = true;
    portEXIT_CRITICAL(&quick_mux);
    if (!pending) return;

    bool connected = false;
    char detail[96];
    detail[0] = '\0';
    const bool ok = apply(target, &connected, detail, sizeof(detail));
    const bool actual = hw_enabled ? hw_enabled() : (ok && target);
#ifndef QUICK_RADIO_HOST_TEST
    printf("[RADIO] type=%u target=%u enabled=%u ok=%u stack_free_min=%u\n",
           (unsigned)radio, (unsigned)target, (unsigned)actual, (unsigned)ok,
           (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#endif

    portENTER_CRITICAL(&quick_mux);
    if (quick_slots[radio].request_seq != seq) {
        portEXIT_CRITICAL(&quick_mux);
        return;
    }
    quick_slots[radio].applied_seq = seq;
    quick_slots[radio].enabled = ok ? target : actual;
    quick_slots[radio].failed = !ok;
    quick_slots[radio].busy = false;
    quick_slots[radio].connected = quick_slots[radio].enabled && connected;
    if (hw_connected && quick_slots[radio].enabled) quick_slots[radio].connected = hw_connected();
    snprintf(quick_slots[radio].detail, sizeof(quick_slots[radio].detail), "%s", detail[0] ? detail : (ok ? "OK" : "Failed"));
    portEXIT_CRITICAL(&quick_mux);
}

extern "C" bool quick_radio_request(quick_radio_t radio, bool enabled)
{
    if ((unsigned)radio >= QUICK_RADIO_COUNT) return false;
    quick_slots_init();
    portENTER_CRITICAL(&quick_mux);
    quick_slots[radio].desired = enabled;
    quick_slots[radio].request_seq++;
    quick_slots[radio].busy = true;
    portEXIT_CRITICAL(&quick_mux);
#ifndef QUICK_RADIO_HOST_TEST
    if (radio == QUICK_RADIO_WIFI) wifi_radio_note_desired(enabled);
#else
    if (radio == QUICK_RADIO_WIFI) wifi_radio_note_desired(enabled);
#endif
    return true;
}

extern "C" void quick_radio_adopt(quick_radio_t radio, bool enabled)
{
    if ((unsigned)radio >= QUICK_RADIO_COUNT) return;
    quick_slots_init();
    portENTER_CRITICAL(&quick_mux);
    quick_slots[radio].desired = enabled;
    quick_slots[radio].request_seq++;
    quick_slots[radio].applied_seq = quick_slots[radio].request_seq;
    quick_slots[radio].enabled = enabled;
    quick_slots[radio].busy = false;
    quick_slots[radio].failed = false;
    snprintf(quick_slots[radio].detail, sizeof(quick_slots[radio].detail), "%s", enabled ? "On" : "Off");
    portEXIT_CRITICAL(&quick_mux);
}

static void quick_overlay(quick_radio_t radio, quick_radio_state_t *state)
{
    if (state->busy) return;
#ifndef QUICK_RADIO_HOST_TEST
    if (radio == QUICK_RADIO_WIFI) {
        state->enabled = wifi_radio_get_enabled();
        state->connected = state->enabled && get_wifi_connected();
    } else if (radio == QUICK_RADIO_HOTSPOT) {
        state->enabled = wifi_hotspot_get_enabled();
        state->connected = state->enabled;
        if (state->enabled && !state->failed) {
            snprintf(state->detail, sizeof(state->detail), "%s",
                     wifi_hotspot_internet_available() ? "Sharing" : "No upstream");
        }
    } else if (radio == QUICK_RADIO_BLUETOOTH) {
        state->enabled = quick_bluetooth_is_enabled();
        state->connected = state->enabled && quick_bluetooth_is_connected();
    }
#else
    if (radio == QUICK_RADIO_WIFI) {
        state->enabled = quick_wifi_hw_enabled();
        state->connected = state->enabled && quick_wifi_hw_connected();
    } else if (radio == QUICK_RADIO_HOTSPOT) {
        state->enabled = quick_hotspot_hw_enabled();
        state->connected = state->enabled;
    } else if (radio == QUICK_RADIO_BLUETOOTH) {
        state->enabled = quick_bluetooth_hw_enabled();
        state->connected = state->enabled && quick_bluetooth_hw_connected();
    }
#endif
    (void)radio;
}

extern "C" void quick_radio_get(quick_radio_t radio, quick_radio_state_t *state)
{
    if (!state) return;
    memset(state, 0, sizeof(*state));
    if ((unsigned)radio >= QUICK_RADIO_COUNT) return;
    quick_slots_init();
    portENTER_CRITICAL(&quick_mux);
    state->enabled = quick_slots[radio].enabled;
    state->busy = quick_slots[radio].busy;
    state->connected = quick_slots[radio].connected;
    state->failed = quick_slots[radio].failed;
    memcpy(state->detail, quick_slots[radio].detail, sizeof(state->detail));
    portEXIT_CRITICAL(&quick_mux);
    state->detail[sizeof(state->detail) - 1] = '\0';
    quick_overlay(radio, state);
}

extern "C" void quick_hotspot_get_info(quick_hotspot_info_t *info)
{
    if (!info) return;
    memset(info, 0, sizeof(*info));
#ifdef QUICK_RADIO_HOST_TEST
    quick_hotspot_hw_info(info);
#else
    wifi_hotspot_copy_info(info->ssid, sizeof(info->ssid), info->password, sizeof(info->password),
                           info->ip, sizeof(info->ip), &info->clients, &info->internet_available);
#endif
}

extern "C" void quick_settings_service_poll(void)
{
    quick_slots_init();
#ifdef QUICK_RADIO_HOST_TEST
    wifi_driver_lock();
    quick_drive_slot(QUICK_RADIO_WIFI, quick_wifi_driver_set, quick_wifi_hw_enabled, quick_wifi_hw_connected);
    quick_drive_slot(QUICK_RADIO_HOTSPOT, quick_hotspot_driver_set, quick_hotspot_hw_enabled, quick_hotspot_hw_enabled);
    wifi_driver_unlock();
    quick_drive_slot(QUICK_RADIO_BLUETOOTH, quick_bluetooth_driver_set, quick_bluetooth_hw_enabled, quick_bluetooth_hw_connected);
#else
    wifi_driver_lock();
    quick_drive_slot(QUICK_RADIO_WIFI, wifi_apply, wifi_radio_get_enabled, get_wifi_connected);
    quick_drive_slot(QUICK_RADIO_HOTSPOT, hotspot_apply, wifi_hotspot_get_enabled, wifi_hotspot_get_enabled);
    wifi_hotspot_maintain_locked();
    wifi_driver_unlock();
    quick_drive_slot(QUICK_RADIO_BLUETOOTH, bluetooth_apply, quick_bluetooth_is_enabled, quick_bluetooth_is_connected);
#endif
}
