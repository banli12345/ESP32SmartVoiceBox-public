#include "QuickBluetooth.h"
#include "QuickSettingsService.h"

#include "IdfRuntime.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#if defined(CONFIG_BT_NIMBLE_DIS_SERVICE)
#include "services/dis/ble_svc_dis.h"
#endif

#include <stdio.h>
#include <string.h>
#include <atomic>

#if CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY && !(CONFIG_SPIRAM_FETCH_INSTRUCTIONS && CONFIG_SPIRAM_RODATA)
#error "Cached BLE controller code requires both PSRAM XiP options on this RGB board"
#endif

extern "C" void ble_store_config_init(void);

static const char *TAG = "quick_ble";

static std::atomic<bool> ble_stack_up{false};
static bool ble_initialized = false;
static bool ble_host_started = false;
static std::atomic<bool> ble_host_loop_exited{false};
static std::atomic<bool> ble_synced{false};
static std::atomic<bool> ble_adv_ok{false};
static std::atomic<bool> ble_cancel{false};
static uint8_t own_addr_type = 0;
static int conn_handle = -1;
static std::atomic<int> conn_count{0};
static char ble_name[32] = "ESP32S3";
static std::atomic<bool> ble_name_ready{false};
static portMUX_TYPE ble_mux = portMUX_INITIALIZER_UNLOCKED;

static int quick_ble_gap_event(struct ble_gap_event *event, void *arg);

extern "C" const char *quick_bluetooth_get_name(void)
{
    if (!ble_name_ready) {
        uint8_t mac[6] = {};
        // Same STA MAC suffix as the hotspot SSID.
        if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) memset(mac, 0, sizeof(mac));
        char name[sizeof(ble_name)] = {};
        snprintf(name, sizeof(name), "ESP32S3-%02X%02X%02X", mac[3], mac[4], mac[5]);
        portENTER_CRITICAL(&ble_mux);
        if (!ble_name_ready) memcpy(ble_name, name, sizeof(ble_name));
        ble_name_ready = true;
        portEXIT_CRITICAL(&ble_mux);
    }
    return ble_name;
}

extern "C" bool quick_bluetooth_is_enabled(void)
{
    return ble_stack_up;
}

extern "C" bool quick_bluetooth_is_connected(void)
{
    return ble_stack_up && conn_count > 0;
}

static int quick_ble_start_adv(void)
{
    // on_sync starts advertising before publishing completion to AppServices.
    if (ble_cancel) return -1;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    const char *name = ble_svc_gap_device_name();
    if (!name || !name[0]) name = quick_bluetooth_get_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields failed rc=%d", rc);
        return rc;
    }
    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof(params));
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &params, quick_ble_gap_event, nullptr);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start failed rc=%d", rc);
        return rc;
    }
    return 0;
}

static int quick_ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (!event) return 0;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            portENTER_CRITICAL(&ble_mux);
            conn_handle = event->connect.conn_handle;
            conn_count = 1;
            portEXIT_CRITICAL(&ble_mux);
            puts("[BLE] connected");
        } else if (ble_stack_up && !ble_cancel) {
            quick_ble_start_adv();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        portENTER_CRITICAL(&ble_mux);
        conn_handle = -1;
        conn_count = 0;
        portEXIT_CRITICAL(&ble_mux);
        printf("[BLE] disconnected reason=%d\n", event->disconnect.reason);
        if (ble_stack_up && !ble_cancel) quick_ble_start_adv();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (ble_stack_up && !ble_cancel) quick_ble_start_adv();
        return 0;
#if CONFIG_BT_NIMBLE_SECURITY_ENABLE
    case BLE_GAP_EVENT_ENC_CHANGE: {
        struct ble_gap_conn_desc connection = {};
        const int found = ble_gap_conn_find(event->enc_change.conn_handle, &connection);
        printf("[BLE] encryption status=%d encrypted=%u bonded=%u\n",
               event->enc_change.status,
               found == 0 ? (unsigned)connection.sec_state.encrypted : 0,
               found == 0 ? (unsigned)connection.sec_state.bonded : 0);
        return 0;
    }
#endif
    default:
        return 0;
    }
}

static void quick_ble_on_sync(void)
{
    if (ble_cancel) {
        ble_synced = true;
        ble_adv_ok = false;
        return;
    }
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure addr failed rc=%d", rc);
        ble_adv_ok = false;
        ble_synced = true;
        return;
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer addr failed rc=%d", rc);
        ble_adv_ok = false;
        ble_synced = true;
        return;
    }
    ble_adv_ok = quick_ble_start_adv() == 0;
    ble_synced = true;
}

static void quick_ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset reason=%d", reason);
    ble_synced = false;
    ble_adv_ok = false;
    portENTER_CRITICAL(&ble_mux);
    conn_handle = -1;
    conn_count = 0;
    portEXIT_CRITICAL(&ble_mux);
}

// Host task runs nimble_port_run only. Deinit happens on AppServices after this returns.
static void quick_ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    ble_host_loop_exited = true;
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

static bool quick_ble_force_down(void)
{
    if (!ble_initialized) return true;
    const bool host_was_started = ble_host_started;
    ble_cancel = true;
    if (host_was_started && ble_synced) {
        const int handle = conn_handle;
        if (handle >= 0) ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        ble_gap_adv_stop();
    }
    bool host_left = !host_was_started || ble_host_loop_exited;
    if (host_was_started && !ble_host_loop_exited) {
        const int stopped = nimble_port_stop();
        if (stopped != 0) ESP_LOGE(TAG, "nimble_port_stop rc=%d", stopped);
        const uint32_t started = app_millis();
        while (!ble_host_loop_exited && (uint32_t)(app_millis() - started) < 3000) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        host_left = ble_host_loop_exited;
    }
    if (!host_left) {
        ESP_LOGE(TAG, "BLE host did not exit; controller left allocated");
        return false;
    }
    if (host_was_started) esp_nimble_disable();
    ble_host_started = false;
    const esp_err_t deinit = nimble_port_deinit();
    if (deinit != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_deinit %s", esp_err_to_name(deinit));
        return false;
    }
    ble_initialized = false;
    ble_stack_up = false;
    ble_host_loop_exited = false;
    ble_synced = false;
    ble_adv_ok = false;
    ble_cancel = false;
    portENTER_CRITICAL(&ble_mux);
    conn_handle = -1;
    conn_count = 0;
    portEXIT_CRITICAL(&ble_mux);
    return true;
}

static bool quick_ble_enable(bool *connected, char *detail, size_t detail_len)
{
    if (ble_stack_up && ble_adv_ok) {
        if (connected) *connected = conn_count > 0;
        if (detail && detail_len) snprintf(detail, detail_len, conn_count > 0 ? "Connected" : "Advertising");
        return true;
    }
    if (ble_initialized && !quick_ble_force_down()) {
        if (detail && detail_len) snprintf(detail, detail_len, "BLE shutdown pending");
        return false;
    }
    ble_cancel = false;
    ble_synced = false;
    ble_adv_ok = false;
    ble_host_loop_exited = false;
    const esp_err_t inited = nimble_port_init();
    if (inited != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init %s", esp_err_to_name(inited));
        if (detail && detail_len) snprintf(detail, detail_len, "BLE init %s", esp_err_to_name(inited));
        return false;
    }
    ble_initialized = true;
    ble_hs_cfg.sync_cb = quick_ble_on_sync;
    ble_hs_cfg.reset_cb = quick_ble_on_reset;
    ble_store_config_init();
#if CONFIG_BT_NIMBLE_SECURITY_ENABLE
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
#endif
    ble_svc_gap_init();
    ble_svc_gatt_init();
#if defined(CONFIG_BT_NIMBLE_DIS_SERVICE)
    ble_svc_dis_init();
#endif
    const int named = ble_svc_gap_device_name_set(quick_bluetooth_get_name());
    if (named != 0) {
        quick_ble_force_down();
        if (detail && detail_len) snprintf(detail, detail_len, "BLE name failed");
        return false;
    }
    const esp_err_t started = esp_nimble_enable((void *)quick_ble_host_task);
    if (started != ESP_OK) {
        ESP_LOGE(TAG, "BLE host task %s", esp_err_to_name(started));
        quick_ble_force_down();
        if (detail && detail_len) snprintf(detail, detail_len, "BLE task %s", esp_err_to_name(started));
        return false;
    }
    ble_host_started = true;
    const uint32_t wait_from = app_millis();
    while (!ble_synced && (uint32_t)(app_millis() - wait_from) < 5000) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!ble_synced || !ble_adv_ok || ble_cancel) {
        quick_ble_force_down();
        if (detail && detail_len) snprintf(detail, detail_len, "BLE advertise failed");
        return false;
    }
    ble_stack_up = true;
    if (connected) *connected = conn_count > 0;
    if (detail && detail_len) snprintf(detail, detail_len, "Advertising");
    ESP_LOGI(TAG, "BLE advertising as %s", quick_bluetooth_get_name());
    return true;
}

extern "C" bool quick_bluetooth_driver_set(bool enabled, bool *connected, char *detail, size_t detail_len)
{
    if (!enabled) {
        if (!ble_initialized) {
            if (connected) *connected = false;
            if (detail && detail_len) snprintf(detail, detail_len, "Off");
            return true;
        }
        const bool down = quick_ble_force_down();
        if (connected) *connected = false;
        if (detail && detail_len) snprintf(detail, detail_len, down ? "Off" : "BLE stop failed");
        return down;
    }
    const bool ok = quick_ble_enable(connected, detail, detail_len);
    if (!ok && (ble_host_started || ble_stack_up)) quick_ble_force_down();
    if (connected) *connected = ok && conn_count > 0;
    return ok;
}
