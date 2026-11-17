from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS,ROOT
OUT=ROOT/'.tools/media-actions-20261114/ui-host'
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 config=(ROOT/'include/lv_conf.h').read_text().replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
 (OUT/'lv_conf.h').write_text(config)
 driver=HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')].replace('static void input(', 'static void read_pointer(')
 init=HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')].replace('pointer.read_cb=input;', 'pointer.read_cb=read_pointer;')
 source=r'''#define SDCAR_H
#include "lvgl.h"
#include "lv_ime_pinyin.h"
#include "MediaFiles.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];lv_font_t*chinese_font_20;
static media_file_status_t backend;static unsigned opens,requests,refreshes;static bool removed;
static char requested_path[96],requested_name[96];
void media_files_get_status(media_file_status_t*s){*s=backend;}
bool media_files_request(media_kind_t k,const char*p,const char*n,bool del){(void)k;requests++;removed=del;snprintf(requested_path,96,"%s",p);snprintf(requested_name,96,"%s",n?n:"");backend.state=1;return true;}
lv_pinyin_dict_t*get_sd_pinyin_dict(void){return NULL;}
#include "@ROOT@/src/MediaActions.c"
static lv_obj_t *gallery,*page;
static void opened(lv_event_t*event){(void)event;opens++;}
static void changed(void){refreshes++;lv_obj_clean(gallery);media_actions_row(gallery,0,MEDIA_PHOTO,"S:/images/新.bin","新.bin",opened,changed);}
static void key(const char*text){
 for(unsigned i=0;i<200;i++){const char*s=lv_btnmatrix_get_btn_text(keyboard,i);if(!s)break;if(!strcmp(s,text)){lv_btnmatrix_set_selected_btn(keyboard,i);lv_event_send(keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}}
 assert(!"keyboard key missing");
}
'''+driver+'int main(int argc,char**argv){\n'+init+r'''
 page=lv_obj_create(NULL);ui_theme_apply_background(page);lv_obj_t*t=lv_label_create(page);lv_label_set_text(t,"照片");style_text(t,UI_COLOR_TEXT);lv_obj_set_pos(t,24,18);
 gallery=lv_obj_create(page);lv_obj_set_pos(gallery,0,58);lv_obj_set_size(gallery,480,410);lv_obj_set_style_pad_all(gallery,0,0);lv_obj_set_style_border_width(gallery,0,0);lv_obj_set_style_bg_opa(gallery,LV_OPA_TRANSP,0);lv_obj_set_scroll_dir(gallery,LV_DIR_VER);
 for(int i=0;i<12;i++)media_actions_row(gallery,i,MEDIA_PHOTO,"S:/images/photo.bin","旅行照片.bin",opened,changed);
 lv_scr_load(page);advance(100);screenshot(argv[2],"list");tap(120,96);assert(opens==1);
 touch(true,120,96,650);touch(false,120,96,100);assert(modal&&input&&opens==1);lv_obj_update_layout(modal);lv_area_t keyboard_area;lv_obj_get_coords(keyboard,&keyboard_area);assert(keyboard_area.y1==316&&keyboard_area.y2<480);screenshot(argv[2],"rename");
 lv_textarea_set_text(input,"");key("x");key("i");key("n");
 assert(!lv_obj_has_flag(candidate_panel,LV_OBJ_FLAG_HIDDEN));lv_btnmatrix_set_selected_btn(candidate_panel,1);lv_event_send(candidate_panel,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(input),"新"));
 screenshot(argv[2],"rename-pinyin");assert(media_actions_back(page));assert(modal&&lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));tap(120,122);assert(!lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));
 tap(350,244);assert(working&&requests==1&&!removed&&!strcmp(requested_name,"新")&&!strcmp(requested_path,"S:/images/photo.bin"));assert(media_actions_back(page)&&working);
 backend.state=2;advance(150);assert(!modal&&refreshes==1&&opens==1);
 tap(434,96);assert(modal&&!input&&opens==1);screenshot(argv[2],"menu");tap(240,276);assert(modal&&save_button);screenshot(argv[2],"delete-confirm");assert(media_actions_back(page)&&!modal&&requests==1);
 tap(434,96);tap(240,276);tap(350,335);assert(working&&requests==2&&removed&&!strcmp(requested_path,"S:/images/新.bin"));backend.state=2;advance(150);assert(!modal&&refreshes==2);
 for(int n=0;n<30;n++){touch(true,120,96,650);touch(false,120,96,70);assert(modal);media_actions_back(page);media_actions_back(page);assert(!modal);}
 lv_mem_monitor_t memory;lv_mem_monitor(&memory);assert(memory.free_size>100000);assert(lv_mem_test()==LV_RES_OK);
 lv_obj_clean(gallery);for(int i=0;i<12;i++)media_actions_row(gallery,i,MEDIA_VIDEO,"/videos/movie.rvf","视频.rvf",opened,changed);advance(100);
 unsigned before=opens;touch(true,120,370,80);touch(true,120,270,80);touch(true,120,180,80);touch(false,120,180,100);assert(opens==before&&lv_obj_get_scroll_y(gallery)>0);
 puts("Media actions real LVGL: short-open, long-rename/no mis-open, pinyin, dots/delete/cancel, async refresh, side-back, scroll and repeated IME teardown passed");
}
'''
 (OUT/'test.c').write_text(source.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'ui.exe'
 cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'components'),'-I',str(ROOT/'src'),'-I',str(ROOT/'components/lvgl/src/extra/others/ime'),str(OUT/'test.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
 result=subprocess.run(cmd,capture_output=True)
 if result.returncode:print(result.stderr.decode(errors='replace')[-4000:]);raise SystemExit(result.returncode)
 subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
 for file in OUT.glob('*.rgb565'):
  raw=file.read_bytes();rgb=bytearray()
  for i in range(0,len(raw),2):
   c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
  Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))
if __name__=='__main__':main()
