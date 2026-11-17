from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS,ROOT
OUT=ROOT/'.tools/media-ota-20261114/ui-host'
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 config=(ROOT/'include/lv_conf.h').read_text().replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
 (OUT/'lv_conf.h').write_text(config)
 driver=HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
 init=HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
 source=r'''#define SDCAR_H
#include "lvgl.h"
#include "SystemUpdate.h"
#include <assert.h>
#include <stdio.h>
static lv_color_t pixels[480*480];lv_font_t*chinese_font_20;
static system_update_status_t backend;static unsigned checks,installs;static lv_event_cb_t back_callback;
void system_update_get_status(system_update_status_t*s){*s=backend;}
bool system_update_check(void){checks++;backend.busy=true;return true;}
bool system_update_install(void){installs++;backend.busy=true;return true;}
static void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;back_callback=cb;}
#define edge_back_bind unused_edge_back_declaration
#include "EdgeBack.h"
#undef edge_back_bind
#include "@ROOT@/src/SystemUpdateUI.c"
'''+driver+'int main(int argc,char**argv){\n'+init+r'''
 lv_obj_t*settings=lv_obj_create(NULL);strcpy(backend.current_version,"1.7.0");strcpy(backend.message,"请先在热点网页上传应用固件，再检查更新");
 system_update_ui_open(settings);advance(300);assert(lv_obj_has_state(install_button,LV_STATE_DISABLED));
 tap(100,414);assert(checks==1);advance(250);assert(lv_obj_has_state(check_button,LV_STATE_DISABLED));
 backend.busy=false;backend.available=true;strcpy(backend.candidate_version,"1.7.1");strcpy(backend.message,"发现新固件，点击安装后将重启设备");advance(250);assert(!lv_obj_has_state(install_button,LV_STATE_DISABLED));screenshot(argv[2],"update-ready");
 tap(350,414);assert(confirm);screenshot(argv[2],"update-confirm");back_callback(NULL);assert(!confirm&&installs==0);
 tap(350,414);assert(confirm);tap(330,320);assert(!confirm&&installs==1);backend.progress=75;advance(250);assert(lv_bar_get_value(progress)==75);
 back_callback(NULL);assert(lv_scr_act()==settings);puts("System update real LVGL: check/available/progress, disabled states, confirmation/cancel and side-back passed");
}
'''
 (OUT/'test.c').write_text(source.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
 exe=OUT/'ui.exe';cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'components'),'-I',str(ROOT/'src'),str(ROOT/'src/UiTransition.c'),str(OUT/'test.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
 result=subprocess.run(cmd,capture_output=True)
 if result.returncode:print(result.stderr.decode(errors='replace')[-3000:]);raise SystemExit(result.returncode)
 subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
 for file in OUT.glob('*.rgb565'):
  raw=file.read_bytes();rgb=bytearray()
  for i in range(0,len(raw),2):
   c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
  Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))
if __name__=='__main__':main()
