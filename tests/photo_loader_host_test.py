from pathlib import Path
import subprocess, tempfile, shutil
from media_ota_host_test import ROOT

HEADERS = {
'compat.h': r'''#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <algorithm>
extern uint32_t app_millis();extern void app_delay_ms(uint32_t);
inline size_t strlcpy(char*d,const char*s,size_t n){size_t k=strlen(s);if(n){size_t c=std::min(k,n-1);memcpy(d,s,c);d[c]=0;}return k;}
#define SMARTVOICEBOX_IDF_RUNTIME_H
''',
'freertos/FreeRTOS.h': r'''#pragma once
#include <mutex>
typedef std::mutex portMUX_TYPE;
typedef unsigned TickType_t;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
''',
'freertos/task.h': r'''#pragma once
#include <cstdint>
typedef void *TaskHandle_t;
extern bool fail_task;extern void(*task_fn)(void*);extern void*task_arg;extern void(*delay_hook)();
struct TaskDone{};
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,unsigned,void*a,int,void**h,int){
 if(fail_task)return 0;task_fn=f;task_arg=a;if(h)*h=(void*)1;return 1;}
inline void vTaskDelete(void*){throw TaskDone{};}
inline void vTaskDelay(int){if(delay_hook)delay_hook();}
inline unsigned uxTaskGetStackHighWaterMark(void*){return 2048;}
''',
'freertos/semphr.h': '#pragma once\n',
'esp_heap_caps.h': r'''#pragma once
#include <cstdlib>
enum{MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2,MALLOC_CAP_INTERNAL=4};
extern size_t max_psram_block;extern size_t live_allocs;extern size_t live_bytes;
extern unsigned internal_large;extern unsigned alloc_calls;
inline void*heap_caps_malloc(size_t n,int caps){
 ++alloc_calls;
 if((caps&MALLOC_CAP_INTERNAL)&&n>=100000u){++internal_large;return nullptr;}
 if(!(caps&MALLOC_CAP_SPIRAM)&&n>=100000u){++internal_large;return nullptr;}
 if((caps&MALLOC_CAP_SPIRAM)&&n>=max_psram_block)return nullptr;
 void*p=malloc(n);if(p){++live_allocs;live_bytes+=n;}return p;}
inline void heap_caps_free(void*p){if(!p)return;--live_allocs;free(p);}
''',
'strings.h': '#pragma once\n#include <string.h>\n#define strcasecmp _stricmp\n',
}

HARNESS = r'''
#include "compat.h"
#include "SDcar.h"
#include "Storage.h"
#include "freertos/task.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
uint32_t app_millis(){return 0;}void app_delay_ms(uint32_t){}
bool fail_task;void(*task_fn)(void*);void*task_arg;void(*delay_hook)();
size_t max_psram_block=SIZE_MAX;size_t live_allocs;size_t live_bytes;
unsigned internal_large;unsigned alloc_calls;
static void run(){auto f=task_fn;auto a=task_arg;task_fn=nullptr;assert(f);try{f(a);}catch(TaskDone&){}}
static uint32_t pack_header(unsigned cf,unsigned w,unsigned h){
 return (cf&31u)|((w&0x7ffu)<<10)|((h&0x7ffu)<<21);
}
static std::vector<uint8_t> make_bin(unsigned cf,unsigned w,unsigned h,bool alpha){
 const size_t px=alpha?3u:2u;
 std::vector<uint8_t> out(4+w*h*px);
 uint32_t hdr=pack_header(cf,w,h);
 memcpy(out.data(),&hdr,4);
 for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
  size_t i=4+(y*w+x)*px;
  uint16_t c=(uint16_t)(((x&31)<<11)|((y&63)<<5)|(x&31));
  out[i]=(uint8_t)c;out[i+1]=(uint8_t)(c>>8);
  if(alpha)out[i+2]=(uint8_t)(x==0?0:255);
 }
 return out;
}
static void put(const char*path,const std::vector<uint8_t>&data){
 StorageStream f;assert(f.open(path,"wb")&&f.write(data.data(),data.size())==data.size()&&f.flush());
}
static void expect_fail(const char*path,const char*needle){
 assert(start_photo_load(path));run();
 assert(get_photo_load_status()<0);
 const char*err=get_photo_load_error();
 if(!err||!strstr(err,needle)){fprintf(stderr,"error='%s' needle='%s'\n",err?err:"(null)",needle);fflush(stderr);}
 assert(err&&strstr(err,needle));
 assert(!get_loaded_photo_image()&&!get_loaded_photo_path());
 release_loaded_photo();
 assert(get_photo_load_status()==0);
}
int main(int argc,char**argv){
 assert(argc==3);
 std::filesystem::create_directories(std::string(argv[1])+"/images");
 std::filesystem::create_directories(std::string(argv[1])+"/imges");
 storage_host_set_root(argv[1]);
 max_psram_block=691200;
 auto alpha=make_bin(5,480,480,true);
 auto opaque=make_bin(4,480,480,false);
 auto small=make_bin(5,64,64,true);
 put("/images/full.bin",alpha);
 put("/images/opaque.bin",opaque);
 put("/images/small.bin",small);
 put("/imges/legacy.bin",small);
 assert(scan_photo_files()>=2);

 assert(start_photo_load("S:/images/small.bin"));run();
 assert(get_photo_load_status()==2);
 const lv_img_dsc_t*img=get_loaded_photo_image();
 assert(img&&img->header.w==64&&img->header.h==64&&img->data);
 assert(!memcmp(img->data,small.data()+4,small.size()-4));
 assert(!get_loaded_photo_path());
 assert(img->data[2]==0);
 release_loaded_photo();assert(!get_loaded_photo_image());

 assert(start_photo_load("S:/images/opaque.bin"));run();
 img=get_loaded_photo_image();
 assert(get_photo_load_status()==2&&img&&img->header.cf==4&&img->data_size==480u*480u*2u);
 assert(!get_loaded_photo_path());
 release_loaded_photo();

 max_psram_block=691199;
 alloc_calls=0;internal_large=0;
 assert(start_photo_load("S:/images/full.bin"));run();
 assert(get_photo_load_status()==2);
 assert(!get_loaded_photo_image());
 const char*stream=get_loaded_photo_path();
 assert(stream&&!strcmp(stream,"S:/images/full.bin"));
 assert(internal_large==0);
 std::ofstream published(argv[2],std::ios::binary);
 published.write(reinterpret_cast<const char*>(alpha.data()),(std::streamsize)alpha.size());
 published.close();
 release_loaded_photo();
 max_psram_block=460799;
 assert(start_photo_load("S:/images/opaque.bin"));run();
 assert(get_photo_load_status()==2&&!get_loaded_photo_image());
 assert(get_loaded_photo_path()&&!strcmp(get_loaded_photo_path(),"S:/images/opaque.bin"));
 release_loaded_photo();
 max_psram_block=691199;

 delay_hook=[]{release_loaded_photo();delay_hook=nullptr;};
 assert(start_photo_load("S:/images/opaque.bin"));run();
 assert(!get_loaded_photo_image()&&!get_loaded_photo_path()&&get_photo_load_status()<=0);
 delay_hook=nullptr;release_loaded_photo();

 static const char*second="S:/images/small.bin";
 delay_hook=[]{assert(start_photo_load(second));delay_hook=nullptr;};
 assert(start_photo_load("S:/images/opaque.bin"));run();
 img=get_loaded_photo_image();
 assert(get_photo_load_status()==2&&img&&img->header.w==64);
 assert(!get_loaded_photo_path());
 release_loaded_photo();

 expect_fail("S:/images/missing.bin","找不到");
 {
  auto bad=alpha;bad[0]=(uint8_t)(bad[0]|0xe0);put("/images/badhdr.bin",bad);
  expect_fail("S:/images/badhdr.bin","头");
 }
 {
  uint32_t hdr=pack_header(7,16,16);std::vector<uint8_t>b(4+16*16*2,1);memcpy(b.data(),&hdr,4);
  put("/images/fmt.bin",b);expect_fail("S:/images/fmt.bin","格式");
 }
 {
  uint32_t hdr=pack_header(5,500,10);std::vector<uint8_t>b(4+20,1);memcpy(b.data(),&hdr,4);
  put("/images/big.bin",b);expect_fail("S:/images/big.bin","尺寸");
 }
 {
  auto trunc=alpha;trunc.resize(100);put("/images/trunc.bin",trunc);
  expect_fail("S:/images/trunc.bin","不完整");
 }
 {
  auto shortf=opaque;shortf.resize(4+100);uint32_t hdr=pack_header(4,480,480);memcpy(shortf.data(),&hdr,4);
  put("/images/short.bin",shortf);expect_fail("S:/images/short.bin","不完整");
 }
 fail_task=true;
 assert(!start_photo_load("S:/images/small.bin"));
 assert(get_photo_load_status()<0&&strstr(get_photo_load_error(),"任务"));
 fail_task=false;
 assert(start_photo_load("S:/images/small.bin"));run();
 assert(get_photo_load_status()==2);release_loaded_photo();
 char long_path[128];memset(long_path,'a',sizeof(long_path));memcpy(long_path,"S:/images/",10);long_path[127]=0;
 assert(!start_photo_load(long_path)&&strstr(get_photo_load_error(),"路径"));
 release_loaded_photo();
 for(unsigned i=0;i<100;++i){
  assert(start_photo_load(i%2?"S:/images/full.bin":"S:/images/small.bin"));run();
  assert(get_photo_load_status()==2);release_loaded_photo();
  assert(get_photo_load_status()==0&&live_allocs==0);
 }
 assert(live_allocs==0&&internal_large==0);
 puts("Photo loader passed: preload, stream fallback, errors, cancel/switch, no internal large alloc");
}
'''

DECODER = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void *fo(lv_fs_drv_t*d,const char*p,lv_fs_mode_t m){(void)d;(void)m;return fopen(p,"rb");}
static lv_fs_res_t fc(lv_fs_drv_t*d,void*f){(void)d;return fclose(f)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t fr(lv_fs_drv_t*d,void*f,void*p,uint32_t n,uint32_t*r){(void)d;*r=(uint32_t)fread(p,1,n,f);return LV_FS_RES_OK;}
static lv_fs_res_t fs(lv_fs_drv_t*d,void*f,uint32_t n,lv_fs_whence_t w){(void)d;return fseek(f,n,w==LV_FS_SEEK_SET?SEEK_SET:w==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t ft(lv_fs_drv_t*d,void*f,uint32_t*n){(void)d;*n=(uint32_t)ftell(f);return LV_FS_RES_OK;}
int main(int argc,char**argv){
 assert(argc==2);lv_init();
 lv_fs_drv_t drv;lv_fs_drv_init(&drv);drv.letter='S';drv.open_cb=fo;drv.close_cb=fc;drv.read_cb=fr;drv.seek_cb=fs;drv.tell_cb=ft;lv_fs_drv_register(&drv);
 char path[1024];snprintf(path,sizeof(path),"S:%s",argv[1]);
 lv_img_decoder_dsc_t dsc;assert(lv_img_decoder_open(&dsc,path,lv_color_black(),0)==LV_RES_OK);
 assert(dsc.header.w==480&&dsc.header.h==480&&dsc.header.cf==LV_IMG_CF_TRUE_COLOR_ALPHA);
 assert(dsc.img_data==NULL);
 FILE*reference=fopen(argv[1],"rb");assert(reference);assert(!fseek(reference,4,SEEK_SET));
 uint8_t line[480*3];
 uint8_t expected[480*3];
 for(unsigned y=0;y<480;++y){
  assert(fread(expected,1,sizeof(expected),reference)==sizeof(expected));
  assert(lv_img_decoder_read_line(&dsc,0,y,480,line)==LV_RES_OK);
  assert(!memcmp(line,expected,sizeof(line)));
 }
 fclose(reference);
 assert(lv_img_decoder_read_line(&dsc,0,0,480,line)==LV_RES_OK);
 assert(line[2]==0);
 assert(lv_img_decoder_read_line(&dsc,1,0,1,line)==LV_RES_OK);
 assert(line[2]==255);
 assert(lv_img_decoder_read_line(&dsc,0,479,480,line)==LV_RES_OK);
 lv_img_decoder_close(&dsc);
 puts("LVGL builtin decoder read_line matched RGB565-alpha BIN");
}
'''

def main():
    compiler = shutil.which('cl')
    assert compiler, 'Use VS developer shell'
    tcc = ROOT / '.tools/host-tcc/tcc/tcc.exe'
    with tempfile.TemporaryDirectory(prefix='photo-loader-', ignore_cleanup_errors=True) as tmp:
        d = Path(tmp)
        for name, body in HEADERS.items():
            p = d / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_text(body, encoding='utf-8')
        unit = d / 'test.cpp'
        unit.write_text(HARNESS, encoding='utf-8')
        exe = d / 'test.exe'
        published = d / 'published.bin'
        cmd = [compiler, '/nologo', '/TP', '/std:c++17', '/utf-8', '/EHsc', '/DNOMINMAX',
               '/D_CRT_SECURE_NO_WARNINGS', '/DSTORAGE_HOST_TEST', '/DSDCAR_PHOTO_HOST',
               '/DLV_CONF_INCLUDE_SIMPLE', '/DLV_KCONFIG_IGNORE',
               f'/FI{d/"compat.h"}', f'/I{d}', f'/I{ROOT/"include"}', f'/I{ROOT/"src"}',
               f'/I{ROOT/"components/lvgl"}', f'/I{ROOT/"components"}',
               f'/I{ROOT/"components/lvgl/src/extra/others/ime"}',
               str(unit), str(ROOT / 'src/SDcar.cpp'), str(ROOT / 'src/Storage.cpp'),
               f'/Fe:{exe}', f'/Fo{d}\\']
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe), str(d / 'sd'), str(published)], check=True)
        out = d / 'lv'
        out.mkdir()
        config = (ROOT / 'include/lv_conf.h').read_text(encoding='utf-8')
        config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
        config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
        config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
        (out / 'lv_conf.h').write_text(config, encoding='utf-8')
        (out / 'dec.c').write_text(DECODER, encoding='utf-8')
        dec = out / 'dec.exe'
        lvgl = [str(p) for p in (ROOT / 'components/lvgl/src').rglob('*.c')]
        subprocess.run([str(tcc), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
                        '-I', str(out), '-I', str(ROOT / 'components/lvgl'),
                        '-I', str(ROOT / 'components'), str(out / 'dec.c'), *lvgl,
                        '-o', str(dec)], check=True)
        subprocess.run([str(dec), str(published)], check=True)

if __name__ == '__main__':
    main()
