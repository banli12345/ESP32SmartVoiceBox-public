"""Breeze OS system identity: production SDK snapshot plus real LVGL information UI."""
from pathlib import Path
import argparse,subprocess,shutil
from PIL import Image
from desktop_ui_host_test import function
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/github-breeze-20261115/host-system-info'
SDK=r'''
#pragma once
#include <stdint.h>
#include <stddef.h>
#define ESP_OK 0
#define ESP_IMAGE_VERIFY_SILENT 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
struct esp_app_desc_t {char version[32];};
static esp_app_desc_t description={"1.20.4"};
static const esp_app_desc_t*esp_app_get_description(){return &description;}
struct esp_chip_info_t {uint8_t cores;uint16_t revision;};
static void esp_chip_info(esp_chip_info_t*p){p->cores=2;p->revision=100;}
static unsigned esp_clk_cpu_freq(){return 240000000;}
static int esp_flash_get_size(void*,uint32_t*n){*n=16*1024*1024;return ESP_OK;}
static uint32_t internal_free=96*1024;
static size_t heap_caps_get_total_size(int caps){return caps&MALLOC_CAP_SPIRAM?8*1024*1024:256*1024;}
static size_t heap_caps_get_free_size(int caps){return caps&MALLOC_CAP_SPIRAM?3*1024*1024:internal_free;}
struct esp_partition_t {uint32_t address,size;};
struct esp_partition_pos_t {uint32_t address,size;};
struct esp_image_metadata_t {uint32_t image_len;};
static esp_partition_t running={0x410000,0x400000};
static const esp_partition_t*esp_ota_get_running_partition(){return &running;}
static unsigned verifications;
static int esp_image_verify(int,const esp_partition_pos_t*p,esp_image_metadata_t*m){if(p->address!=running.address)return 1;verifications++;m->image_len=2300000;return ESP_OK;}
'''
CPP=r'''
#include <cassert>
#include <cstring>
#include <cstdio>
#include "@ROOT@/src/DeviceInfo.cpp"
int main(){device_info_snapshot_t i;device_info_get_snapshot(nullptr);device_info_get_snapshot(&i);assert(!strcmp(i.system_name,"Breeze OS")&&!strcmp(i.firmware_version,"1.20.4")&&i.cpu_cores==2&&i.cpu_mhz==240&&i.psram_total_bytes==8*1024*1024&&i.flash_firmware_bytes==2300000&&verifications==1);internal_free-=1024;strcpy(description.version,"2.0.1");device_info_get_snapshot(&i);assert(!strcmp(i.firmware_version,"2.0.1")&&i.heap_free_bytes==95*1024&&verifications==1);device_info_log_snapshot(&i);puts("System identity PASS: Breeze OS, runtime app descriptor, live heap and one image verification");return 0;}
'''
UI=r'''
#include "lvgl.h"
#include "DeviceInfo.h"
#include "SystemBranding.h"
#include "UITheme.h"
#include "UiTransition.h"
#include "EdgeBack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
#define DEVICE_INFO_ROW_COUNT 7
static lv_color_t pixels[480*480];static lv_font_t*chinese_font_20;
static lv_obj_t*device_info_screen,*device_info_values[7],*settings;
static lv_timer_t*device_info_timer;static unsigned snapshots;
static char current_version[32]="1.20.4";
static uint32_t gui_fps=35;
static const lv_font_t*ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_font_t*ui_chinese_font_16(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static void settings_btn_cb(lv_event_t*e){(void)e;ui_screen_back(settings);}
static lv_obj_t*create_back_button(lv_obj_t*p,lv_align_t a,lv_coord_t x,lv_event_cb_t cb){(void)a;(void)x;edge_back_bind(lv_obj_get_screen(p),cb,NULL);return NULL;}
void device_info_get_snapshot(device_info_snapshot_t*i){memset(i,0,sizeof(*i));snapshots++;i->system_name=BREEZE_OS_NAME;i->firmware_version=current_version;i->device_model="ESP32-S3 N16R8";i->cpu_cores=2;i->cpu_mhz=240;i->flash_bytes=16*1048576;i->flash_firmware_bytes=2300000;i->heap_total_bytes=256*1024;i->heap_free_bytes=100*1024;i->psram_total_bytes=8*1048576;i->psram_free_bytes=3*1048576;}
void device_info_log_snapshot(const device_info_snapshot_t*i){(void)i;}
static void device_info_timer_cb(lv_timer_t*);
static void device_info_screen_event_cb(lv_event_t*);
@FUNCTIONS@
@DRIVER@
int main(int argc,char**argv){
@INIT@
 settings=lv_obj_create(NULL);lv_scr_load(settings);edge_back_init();create_device_info_screen();ui_screen_open(device_info_screen);advance(100);assert(!strcmp(lv_label_get_text(device_info_values[1]),"Breeze OS 1.20.4"));lv_obj_t*row=lv_obj_get_parent(device_info_values[1]);assert(!strcmp(lv_label_get_text(lv_obj_get_child(row,0)),"系统信息"));lv_point_t need;lv_txt_get_size(&need,lv_label_get_text(device_info_values[1]),lv_obj_get_style_text_font(device_info_values[1],0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);assert(need.x<=lv_obj_get_content_width(device_info_values[1]));screenshot(argv[2],"system-info");strcpy(current_version,"1.19.1");advance(2100);assert(!strcmp(lv_label_get_text(device_info_values[1]),"Breeze OS 1.19.1"));edge_back_pointer(true,1,220,true);edge_back_pointer(true,88,220,true);edge_back_pointer(false,88,220,true);advance(80);assert(lv_scr_act()==settings&&device_info_timer->paused);unsigned n=snapshots;advance(2200);assert(snapshots==n);lv_timer_del(device_info_timer);lv_obj_del(device_info_screen);puts("System info UI PASS: title, Breeze name+version fit, refresh and real edge return pause");return 0;}
'''

def main():
    p=argparse.ArgumentParser();p.add_argument('--cxx');a=p.parse_args();cc=a.cxx or shutil.which('cl') or shutil.which('g++');assert cc,'C++ compiler required';OUT.mkdir(parents=True,exist_ok=True);sdk=OUT/'sdk';(sdk/'esp_private').mkdir(parents=True,exist_ok=True);(sdk/'sdk_fake.h').write_text(SDK)
    for name in ['esp_app_desc.h','esp_chip_info.h','esp_flash.h','esp_heap_caps.h','esp_image_format.h','esp_ota_ops.h','esp_private/esp_clk.h']:(sdk/name).write_text('#include "sdk_fake.h"\n')
    cpp=OUT/'snapshot.cpp';cpp.write_text(CPP.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'snapshot.exe'
    if Path(cc).stem.lower()=='cl':cmd=[cc,'/nologo','/std:c++17','/EHsc','/utf-8','/I'+str(sdk),str(cpp),'/Fe:'+str(exe),'/Fo:'+str(OUT/'snapshot.obj')]
    else:cmd=[cc,'-std=c++17','-I'+str(sdk),str(cpp),'-o',str(exe)]
    subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
    conf=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)');(OUT/'lv_conf.h').write_text(conf)
    source=(ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8');names=['device_info_set_value','device_info_update','device_info_timer_cb','device_info_screen_event_cb','create_device_info_screen'];driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')];init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    unit=OUT/'ui.c';unit.write_text(UI.replace('@FUNCTIONS@','\n'.join(function(source,n)for n in names)).replace('@DRIVER@',driver).replace('@INIT@',init),encoding='utf-8');exe=OUT/'ui.exe';cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)];r=subprocess.run(cmd,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace');subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=30)
    for f in OUT.glob('*.rgb565'):
        raw=f.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(f.with_suffix('.png'))
if __name__=='__main__':main()
