"""Cache coalescing and receive-versus-playback timing. No credentials, device, or network."""
from pathlib import Path
import shutil
import subprocess
import tempfile
from ai_direct_client_host_test import ROOT, HEADERS, HARNESS

TAIL = r'''
static int field_in(const std::string &line, const char *name) {
    const auto at = line.find(name);
    assert(at != std::string::npos);
    return std::atoi(line.c_str() + at + std::strlen(name));
}
static void pace_matches_final() {
    const auto pace_at = log_capture.find("cache_pace ");
    const auto final_at = log_capture.find("speech cache=sd");
    assert(pace_at != std::string::npos && final_at != std::string::npos && pace_at < final_at);
    const std::string pace = log_capture.substr(pace_at, log_capture.find('\n', pace_at) - pace_at);
    const std::string fin = log_capture.substr(final_at, log_capture.find('\n', final_at) - final_at);
    for (const char *key : {"ttfb_ms=", "recv_ms=", "http_wait_ms=", "sd_wait_ms=", "sd_writes=", "flush_ms=", "bps=", "audio_ms=", "dl_vs_audio_x1000="})
        assert(field_in(pace, key) == field_in(fin, key));
}
int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    main_id = std::this_thread::get_id();
    ai_api_config_t cfg = {};
    strcpy(cfg.api_base, "https://api.deepseek.com");
    strcpy(cfg.api_key, "test-key-not-real");
    cfg.speak = true;
    char error[160];
    auto speak = [&] {
        const bool ok = ai_direct_speak(&cfg, "throughput", error, sizeof(error));
        assert(!cache_exists && !sound_active.load() && !live_tasks.load() && heap_live == 0);
        assert(log_capture.find("Bearer") == std::string::npos);
        assert(log_capture.find(cfg.api_key) == std::string::npos);
        assert(log_capture.find("speech cache=sd") != std::string::npos);
        return ok;
    };
    auto writes_full = [](size_t tail) {
        assert(!cache_write_sizes.empty() && cache_write_sizes.back() == tail);
        for (size_t i = 0; i + 1 < cache_write_sizes.size(); ++i) assert(cache_write_sizes[i] == 16384);
    };

    reset();
    response.assign(20000, 'a');
    read_limit = 1000;
    header_delay_ms = 37;
    assert(speak() && begin_calls == 1 && audio_finish == 1);
    assert(read_request_max == 4096 && committed_pcm == response);
    writes_full(20000 - 16384);
    assert(cache_read_sizes.size() == 5 && cache_read_sizes.back() == 20000 % 4096);
    for (size_t i = 0; i + 1 < cache_read_sizes.size(); ++i) assert(cache_read_sizes[i] == 4096);
    assert(log_field("ttfb_ms=") >= 37 && log_field("sd_writes=") == 2);
    pace_matches_final();

    reset();
    response.assign(16384, 'b');
    read_limit = 4096;
    assert(speak() && cache_write_sizes == std::vector<size_t>{16384} && committed_pcm == response && log_field("audio_ms=") == 16384 * 1000 / 48000);

    reset();
    response.assign(32868, 'c');
    read_limit = 4096;
    assert(speak() && committed_pcm == response);
    writes_full(100);
    assert(cache_read_sizes.back() == 32868 % 4096);

    reset();
    response.clear();
    declared = 0;
    assert(!speak() && begin_calls == 0 && log_field("sd_writes=") == 0 && log_capture.find("cache_pace ") == std::string::npos);

    reset();
    response.assign(16385, 'd');
    read_limit = 4096;
    assert(!speak() && begin_calls == 0 && cache_write_sizes == std::vector<size_t>({16384, 1}));

    reset();
    response.assign(40000, 'e');
    read_limit = 4096;
    cut_at = 12000;
    assert(!speak() && begin_calls == 0 && !cache_exists);

    reset();
    response.assign(40000, 'f');
    read_limit = 4096;
    fail_cache_short = true;
    assert(!speak() && begin_calls == 0 && cache_removes >= 1);

    reset();
    response.assign(40000, 'g');
    read_limit = 4096;
    writes_before_fail = 1;
    assert(!speak() && begin_calls == 0 && cache_write_sizes.size() == 2 && cache_removes >= 1);

    reset();
    response.assign(8192, 'h');
    fail_cache_flush = true;
    assert(!speak() && begin_calls == 0 && cache_removes >= 1 && log_capture.find("cache_pace ") == std::string::npos);

    reset();
    response.assign(48000, 'i');
    read_limit = 4096;
    cancel_read = true;
    assert(!speak() && begin_calls == 0 && cache_removes >= 1 && strstr(error, "停止"));

    reset();
    response.assign(48000, 'j');
    read_limit = 4096;
    http_delay_ms = 2;
    play_time_add = 20000;
    flush_delay_ms = 5000;
    assert(speak() && audio_finish == 1);
    pace_matches_final();
    assert(log_field("audio_ms=") == 1000);
    assert(log_field("recv_ms=") < 1000 && log_field("flush_ms=") >= 5000);
    assert(log_field("bps=") > 100000);
    assert(log_field("http_wait_ms=") >= 12 * 2);
    assert(log_field("dl_vs_audio_x1000=") == log_field("recv_ms="));

    reset();
    response.assign(32768, 'k');
    read_limit = 4096;
    http_delay_ms = 40;
    sd_delay_ms = 1;
    assert(speak());
    assert(log_field("http_wait_ms=") >= log_field("sd_wait_ms=") * 10);
    assert(log_field("sd_writes=") == 2 && log_field("http_wait_ms=") >= 8 * 40);

    reset();
    response.assign(32768, 'l');
    read_limit = 4096;
    http_delay_ms = 1;
    sd_delay_ms = 80;
    assert(speak());
    assert(log_field("sd_wait_ms=") > log_field("http_wait_ms=") * 5);
    assert(log_field("sd_writes=") == 2 && log_field("recv_ms=") >= log_field("sd_wait_ms="));

    puts("speech cache throughput: 4KiB HTTP reads, 16KiB SD coalescing, tail, cleanup, and receive rate excluding playback PASS");
}
'''


def main():
    compiler = shutil.which('cl')
    if not compiler:
        raise SystemExit('Use a Visual Studio C++ developer terminal')
    text = HARNESS[:HARNESS.index('int main(){')] + TAIL
    with tempfile.TemporaryDirectory(prefix='speech-throughput-', ignore_cleanup_errors=True) as tmp:
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
