"""Exercise PCM read recovery and clock accounting during output starvation.

Run: py -3 tests/video_audio_read_host_test.py [--cc path/to/tcc-or-cc]
Only SDK/file operations are faked; recovery logic comes from video_audio.inc.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import tempfile

from audio_handoff_host_test import function

ROOT = Path(__file__).resolve().parents[1]

FAKE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
static uint8_t source[4096], video_stage[2048];
static int video_wav_file, calls, seeks, delays, errors;
static uint32_t cursor, length, short_first;
static uint32_t video_data_offset, video_data_bytes, video_data_remaining;
static bool fail_once, fail_always, fail_seek, cancel_on_delay, video_wav_stop;
static char video_wav_path[]="/videos/test.wav";
static uint8_t music_audio_buffer[8192];
static int music_i2s_tx=1, video_i2s_mux;
static uint32_t video_i2s_skip_bytes, isr_consumed;
static size_t output_written;
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
static int i2s_channel_write(int handle, const void *data, size_t count,
                             size_t *written, unsigned timeout) {
    assert(handle==1 && data==music_audio_buffer && timeout>0);
    assert(output_written<=count);
    /* ISR completes older DMA while the writer waits, even on a timeout. */
    uint32_t used=isr_consumed<video_i2s_skip_bytes ? isr_consumed : video_i2s_skip_bytes;
    video_i2s_skip_bytes-=used;
    *written=output_written;
    return output_written==count ? 0 : -1;
}
static bool fake_seek(uint32_t offset) {
    ++seeks;
    if (fail_seek || offset>length) return false;
    cursor=offset;
    return true;
}
static int video_wav_reader_seek(void *ctx, uint32_t offset) {
    assert(ctx==&video_wav_file);
    return fake_seek(offset) ? 0 : -1;
}
static int wav_file_read(void *ctx, void *dst, uint32_t count) {
    assert(ctx==&video_wav_file);
    ++calls;
    if (fail_always || (fail_once && calls==1)) return -1;
    if (short_first && calls==1 && count>short_first) count=short_first;
    if (count>length-cursor) count=length-cursor;
    memcpy(dst,source+cursor,count);
    cursor+=count;
    return (int)count;
}
#define pdMS_TO_TICKS(ms) (ms)
static void vTaskDelay(uint32_t ticks) {
    assert(ticks>0);
    ++delays;
    if (cancel_on_delay) video_wav_stop=true;
}
static void quiet_log(const char *fmt, ...) {(void)fmt; ++errors;}
static struct {void (*printf)(const char *, ...);} Serial={quiet_log};
static void reset(void) {
    cursor=0; length=sizeof(source); short_first=0;
    video_data_offset=0; video_data_bytes=video_data_remaining=length;
    calls=seeks=delays=errors=0;
    fail_once=fail_always=fail_seek=cancel_on_delay=video_wav_stop=false;
    memset(video_stage,0xcc,sizeof(video_stage));
    for (size_t i=0;i<sizeof(source);++i) source[i]=(uint8_t)(i*37u+11u);
}
'''

TESTS = r'''
int main(void) {
    reset();
    assert(read_video_stage(2048,true));
    assert(calls==1 && seeks==0 && cursor==2048);
    assert(memcmp(source,video_stage,2048)==0);

    /* Preserve an odd partial read: no duplicated/lost bytes or sample shift. */
    reset(); cursor=44; video_data_offset=44; video_data_bytes-=44;
    video_data_remaining=video_data_bytes; short_first=13;
    assert(read_video_stage(2048,true));
    assert(calls==2 && seeks==1 && cursor==2092);
    assert(memcmp(source+44,video_stage,2048)==0);

    video_data_remaining-=2048;
    assert(read_video_stage(512,true));
    assert(cursor==2604 && memcmp(source+2092,video_stage,512)==0);

    reset(); cursor=311; video_data_remaining-=311; fail_once=true;
    assert(read_video_stage(1024,true));
    assert(calls==2 && seeks==1 && errors==0);
    assert(memcmp(source+311,video_stage,1024)==0);

    /* Persistent failure/truncation must stop within a bounded retry budget. */
    reset(); fail_always=true;
    assert(!read_video_stage(2048,true));
    assert(calls==4 && seeks==3 && delays==3 && cursor==0 && errors==1);
    reset(); cursor=44; video_data_remaining-=44; length=64;
    assert(!read_video_stage(2048,true));
    assert(calls==4 && cursor==64 && errors==1);
    assert(memcmp(source+44,video_stage,20)==0);

    reset(); short_first=8; fail_seek=true;
    assert(!read_video_stage(2048,true));
    assert(calls==1 && seeks==1 && delays==0 && errors==1);

    /* Exit during retry is cancellation, not a media error. Initial prefill
       ignores the previous prefetch task's stop flag. */
    reset(); short_first=8; cancel_on_delay=true;
    assert(!read_video_stage(2048,true));
    assert(calls==1 && delays==1 && errors==0);
    reset(); video_wav_stop=true;
    assert(!read_video_stage(2048,true) && calls==0 && errors==0);
    reset(); video_wav_stop=true;
    assert(read_video_stage(2048,false));
    assert(memcmp(source,video_stage,2048)==0);

    video_i2s_skip_bytes=0; output_written=960; isr_consumed=0;
    queue_video_silence(960);
    assert(video_i2s_skip_bytes==960);
    video_i2s_skip_bytes=0; output_written=480; isr_consumed=240;
    queue_video_silence(960);
    assert(video_i2s_skip_bytes==240);
    video_i2s_skip_bytes=0; output_written=0; isr_consumed=480;
    queue_video_silence(960);
    assert(video_i2s_skip_bytes==0);
    video_i2s_skip_bytes=0; output_written=480; isr_consumed=960;
    queue_video_silence(960);
    assert(video_i2s_skip_bytes==0);
    puts("video PCM read checks passed: partial reads, transient SD failure, truncation, seek failure, cancellation");
    puts("video silence clock checks passed: full/partial writes, timeout with ISR progress; no counter wrap");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc')
    args = parser.parse_args()
    cc = args.cc or shutil.which('cc') or shutil.which('gcc') or shutil.which('tcc')
    if not cc:
        raise SystemExit('Pass --cc with a host C compiler')
    firmware = (ROOT / 'src/video_audio.inc').read_text(encoding='utf-8')
    reader = function(firmware, 'read_video_stage')
    silence = function(firmware, 'queue_video_silence')
    with tempfile.TemporaryDirectory(prefix='video-pcm-read-') as temp:
        source = Path(temp) / 'test.c'
        binary = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
        source.write_text(FAKE + reader + silence + TESTS, encoding='utf-8')
        subprocess.run([str(Path(cc).resolve()), str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
