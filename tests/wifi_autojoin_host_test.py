"""Compile production auto-join policy and check the GUI request boundary."""
from pathlib import Path
import argparse, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = r'''
#include "WifiProfiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static wifi_profile_store_t sample(void) {
    wifi_profile_store_t store = {};
    bool changed = false;
    assert(wifi_profile_note_success(&store, "old", 3, "good", 4, &changed));
    assert(wifi_profile_note_success(&store, "mid", 3, "", 0, &changed));
    assert(wifi_profile_note_success(&store, "new", 3, "secret", 6, &changed));
    return store;
}
static wifi_visible_ap_t ap(const char *ssid, bool open) {
    wifi_visible_ap_t item = {};
    item.ssid_len = static_cast<uint8_t>(std::strlen(ssid));
    std::memcpy(item.ssid, ssid, item.ssid_len);
    item.open_network = open;
    return item;
}

int main() {
    wifi_profile_store_t store = sample();
    wifi_visible_ap_t visible[3] = {ap("old", false), ap("mid", true), ap("stranger", false)};
    assert(wifi_autojoin_pick(&store, visible, 3, 0) == 1); /* mid is newer than old; new is hidden */
    assert(wifi_autojoin_pick(&store, visible, 3, 1u << 1) == 2); /* newest visible failed, use old */
    assert(wifi_autojoin_pick(&store, visible, 3, (1u << 1) | (1u << 2)) < 0);
    wifi_visible_ap_t alone[1] = {ap("new", false)};
    assert(wifi_autojoin_pick(&store, alone, 1, 0) == 0);
    assert(!wifi_auto_scan_due(true, true, false, false, true, 100, 0));
    assert(!wifi_auto_scan_due(false, false, false, false, true, 100, 0));
    assert(!wifi_auto_scan_due(true, false, true, false, true, 100, 0));
    assert(!wifi_auto_scan_due(true, false, false, true, true, 100, 0));
    assert(!wifi_auto_scan_due(true, false, false, false, false, 100, 0));
    assert(wifi_auto_scan_due(true, false, false, false, true, 100, 100));
    assert(!wifi_auto_scan_due(true, false, false, false, true, 100, 101));
    assert(wifi_deadline_elapsed(5, 0xfffffff0u));
    assert(!wifi_deadline_elapsed(0xfffffff0u, 0xfffffff8u));
    assert(wifi_deadline_elapsed(0xfffffff8u, 0xfffffff8u));

    wifi_attempt_t attempt = {};
    attempt.active = true;
    attempt.manual = true;
    attempt.epoch = 4;
    attempt.ssid_len = 3;
    std::memcpy(attempt.ssid, "new", 3);
    attempt.password_len = 6;
    std::memcpy(attempt.password, "secret", 6);
    assert(wifi_attempt_on_ip(&attempt, 3, "new", 3, &store) == WIFI_ATTEMPT_IGNORED);
    assert(wifi_attempt_on_ip(&attempt, 4, "old", 3, &store) == WIFI_ATTEMPT_IGNORED);
    assert(wifi_attempt_on_ip(&attempt, 4, "new", 3, &store) == WIFI_ATTEMPT_NO_WRITE);
    std::memcpy(attempt.password, "wrong", 5);
    attempt.password_len = 5;
    assert(wifi_attempt_on_ip(&attempt, 4, "new", 3, &store) == WIFI_ATTEMPT_SAVE);
    wifi_profile_store_t unchanged = store;
    assert(wifi_attempt_on_disconnect(&attempt, 3) == WIFI_ATTEMPT_IGNORED);
    assert(wifi_attempt_on_disconnect(&attempt, 4) == WIFI_ATTEMPT_FAIL);
    assert(std::memcmp(&store, &unchanged, sizeof(store)) == 0);
    assert(std::strcmp(store.items[0].password, "secret") == 0);
    attempt.epoch = 9;
    std::memcpy(attempt.ssid, "beta", 4);
    attempt.ssid_len = 4;
    assert(wifi_attempt_on_timeout(&attempt, 0xfffffff0u, 0xfffffff8u) == WIFI_ATTEMPT_IGNORED);
    assert(wifi_attempt_on_timeout(&attempt, 8, 0xfffffff0u) == WIFI_ATTEMPT_FAIL);
    assert(std::strcmp(store.items[0].password, "secret") == 0);

    wifi_request_queue_t queue = {};
    assert(!wifi_request_scan(&queue, false) && !queue.scan_pending);
    assert(wifi_request_scan(&queue, true) && queue.scan_pending);
    assert(!wifi_request_connect(&queue, "beta", "secret", false));
    assert(!queue.connect_pending);
    assert(wifi_request_connect(&queue, "alpha", "first", true));
    const uint32_t first = queue.connect_seq;
    assert(queue.password_len == 5 && std::strcmp(queue.ssid, "alpha") == 0);
    assert(wifi_request_saved(&queue, "missing", false, true) == false);
    assert(std::strcmp(queue.ssid, "alpha") == 0 && std::strcmp(queue.password, "first") == 0);
    assert(wifi_request_connect(&queue, "beta", "second", true));
    assert(queue.connect_seq != first && std::strcmp(queue.ssid, "beta") == 0);
    assert(std::strcmp(queue.password, "second") == 0 && !queue.saved_connect);
    assert(wifi_request_saved(&queue, "old", true, true));
    assert(queue.saved_connect && queue.password_len == 0 && queue.password[0] == 0);
    assert(std::strcmp(queue.ssid, "old") == 0);
    wifi_request_cancel(&queue);
    assert(!queue.connect_pending && !queue.scan_pending && queue.password[0] == 0);

    assert(wifi_radio_mode(true, true) == 3);
    assert(wifi_radio_mode(false, true) == 2);
    assert(wifi_radio_mode(true, false) == 1);
    assert(wifi_radio_mode(false, false) == 0);
    assert(!wifi_scan_epoch_current(1, 2));
    assert(wifi_scan_epoch_current(2, 2));
    puts("wifi autojoin host passed");
}
'''

def extract(text, signature):
    match = re.search(signature, text)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx")
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("cl") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        parser.error("Run in a C++ developer terminal or pass --cxx")
    source = (ROOT / "src" / "setwifi.cpp").read_text(encoding="utf-8")
    scan = extract(source, r"extern \"C\" int do_wifi_scan\(void\)\s*\{")
    connect = extract(source, r"extern \"C\" bool do_wifi_connect\(const char \*ssid, const char \*password\)\s*\{")
    saved = extract(source, r"extern \"C\" bool do_wifi_connect_saved\(const char \*ssid\)\s*\{")
    for body in (scan, connect, saved):
        for banned in ("nvs_", "calloc", "esp_wifi_scan_start", "wifi_driver_lock", "esp_wifi_connect", "password"):
            if banned == "password" and body is not connect and body is not saved:
                continue
            if banned == "password":
                assert "password" not in body or "password" in connect
        assert "nvs_" not in body and "calloc" not in body and "esp_wifi_" not in body and "wifi_driver_lock" not in body
    assert "esp_wifi_scan_start(&scan, false)" in source
    assert "esp_wifi_scan_start(&scan, true)" not in source
    assert "WIFI_MODE_AP" in source
    poll = extract(source, r"void wifi_service_poll\(void\)\s*\{")
    assert "if (!sta_user_enabled || !sta_desired)" in poll and "return;" in poll
    assert "nvs_" not in scan
    with tempfile.TemporaryDirectory(prefix="wifi-autojoin-", ignore_cleanup_errors=True) as temporary:
        folder = Path(temporary)
        unit = folder / "autojoin_test.cpp"
        exe = folder / "autojoin_test.exe"
        unit.write_text(CASES, encoding="utf-8")
        sources = [str(unit), str(ROOT / "src" / "WifiProfiles.cpp")]
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/utf-8", f"/I{ROOT / 'src'}",
                       *sources, f"/Fe:{exe}", f"/Fo{folder}\\"]
        else:
            command = [compiler, "-std=c++17", f"-I{ROOT / 'src'}", *sources, "-o", str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
