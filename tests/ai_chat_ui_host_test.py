from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS,ROOT
from desktop_ui_host_test import function
OUT=ROOT/'.tools/ai-keyboard-20261114/ui-host'
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 config=(ROOT/'include/lv_conf.h').read_text().replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
 (OUT/'lv_conf.h').write_text(config)
 source=(ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8')
 names=['show_text_keyboard','hide_text_keyboard','ai_chat_layout','ai_chat_hide_keyboard','back_btn_cb','ai_chat_stop_cb','ai_chat_send_cb','ai_chat_poll_cb','ai_chat_show_keyboard','ai_chat_keyboard_cb','ai_assistant_app_btn_cb']
 driver=HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
 init=HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
 scaffold=r'''#include "lvgl.h"
#include "lv_ime_pinyin.h"
#include "UITheme.h"
#include "UiTransition.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
@CONSTANTS@
static lv_color_t pixels[480*480];static lv_font_t*chinese_font_20;
static lv_obj_t *main_screen_ref,*ai_assistant_screen,*ai_chat_input,*ai_chat_reply,*ai_chat_status,*ai_chat_send_button,*ai_chat_stop_button,*ai_chat_keyboard,*ai_chat_ime;
static lv_timer_t *ai_chat_poll_timer;static char ai_chat_last_reply[8193],ai_chat_last_error[128];
static lv_pinyin_dict_t ai_chat_ime_dict[]={{"xin","新心"},{NULL,NULL}};
static bool request_busy;static unsigned sends;static char submitted[600];
static bool ai_chat_is_busy(void){return request_busy;}
static bool ai_chat_submit(const char*s){sends++;snprintf(submitted,sizeof(submitted),"%s",s);request_busy=true;return true;}
static bool speaking,caching;static unsigned stops;
static bool ai_audio_is_playing(void){return speaking;}
static bool known_cache_size=true;
static bool ai_chat_speech_status(char *p,size_t n){if(caching)snprintf(p,n,known_cache_size?"缓存 42%% · 267KiB/s · 38s":"缓存 10240KiB · 1024KiB/s · 120s");return caching;}
static void ai_chat_stop_speech(void){speaking=caching=false;stops++;}
static const char*ai_service_status(void){return "已连接";}
static int result_state;static char result_reply[8193];
static int ai_chat_get_result(char*a,size_t n,char*b,size_t m){snprintf(a,n,"%s",result_reply);if(m)b[0]=0;return result_state;}
static lv_pinyin_dict_t*get_sd_pinyin_dict(void){return ai_chat_ime_dict;}
static const lv_font_t*ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_font_t*ui_chinese_font_16(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static lv_obj_t*create_back_button(lv_obj_t*p,lv_align_t a,int x,lv_event_cb_t cb){(void)p;(void)a;(void)x;(void)cb;return NULL;}
'''
 import re
 constants='\n'.join(re.findall(r'^#define AI_(?:KEYBOARD|CANDIDATE)_HEIGHT .*$',source,re.M))
 declarations='\n'.join('static void '+n+('();'if n in ['ai_chat_show_keyboard','ai_chat_hide_keyboard']else '(bool);'if n=='ai_chat_layout'else '(lv_timer_t*);'if n=='ai_chat_poll_cb'else '(lv_event_t*);') for n in ['ai_chat_layout','ai_chat_hide_keyboard','ai_chat_show_keyboard','ai_chat_stop_cb','ai_chat_send_cb','ai_chat_poll_cb','ai_chat_keyboard_cb'])
 tail=r'''
static lv_area_t coords(lv_obj_t*o){lv_area_t a;lv_obj_get_coords(o,&a);return a;}
static void geometry(bool visible){
 lv_obj_update_layout(ai_assistant_screen);lv_area_t field=coords(ai_chat_input),send=coords(ai_chat_send_button),reply=coords(lv_obj_get_parent(ai_chat_reply)),kb=coords(ai_chat_keyboard),panel=coords(lv_ime_pinyin_get_cand_panel(ai_chat_ime));
 assert(kb.y1==480-AI_KEYBOARD_HEIGHT&&kb.y2==479&&kb.x1==0&&kb.x2==479);
 assert(panel.y1==kb.y1-AI_CANDIDATE_HEIGHT&&panel.y2<kb.y1);
 assert(field.y1==send.y1&&field.y2==send.y2&&field.x2<send.x1&&send.x2<480);
 assert(reply.y2<field.y1&&reply.y1==108);
 if(visible){assert(field.y2<panel.y1&&field.y1>reply.y2&&!lv_obj_has_flag(ai_chat_keyboard,LV_OBJ_FLAG_HIDDEN));}
 else assert(field.y1==412&&lv_obj_has_flag(ai_chat_keyboard,LV_OBJ_FLAG_HIDDEN)&&lv_obj_get_height(lv_obj_get_parent(ai_chat_reply))==292);
}
static void key(const char*text){for(unsigned i=0;i<200;i++){const char*s=lv_btnmatrix_get_btn_text(ai_chat_keyboard,i);if(!s)break;if(!strcmp(text,s)){lv_btnmatrix_set_selected_btn(ai_chat_keyboard,i);lv_event_send(ai_chat_keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}}assert(!"key missing");}
int main(int argc,char**argv){
@INIT@
 main_screen_ref=lv_obj_create(NULL);ai_assistant_app_btn_cb(NULL);advance(100);geometry(false);screenshot(argv[2],"idle");
 tap(120,436);geometry(true);assert(lv_scr_act()==ai_assistant_screen);screenshot(argv[2],"typing");
 key("x");key("i");key("n");lv_obj_t*panel=lv_ime_pinyin_get_cand_panel(ai_chat_ime);assert(!lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"pinyin");
 lv_btnmatrix_set_selected_btn(panel,1);lv_event_send(panel,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(ai_chat_input),"新"));
 back_btn_cb(NULL);advance(80);geometry(false);assert(!strcmp(lv_textarea_get_text(ai_chat_input),"新"));
 tap(120,436);geometry(true);tap(416,218);assert(sends==1&&!strcmp(submitted,"新"));
 request_busy=false;advance(200);lv_textarea_set_text(ai_chat_input,"Hello");key(LV_SYMBOL_OK);advance(100);geometry(false);assert(sends==2&&!strcmp(submitted,"Hello"));
 request_busy=false;advance(200);
 tap(120,436);lv_textarea_set_text(ai_chat_input,"keep");back_btn_cb(NULL);advance(80);geometry(false);assert(sends==2&&!strcmp(lv_textarea_get_text(ai_chat_input),"keep"));
 for(int n=0;n<15;n++){tap(120,436);geometry(true);back_btn_cb(NULL);advance(30);geometry(false);}
 back_btn_cb(NULL);assert(lv_scr_act()==main_screen_ref);ai_assistant_app_btn_cb(NULL);geometry(false);
 lv_obj_t*other=lv_keyboard_create(main_screen_ref);lv_obj_set_size(other,480,220);lv_obj_t*ta=lv_textarea_create(main_screen_ref);show_text_keyboard(other,ta);lv_obj_update_layout(main_screen_ref);assert(coords(other).y1==260);hide_text_keyboard(other);assert(lv_obj_has_flag(other,LV_OBJ_FLAG_HIDDEN));
 speaking=true;advance(200);assert(!lv_obj_has_flag(ai_chat_stop_button,LV_OBJ_FLAG_HIDDEN));lv_event_send(ai_chat_stop_button,LV_EVENT_CLICKED,NULL);assert(stops==1);advance(200);assert(lv_obj_has_flag(ai_chat_stop_button,LV_OBJ_FLAG_HIDDEN));
 result_state=1;memset(result_reply,'A',1800);result_reply[1800]=0;caching=true;advance(200);
 assert(strstr(lv_label_get_text(ai_chat_status),"42%")&&strstr(lv_label_get_text(ai_chat_status),"267KiB/s"));
 lv_obj_update_layout(ai_assistant_screen);assert(lv_obj_get_height(ai_chat_status)<=24&&coords(ai_chat_status).y2<108);screenshot(argv[2],"cache-speed");
 known_cache_size=false;advance(200);lv_obj_update_layout(ai_assistant_screen);
 assert(strstr(lv_label_get_text(ai_chat_status),"10240KiB")&&lv_obj_get_height(ai_chat_status)<=24&&coords(ai_chat_status).y2<108);screenshot(argv[2],"cache-speed-long");
 lv_obj_t*viewport=lv_obj_get_parent(ai_chat_reply);lv_obj_scroll_to_y(viewport,180,LV_ANIM_OFF);advance(600);assert(lv_obj_get_scroll_y(viewport)==180);assert(!lv_obj_has_flag(ai_chat_stop_button,LV_OBJ_FLAG_HIDDEN));lv_event_send(ai_chat_stop_button,LV_EVENT_CLICKED,NULL);assert(stops==2&&!caching);advance(200);
 assert(lv_mem_test()==LV_RES_OK);puts("AI real LVGL: bottom keyboard/candidate/composer geometry, pinyin, send/confirm-once, cancel/back/text preservation, reopen and shared-form keyboard regression passed");
}
'''.replace('@INIT@',init)
 unit=OUT/'test.c';unit.write_text(scaffold.replace('@CONSTANTS@',constants)+declarations+'\n'+'\n'.join(function(source,n)for n in names)+driver+tail,encoding='utf-8');exe=OUT/'ui.exe'
 cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'components'),'-I',str(ROOT/'src'),'-I',str(ROOT/'components/lvgl/src/extra/others/ime'),str(unit),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True)
 if r.returncode:print(r.stderr.decode(errors='replace')[-4000:]);raise SystemExit(r.returncode)
 subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
 for file in OUT.glob('*.rgb565'):
  raw=file.read_bytes();rgb=bytearray()
  for i in range(0,len(raw),2):
   c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
  Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))
if __name__=='__main__':main()
