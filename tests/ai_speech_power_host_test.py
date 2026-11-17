"""Modem power-save boost and cache-only status timing. No credentials, device, or network."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from ai_direct_client_host_test import ROOT, HEADERS, HARNESS

TAIL = r'''
static int log_count(const char *text) {
    int count = 0;
    for (size_t at = 0; (at = log_capture.find(text, at)) != std::string::npos; at += std::strlen(text)) ++count;
    return count;
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    main_id = std::this_thread::get_id();
    ai_api_config_t cfg = {};
    strcpy(cfg.api_base, "https://api.deepseek.com");
    strcpy(cfg.api_key, "test-key-not-real");
    cfg.speak = true;
    char error[160];
    auto idle_zero = [] {
        ai_speech_status_t state;
        ai_speech_get_status(&state);
        assert(state.phase == AI_SPEECH_IDLE);
        assert(state.cache_elapsed_ms == 0 && state.cache_bytes_per_second == 0);
    };
    auto speak = [&] {
        const bool ok = ai_direct_speak(&cfg, "power", error, sizeof(error));
        assert(!cache_exists && !sound_active.load() && !live_tasks.load() && heap_live == 0);
        assert(log_capture.find("Bearer") == std::string::npos);
        assert(log_capture.find(cfg.api_key) == std::string::npos);
        assert(log_capture.find("power") == std::string::npos);
        idle_zero();
        return ok;
    };
    auto arm = [&](wifi_ps_type_t mode, size_t bytes) {
        reset();
        wifi_ps_mode = mode;
        response.assign(bytes, 'p');
        read_limit = 4096;
    };

    idle_zero();
    arm(WIFI_PS_MIN_MODEM, 16384);
    assert(speak() && begin_calls == 1);
    assert(wifi_get_calls >= 1 && wifi_set_calls == 2 && wifi_sets_at_i2s == 2);
    assert(wifi_ps_at_prepare == WIFI_PS_MIN_MODEM);
    assert(wifi_ps_at_read == WIFI_PS_NONE && wifi_ps_at_i2s == WIFI_PS_MIN_MODEM);
    assert(wifi_ps_mode == WIFI_PS_MIN_MODEM);
    assert(log_field("ps_from=") == WIFI_PS_MIN_MODEM && log_field("boost=") == 1 && log_field("restore=") == 1);
    assert(log_count("wifi ps restore failed") == 0);

    arm(WIFI_PS_MAX_MODEM, 8192);
    assert(speak() && wifi_set_calls == 2 && wifi_ps_at_read == WIFI_PS_NONE);
    assert(wifi_ps_at_i2s == WIFI_PS_MAX_MODEM && wifi_ps_mode == WIFI_PS_MAX_MODEM);
    assert(log_field("ps_from=") == WIFI_PS_MAX_MODEM && log_field("boost=") == 1 && log_field("restore=") == 1);

    arm(WIFI_PS_NONE, 8192);
    assert(speak() && wifi_set_calls == 0 && wifi_get_calls >= 1);
    assert(wifi_ps_at_read == WIFI_PS_NONE && wifi_ps_at_i2s == WIFI_PS_NONE && wifi_ps_mode == WIFI_PS_NONE);
    assert(log_field("ps_from=") == WIFI_PS_NONE && log_field("boost=") == 0 && log_field("restore=") == 0);

    arm(WIFI_PS_MIN_MODEM, 8192);
    wifi_get_error = 12289;
    assert(speak() && wifi_set_calls == 0 && wifi_ps_mode == WIFI_PS_MIN_MODEM);
    assert(wifi_ps_at_read == WIFI_PS_MIN_MODEM && wifi_ps_at_i2s == WIFI_PS_MIN_MODEM);
    assert(log_count("wifi ps get failed err=12289") == 1);
    assert(log_field("ps_from=") == -1 && log_field("boost=") == 0 && log_field("restore=") == 0);

    arm(WIFI_PS_MAX_MODEM, 8192);
    wifi_set_error = 9;
    assert(speak() && wifi_set_calls == 1 && wifi_ps_mode == WIFI_PS_MAX_MODEM);
    assert(wifi_ps_at_read == WIFI_PS_MAX_MODEM && wifi_ps_at_i2s == WIFI_PS_MAX_MODEM);
    assert(log_count("wifi ps set none failed err=9") == 1);
    assert(log_count("wifi ps restore failed") == 0);
    assert(log_field("ps_from=") == WIFI_PS_MAX_MODEM && log_field("boost=") == 0 && log_field("restore=") == 0);

    arm(WIFI_PS_MIN_MODEM, 8192);
    wifi_restore_error = 11;
    assert(speak() && begin_calls == 1 && wifi_set_calls == 4);
    assert(wifi_ps_at_read == WIFI_PS_NONE && wifi_ps_at_i2s == WIFI_PS_NONE && wifi_ps_mode == WIFI_PS_NONE);
    assert(log_count("wifi ps restore failed err=11") == 1);
    assert(log_capture.find("restore ok") == std::string::npos);
    assert(log_field("boost=") == 1 && log_field("restore=") == 0 && log_field("ps_from=") == WIFI_PS_MIN_MODEM);

    arm(WIFI_PS_MIN_MODEM, 8192);
    wifi_restore_transient = 1;
    assert(speak() && wifi_set_calls == 3 && wifi_sets_at_i2s == 3);
    assert(wifi_restore_transient == 0 && wifi_ps_at_i2s == WIFI_PS_MIN_MODEM && wifi_ps_mode == WIFI_PS_MIN_MODEM);
    assert(log_field("restore=") == 1 && log_count("wifi ps restore failed") == 0);

    struct Early { void (*set)(); const char *name; };
    Early early[] = {
        {[] { fail_open = true; }, "open"},
        {[] { fail_header = true; }, "header"},
        {[] { fail_client = true; }, "client"},
        {[] { status = 401; }, "http"},
        {[] { content_type = "application/json"; }, "type"},
        {[] { incomplete = true; }, "incomplete"},
        {[] { cut_at = 1000; }, "cut"},
        {[] { response = "xxx"; }, "odd"},
        {[] { fail_cache_flush = true; }, "flush"},
        {[] { fail_cache_reopen = true; }, "reopen"},
        {[] { corrupt_cache = true; }, "size"},
        {[] { response.assign(48000, 'c'); cancel_read = true; }, "cancel"},
    };
    for (const Early &item : early) {
        arm(WIFI_PS_MIN_MODEM, 16384);
        item.set();
        assert(!speak() && begin_calls == 0);
        assert(wifi_set_calls == 2 && wifi_ps_mode == WIFI_PS_MIN_MODEM);
        assert(log_capture.find("cache_pace ") == std::string::npos);
        assert(log_count("wifi ps restore failed") == 0);
        (void)item.name;
    }
    arm(WIFI_PS_MAX_MODEM, 8192);
    sd = false;
    assert(!speak() && wifi_get_calls == 0 && wifi_set_calls == 0 && wifi_ps_mode == WIFI_PS_MAX_MODEM);

    reset();
    now.store(5000);
    wifi_ps_mode = WIFI_PS_MIN_MODEM;
    response.assign(16384, 'q');
    read_limit = 4096;
    header_delay_ms = 100;
    stall_in_flush_ms = 300;
    play_time_add = 20000;
    sample_on_dir = sample_on_flush = sample_on_begin = true;
    idle_zero();
    assert(speak() && begin_calls == 1);
    assert(status_at_dir.phase == AI_SPEECH_CACHING);
    assert(status_at_dir.cache_elapsed_ms == 0 && status_at_dir.cache_bytes_per_second == 0);
    assert(status_at_dir.cached_bytes == 0);
    assert(wifi_ps_at_prepare == WIFI_PS_MIN_MODEM);
    assert(status_flush_before.phase == AI_SPEECH_CACHING);
    assert(status_flush_before.cached_bytes == 16384 && status_flush_before.expected_bytes == 16384);
    assert(status_flush_before.cache_elapsed_ms == 100);
    assert(status_flush_before.cache_bytes_per_second == 163840);
    assert(status_flush_after.phase == AI_SPEECH_CACHING);
    assert(status_flush_after.cached_bytes == 16384 && status_flush_after.cache_elapsed_ms == 400);
    assert(status_flush_after.cache_bytes_per_second == 40960);
    assert(status_flush_after.cache_bytes_per_second < status_flush_before.cache_bytes_per_second);
    assert(status_at_begin.phase == AI_SPEECH_PLAYING);
    assert(status_at_begin.cache_elapsed_ms == 400 && status_at_begin.cache_bytes_per_second == 40960);
    assert(wifi_ps_at_read == WIFI_PS_NONE && wifi_ps_at_i2s == WIFI_PS_MIN_MODEM && wifi_ps_mode == WIFI_PS_MIN_MODEM);

    const uint32_t clock = now.load();
    assert(clock >= 25000);
    reset();
    now.store(clock);
    idle_zero();
    wifi_ps_mode = WIFI_PS_MIN_MODEM;
    response.assign(4294976, 'r');
    read_limit = 4096;
    header_delay_ms = 100;
    sample_on_begin = true;
    assert(speak());
    assert(status_at_begin.phase == AI_SPEECH_PLAYING);
    assert(status_at_begin.cache_elapsed_ms == 100);
    assert(status_at_begin.cache_bytes_per_second == 42949760);
    assert(status_at_begin.cached_bytes == 4294976 && status_at_begin.expected_bytes == 4294976);

    const uint32_t after_large = now.load();
    reset();
    now.store(after_large);
    response.assign(8192, 's');
    read_limit = 4096;
    header_delay_ms = 40;
    sample_on_begin = true;
    assert(speak());
    assert(status_at_begin.cache_elapsed_ms == 40);
    assert(status_at_begin.cache_bytes_per_second == 204800);
    assert(status_at_begin.phase == AI_SPEECH_PLAYING);
    idle_zero();

    puts("speech power: temporary modem boost with restore, and cache elapsed/rate excluding playback PASS");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    text = HARNESS[:HARNESS.index('int main(){')] + TAIL
    with tempfile.TemporaryDirectory(prefix='speech-power-', ignore_cleanup_errors=True) as tmp:
        directory = Path(tmp)
        for name, content in HEADERS.items():
            file = directory / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content, encoding='utf-8')
        unit = directory / 'test.cpp'
        unit.write_text(text.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
        sdk = Path('C:/Espressif/frameworks/esp-idf-v5.5.6/components/json/cJSON')
        exe = directory / 'test.exe'
        built = subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/utf-8',
                                '/D_CRT_SECURE_NO_WARNINGS', f'/I{directory}', f'/I{ROOT / "src"}',
                                f'/I{sdk}', str(unit), str(sdk / 'cJSON.c'), f'/Fe:{exe}', f'/Fo{directory}\\'],
                               capture_output=True)
        if built.returncode:
            print((built.stdout + built.stderr).decode(errors='replace')[-5000:])
            raise SystemExit(built.returncode)
        result = subprocess.run([str(exe)], check=False, timeout=90, capture_output=True, text=True)
        print(result.stdout)
        print(result.stderr)
        if result.returncode:
            raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
