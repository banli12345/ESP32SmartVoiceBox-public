"""Exercise the production semaphore frame fence with stale IRQs and tick wrap."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
static void *s_frame_done=(void*)1;
static TickType_t now;
static uint32_t frames;
static unsigned takes, stale, event_in;
static uint32_t xTaskGetTickCount(void){return now;}
static uint32_t board_lcd_read_frame_complete_count(void){return frames;}
static int xSemaphoreTake(void *s,unsigned ticks){
    assert(s && ticks);++takes;
    if(stale){--stale;++now;return 1;}
    if(event_in && event_in<=ticks){now+=event_in;event_in=0;++frames;return 1;}
    if(event_in)event_in-=ticks;
    now+=ticks;return 0;
}
'''
CASES = r'''
int main(void){
    frames=42;event_in=7;assert(board_lcd_wait_frame_complete(42,100));assert(now==7 && frames==43);
    stale=1;event_in=8;unsigned before=takes;
    assert(board_lcd_wait_frame_complete(43,100));assert(takes==before+2 && frames==44 && now==16);
    stale=1;before=takes;uint32_t started=now;
    assert(!board_lcd_wait_frame_complete(44,12));assert(now-started==12 && takes==before+2);
    before=takes;assert(board_lcd_wait_frame_complete(43,100));assert(takes==before);
    frames=UINT32_MAX;now=UINT32_MAX-3;event_in=9;
    assert(board_lcd_wait_frame_complete(UINT32_MAX,20));assert(frames==0 && now==5);
    started=now;assert(!board_lcd_wait_frame_complete(0,0));assert(now-started==1);
    s_frame_done=0;before=takes;
    assert(!board_lcd_wait_frame_complete(0,100) && takes==before);
    puts("LCD fence: stale/coalesced events, timeout, counter/tick wrap and missing semaphore PASS");
}
'''


def main():
    source = (ROOT/'src/board_lcd.c').read_text(encoding='utf-8')
    match = re.search(r'bool board_lcd_wait_frame_complete\([^{}]*\)\s*\{', source)
    assert match
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    with tempfile.TemporaryDirectory(prefix='lcd-fence-') as temp:
        path = Path(temp)
        unit = path/'test.c'
        unit.write_text(PREFIX+source[match.start():end]+CASES, encoding='utf-8')
        subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), str(unit), '-o', str(path/'test.exe')], check=True)
        subprocess.run([str(path/'test.exe')], check=True, timeout=10)


if __name__ == '__main__':
    main()
