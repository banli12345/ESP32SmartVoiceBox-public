"""Exercise actual sensor polling, publication and scheduling with a fake clock.

Checks no-ready/error handling, 120ms error release, wraparound, coherent packed
coordinates, 10ms cadence, 20ms IMU cadence, and no catch-up bursts on timeout.
"""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/refresh-touch-20261116/input-host'


def function(source, name):
    match = re.search(r'^static[^\n]*\b' + name + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


PREFIX = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "touch.h"
#include "board_pins.h"
using TickType_t=uint32_t;
enum {pdTRUE=1};
#define pdMS_TO_TICKS(x) (x)
static std::atomic<uint32_t> touch_pointer_snapshot{0};
static void *i2c_mutex=(void*)1;
volatile bool i2c_busy=false;
static uint32_t now;
static bool lock_ok=true, scheduling, video, asleep;
static unsigned reads, takes, gives, loops, stall_loop;
static touch_sample_status_t sample=TOUCH_SAMPLE_NONE;
static int px,py;
static std::vector<uint32_t> samples, motions;
static uint32_t app_millis(){return now;}
static uint32_t xTaskGetTickCount(){return now;}
static int xSemaphoreTake(void *m,unsigned timeout){assert(m && timeout==0);if(!lock_ok)return 0;++takes;return pdTRUE;}
static void xSemaphoreGive(void *m){assert(m);++gives;}
extern "C" touch_sample_status_t touch_read_sample(int *x,int *y){
    assert(i2c_busy);++reads;
    if(scheduling){samples.push_back(now);now+=loops==stall_loop?50:1;}
    *x=px;*y=py;return sample;
}
static bool video_playback_active(){return video;}
static bool screen_idle_is_asleep(){return asleep;}
static void mpu_update(){motions.push_back(now);now+=3;}
struct Done {};
static void vTaskDelayUntil(uint32_t *next,uint32_t period){
    assert(period==10); *next+=period;
    if((int32_t)(*next-now)>0)now=*next;
    if(++loops==20)throw Done{};
}
'''
CASES = r'''
static void poll(touch_sample_status_t s,uint32_t t,int x=479,int y=478){sample=s;now=t;px=x;py=y;touch_poll();assert(!i2c_busy);}
static bool down(){return (touch_snapshot_load()&1)!=0;}
int main(){
    poll(TOUCH_SAMPLE_PRESSED,0);assert(down());
    assert(((touch_snapshot_load()>>1)&511)==479 && ((touch_snapshot_load()>>10)&511)==478);
    poll(TOUCH_SAMPLE_NONE,5);assert(down());
    poll(TOUCH_SAMPLE_ERROR,10);poll(TOUCH_SAMPLE_ERROR,129);assert(down());
    poll(TOUCH_SAMPLE_ERROR,130);assert(!down());
    assert(((touch_snapshot_load()>>1)&511)==479);
    poll(TOUCH_SAMPLE_PRESSED,140,3,7);assert(down());
    poll(TOUCH_SAMPLE_ERROR,150);poll(TOUCH_SAMPLE_NONE,260);assert(down());
    poll(TOUCH_SAMPLE_ERROR,270);poll(TOUCH_SAMPLE_ERROR,389);assert(down());
    poll(TOUCH_SAMPLE_RELEASED,390);assert(!down());
    poll(TOUCH_SAMPLE_PRESSED,UINT32_MAX-60,0,0);assert(down());
    poll(TOUCH_SAMPLE_ERROR,UINT32_MAX-50);poll(TOUCH_SAMPLE_ERROR,70);assert(!down());
    poll(TOUCH_SAMPLE_PRESSED,80,44,55);assert(down());
    unsigned old_reads=reads;lock_ok=false;poll(TOUCH_SAMPLE_RELEASED,81);assert(down() && reads==old_reads);
    lock_ok=true;poll(TOUCH_SAMPLE_RELEASED,82);assert(!down());
    i2c_mutex=nullptr;poll(TOUCH_SAMPLE_PRESSED,83);assert(!down());i2c_mutex=(void*)1;
    // Run the production sensor loop; independently schedule the IMU while
    // touch reads occur every 10ms, including recovery after a 50ms timeout.
    scheduling=true;sample=TOUCH_SAMPLE_NONE;stall_loop=~0u;now=0;loops=0;
    try{sensor_task(nullptr);}catch(const Done&){}
    assert(samples.size()==20 && motions.size()==9);
    for(size_t i=0;i<samples.size();i++)assert(samples[i]==i*10);
    for(size_t i=1;i<motions.size();i++)assert(motions[i]-motions[i-1]==20);
    samples.clear();motions.clear();now=0;loops=0;stall_loop=5;
    try{sensor_task(nullptr);}catch(const Done&){}
    assert(samples[6]-samples[5]>=60);
    for(size_t i=1;i<samples.size();i++)assert(samples[i]-samples[i-1]>=10);
    // During video, gyro is skipped, but sleeping still permits motion wake.
    samples.clear();motions.clear();now=0;loops=0;stall_loop=~0u;video=true;
    try{sensor_task(nullptr);}catch(const Done&){} assert(motions.empty());
    now=0;loops=0;asleep=true;
    try{sensor_task(nullptr);}catch(const Done&){} assert(!motions.empty());
    assert(reads<=gives && takes==gives);
    puts("input: coherent samples, no-ready hold, bounded error release, wrap, 10ms touch/20ms IMU, timeout cadence PASS");
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    source = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
    unit = OUT/'test.cpp'
    unit.write_text(PREFIX + '\n'.join(function(source, name) for name in
                                     ['touch_snapshot_load', 'touch_poll', 'sensor_task']) + CASES, encoding='utf-8')
    compiler = shutil.which('cl.exe')
    if not compiler:
        raise SystemExit('Run from the MSVC developer environment (cl.exe required).')
    result = subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/O2', '/utf-8',
                             '/I'+str(ROOT/'include'), '/I'+str(ROOT/'src'), str(unit),
                             '/Fe:'+str(OUT/'test.exe'), '/Fo'+str(OUT)+'\\'], capture_output=True)
    if result.returncode:
        print((result.stdout+result.stderr).decode(errors='replace')[-4000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(OUT/'test.exe')], check=True, timeout=10)


if __name__ == '__main__':
    main()
