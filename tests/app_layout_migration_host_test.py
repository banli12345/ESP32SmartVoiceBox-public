"""Real launcher migration preserves legacy ordering when two games become one app."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/game-hub-20261115/host-order'
SOURCE=r'''
#include "lvgl.h"
#include "AppLauncher.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t blob[14];static size_t length;static unsigned commits;
int nvs_open(const char*n,int m,int*h){(void)n;(void)m;*h=1;return 0;}
int nvs_get_blob(int h,const char*k,void*out,size_t*n){(void)h;(void)k;if(!length||*n<length)return 1;memcpy(out,blob,length);*n=length;return 0;}
int nvs_set_blob(int h,const char*k,const void*in,size_t n){(void)h;(void)k;assert(n<=14);memcpy(blob,in,n);length=n;return 0;}
int nvs_commit(int h){(void)h;commits++;return 0;}
void nvs_close(int h){(void)h;}
#include "@ROOT@/src/AppLauncher.c"
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){(void)a;(void)c;lv_disp_flush_ready(d);}
static void expected(const uint8_t*want,unsigned n){assert(app_count==n);for(unsigned i=0;i<n;i++)assert(order[i]==want[i]);}
int main(){
 lv_init();static lv_color_t pixels[480*20];lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,pixels,NULL,480*20);lv_disp_drv_t d;lv_disp_drv_init(&d);d.hor_res=d.ver_res=480;d.draw_buf=&buf;d.flush_cb=flush;lv_disp_drv_register(&d);
 static const uint16_t px=0;static const lv_img_dsc_t icon={.header={.cf=LV_IMG_CF_TRUE_COLOR,.w=1,.h=1},.data_size=2,.data=(const uint8_t*)&px};
 app_launcher_item_t items[12];for(unsigned i=0;i<12;i++){items[i]=(app_launcher_item_t){"App",&icon,NULL,(uint8_t)(i<2?i:i+1)};}items[11].persist_id=APP_ID_WORDS;
 const uint8_t legacy[]={1,12,7,2,0,1,11,10,9,8,6,5,4,3};const uint8_t want[]={6,1,0,10,9,8,7,5,4,3,2};memcpy(blob,legacy,14);length=14;
 lv_obj_t*p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);expected(want,11);assert(commits==0);assert(save_order());assert(blob[0]==2&&blob[1]==11&&length==13&&commits==1);for(unsigned i=2;i<length;i++)assert(blob[i]!=APP_ID_OLD_BALANCE);lv_obj_del(p);
 p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);expected(want,11);lv_obj_del(p);
 p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,12);uint8_t extended[12];memcpy(extended,want,11);extended[11]=11;expected(extended,12);assert(save_order());lv_obj_del(p);
 p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);expected(want,11);lv_obj_del(p);
 blob[3]=blob[2];p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);for(unsigned i=0;i<11;i++)assert(order[i]==i);lv_obj_del(p);
 for(unsigned bad=0;bad<3;bad++){length=bad==0?1:14;blob[0]=bad==1?9:2;blob[1]=255;p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);for(unsigned i=0;i<11;i++)assert(order[i]==i);lv_obj_del(p);}
 items[10].persist_id=items[0].persist_id;p=lv_obj_create(lv_scr_act());app_launcher_create(p,&lv_font_montserrat_20,false,items,11);assert(!root&&app_count==0);lv_obj_del(p);assert(lv_mem_test()==LV_RES_OK);
 puts("App layout PASS: legacy game merge preserves order, no boot Flash writes, stable-id save/reload, added/deleted apps and corrupt blobs");return 0;
}
'''
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)');(OUT/'lv_conf.h').write_text(config)
    (OUT/'nvs.h').write_text('#pragma once\n#include <stddef.h>\ntypedef int nvs_handle_t;typedef int esp_err_t;\n#define ESP_OK 0\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nint nvs_open(const char*,int,int*);int nvs_get_blob(int,const char*,void*,size_t*);int nvs_set_blob(int,const char*,const void*,size_t);int nvs_commit(int);void nvs_close(int);\n')
    unit=OUT/'test.c';unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'order.exe'
    command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    r=subprocess.run(command,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace');subprocess.run([str(exe)],check=True,timeout=30)
if __name__=='__main__':main()
