#include "ScreenIdle.h"
#include "IdfRuntime.h"
#include "board_backlight.h"
#include "nvs.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {
constexpr uint16_t default_minutes = 5;
constexpr uint32_t tap_limit_ms = 220, tap_gap_ms = 350, retry_ms = 500;
uint16_t timeout_minutes = default_minutes;
uint32_t last_activity, epoch_counter, last_attempt;
bool pointer_down, swallow_release, tap_active, tap_moved, first_tap, retry;
uint32_t tap_started, first_release;
int tap_x, tap_y, first_x, first_y;
const char *pending_wake;
/* Generations prevent a delayed sensor notification from waking a later sleep. */
std::atomic<uint32_t> sleep_epoch{0}, motion_epoch{0}, published_sleep_time{0};
std::atomic<bool> external_wake{false};

bool valid_minutes(uint16_t minutes)
{
    switch (minutes) {
    case 0: case 1: case 2: case 3: case 5: case 10: case 20: case 30: case 60: return true;
    default: return false;
    }
}

void clear_taps()
{
    tap_active = tap_moved = first_tap = false;
}

bool wake(uint32_t now, const char *reason)
{
    if (!screen_idle_is_asleep()) return true;
    if (!pending_wake) {pending_wake = reason; retry = false;}
    if (retry && now - last_attempt < retry_ms) return false;
    last_attempt = now;
    retry = true;
    if (!board_backlight_set_sleeping(false)) return false;
    sleep_epoch.store(0, std::memory_order_release);
    motion_epoch.store(0, std::memory_order_release);
    last_activity = now;
    swallow_release = true;
    clear_taps();
    std::printf("[SCREEN] awake reason=%s brightness=%d\n", pending_wake, get_backlight_brightness());
    pending_wake = nullptr;
    retry = false;
    return true;
}
}

void screen_idle_init(void)
{
    timeout_minutes = default_minutes;
    nvs_handle_t handle;
    if (nvs_open("screen_idle", NVS_READONLY, &handle) == ESP_OK) {
        uint16_t stored = default_minutes;
        if (nvs_get_u16(handle, "minutes", &stored) == ESP_OK && valid_minutes(stored)) timeout_minutes = stored;
        nvs_close(handle);
    }
    sleep_epoch.store(0);
    motion_epoch.store(0);
    external_wake.store(false);
    pointer_down = swallow_release = retry = false;
    pending_wake = nullptr;
    clear_taps();
    last_activity = app_millis();
    std::printf("[SCREEN] timeout_minutes=%u\n", (unsigned)timeout_minutes);
}

uint16_t screen_idle_get_timeout(void) {return timeout_minutes;}
bool screen_idle_is_asleep(void) {return sleep_epoch.load(std::memory_order_acquire) != 0;}

bool screen_idle_set_timeout(uint16_t minutes)
{
    if (!valid_minutes(minutes)) return false;
    if (minutes != timeout_minutes) {
        nvs_handle_t handle;
        if (nvs_open("screen_idle", NVS_READWRITE, &handle) != ESP_OK) return false;
        esp_err_t result = nvs_set_u16(handle, "minutes", minutes);
        if (result == ESP_OK) result = nvs_commit(handle);
        nvs_close(handle);
        if (result != ESP_OK) return false;
        timeout_minutes = minutes;
    }
    last_activity = app_millis();
    if (screen_idle_is_asleep()) wake(last_activity, "setting");
    return true;
}

bool screen_idle_pointer(bool pressed, int x, int y, uint32_t now)
{
    const bool was_down = pointer_down;
    pointer_down = pressed;
    if (!screen_idle_is_asleep()) {
        if (pressed || was_down) last_activity = now;
        if (swallow_release) {
            if (!pressed) swallow_release = false;
            return true;
        }
        return false;
    }
    if (pressed && !was_down) {
        tap_active = true;
        tap_moved = false;
        tap_started = now;
        tap_x = x; tap_y = y;
    }
    if (tap_active && (std::abs(x - tap_x) > 40 || std::abs(y - tap_y) > 40)) tap_moved = true;
    if (!pressed && was_down) {
        const bool valid = tap_active && !tap_moved && now - tap_started <= tap_limit_ms;
        tap_active = false;
        if (valid && first_tap && now - first_release <= tap_gap_ms &&
            std::abs(tap_x - first_x) <= 50 && std::abs(tap_y - first_y) <= 50) {
            wake(now, "double-tap");
            first_tap = false;
        } else if (valid) {
            first_tap = true;
            first_release = now;
            first_x = tap_x; first_y = tap_y;
        } else first_tap = false;
    }
    if (!pressed && first_tap && now - first_release > tap_gap_ms) first_tap = false;
    return true;
}

void screen_idle_poll(uint32_t now)
{
    if (external_wake.exchange(false, std::memory_order_acq_rel)) {
        last_activity = now;
        if (screen_idle_is_asleep()) {pending_wake = "alarm"; retry = false;}
    }
    const uint32_t epoch = sleep_epoch.load(std::memory_order_acquire);
    if (epoch) {
        if (motion_epoch.exchange(0, std::memory_order_acq_rel) == epoch && !pending_wake) {
            pending_wake = "motion";
            retry = false;
        }
        if (pending_wake) wake(now, pending_wake);
        return;
    }
    if (!timeout_minutes || now - last_activity < (uint32_t)timeout_minutes * 60000U) return;
    if (retry && now - last_attempt < retry_ms) return;
    last_attempt = now;
    retry = true;
    if (!board_backlight_set_sleeping(true)) return;
    clear_taps();
    pending_wake = nullptr;
    retry = false;
    published_sleep_time.store(now, std::memory_order_relaxed);
    if (++epoch_counter == 0) ++epoch_counter;
    motion_epoch.store(0, std::memory_order_relaxed);
    sleep_epoch.store(epoch_counter, std::memory_order_release);
    std::printf("[SCREEN] asleep idle_ms=%u\n", (unsigned)(now - last_activity));
}

void screen_idle_request_wake(void)
{
    external_wake.store(true, std::memory_order_release);
}

void screen_idle_motion(float roll_rate, float pitch_rate, uint32_t now)
{
    /* This history belongs exclusively to the sensor task. */
    static uint32_t observed_epoch, high_since, previous_sample;
    static bool tracking;
    const uint32_t epoch = sleep_epoch.load(std::memory_order_acquire);
    if (!epoch || epoch != observed_epoch) {
        observed_epoch = epoch;
        tracking = false;
        previous_sample = now;
        return;
    }
    const uint32_t gap = now - previous_sample;
    previous_sample = now;
    if (now - published_sleep_time.load(std::memory_order_relaxed) < 600U || gap > 100U ||
        !std::isfinite(roll_rate) || !std::isfinite(pitch_rate) ||
        /* 0.525 rad/s is 50% above the previous 0.35 rad/s magnitude. */
        roll_rate * roll_rate + pitch_rate * pitch_rate < 0.525f * 0.525f) {
        tracking = false;
        return;
    }
    if (!tracking) {tracking = true; high_since = now;}
    else if (now - high_since >= 60U) motion_epoch.store(epoch, std::memory_order_release);
}
