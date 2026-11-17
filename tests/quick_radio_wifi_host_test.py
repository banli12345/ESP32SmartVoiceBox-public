"""Host-test hotspot identity and that saved WiFi passwords are not the passphrase."""
from pathlib import Path
import argparse, re, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]

def extract(text, signature):
    match = re.search(signature, text)
    assert match, signature
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]

SOURCE = r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
static void hotspot_format_ssid(char *ssid, size_t ssid_len, const uint8_t mac[6]);
static bool hotspot_generate_password(char *out, size_t out_len, uint32_t (*rng)(void));
'''
CASES = r'''
static uint32_t sequence = 1;
static uint32_t rng(void) { sequence = sequence * 1664525u + 1013904223u; return sequence; }
int main() {
    char ssid[33];
    const uint8_t mac[6] = {0x10, 0x20, 0x30, 0xAA, 0xBB, 0xCC};
    hotspot_format_ssid(ssid, sizeof(ssid), mac);
    assert(strcmp(ssid, "ESP32S3-AABBCC") == 0);
    char password[16];
    assert(hotspot_generate_password(password, sizeof(password), rng));
    assert(strlen(password) == 12);
    assert(strcmp(password, "12345678") != 0);
    assert(strchr(password, ' ') == nullptr);
    char again[16];
    assert(hotspot_generate_password(again, sizeof(again), rng));
    assert(strcmp(password, again) != 0);
    assert(!hotspot_generate_password(password, 4, rng));
    puts("quick radio wifi host passed");
}
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx")
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("cl") or shutil.which("c++") or shutil.which("g++")
    if not compiler:
        parser.error("Run in a C++ developer terminal or pass --cxx")
    text = (ROOT / "src" / "setwifi.cpp").read_text(encoding="utf-8")
    assert "esp_netif_napt_enable" in text
    assert "ip_napt_enable" not in text
    assert "if (!sta_user_enabled || !sta_desired) return;" in text
    assert 'nvs_open("hotspot"' in text
    for line in text.splitlines():
        if "ESP_LOG" in line and "hotspot_password" in line:
            raise SystemExit("hotspot password must not be logged")
    body = "\n".join([
        extract(text, r"static void hotspot_format_ssid\([^;]*\)\s*\{"),
        extract(text, r"static bool hotspot_generate_password\([^;]*\)\s*\{"),
    ])
    with tempfile.TemporaryDirectory(prefix="quick-radio-wifi-", ignore_cleanup_errors=True) as temporary:
        unit = Path(temporary) / "wifi.cpp"
        exe = Path(temporary) / "wifi.exe"
        unit.write_text(SOURCE + body + CASES, encoding="utf-8")
        if Path(compiler).stem.lower() == "cl":
            command = [compiler, "/nologo", "/std:c++17", "/EHsc", "/utf-8", str(unit), f"/Fe:{exe}",
                       f"/Fo{Path(temporary) / 'wifi.obj'}"]
        else:
            command = [compiler, "-std=c++17", str(unit), "-o", str(exe)]
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    main()
