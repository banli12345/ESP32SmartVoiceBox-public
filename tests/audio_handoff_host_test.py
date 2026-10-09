"""Run firmware audio handoff functions against a fault-injecting I2S fake.

The host only supplies SDK operations and playback endpoints; the handoff and
deferred-video decisions are extracted from the real firmware source. This
checks ordering and recovery, not physical audio quality or FreeRTOS timing.
Run: py -3 tests/audio_handoff_host_test.py [--cc path/to/tcc-or-cc]
"""
from pathlib import Path
import argparse
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r'((?:static\s+)?(?:extern\s+"C"\s+)?(?:bool|void|uint32_t)\s+'
                      + re.escape(name) + r'\s*\([^;{}]*\)\s*)\{', source)
    if not match:
        raise AssertionError(f'firmware function missing: {name}')
    start, pos, depth = match.start(), match.end(), 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    result = source[start:pos].replace('extern "C" ', '')
    # Translate scalar/pointer cast syntax, preserving even nested operands.
    while cast := re.search(r'(?:reinterpret_cast|static_cast)<([^>]+)>\(', result):
        pos, depth = cast.end(), 1
        while depth:
            depth += (result[pos] == '(') - (result[pos] == ')')
            pos += 1
        operand = result[cast.end():pos - 1]
        result = result[:cast.start()] + f'(({cast[1]})({operand}))' + result[pos:]
    result = result.replace('min(', 'host_min(')
    result = result.replace('HTTPClient http;', 'HTTPClient http = FakeHttp;')
    return result


FAKE_SDK = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef int esp_err_t;
enum {ESP_OK, ESP_ERR_INVALID_STATE, ESP_ERR_TIMEOUT, ESP_FAIL};
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define MUSIC_SAMPLE_RATE 44100u
#define AI_I2S_SAMPLE_RATE 16000u
enum {VIDEO_CMD_NONE, VIDEO_CMD_PREPARE, VIDEO_CMD_START, VIDEO_CMD_HALT, VIDEO_CMD_RELEASE};
typedef struct {uint32_t sample_rate;} i2s_std_clk_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) ((i2s_std_clk_config_t){rate})
static int music_i2s_tx=1, music_i2s_gpio_config, music_control_mux;
static bool music_i2s_enabled, ai_i2s_enabled, ai_i2s_held, ai_audio_playing;
static uint32_t output_sample_rate;
static uint8_t music_audio_buffer[8192];
static uint32_t music_dma_bytes=5760;
static int ai_audio_mutex=1, music_audio_task_handle=1, mutex_gives;
static bool music_resume_after_ai_audio, music_resume_after_video;
static bool music_audio_playing, music_audio_stop_requested, music_audio_pause_requested;
static bool video_session, video_running, video_prepared, video_cmd_ok;
static bool video_prepare_pending, video_start_pending;
static bool driver_enabled, fail_enable, fail_release, fail_mutex, fail_prepare;
static uint32_t driver_rate, fail_rate;
static int clock_changes, prepares, starts, stops, resumes, writes;
static void quiet_log(const char *fmt, ...) {(void)fmt;}
static struct {void (*printf)(const char *, ...); void (*println)(const char *, ...);}
    Serial={quiet_log, quiet_log};
static const char *esp_err_to_name(int error) {(void)error; return "injected error";}
static int xSemaphoreTake(int sem, uint32_t timeout) {(void)sem; (void)timeout; return !fail_mutex;}
static void xSemaphoreGive(int sem) {(void)sem; ++mutex_gives;}
static void xTaskNotifyGive(int task) {(void)task;}
static int i2s_channel_disable(int handle) {
    assert(handle==1);
    if (!driver_enabled) return ESP_ERR_INVALID_STATE;
    driver_enabled=false; return ESP_OK;
}
static int i2s_channel_enable(int handle) {
    assert(handle==1);
    if (fail_enable) return ESP_FAIL;
    if (driver_enabled) return ESP_ERR_INVALID_STATE;
    driver_enabled=true; return ESP_OK;
}
static int i2s_channel_reconfig_std_clock(int handle, const i2s_std_clk_config_t *clock) {
    assert(handle==1 && !driver_enabled);
    if (clock->sample_rate==fail_rate) return ESP_FAIL;
    driver_rate=clock->sample_rate; ++clock_changes; return ESP_OK;
}
static int i2s_channel_reconfig_std_gpio(int handle, const int *gpio) {
    (void)gpio; assert(handle==1 && !driver_enabled); return ESP_OK;
}
static int i2s_channel_write(int handle, const void *data, size_t count, size_t *written, int wait) {
    (void)data; (void)wait;
    assert(handle==1 && driver_enabled && driver_rate==16000);
    *written=count; ++writes; return ESP_OK;
}
static void apply_audio_volume(uint8_t *data, size_t count) {(void)data; (void)count;}
static void music_audio_play(void) {
    assert(!ai_i2s_held && output_sample_rate==44100);
    if (video_session) music_resume_after_video=true;
    else music_audio_playing=true;
    ++resumes;
}
static void free_video_ring(void) {}
static void stop_video_playback(bool log) {
    (void)log; ++stops; video_running=false; video_prepared=false;
}
static void handle_video_prepare(void) {
    assert(!ai_i2s_held); ++prepares;
    video_prepared=!fail_prepare; video_cmd_ok=video_prepared;
}
static void handle_video_start(void) {
    assert(!ai_i2s_held && output_sample_rate==44100 && video_prepared);
    ++starts; video_running=true; video_cmd_ok=true;
}
static bool disable_music_i2s_for_ai(void);
static bool request_ai_release(void) {
    if (fail_release) return false;
    assert(!ai_i2s_held);
    if (!disable_music_i2s_for_ai()) return false;
    ai_i2s_held=true; return true;
}
'''

FAKE_HTTP = r'''
#define HTTP_CODE_OK 200
static uint8_t response_bytes[48];
static size_t response_size, response_pos;
static int response_status, http_closes;
static size_t host_min(size_t a, size_t b) {return a < b ? a : b;}
typedef struct {size_t (*readBytes)(uint8_t *, size_t);} Stream;
typedef Stream WiFiClient;
static size_t response_read(uint8_t *dst, size_t count) {
    count=host_min(count,response_size-response_pos);
    memcpy(dst,response_bytes+response_pos,count); response_pos+=count; return count;
}
static Stream response_stream={response_read};
static void http_timeout(uint32_t timeout) {(void)timeout;}
static bool http_begin(const char *url) {(void)url; return true;}
static int http_get(void) {assert(!ai_i2s_held && driver_rate==44100); return response_status;}
static WiFiClient *http_stream(void) {return &response_stream;}
static void http_end(void) {++http_closes;}
static void vTaskDelay(uint32_t ticks) {(void)ticks;}
typedef struct {
    void (*setConnectTimeout)(uint32_t);
    void (*setTimeout)(uint32_t);
    bool (*begin)(const char *);
    int (*GET)(void);
    WiFiClient *(*getStreamPtr)(void);
    void (*end)(void);
} HTTPClient;
static HTTPClient FakeHttp={http_timeout,http_timeout,http_begin,http_get,http_stream,http_end};
static void put16(size_t offset, uint16_t n) {
    response_bytes[offset]=(uint8_t)n; response_bytes[offset+1]=(uint8_t)(n>>8);
}
static void put32(size_t offset, uint32_t n) {
    put16(offset,(uint16_t)n); put16(offset+2,(uint16_t)(n>>16));
}
static void reset_http(void) {
    memset(response_bytes,0,sizeof(response_bytes));
    memcpy(response_bytes,"RIFF",4); put32(4,40); memcpy(response_bytes+8,"WAVEfmt ",8);
    put32(16,16); put16(20,1); put16(22,1); put32(24,16000); put32(28,32000);
    put16(32,2); put16(34,16); memcpy(response_bytes+36,"data",4); put32(40,4);
    put16(44,100); put16(46,200);
    response_size=48; response_pos=0; response_status=200; http_closes=0;
}
'''

CASES = r'''
static void reset(void) {
    reset_http();
    music_i2s_enabled=driver_enabled=true;
    driver_rate=output_sample_rate=44100;
    ai_i2s_enabled=ai_i2s_held=ai_audio_playing=false;
    music_resume_after_ai_audio=music_resume_after_video=false;
    music_audio_playing=music_audio_stop_requested=music_audio_pause_requested=false;
    video_session=video_running=video_prepared=video_cmd_ok=false;
    video_prepare_pending=video_start_pending=false;
    fail_enable=fail_release=fail_mutex=fail_prepare=false;
    fail_rate=0; ai_audio_mutex=1;
    clock_changes=mutex_gives=prepares=starts=stops=resumes=writes=0;
}
int main(void) {
    int16_t pcm[32]={1};
    reset();
    for (int i=0; i<10; ++i) {
        assert(sound_ai_output_begin());
        assert(ai_i2s_held && ai_audio_playing && driver_rate==16000);
        assert(sound_ai_output_write(pcm,sizeof(pcm)));
        assert(sound_ai_output_finish(true));
        assert(!ai_i2s_held && !ai_audio_playing && !driver_enabled && driver_rate==44100);
        assert(enable_music_i2s_channel());
    }
    assert(clock_changes==20 && mutex_gives==10 && writes==20);
    reset(); fail_rate=16000;
    assert(!sound_ai_output_begin());
    assert(!ai_i2s_held && !ai_audio_playing && driver_rate==44100 && mutex_gives==1);
    fail_rate=0;
    assert(sound_ai_output_begin() && sound_ai_output_finish(false)==false);
    assert(!ai_i2s_held && driver_rate==44100 && mutex_gives==2);
    reset(); fail_enable=true;
    assert(!sound_ai_output_begin());
    assert(!ai_i2s_held && !ai_audio_playing && driver_rate==44100 && mutex_gives==1);
    fail_enable=false; assert(enable_music_i2s_channel());
    reset(); fail_release=true;
    assert(!sound_ai_output_begin());
    assert(driver_enabled && driver_rate==44100 && !ai_audio_playing && mutex_gives==1);
    reset(); ai_audio_mutex=0;
    assert(!sound_ai_output_begin() && driver_enabled && clock_changes==0);
    reset(); fail_mutex=true;
    assert(!sound_ai_output_begin() && driver_enabled && clock_changes==0 && mutex_gives==0);
    reset(); assert(sound_ai_output_begin()); fail_rate=44100;
    assert(!sound_ai_output_finish(true));
    assert(!ai_i2s_held && !ai_audio_playing && mutex_gives==1);
    fail_rate=0; assert(enable_music_i2s_channel() && driver_rate==44100);
    reset(); assert(sound_ai_output_begin()); music_resume_after_ai_audio=true;
    assert(sound_ai_output_finish(true) && resumes==1 && music_audio_playing);
    reset(); assert(sound_ai_output_begin());
    music_resume_after_ai_audio=true;
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    assert(video_cmd_ok && video_prepare_pending && music_resume_after_video);
    defer_video_command_for_ai(VIDEO_CMD_START);
    service_deferred_video_audio();
    assert(video_cmd_ok && video_start_pending && prepares==0 && starts==0);
    assert(sound_ai_output_finish(true));
    service_deferred_video_audio(); service_deferred_video_audio();
    assert(prepares==1 && starts==1 && video_running && !video_prepare_pending);
    reset(); assert(sound_ai_output_begin());
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    defer_video_command_for_ai(VIDEO_CMD_START);
    handle_video_command(VIDEO_CMD_RELEASE);
    assert(sound_ai_output_finish(true)); service_deferred_video_audio();
    assert(!video_session && prepares==0 && starts==0);
    reset(); assert(sound_ai_output_begin());
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    handle_video_command(VIDEO_CMD_HALT);
    assert(sound_ai_output_finish(true)); service_deferred_video_audio();
    assert(prepares==0 && starts==0);
    reset(); assert(sound_ai_output_begin());
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    assert(sound_ai_output_finish(true));
    handle_video_command(VIDEO_CMD_START); service_deferred_video_audio();
    assert(prepares==1 && starts==1);
    reset(); assert(sound_ai_output_begin());
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    assert(sound_ai_output_finish(true)); service_deferred_video_audio();
    assert(prepares==1 && starts==0);
    handle_video_command(VIDEO_CMD_START); assert(starts==1);
    reset(); assert(sound_ai_output_begin());
    defer_video_command_for_ai(VIDEO_CMD_PREPARE);
    defer_video_command_for_ai(VIDEO_CMD_START); fail_prepare=true;
    assert(sound_ai_output_finish(true)); service_deferred_video_audio();
    assert(prepares==1 && starts==0 && !video_running && !video_prepare_pending);
    reset(); response_status=503;
    assert(!play_ai_wav("fixture"));
    assert(driver_enabled && driver_rate==44100 && mutex_gives==0 && clock_changes==0);
    reset(); memcpy(response_bytes,"BAD!",4);
    assert(!play_ai_wav("fixture") && clock_changes==0 && mutex_gives==0);
    reset(); put32(24,44100);
    assert(!play_ai_wav("fixture") && clock_changes==0 && mutex_gives==0);
    reset(); put32(40,3);
    assert(!play_ai_wav("fixture") && clock_changes==0 && mutex_gives==0);
    reset(); response_size=44;
    assert(!play_ai_wav("fixture"));
    assert(!ai_i2s_held && !ai_audio_playing && driver_rate==44100 && mutex_gives==1);
    reset(); assert(play_ai_wav("fixture"));
    assert(!ai_i2s_held && !ai_audio_playing && driver_rate==44100 && mutex_gives==1);
    assert(writes==2 && http_closes==1);
    puts("audio handoff checks passed: AI failures, video queue/cancel, WAV/HTTP failure isolation");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    args = parser.parse_args()
    bundled = ROOT / '.tools/host-tcc/tcc/tcc.exe'
    compiler = args.cc or (str(bundled) if bundled.is_file() else shutil.which('cc'))
    if not compiler:
        parser.error('Specify --cc with a native C compiler')
    sound = (ROOT / 'src/Sound.cpp').read_text(encoding='utf-8')
    video = (ROOT / 'src/video_audio.inc').read_text(encoding='utf-8')
    web = (ROOT / 'src/Web_ai.cpp').read_text(encoding='utf-8')
    sound_names = ['disable_music_i2s_for_ai', 'route_music_i2s_gpio',
                   'set_output_sample_rate', 'init_ai_i2s', 'resume_music_after_ai_audio',
                   'finish_ai_audio', 'write_ai_i2s_all', 'enable_music_i2s_channel',
                   'sound_ai_output_begin', 'sound_ai_output_write', 'sound_ai_output_finish']
    video_names = ['handle_video_command', 'defer_video_command_for_ai',
                   'service_deferred_video_audio']
    bodies = [function(sound, name) for name in sound_names]
    bodies += [function(video, name) for name in video_names]
    bodies += [function(web, name) for name in ['read_ai_stream_exact',
              'skip_ai_stream_bytes', 'read_ai_le32', 'play_ai_wav']]
    declarations = [body[:body.index('{')].rstrip() + ';' for body in bodies]
    source = FAKE_SDK + FAKE_HTTP + '\n'.join(declarations + bodies) + CASES
    with tempfile.TemporaryDirectory(prefix='audio-handoff-') as temp:
        path = Path(temp)
        unit, binary = path / 'handoff.c', path / 'handoff.exe'
        unit.write_text(source, encoding='utf-8')
        subprocess.run([compiler, '-std=c11', '-Wall', '-Wextra', str(unit), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
