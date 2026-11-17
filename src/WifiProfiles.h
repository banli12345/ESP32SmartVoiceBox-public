#ifndef WIFI_PROFILES_H
#define WIFI_PROFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_PROFILE_CAPACITY 8
#define WIFI_PROFILE_SSID_MAX 32
#define WIFI_PROFILE_PASSWORD_MAX 63
#define WIFI_PROFILE_BLOB_BYTES 800

typedef struct {
    uint8_t ssid_len;
    uint8_t password_len;
    char ssid[33];
    char password[64];
} wifi_profile_t;

typedef struct {
    uint8_t count;
    wifi_profile_t items[WIFI_PROFILE_CAPACITY];
} wifi_profile_store_t;

typedef enum {
    WIFI_NVS_OK = 0,
    WIFI_NVS_NOT_FOUND = 1,
    WIFI_NVS_ERR = 2
} wifi_nvs_status_t;

typedef enum {
    WIFI_PROFILES_NOT_FOUND = 0,
    WIFI_PROFILES_LOADED = 1,
    WIFI_PROFILES_MIGRATED = 2,
    WIFI_PROFILES_REJECTED = 3,
    WIFI_PROFILES_IO_ERROR = 4
} wifi_profiles_status_t;

/* Open/close bracket one namespace transaction. get/set never erase other keys. */
typedef struct {
    void *ctx;
    int (*open_ns)(void *ctx, int writable);
    void (*close_ns)(void *ctx);
    int (*get_blob)(void *ctx, const char *key, void *buffer, size_t *length);
    int (*set_blob)(void *ctx, const char *key, const void *buffer, size_t length);
    int (*get_str)(void *ctx, const char *key, char *buffer, size_t *length);
    int (*commit)(void *ctx);
} wifi_nvs_io_t;

typedef struct {
    uint8_t ssid_len;
    char ssid[33];
    bool open_network;
} wifi_visible_ap_t;

typedef struct {
    bool scan_pending;
    bool connect_pending;
    bool saved_connect;
    uint32_t connect_seq;
    uint8_t ssid_len;
    char ssid[33];
    uint8_t password_len;
    char password[64];
} wifi_request_queue_t;

typedef struct {
    bool active;
    bool manual;
    uint32_t epoch;
    uint8_t ssid_len;
    char ssid[33];
    uint8_t password_len;
    char password[64];
} wifi_attempt_t;

typedef enum {
    WIFI_ATTEMPT_IGNORED = 0,
    WIFI_ATTEMPT_SAVE = 1,
    WIFI_ATTEMPT_NO_WRITE = 2,
    WIFI_ATTEMPT_FAIL = 3
} wifi_attempt_result_t;

bool wifi_profile_credentials_ok(const char *ssid, size_t ssid_len, const char *password, size_t password_len);
void wifi_profile_store_clear(wifi_profile_store_t *store);
int wifi_profile_find(const wifi_profile_store_t *store, const char *ssid, size_t ssid_len);
bool wifi_profile_note_success(wifi_profile_store_t *store, const char *ssid, size_t ssid_len,
                               const char *password, size_t password_len, bool *changed);

wifi_profiles_status_t wifi_profiles_load(wifi_profile_store_t *store, const wifi_nvs_io_t *io);
bool wifi_profiles_store(const wifi_profile_store_t *store, const wifi_nvs_io_t *io);

int wifi_autojoin_pick(const wifi_profile_store_t *store, const wifi_visible_ap_t *visible, int visible_count,
                       uint8_t failed_mask);
bool wifi_deadline_elapsed(uint32_t now, uint32_t deadline);
bool wifi_auto_scan_due(bool radio_on, bool has_ip, bool connect_busy, bool scan_busy, bool have_profiles,
                        uint32_t now, uint32_t due_at);

bool wifi_request_scan(wifi_request_queue_t *queue, bool radio_on);
bool wifi_request_connect(wifi_request_queue_t *queue, const char *ssid, const char *password, bool radio_on);
bool wifi_request_saved(wifi_request_queue_t *queue, const char *ssid, bool known, bool radio_on);
void wifi_request_cancel(wifi_request_queue_t *queue);

int wifi_radio_mode(bool sta_on, bool ap_on);
bool wifi_scan_epoch_current(uint32_t event_epoch, uint32_t active_epoch);

wifi_attempt_result_t wifi_attempt_on_ip(const wifi_attempt_t *attempt, uint32_t event_epoch,
                                        const char *connected_ssid, size_t connected_len,
                                        const wifi_profile_store_t *store);
wifi_attempt_result_t wifi_attempt_on_disconnect(const wifi_attempt_t *attempt, uint32_t event_epoch);
wifi_attempt_result_t wifi_attempt_on_timeout(const wifi_attempt_t *attempt, uint32_t now, uint32_t deadline);

#ifdef __cplusplus
}
#endif

#endif
