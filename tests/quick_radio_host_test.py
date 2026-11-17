"""Host-test quick-radio request coalescing and honest failure state."""
from pathlib import Path
import argparse, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
STUB = r'''
#include "QuickSettingsService.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
extern "C" void quick_radio_adopt(quick_radio_t radio, bool enabled);
struct Call { int radio; bool enabled; };
static std::vector<Call> calls;
static bool wifi_on = false, hotspot_on = false, ble_on = false, ble_link = false;
static int fail_radio = -1;
static bool cancel_during_ble_apply = false;
static void note(int radio, bool enabled, bool ok, bool *connected, char *detail, size_t n, const char *ok_text) {
    calls.push_back({radio, enabled});
    if (!ok) {
        if (detail && n) snprintf(detail, n, "failed");
        if (connected) *connected = false;
        return;
    }
    if (connected) *connected = radio == 2 && ble_link;
    if (detail && n) snprintf(detail, n, "%s", ok_text);
}
extern "C" void wifi_driver_lock(void) {}
extern "C" void wifi_driver_unlock(void) {}
extern "C" void wifi_radio_note_desired(bool) {}
extern "C" bool quick_wifi_driver_set(bool enabled, bool *connected, char *detail, size_t n) {
    const bool ok = fail_radio != 0;
    if (fail_radio == 0) fail_radio = -1;
    if (ok) wifi_on = enabled;
    note(0, enabled, ok, connected, detail, n, enabled ? "On" : "Off");
    return ok;
}
extern "C" bool quick_hotspot_driver_set(bool enabled, bool *connected, char *detail, size_t n) {
    const bool ok = fail_radio != 1;
    if (fail_radio == 1) fail_radio = -1;
    if (ok) hotspot_on = enabled;
    note(1, enabled, ok, connected, detail, n, enabled ? "No upstream" : "Off");
    return ok;
}
extern "C" bool quick_bluetooth_driver_set(bool enabled, bool *connected, char *detail, size_t n) {
    if (cancel_during_ble_apply) {
        cancel_during_ble_apply = false;
        quick_radio_request(QUICK_RADIO_BLUETOOTH, false);
    }
    const bool ok = fail_radio != 2;
    if (fail_radio == 2) fail_radio = -1;
    if (ok) ble_on = enabled;
    note(2, enabled, ok, connected, detail, n, enabled ? "Advertising" : "Off");
    return ok;
}
extern "C" bool quick_wifi_hw_enabled(void) { return wifi_on; }
extern "C" bool quick_hotspot_hw_enabled(void) { return hotspot_on; }
extern "C" bool quick_bluetooth_hw_enabled(void) { return ble_on; }
extern "C" bool quick_wifi_hw_connected(void) { return false; }
extern "C" bool quick_bluetooth_hw_connected(void) { return ble_on && ble_link; }
extern "C" void quick_hotspot_hw_info(quick_hotspot_info_t *info) {
    if (!info) return;
    snprintf(info->ssid, sizeof(info->ssid), "ESP32S3-AABBCC");
    snprintf(info->password, sizeof(info->password), "Abcdefghijk2");
    snprintf(info->ip, sizeof(info->ip), "192.168.4.1");
    info->clients = 0;
    info->internet_available = false;
}
static int count_radio(int radio) {
    int n = 0;
    for (const auto &call : calls) if (call.radio == radio) ++n;
    return n;
}
int main() {
    quick_settings_service_poll();
    assert(count_radio(0) == 1 && calls[0].enabled);
    assert(count_radio(1) == 0 && count_radio(2) == 0);
    calls.clear();
    assert(quick_radio_request(QUICK_RADIO_WIFI, false));
    assert(quick_radio_request(QUICK_RADIO_WIFI, true));
    assert(quick_radio_request(QUICK_RADIO_WIFI, false));
    assert(quick_radio_request(QUICK_RADIO_BLUETOOTH, true));
    assert(quick_radio_request(QUICK_RADIO_BLUETOOTH, false));
    assert(quick_radio_request(QUICK_RADIO_HOTSPOT, true));
    quick_radio_adopt(QUICK_RADIO_HOTSPOT, true);
    quick_radio_state_t busy{};
    quick_radio_get(QUICK_RADIO_WIFI, &busy);
    assert(busy.busy);
    quick_settings_service_poll();
    assert(count_radio(0) == 1 && !calls[0].enabled);
    assert(count_radio(2) == 1 && !calls[1].enabled);
    assert(count_radio(1) == 0);
    quick_radio_state_t state{};
    quick_radio_get(QUICK_RADIO_WIFI, &state);
    assert(!state.enabled && !state.busy && !state.failed);
    quick_radio_get(QUICK_RADIO_BLUETOOTH, &state);
    assert(!state.enabled && !state.failed && !state.busy);
    fail_radio = 0;
    assert(quick_radio_request(QUICK_RADIO_WIFI, true));
    quick_settings_service_poll();
    quick_radio_get(QUICK_RADIO_WIFI, &state);
    assert(!state.enabled && state.failed && !state.busy);
    assert(!quick_wifi_hw_enabled());
    quick_hotspot_info_t info{};
    quick_hotspot_get_info(&info);
    assert(strcmp(info.ssid, "ESP32S3-AABBCC") == 0);
    assert(strcmp(info.password, "12345678") != 0);
    assert(!info.internet_available);
    assert(!quick_radio_request(static_cast<quick_radio_t>(99), true));
    cancel_during_ble_apply = true;
    assert(quick_radio_request(QUICK_RADIO_BLUETOOTH, true));
    quick_settings_service_poll();
    quick_radio_get(QUICK_RADIO_BLUETOOTH, &state);
    assert(state.busy && ble_on);
    quick_settings_service_poll();
    quick_radio_get(QUICK_RADIO_BLUETOOTH, &state);
    assert(!state.busy && !state.enabled && !state.failed && !ble_on);
    puts("quick radio host passed");
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx")
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("cl") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        parser.error("Run in a C++ developer terminal or pass --cxx")
    with tempfile.TemporaryDirectory(prefix="quick-radio-", ignore_cleanup_errors=True) as temporary:
        directory = Path(temporary)
        (directory / "stub.cpp").write_text(STUB, encoding="utf-8")
        exe = directory / "quick_radio.exe"
        include = str(ROOT / "src")
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/utf-8", f"/I{include}",
                       "/DQUICK_RADIO_HOST_TEST", str(ROOT / "src" / "QuickSettingsService.cpp"),
                       str(directory / "stub.cpp"), f"/Fe:{exe}", f"/Fo{directory}\\"]
        else:
            command = [compiler, "-std=c++17", f"-I{include}", "-DQUICK_RADIO_HOST_TEST",
                       str(ROOT / "src" / "QuickSettingsService.cpp"), str(directory / "stub.cpp"),
                       "-o", str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
