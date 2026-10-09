"""Exercise the real display-recovery functions with a fault-injecting SDK fake.

Checks request coalescing, VSYNC timeout/wraparound, driver errors and brightness
restoration. This does not validate physical LCD recovery or FreeRTOS timing.
Run: py -3 tests/display_restart_host_test.py [--cc path/to/tcc-or-cc]
"""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    pattern = (r'(?:(?:static\s+)|(?:extern\s+"C"\s+))?'
               r'(?:bool|void|display_restart_status_t|esp_err_t)\s+(?:IRAM_ATTR\s+)?'
               + re.escape(name) + r'\s*\([^;{}]*\)\s*\{')
    match = re.search(pattern, source)
    if not match:
        raise AssertionError(f'firmware function missing: {name}')
    pos, depth = match.end(), 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    result = source[match.start():pos].replace('extern "C" ', '')
    result = result.replace('static_cast<unsigned>', '')
    result = result.replace('rgbpanel->restartController()', 'restartController()')
    result = result.replace('rgbpanel->stopTransmissionAtVsync()', 'stopTransmissionAtVsync()')
    result = result.replace('Arduino_ESP32RGBPanel::restartController', 'restartController')
    result = result.replace('Arduino_ESP32RGBPanel::stopTransmissionAtVsync', 'stopTransmissionAtVsync')
    return result


FAKE_SDK = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "DisplayControl.h"
typedef int esp_err_t;
typedef int esp_lcd_panel_handle_t;
typedef int esp_lcd_rgb_panel_event_data_t;
enum {ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT, ESP_FAIL};
#define GFX_BL 38
#define IRAM_ATTR
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define LCD_LL_GET_HW(x) (x)
static display_restart_status_t display_restart_status = DISPLAY_RESTART_IDLE;
static const uint32_t DISPLAY_RESTART_TIMEOUT_MS = 500;
static uint32_t backlight_requested_duty, output_duty, now, rgb_vsync_count;
static bool backlight_write_ok, stall, fail_blank, fail_restore, engine_stopped;
static bool stall_after_init, rgb_pause_requested, rgb_pause_complete;
static int _panel_handle, driver_calls, delays, invalidates, writes, stops, inits;
static int rgb_frame_complete_lock;
static esp_err_t driver_result;
static void stop_rgb_at_vsync(void);
static bool rgb_vsync_cb(int panel, const int *edata, void *user_ctx);
static uint32_t millis(void) {return now;}
static uint32_t read_rgb_vsync_count(void) {return rgb_vsync_count;}
static void vTaskDelay(int ticks) {
    assert(ticks == 1); ++delays; ++now;
    assert(delays <= (int)DISPLAY_RESTART_TIMEOUT_MS);
    if (!stall && !engine_stopped && delays % 20 == 0) rgb_vsync_cb(1, NULL, NULL);
}
static bool ledcWrite(int pin, uint32_t duty) {
    assert(pin == GFX_BL); ++writes;
    if ((duty == 0 && fail_blank) || (duty != 0 && fail_restore)) return false;
    output_duty = duty; return true;
}
static int lv_scr_act(void) {return 1;}
static int lv_layer_top(void) {return 2;}
static void lv_obj_invalidate(int object) {assert(object == 1 || object == 2); ++invalidates;}
static const char *esp_err_to_name(esp_err_t result) {(void)result; return "fake";}
static void quiet_log(const char *format, ...) {(void)format;}
static struct {void (*printf)(const char *, ...);} Serial = {quiet_log};
static void lcd_ll_stop(int hardware) {
    assert(hardware == 0 && rgb_pause_requested); engine_stopped = true; ++stops;
}
static esp_err_t esp_lcd_panel_reset(int handle) {
    assert(handle == 1); ++driver_calls;
    assert(engine_stopped && !rgb_pause_requested && rgb_pause_complete);
    assert(display_restart_get_status() == DISPLAY_RESTART_RUNNING);
    assert(!display_restart_request());
    return driver_result;
}
static esp_err_t esp_lcd_panel_init(int handle) {
    assert(handle == 1 && engine_stopped); ++inits;
    engine_stopped = false;
    if (stall_after_init) stall = true;
    return ESP_OK;
}
'''

CASES = r'''
static void reset(uint32_t duty) {
    display_restart_status = DISPLAY_RESTART_IDLE;
    backlight_requested_duty = output_duty = duty;
    backlight_write_ok = true; now = 0; rgb_vsync_count = 0;
    stall = fail_blank = fail_restore = engine_stopped = stall_after_init = false;
    rgb_pause_requested = rgb_pause_complete = false;
    _panel_handle = 1; driver_result = ESP_OK;
    driver_calls = delays = invalidates = writes = stops = inits = 0;
}
static void run_request(void) {
    assert(display_restart_request());
    assert(display_restart_get_status() == DISPLAY_RESTART_PENDING);
    assert(!display_restart_request());
    service_display_restart();
}
int main(void) {
    // Idle service is inert. Repeated requests use the same panel and brightness.
    reset(255); service_display_restart(); assert(driver_calls == 0 && writes == 0);
    for (int i = 0; i < 10; ++i) {
        uint32_t saved = backlight_requested_duty;
        run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_SUCCEEDED);
        assert(backlight_requested_duty == saved && output_duty == saved && backlight_write_ok);
        assert(_panel_handle == 1 && driver_calls == i + 1 && invalidates == 2 * (i + 1));
        assert(stops == i + 1 && inits == i + 1 && !engine_stopped);
        assert(writes == 2 * (i + 1));
        delays = 0;
    }
    // Both millis and the ISR counter can wrap during recovery.
    reset(213); now = UINT32_MAX - 10; rgb_vsync_count = UINT32_MAX - 1;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_SUCCEEDED);
    assert(delays == 60 && output_duty == 213 && backlight_requested_duty == 213);
    // No VSYNC: exit within 500ms, restore brightness, then permit another attempt.
    reset(197); stall = true;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(delays == 500 && invalidates == 0 && output_duty == 197 && backlight_write_ok);
    assert(driver_calls == 0 && stops == 0 && !rgb_pause_requested);
    // A late VSYNC after a cancelled request must not stop the running engine.
    rgb_vsync_cb(1, NULL, NULL); assert(stops == 0 && !engine_stopped);
    stall = false; delays = 0; run_request();
    assert(display_restart_get_status() == DISPLAY_RESTART_SUCCEEDED);
    // The resumed engine must produce new VSYNC events before claiming success.
    reset(213); stall_after_init = true;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(delays == 500 && inits == 1 && output_duty == 213 && !rgb_pause_requested);
    // Driver errors or an absent panel do not leave the backlight disabled.
    reset(255); driver_result = ESP_FAIL;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(delays == 20 && inits == 1 && !engine_stopped);
    assert(output_duty == 255 && writes == 2 && invalidates == 0);
    reset(255); _panel_handle = 0;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(driver_calls == 0 && delays == 20 && output_duty == 255);
    // PWM failures are reported; restoration is always attempted and can retry.
    reset(213); fail_blank = true;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(output_duty == 213 && writes == 2 && backlight_write_ok);
    reset(213); fail_restore = true;
    run_request(); assert(display_restart_get_status() == DISPLAY_RESTART_FAILED);
    assert(writes == 2 && !backlight_write_ok && backlight_requested_duty == 213);
    fail_restore = false; delays = 0; run_request();
    assert(display_restart_get_status() == DISPLAY_RESTART_SUCCEEDED && output_duty == 213);
    puts("display restart: VSYNC pause/resume, repeated requests, wrap/timeout, driver/PWM failures passed");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path)
    args = parser.parse_args()
    compiler = args.cc or ROOT / '.tools/host-tcc/tcc/tcc.exe'
    if not compiler.is_file():
        found = shutil.which('cc') or shutil.which('gcc') or shutil.which('clang')
        if not found:
            parser.error('C compiler unavailable; pass --cc')
        compiler = Path(found)
    main_source = (ROOT / 'src/main.cpp').read_text(encoding='utf-8')
    panel_source = (ROOT / 'include/Arduino_GFX-master/src/databus/Arduino_ESP32RGBPanel.cpp').read_text(encoding='utf-8')
    extracted = [function(main_source, 'display_restart_request'),
                 function(main_source, 'display_restart_get_status'),
                 function(panel_source, 'Arduino_ESP32RGBPanel::stopTransmissionAtVsync'),
                 function(main_source, 'stop_rgb_at_vsync'),
                 function(main_source, 'rgb_vsync_cb'),
                 function(panel_source, 'Arduino_ESP32RGBPanel::restartController'),
                 function(main_source, 'read_rgb_pause_complete'),
                 function(main_source, 'service_display_restart')]
    with tempfile.TemporaryDirectory(prefix='display-restart-') as temporary:
        source = Path(temporary) / 'display_restart_test.c'
        exe = Path(temporary) / 'display_restart_test.exe'
        source.write_text(FAKE_SDK + '\n'.join(extracted) + CASES, encoding='utf-8')
        subprocess.run([str(compiler), '-I', str(ROOT / 'src'), str(source), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
