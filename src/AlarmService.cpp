#include "AlarmService.h"
#include "Sound.h"
#include "ScreenIdle.h"
#include "IdfRuntime.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {
constexpr int64_t kTimeFloor = 1704067200; /* 2024-01-01 UTC */
constexpr int64_t kTimeCeiling = 4133980800; /* 2101-01-01 UTC */
constexpr uint32_t kMagic = 0x414c4d31;
struct Stored {
    uint32_t magic;
    uint8_t enabled, hour, minute, days, duration, reserved[7];
    int64_t once, fired;
};
static_assert(sizeof(Stored) == 32, "Stable alarm persistence layout");
portMUX_TYPE alarm_mux = portMUX_INITIALIZER_UNLOCKED;
alarm_snapshot_t active;
alarm_config_t queued;
uint32_t serial, ring_started, last_persist_try, last_tick;
int64_t last_fired, last_wall;
int64_t queued_due;
bool committing, dirty;

bool local(int64_t epoch, struct tm &out)
{
    const time_t t = (time_t)epoch;
#if defined(_WIN32)
    return localtime_s(&out, &t) == 0;
#else
    return localtime_r(&t, &out) != nullptr;
#endif
}
bool clock_valid(int64_t now) { return now >= kTimeFloor && now < kTimeCeiling; }
bool valid(const alarm_config_t &c)
{
    return c.hour < 24 && c.minute < 60 && c.weekdays <= 0x7f &&
        c.ring_minutes >= 1 && c.ring_minutes <= 60 &&
        (!c.once_epoch || (c.once_epoch >= kTimeFloor && c.once_epoch < kTimeCeiling));
}
bool write_config(const alarm_config_t &c, int64_t fired)
{
    Stored saved = {};
    saved.magic = kMagic; saved.enabled = c.enabled;
    saved.hour = c.hour; saved.minute = c.minute; saved.days = c.weekdays;
    saved.duration = c.ring_minutes; saved.once = c.once_epoch; saved.fired = fired;
    nvs_handle_t h;
    if (nvs_open("alarm", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t result = nvs_set_blob(h, "config", &saved, sizeof(saved));
    if (result == ESP_OK) result = nvs_commit(h);
    nvs_close(h);
    return result == ESP_OK;
}
bool load(alarm_config_t &c, int64_t &fired, bool &exists)
{
    nvs_handle_t h;
    const esp_err_t opened = nvs_open("alarm", NVS_READONLY, &h);
    exists = opened != ESP_ERR_NVS_NOT_FOUND;
    if (opened != ESP_OK) return false;
    Stored s = {};
    size_t length = sizeof(s);
    const esp_err_t result = nvs_get_blob(h, "config", &s, &length);
    nvs_close(h);
    if (result == ESP_ERR_NVS_NOT_FOUND) { exists = false; return false; }
    c = {s.hour, s.minute, s.days, s.duration, s.enabled != 0, s.once};
    if (result != ESP_OK || length != sizeof(s) || s.magic != kMagic || s.enabled > 1 ||
        s.fired < 0 || s.fired >= kTimeCeiling || !valid(c)) return false;
    fired = s.fired;
    return true;
}
bool migrate(alarm_config_t &c)
{
    nvs_handle_t h;
    if (nvs_open("clock", NVS_READONLY, &h) != ESP_OK) return false;
    uint8_t hour = 0, minute = 0, enabled = 0;
    const bool ok = nvs_get_u8(h, "hour", &hour) == ESP_OK &&
        nvs_get_u8(h, "minute", &minute) == ESP_OK &&
        nvs_get_u8(h, "enabled", &enabled) == ESP_OK;
    nvs_close(h);
    if (!ok || hour > 23 || minute > 59 || enabled > 1) return false;
    c = {hour, minute, 0x7f, 1, enabled != 0, 0};
    return true;
}
}

extern "C" int64_t alarm_day_time(int64_t now, int day_offset, unsigned hour, unsigned minute)
{
    if (!clock_valid(now) || day_offset < 0 || day_offset > 7 || hour > 23 || minute > 59) return 0;
    struct tm t;
    if (!local(now, t)) return 0;
    t.tm_mday += day_offset; t.tm_hour = hour; t.tm_min = minute; t.tm_sec = 0; t.tm_isdst = -1;
    return (int64_t)mktime(&t);
}

extern "C" int64_t alarm_next_occurrence(const alarm_config_t *c, int64_t after, int64_t fired)
{
    if (!c || !valid(*c) || !c->enabled || !clock_valid(after)) return 0;
    if (!c->weekdays) return c->once_epoch > after && c->once_epoch > fired ? c->once_epoch : 0;
    for (int day = 0; day <= 7; ++day) {
        const int64_t candidate = alarm_day_time(after, day, c->hour, c->minute);
        struct tm t;
        if (!candidate || !local(candidate, t)) continue;
        const unsigned weekday = (t.tm_wday + 6) % 7;
        if ((c->weekdays & (1u << weekday)) && candidate > after && candidate > fired) return candidate;
    }
    return 0;
}

extern "C" void alarm_service_init(void)
{
#if defined(_WIN32)
    _putenv_s("TZ", "CST-8"); _tzset();
#else
    setenv("TZ", "CST-8", 1); tzset();
#endif
    alarm_config_t c = {7, 0, 0, 1, false, 0};
    int64_t fired = 0; bool exists = false;
    const bool loaded = load(c, fired, exists);
    bool warning = exists && !loaded;
    if (!loaded) {
        c = {7, 0, 0, 1, false, 0};
        if (!exists && migrate(c)) warning = !write_config(c, 0);
    }
    const int64_t now = (int64_t)time(nullptr);
    const int64_t next = alarm_next_occurrence(&c, now - 90, fired);
    portENTER_CRITICAL(&alarm_mux);
    active = {}; active.config = c; active.storage_warning = warning;
    active.next_epoch = next;
    active.revision = 1;
    queued = {}; serial = 0; last_fired = fired;
    committing = dirty = false; last_wall = 0; last_tick = 0; ring_started = 0;
    portEXIT_CRITICAL(&alarm_mux);
}

extern "C" void alarm_get_snapshot(alarm_snapshot_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&alarm_mux);
    *out = active;
    const uint32_t started = ring_started;
    portEXIT_CRITICAL(&alarm_mux);
    out->now_epoch = (int64_t)time(nullptr);
    out->time_valid = clock_valid(out->now_epoch);
    const int64_t left = out->next_epoch - out->now_epoch;
    out->seconds_to_alarm = left > 0 && left <= UINT32_MAX ? (uint32_t)left : 0;
    const uint32_t elapsed = app_millis() - started;
    const uint32_t duration = (uint32_t)out->config.ring_minutes * 60000u;
    out->ring_seconds_left = out->ringing && elapsed < duration ? (duration - elapsed + 999) / 1000 : 0;
}

extern "C" bool alarm_request_save(const alarm_config_t *input, uint32_t *request_id)
{
    if (!input || !valid(*input)) return false;
    alarm_config_t c = *input;
    const int64_t now = (int64_t)time(nullptr);
    if (c.weekdays) c.once_epoch = 0;
    else if (c.enabled) {
        if (!clock_valid(now)) return false;
        if (!c.once_epoch) {
            c.once_epoch = alarm_day_time(now, 0, c.hour, c.minute);
            if (c.once_epoch <= now) c.once_epoch = alarm_day_time(now, 1, c.hour, c.minute);
        }
        if (c.once_epoch <= now) return false;
    }
    const int64_t next = alarm_next_occurrence(&c, now, 0);
    portENTER_CRITICAL(&alarm_mux);
    const bool accepted = !active.save_busy;
    if (accepted) {
        queued = c; queued_due = next;
        if (++serial == 0) ++serial;
        active.request_id = serial; active.save_busy = true; active.save_failed = false;
        if (request_id) *request_id = serial;
    }
    portEXIT_CRITICAL(&alarm_mux);
    return accepted;
}

extern "C" void alarm_dismiss(void)
{
    portENTER_CRITICAL(&alarm_mux);
    if (active.ringing) {active.ringing = false; ++active.revision;}
    portEXIT_CRITICAL(&alarm_mux);
    sound_alarm_stop();
}

extern "C" void alarm_service_poll(void)
{
    alarm_config_t pending; uint32_t request = 0; int64_t requested_due = 0;
    portENTER_CRITICAL(&alarm_mux);
    if (active.save_busy && !committing) {
        committing = true; pending = queued; request = active.request_id; requested_due = queued_due;
    }
    portEXIT_CRITICAL(&alarm_mux);
    if (request) {
        const bool saved = write_config(pending, 0);
        const int64_t now = (int64_t)time(nullptr);
        const int64_t next = requested_due > now - 90 ? requested_due : alarm_next_occurrence(&pending, now, 0);
        portENTER_CRITICAL(&alarm_mux);
        if (saved) {
            active.config = pending; last_fired = 0;
            active.next_epoch = next;
            active.ringing = false; active.saved_request = request;
            active.storage_warning = false; dirty = false; ++active.revision;
        } else active.failed_request = request;
        active.save_failed = !saved; active.save_busy = false; committing = false;
        portEXIT_CRITICAL(&alarm_mux);
        if (saved) sound_alarm_stop();
    }
    const uint32_t ticks = app_millis();
    const int64_t now = (int64_t)time(nullptr);
    bool fire = false, expired = false; uint8_t minutes = 1;
    alarm_config_t schedule; int64_t next = 0, fired_before = 0;
    portENTER_CRITICAL(&alarm_mux);
    if (active.ringing && ticks - ring_started >= (uint32_t)active.config.ring_minutes * 60000u) {
        active.ringing = false; expired = true; ++active.revision;
    }
    schedule = active.config; next = active.next_epoch; fired_before = last_fired;
    portEXIT_CRITICAL(&alarm_mux);
    if (schedule.enabled && clock_valid(now)) {
        const int64_t wall_delta = now - last_wall;
        const uint32_t tick_delta = ticks - last_tick;
        const bool clock_changed = last_wall && (wall_delta < 0 || wall_delta > (int64_t)(tick_delta / 1000) + 5);
        if (!next || clock_changed)
            next = alarm_next_occurrence(&schedule, now - 90, fired_before);
        if (next && now >= next) {
            if (now - next <= 90 && next > fired_before) {
                fired_before = next; minutes = schedule.ring_minutes;
                if (!schedule.weekdays) schedule.enabled = false;
                fire = true;
            }
            next = alarm_next_occurrence(&schedule, now, fired_before);
        }
    } else if (!schedule.enabled) next = 0;
    portENTER_CRITICAL(&alarm_mux);
    active.next_epoch = next;
    if (fire) {
        active.config = schedule; last_fired = fired_before;
        active.ringing = true; ring_started = ticks;
        dirty = true; last_persist_try = ticks - 5000u; ++active.revision;
    }
    last_wall = now; last_tick = ticks;
    portEXIT_CRITICAL(&alarm_mux);
    if (expired) sound_alarm_stop();
    if (fire) {
        sound_alarm_start(minutes);
        portENTER_CRITICAL(&alarm_mux);
        const bool still_ringing = active.ringing;
        portEXIT_CRITICAL(&alarm_mux);
        if (!still_ringing) sound_alarm_stop();
        else screen_idle_request_wake();
    }

    alarm_config_t persist; int64_t fired = 0; uint32_t revision = 0;
    portENTER_CRITICAL(&alarm_mux);
    if (dirty && !active.save_busy && ticks - last_persist_try >= 5000) {
        persist = active.config; fired = last_fired; revision = active.revision; last_persist_try = ticks;
    }
    portEXIT_CRITICAL(&alarm_mux);
    if (revision) {
        const bool saved = write_config(persist, fired);
        portENTER_CRITICAL(&alarm_mux);
        active.storage_warning = !saved;
        if (saved && active.revision == revision) dirty = false;
        portEXIT_CRITICAL(&alarm_mux);
    }
}
