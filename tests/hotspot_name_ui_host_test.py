"""Actual hotspot name editor, pinyin, byte limits and async save UI in LVGL."""
from pathlib import Path
import subprocess
import re
from PIL import Image
from music_playlist_ui_host_test import ROOT, HARNESS

OUT=ROOT/'.tools/hotspot-password-20261114/host-ui'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (OUT/'lv_conf.h').write_text(config,encoding='utf-8')
    driver=HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init=HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    driver=re.sub(r'\binput\b','host_pointer',driver)
    init=re.sub(r'\binput\b','host_pointer',init)
    source=r'''
#include "lvgl.h"
#include "HotspotName.h"
#include "lv_ime_pinyin.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];static lv_font_t *chinese_font_20;
static hotspot_name_state_t backend;static unsigned requests;static char submitted[33],submitted_password[64];
bool hotspot_name_normalize(const char*v,char out[33]){memset(out,0,33);size_t n=strlen(v);if(!n||n>32)return false;memcpy(out,v,n);return true;}
bool hotspot_password_valid(const char*v){size_t n=strlen(v);if(n<8||n>63)return false;for(size_t i=0;i<n;i++)if((unsigned char)v[i]<32||(unsigned char)v[i]>126)return false;return true;}
bool hotspot_credentials_request(const char*v,const char*p){if(backend.busy||!hotspot_name_normalize(v,submitted)||!hotspot_password_valid(p))return false;strcpy(submitted_password,p);requests++;backend.busy=true;backend.result=0;backend.revision++;return true;}
void hotspot_name_get(hotspot_name_state_t*s){*s=backend;}
lv_pinyin_dict_t *get_sd_pinyin_dict(void){return NULL;}
static lv_event_cb_t back_callback;
void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;back_callback=cb;}
#include "@ROOT@/src/HotspotNameUI.c"
''' + driver + r'''
static void key(const char*value){for(int i=0;i<64;i++){const char*t=lv_btnmatrix_get_btn_text(keyboard,i);if(t&&!strcmp(t,value)){lv_btnmatrix_set_selected_btn(keyboard,i);lv_event_send(keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}}assert(!"missing key");}
int main(int argc,char**argv){
@INIT@
 lv_obj_t *settings=lv_obj_create(NULL);lv_scr_load(settings);strcpy(backend.name,"ESP32S3-Preview");strcpy(backend.password,"Test!Pass8");backend.revision=1;
 hotspot_name_ui_open(settings,chinese_font_20);advance(100);assert(lv_scr_act()==screen&&keyboard&&name_input&&password_input);assert(lv_textarea_get_password_mode(password_input)&&!strcmp(lv_textarea_get_text(password_input),"Test!Pass8")&&lv_keyboard_get_textarea(keyboard)==name_input);screenshot(argv[2],"name-editor");
 lv_area_t k,c;lv_obj_update_layout(screen);lv_obj_get_coords(keyboard,&k);assert(k.y1==316&&k.y2<480);lv_obj_get_coords(candidates,&c);assert(c.y1==282&&c.y2<316);
 lv_textarea_set_text(name_input,"一二三四五六七八九十一");advance(50);assert(lv_obj_has_state(save_button,LV_STATE_DISABLED)&&strstr(lv_label_get_text(message),"33"));
 lv_textarea_set_text(name_input,"");for(int n=0;n<32;n++)key("a");assert(strlen(lv_textarea_get_text(name_input))==32&&((lv_ime_pinyin_t*)ime)->ta_count<16&&lv_mem_test()==LV_RES_OK);for(int n=0;n<32;n++)key(LV_SYMBOL_BACKSPACE);assert(!lv_textarea_get_text(name_input)[0]);key("w");key("o");assert(!lv_obj_has_flag(candidates,LV_OBJ_FLAG_HIDDEN));lv_btnmatrix_set_selected_btn(candidates,1);lv_event_send(candidates,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(name_input),"我"));

 /* Switch to password using real pointer events; pinyin must stay out. */
 tap(100,175);assert(lv_keyboard_get_textarea(keyboard)==password_input&&lv_obj_has_flag(candidates,LV_OBJ_FLAG_HIDDEN));lv_textarea_set_text(password_input,"");
 key("w");key("o");assert(!strcmp(lv_textarea_get_text(password_input),"wo")&&((lv_ime_pinyin_t*)ime)->ta_count==0&&lv_obj_has_flag(candidates,LV_OBJ_FLAG_HIDDEN));
 key("ABC");key("A");key("1#");key("1");key("!");assert(!strcmp(lv_textarea_get_text(password_input),"woA1!"));key("abc");lv_textarea_set_text(password_input,"");
 for(int n=0;n<63;n++)key("a");assert(strlen(lv_textarea_get_text(password_input))==63&&lv_mem_test()==LV_RES_OK&&!lv_obj_has_state(save_button,LV_STATE_DISABLED));key("b");assert(strlen(lv_textarea_get_text(password_input))==63);
 lv_textarea_set_text(password_input,"short");assert(lv_obj_has_state(save_button,LV_STATE_DISABLED));lv_textarea_set_text(password_input,"Fresh!Pass8");assert(!lv_obj_has_state(save_button,LV_STATE_DISABLED));
 tap(390,135);assert(!lv_textarea_get_password_mode(password_input)&&lv_keyboard_get_textarea(keyboard)==password_input);tap(390,135);assert(lv_textarea_get_password_mode(password_input));
 tap(100,95);assert(lv_keyboard_get_textarea(keyboard)==name_input&&((lv_ime_pinyin_t*)ime)->ta_count==0);
 lv_textarea_set_text(name_input,"我的热点");advance(200);screenshot(argv[2],"name-before-save");tap(100,260);assert(submitting&&requests==1&&backend.busy&&!strcmp(submitted,"我的热点")&&!strcmp(submitted_password,"Fresh!Pass8")&&lv_obj_has_state(name_input,LV_STATE_DISABLED)&&lv_obj_has_state(password_input,LV_STATE_DISABLED)&&lv_obj_has_state(show_button,LV_STATE_DISABLED));tap(350,260);assert(lv_scr_act()==screen);
 backend.busy=false;backend.result=1;strcpy(backend.name,submitted);strcpy(backend.password,submitted_password);strcpy(backend.message,"已保存");backend.revision++;advance(200);assert(!submitting&&strstr(lv_label_get_text(message),"已保存")&&lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"name-saved");
 back_callback(NULL);assert(lv_scr_act()==settings);hotspot_name_ui_open(settings,chinese_font_20);advance(100);assert(!strcmp(lv_textarea_get_text(name_input),"我的热点")&&!strcmp(lv_textarea_get_text(password_input),"Fresh!Pass8")&&lv_textarea_get_password_mode(password_input));
 lv_textarea_set_text(name_input,"");key("w");assert(((lv_ime_pinyin_t*)ime)->ta_count>0);tap(350,260);assert(lv_scr_act()==settings&&requests==1&&!strcmp(backend.name,"我的热点"));
 hotspot_name_ui_open(settings,chinese_font_20);advance(100);assert(((lv_ime_pinyin_t*)ime)->ta_count==0);lv_textarea_set_text(name_input,"Retry");lv_textarea_set_text(password_input,"Retry!Pass");tap(100,260);backend.busy=false;backend.result=-1;strcpy(backend.message,"保存失败");backend.revision++;advance(200);assert(!submitting&&strstr(lv_label_get_text(message),"保存失败")&&!lv_obj_has_state(save_button,LV_STATE_DISABLED)&&!strcmp(lv_textarea_get_text(password_input),"Retry!Pass"));
 back_callback(NULL);assert(lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN));back_callback(NULL);assert(lv_scr_act()==settings);
 for(int n=0;n<15;n++){hotspot_name_ui_open(settings,chinese_font_20);advance(30);cancel(NULL);assert(lv_scr_act()==settings);}
 lv_obj_del(screen);assert(screen==NULL&&ime==NULL&&timer==NULL&&lv_mem_test()==LV_RES_OK);
 puts("Hotspot credentials UI PASS: two-field layout, Chinese SSID, literal ASCII password, 63-character limit, show/hide, combined save/error/cancel/back and IME deletion");
}
'''
    unit=OUT/'test.c';unit.write_text(source.replace('@INIT@',init).replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
    exe=OUT/'test.exe'
    result=subprocess.run([str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'src'),'-I',str(ROOT/'components/lvgl/src/extra/others/ime'),str(unit),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)],capture_output=True)
    if result.returncode:print((result.stdout+result.stderr).decode(errors='replace')[-4000:]);raise SystemExit(result.returncode)
    subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
    for f in OUT.glob('*.rgb565'):
        raw=f.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(f.with_suffix('.png'))

if __name__=='__main__':main()
