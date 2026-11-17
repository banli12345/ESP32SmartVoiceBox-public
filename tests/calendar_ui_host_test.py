"""Drive the actual offline calendar in LVGL, with production SD fonts."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import ROOT, HARNESS

OUT = ROOT/'.tools/lunar-life-20261114/calendar-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (OUT/'lv_conf.h').write_text(config,encoding='utf-8')
    driver=HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init=HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    code=r'''#include "lvgl.h"
#include "LifeData.h"
#include "LocalLunar.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static lv_color_t pixels[480*480];static lv_font_t*chinese_font_20;
static struct tm host_date={0};static bool valid_clock=true;
static uint32_t revision=1;static unsigned requests;static lv_event_cb_t saved_back;
static time_t test_time(time_t*p){time_t v=valid_clock?1794614400:0;if(p)*p=v;return v;}
static struct tm*test_localtime(const time_t*t,struct tm*out){(void)t;*out=host_date;return out;}
bool life_data_get_day(int y,int m,int d,life_calendar_day_t*out){return local_lunar_get(y,m,d,out);}
void life_data_request_month(int y,int m){assert(y>=1901&&y<=2099&&m>=1&&m<=12);requests++;}
void life_data_request_day(int y,int m,int d){life_calendar_day_t out;assert(local_lunar_get(y,m,d,&out));}
void life_data_calendar_status(char*out,size_t n){snprintf(out,n,"本地农历换算");}
uint32_t life_data_revision(void){return revision;}
void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;saved_back=cb;}
#define time test_time
#define localtime_r test_localtime
#include "@ROOT@/src/CalendarUI.c"
#undef time
#undef localtime_r
''' + driver + r'''
static void tap_day(int day){int i=first_weekday(view_year,view_month)+day-1;lv_area_t a;lv_obj_get_coords(cells[i],&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
int main(int argc,char**argv){
@INIT@
    font_file="@ROOT@/sd_card_files/fonts/chinese_16.bin";lv_font_t*small=lv_font_load("S:/small.bin");assert(small);
    lv_obj_t*home=lv_obj_create(NULL);host_date.tm_year=126;host_date.tm_mon=10;host_date.tm_mday=14;
    calendar_ui_open(home,chinese_font_20,small);advance(300);
    assert(selected_day==14&&strstr(lv_label_get_text(detail_title),"十月初六"));screenshot(argv[2],"calendar-normal");
    for(int i=0;i<42;i++)if(!lv_obj_has_flag(cells[i],LV_OBJ_FLAG_HIDDEN)){
        lv_area_t a;lv_obj_get_coords(cells[i],&a);assert(a.y1>=84&&a.y2<363&&a.x1>=14&&a.x2<466);
        assert(lv_label_get_text(lunar_labels[i])[0]);
    }
    host_date.tm_mon=1;host_date.tm_mday=17;calendar_ui_open(home,chinese_font_20,small);advance(300);
    assert(strstr(lv_label_get_text(detail_body),"春节")&&strstr(lv_label_get_text(detail_body),"马"));screenshot(argv[2],"calendar-spring");
    tap_day(18);assert(selected_day==18&&!strstr(lv_label_get_text(detail_body),"春节")&&!strstr(lv_label_get_text(detail_body),"节日："));
    unsigned before=requests;touch(true,210,220,50);touch(true,210,135,100);touch(false,210,135,150);
    assert(view_month==3&&requests==before+1&&selected_day==1);
    host_date.tm_year=125;host_date.tm_mon=6;host_date.tm_mday=25;calendar_ui_open(home,chinese_font_20,small);advance(300);
    assert(strstr(lv_label_get_text(detail_title),"闰六月初一"));screenshot(argv[2],"calendar-leap");
    host_date.tm_year=126;host_date.tm_mon=11;host_date.tm_mday=31;calendar_ui_open(home,chinese_font_20,small);advance(300);
    tap(436,64);assert(view_year==2027&&view_month==1);tap(44,64);assert(view_year==2026&&view_month==12);
    saved_back(NULL);assert(lv_scr_act()==home);
    valid_clock=false;calendar_ui_open(home,chinese_font_20,small);assert(view_year==0);advance(50);
    assert(strstr(lv_label_get_text(detail_title),"校时"));valid_clock=true;revision++;advance(400);assert(view_year==2026);
    for(int i=0;i<20;i++){saved_back(NULL);calendar_ui_open(home,chinese_font_20,small);advance(10);}
    assert(lv_mem_test()==LV_RES_OK);
    puts("Calendar real LVGL PASS: offline lunar grid/detail, festival removal, leap month, date taps/swipe isolation, year navigation, back/reopen and clock recovery");
}
'''
    code=code.replace('@INIT@',init).replace('@ROOT@',ROOT.as_posix())
    unit=OUT/'test.c';unit.write_text(code,encoding='utf-8');exe=OUT/'calendar.exe'
    result=subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'src'),str(ROOT/'src/UiTransition.c'),str(unit),str(ROOT/'src/LocalLunar.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)],capture_output=True)
    if result.returncode:print(result.stderr.decode(errors='replace')[-4000:]);raise SystemExit(result.returncode)
    subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))


if __name__=='__main__':main()
