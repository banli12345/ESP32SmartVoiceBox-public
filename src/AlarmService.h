#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint8_t hour, minute, weekdays, ring_minutes;
    bool enabled;
    int64_t once_epoch; /* 0=resolve next HH:mm on save; days=0 means once. */
} alarm_config_t;
typedef struct {
    alarm_config_t config;
    int64_t next_epoch, now_epoch;
    uint32_t seconds_to_alarm, ring_seconds_left;
    uint32_t revision, request_id, saved_request, failed_request;
    bool ringing, save_busy, save_failed, time_valid, storage_warning;
} alarm_snapshot_t;
void alarm_service_init(void);
void alarm_service_poll(void); /* AppServices only; performs flash writes. */
void alarm_get_snapshot(alarm_snapshot_t *out);
bool alarm_request_save(const alarm_config_t *config, uint32_t *request_id);
void alarm_dismiss(void); /* Stops this occurrence, keeps recurring days. */
int64_t alarm_next_occurrence(const alarm_config_t *config, int64_t after, int64_t last_fired);
int64_t alarm_day_time(int64_t now, int day_offset, unsigned hour, unsigned minute);
#ifdef __cplusplus
}
#endif
