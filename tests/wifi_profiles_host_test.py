"""Compile production WiFi profile storage against a fake NVS namespace."""
from pathlib import Path
import argparse, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]
CASES = r'''
#include "WifiProfiles.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::map<std::string, std::vector<uint8_t>> committed;
static std::map<std::string, std::vector<uint8_t>> staged;
static std::string other = "keep-me";
static int fail_set = 0, fail_commit = 0, fail_open = 0, erases = 0;
static bool open = false;

static int open_ns(void *, int) {
    if (fail_open == 2) return WIFI_NVS_NOT_FOUND;
    if (fail_open) return WIFI_NVS_ERR;
    staged = committed;
    open = true;
    return WIFI_NVS_OK;
}
static void close_ns(void *) { open = false; staged.clear(); }
static int get_blob(void *, const char *key, void *buffer, size_t *length) {
    assert(open && length);
    auto it = staged.find(key);
    if (it == staged.end()) return WIFI_NVS_NOT_FOUND;
    if (*length < it->second.size()) return WIFI_NVS_ERR;
    std::memcpy(buffer, it->second.data(), it->second.size());
    *length = it->second.size();
    return WIFI_NVS_OK;
}
static int set_blob(void *, const char *key, const void *buffer, size_t length) {
    assert(open && key && std::strcmp(key, "profiles") == 0);
    assert(other == "keep-me");
    if (fail_set) return WIFI_NVS_ERR;
    staged[key] = std::vector<uint8_t>(static_cast<const uint8_t *>(buffer), static_cast<const uint8_t *>(buffer) + length);
    return WIFI_NVS_OK;
}
static int get_str(void *, const char *key, char *buffer, size_t *length) {
    assert(open && length);
    auto it = staged.find(key);
    if (it == staged.end()) return WIFI_NVS_NOT_FOUND;
    if (*length < it->second.size()) return WIFI_NVS_ERR;
    std::memcpy(buffer, it->second.data(), it->second.size());
    *length = it->second.size();
    return WIFI_NVS_OK;
}
static int commit(void *) {
    assert(open);
    if (fail_commit) return WIFI_NVS_ERR;
    committed = staged;
    return WIFI_NVS_OK;
}
static wifi_nvs_io_t io() {
    wifi_nvs_io_t nvs = {};
    nvs.open_ns = open_ns; nvs.close_ns = close_ns; nvs.get_blob = get_blob;
    nvs.set_blob = set_blob; nvs.get_str = get_str; nvs.commit = commit;
    return nvs;
}
static void put_str(const char *key, const char *value) {
    committed[key] = std::vector<uint8_t>(value, value + std::strlen(value) + 1);
}
static bool same_front(const wifi_profile_store_t &store, const char *ssid, const char *password) {
    return store.count > 0 && std::strcmp(store.items[0].ssid, ssid) == 0 &&
           std::strcmp(store.items[0].password, password) == 0;
}

int main() {
    char ssid32[33];
    std::memset(ssid32, 'S', 32); ssid32[32] = 0;
    char pass63[64];
    std::memset(pass63, 'p', 63); pass63[63] = 0;
    assert(!wifi_profile_credentials_ok(nullptr, 1, "", 0));
    assert(!wifi_profile_credentials_ok("", 0, "", 0));
    assert(!wifi_profile_credentials_ok("x", 1, pass63, 64));
    assert(wifi_profile_credentials_ok(ssid32, 32, "", 0));
    assert(wifi_profile_credentials_ok("open", 4, "", 0));

    wifi_profile_store_t store = {};
    bool changed = true;
    assert(wifi_profile_note_success(&store, "alpha", 5, "one", 3, &changed));
    assert(changed && store.count == 1 && same_front(store, "alpha", "one"));
    assert(wifi_profile_note_success(&store, "alpha", 5, "one", 3, &changed));
    assert(!changed && store.count == 1);
    assert(wifi_profile_note_success(&store, "beta", 4, "", 0, &changed));
    assert(changed && same_front(store, "beta", "") && std::strcmp(store.items[1].ssid, "alpha") == 0);
    assert(wifi_profile_note_success(&store, "alpha", 5, "two", 3, &changed));
    assert(changed && same_front(store, "alpha", "two") && store.count == 2);
    assert(std::strcmp(store.items[1].password, "") == 0);

    const char *names[8] = {"n0","n1","n2","n3","n4","n5","n6","n7"};
    wifi_profile_store_clear(&store);
    for (int i = 0; i < 8; ++i) assert(wifi_profile_note_success(&store, names[i], 2, "pw", 2, &changed));
    assert(store.count == 8 && same_front(store, "n7", "pw") && std::strcmp(store.items[7].ssid, "n0") == 0);
    assert(wifi_profile_note_success(&store, ssid32, 32, pass63, 63, &changed));
    assert(store.count == 8 && same_front(store, ssid32, pass63));
    assert(std::strcmp(store.items[7].ssid, "n1") == 0);
    for (uint8_t i = 0; i < store.count; ++i)
        for (uint8_t j = i + 1; j < store.count; ++j)
            assert(std::strcmp(store.items[i].ssid, store.items[j].ssid) != 0);

    wifi_nvs_io_t nvs = io();
    assert(wifi_profiles_store(&store, &nvs));
    assert(committed.count("other") == 0);
    committed["other"] = {1, 2, 3, 4};
    wifi_profile_store_t loaded = {};
    loaded.count = 3;
    std::memcpy(loaded.items[0].ssid, "sentinel", 8);
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_LOADED);
    assert(loaded.count == 8 && same_front(loaded, ssid32, pass63));
    assert(committed["other"].size() == 4);

    fail_set = 1;
    wifi_profile_store_t rejected = loaded;
    assert(!wifi_profiles_store(&store, &nvs));
    fail_set = 0;
    assert(wifi_profiles_load(&rejected, &nvs) == WIFI_PROFILES_LOADED);
    assert(same_front(rejected, ssid32, pass63) && committed["other"].size() == 4);
    fail_commit = 1;
    assert(!wifi_profiles_store(&store, &nvs));
    fail_commit = 0;
    wifi_profile_store_clear(&loaded);
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_LOADED);
    assert(same_front(loaded, ssid32, pass63));
    assert(erases == 0 && committed["other"].size() == 4);

    auto corrupt = [&](auto mutate) {
        wifi_profile_store_t fresh = {};
        std::memcpy(fresh.items[0].ssid, "sentinel", 8);
        fresh.count = 1;
        auto saved = committed["profiles"];
        mutate(committed["profiles"]);
        assert(wifi_profiles_load(&fresh, &nvs) == WIFI_PROFILES_REJECTED);
        assert(fresh.count == 1 && std::strcmp(fresh.items[0].ssid, "sentinel") == 0);
        assert(committed["other"].size() == 4 && erases == 0);
        committed["profiles"] = saved;
    };
    corrupt([](std::vector<uint8_t> &blob) { blob[0] ^= 0xff; });
    corrupt([](std::vector<uint8_t> &blob) { blob[4] = 9; });
    corrupt([](std::vector<uint8_t> &blob) { blob[5] = 9; });
    corrupt([](std::vector<uint8_t> &blob) { blob.resize(20); });
    corrupt([](std::vector<uint8_t> &blob) { blob[8 + 2] = 0; });
    corrupt([](std::vector<uint8_t> &blob) {
        blob[5] = 2;
        std::memcpy(blob.data() + 8 + 99, blob.data() + 8, 99);
    });
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_LOADED);

    committed.erase("profiles");
    put_str("ssid", "legacy");
    put_str("password", "legacy-secret");
    put_str("city", "nanning");
    wifi_profile_store_clear(&loaded);
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_MIGRATED);
    assert(same_front(loaded, "legacy", "legacy-secret"));
    assert(committed.count("profiles") == 0);
    assert(std::string(reinterpret_cast<char *>(committed["city"].data())) == "nanning");
    assert(wifi_profiles_store(&loaded, &nvs));
    assert(committed.count("ssid") == 1 && committed.count("password") == 1);
    wifi_profile_store_clear(&loaded);
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_LOADED);
    assert(same_front(loaded, "legacy", "legacy-secret"));

    committed.erase("profiles");
    committed.erase("ssid");
    committed.erase("password");
    assert(wifi_profiles_load(&loaded, &nvs) == WIFI_PROFILES_NOT_FOUND);
    fail_open = 1;
    wifi_profile_store_t kept = loaded;
    assert(wifi_profiles_load(&kept, &nvs) == WIFI_PROFILES_IO_ERROR);
    assert(same_front(kept, "legacy", "legacy-secret"));
    fail_open = 0;
    fail_open = 2;
    wifi_profile_store_t first_boot = store;
    const wifi_nvs_io_t first_io = io();
    assert(wifi_profiles_load(&first_boot, &first_io) == WIFI_PROFILES_NOT_FOUND);
    assert(first_boot.count == 0);
    fail_open = 0;
    puts("wifi profiles host passed");
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx")
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("cl") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        parser.error("Run in a C++ developer terminal or pass --cxx")
    with tempfile.TemporaryDirectory(prefix="wifi-profiles-", ignore_cleanup_errors=True) as temporary:
        folder = Path(temporary)
        unit = folder / "profiles_test.cpp"
        exe = folder / "profiles_test.exe"
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
