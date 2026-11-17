"""Real LVGL regression for pinyin paging, bounded composition and CN/EN input.

Network/audio are not involved. Guard pages catch reads beyond SD dictionary rows.
"""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/ime-fix-20261115/host'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (OUT/'lv_conf.h').write_text(config,encoding='utf-8')
    driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')]
    init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    (OUT/'test.c').write_text(HARNESS.replace('@DRIVER@',driver).replace('@INIT@',init),encoding='utf-8')
    exe=OUT/'ime.exe'
    cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'components/lvgl'),str(OUT/'test.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    r=subprocess.run(cmd,capture_output=True)
    if r.returncode:print(r.stderr.decode(errors='replace'));raise SystemExit(r.returncode)
    subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
    for f in OUT.glob('*.rgb565'):
        raw=f.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(f.with_suffix('.png'))

HARNESS=r'''
#include "lvgl.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];static lv_font_t *chinese_font_20;
@DRIVER@
typedef struct {lv_obj_t *screen,*ta,*kb,*obj,*panel;lv_ime_pinyin_t *ime;} Editor;
static Editor editor(void){
 Editor e={0};e.screen=lv_obj_create(NULL);lv_obj_set_style_pad_all(e.screen,0,0);lv_obj_set_style_bg_color(e.screen,lv_color_hex(0xF1F8F3),0);lv_scr_load(e.screen);
 lv_obj_t*l=lv_label_create(e.screen);lv_obj_set_style_text_font(l,chinese_font_20,0);lv_label_set_text(l,"输入问题 · CN 中文 / EN 英文");lv_obj_set_pos(l,18,150);
 e.ta=lv_textarea_create(e.screen);lv_obj_set_pos(e.ta,16,198);lv_obj_set_size(e.ta,448,48);lv_textarea_set_one_line(e.ta,true);lv_obj_set_style_text_font(e.ta,chinese_font_20,0);
 e.kb=lv_keyboard_create(e.screen);lv_obj_set_align(e.kb,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(e.kb,0,288);lv_obj_set_size(e.kb,480,192);lv_obj_set_style_text_font(e.kb,&lv_font_montserrat_16,LV_PART_ITEMS);lv_keyboard_set_textarea(e.kb,e.ta);
 e.obj=lv_ime_pinyin_create(e.screen);e.ime=(lv_ime_pinyin_t*)e.obj;lv_obj_set_size(e.obj,1,1);lv_obj_clear_flag(e.obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
 lv_ime_pinyin_set_keyboard(e.obj,e.kb);e.panel=lv_ime_pinyin_get_cand_panel(e.obj);lv_obj_set_parent(e.panel,e.screen);lv_obj_set_align(e.panel,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(e.panel,0,252);lv_obj_set_size(e.panel,480,36);lv_obj_set_style_text_font(e.panel,chinese_font_20,LV_PART_ITEMS);advance(50);return e;
}
static void key(Editor*e,const char*t){for(unsigned i=0;i<128;i++){const char*s=lv_btnmatrix_get_btn_text(e->kb,i);if(!s)break;if(!strcmp(t,s)){lv_btnmatrix_set_selected_btn(e->kb,i);lv_event_send(e->kb,LV_EVENT_VALUE_CHANGED,NULL);return;}}assert(!"missing key");}
static void type(Editor*e,const char*t){for(;*t;t++){char s[]={*t,0};key(e,s);}}
static void choose(Editor*e,unsigned id){lv_btnmatrix_set_selected_btn(e->panel,id);lv_event_send(e->panel,LV_EVENT_VALUE_CHANGED,NULL);}
static void tap_obj(lv_obj_t*o){lv_obj_update_layout(o);lv_area_t a;lv_obj_get_coords(o,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static void tap_candidate(Editor*e,unsigned id){lv_obj_update_layout(e->panel);lv_btnmatrix_t*b=(lv_btnmatrix_t*)e->panel;lv_area_t a;b->btn_id_sel=LV_BTNMATRIX_BTN_NONE;lv_obj_get_coords(e->panel,&a);lv_area_t c=b->button_areas[id];tap(a.x1+(c.x1+c.x2)/2,a.y1+(c.y1+c.y2)/2);}
static void clear(Editor*e){lv_ime_pinyin_clear(e->obj);lv_textarea_set_text(e->ta,"");}
static void bounds(Editor*e){assert(strlen(e->ime->input_char)<sizeof(e->ime->input_char));assert(e->ime->ta_count==strlen(e->ime->input_char));assert(lv_mem_test()==LV_RES_OK);}
int main(int argc,char**argv){
@INIT@
 Editor e=editor();
 /* Deliberately unordered and sparse initials; two consecutive syllables. */
 lv_pinyin_dict_t dict[]={{"hao","好号浩"},{"ni","你呢泥拟逆妮尼倪腻匿霓"},{"he","和河"},{NULL,NULL}};
 lv_ime_pinyin_set_dict(e.obj,dict);type(&e,"nihao");assert(e.ime->matched_len==2&&e.ime->ta_count==5);choose(&e,7);assert(e.ime->py_page==1);choose(&e,0);choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"你hao")&&e.ime->matched_len==3);choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"你好")&&!e.ime->ta_count);
 clear(&e);type(&e,"ni");type(&e,"zzq");choose(&e,7);choose(&e,8);assert(e.ime->expanded);tap_candidate(&e,3);assert(!e.ime->expanded&&lv_obj_get_height(e.panel)==36);clear(&e);
 for(unsigned i=0;i<300;i++){key(&e,"a");bounds(&e);}assert(strlen(lv_textarea_get_text(e.ta))==300);for(unsigned i=0;i<300;i++){key(&e,LV_SYMBOL_BACKSPACE);bounds(&e);}assert(!lv_textarea_get_text(e.ta)[0]);
 for(const char*p="iuv";*p;p++){char s[]={*p,0};type(&e,s);assert(!e.ime->cand_num&&e.ime->cand_str==NULL&&lv_obj_has_flag(e.panel,LV_OBJ_FLAG_HIDDEN));choose(&e,7);choose(&e,8);clear(&e);}
 /* CN/EN is a real pointer-clicked child button, not a keyboard map change. */
 type(&e,"ni");tap_obj(e.ime->language_button);assert(!lv_ime_pinyin_get_enabled(e.obj)&&!strcmp(lv_textarea_get_text(e.ta),"ni"));type(&e,"hello");assert(!strcmp(lv_textarea_get_text(e.ta),"nihello")&&!e.ime->ta_count);lv_ime_pinyin_refresh(e.obj);assert(lv_obj_has_flag(e.panel,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"english");
 tap_obj(e.ime->language_button);clear(&e);key(&e,"ABC");key(&e,"N");key(&e,"I");choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"你"));key(&e,"abc");clear(&e);
 /* No candidate button can delete other text after an edit or cursor move. */
 type(&e,"ni");lv_textarea_set_text(e.ta,"保留");choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"保留"));clear(&e);type(&e,"ni");lv_textarea_cursor_left(e.ta);choose(&e,1);assert(!e.ime->ta_count&&!strcmp(lv_textarea_get_text(e.ta),"ni"));
 clear(&e);type(&e,"ni");lv_obj_t*other=lv_textarea_create(e.screen);lv_keyboard_set_textarea(e.kb,other);assert(!e.ime->ta_count&&e.ime->textarea==other);choose(&e,1);assert(!lv_textarea_get_text(other)[0]);lv_textarea_set_password_mode(other,true);lv_event_send(e.kb,LV_EVENT_REFRESH,NULL);assert(lv_obj_has_flag(e.ime->language_button,LV_OBJ_FLAG_HIDDEN));type(&e,"ni");assert(!strcmp(lv_textarea_get_text(other),"ni")&&!e.ime->ta_count);lv_keyboard_set_textarea(e.kb,e.ta);lv_obj_del(other);clear(&e);
 /* A second cached app must not change the first app's candidates. */
 type(&e,"ni");Editor second=editor();lv_pinyin_dict_t d2[]={{"ni","妮尼"},{NULL,NULL}};lv_ime_pinyin_set_dict(second.obj,d2);type(&second,"ni");assert(!strcmp(lv_btnmatrix_get_btn_text(e.panel,1),"你"));choose(&second,1);assert(!strcmp(lv_textarea_get_text(second.ta),"妮"));lv_scr_load(e.screen);choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"你"));lv_obj_del(second.screen);clear(&e);
 /* Place the terminating NUL immediately before inaccessible memory. Original
  * last-page code read another character past this boundary. */
 unsigned counts[]={0,1,6,7,12,13,24,25,48,49};
 SYSTEM_INFO sys;GetSystemInfo(&sys);size_t page=sys.dwPageSize;
 for(unsigned test=0;test<sizeof(counts)/sizeof(counts[0]);test++){
  unsigned n=counts[test];char*mem=VirtualAlloc(NULL,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(mem);DWORD old;assert(VirtualProtect(mem+page,page,PAGE_NOACCESS,&old));char*text=mem+page-1-n*3;for(unsigned i=0;i<n;i++)memcpy(text+i*3,"你",3);text[n*3]=0;lv_pinyin_dict_t guarded[]={{"ni",text},{NULL,NULL}};lv_ime_pinyin_set_dict(e.obj,guarded);clear(&e);type(&e,"ni");assert(e.ime->cand_num==n);
  for(unsigned i=0;i<15;i++)choose(&e,7);assert(e.ime->py_page==(n?(n-1)/6:0));unsigned remain=n?n-e.ime->py_page*6:0;if(remain<6){const char*before=lv_textarea_get_text(e.ta);char saved[64];strcpy(saved,before);choose(&e,1+remain);assert(!strcmp(lv_textarea_get_text(e.ta),saved));}
  for(unsigned i=0;i<15;i++)choose(&e,0);if(n){tap_candidate(&e,8);assert(e.ime->expanded);for(unsigned i=0;i<10;i++)choose(&e,2);assert(e.ime->py_page==(n-1)/24);if(n==49){screenshot(argv[2],"expanded-last");tap_candidate(&e,4);assert(!strcmp(lv_textarea_get_text(e.ta),"你")&&!e.ime->expanded&&lv_obj_get_height(e.panel)==36);}else{tap_candidate(&e,3);assert(!e.ime->expanded);}}
  lv_ime_pinyin_set_dict(e.obj,NULL);VirtualFree(mem,0,MEM_RELEASE);bounds(&e);
 }
 lv_pinyin_dict_t full[]={{"ni","你呢泥拟逆妮尼倪腻匿霓你呢泥拟逆妮尼倪腻匿霓你呢"},{NULL,NULL}};lv_ime_pinyin_set_dict(e.obj,full);clear(&e);type(&e,"nihao");tap_candidate(&e,8);tap_candidate(&e,27);assert(!e.ime->expanded&&e.ime->ta_count==3&&lv_btnmatrix_get_selected_btn(e.panel)==LV_BTNMATRIX_BTN_NONE);clear(&e);
 char malformed[]="你\xE4\xB8";lv_pinyin_dict_t invalid[]={{"ni",malformed},{NULL,NULL}};lv_ime_pinyin_set_dict(e.obj,invalid);clear(&e);type(&e,"ni");assert(e.ime->cand_num==1);choose(&e,7);choose(&e,1);assert(!strcmp(lv_textarea_get_text(e.ta),"你"));
 lv_pinyin_dict_t mixed[]={{"ni","A你\xF0\xA0\x80\x80"},{NULL,NULL}};lv_ime_pinyin_set_dict(e.obj,mixed);clear(&e);type(&e,"ni");assert(e.ime->cand_num==3&&!strcmp(e.ime->candidate_text[0],"A")&&!strcmp(e.ime->candidate_text[2],"\xF0\xA0\x80\x80"));
 /* Rebind, independent old keyboard, delete the keyboard before the IME. */
 lv_obj_t*old_kb=e.kb;e.kb=lv_keyboard_create(e.screen);lv_keyboard_set_textarea(e.kb,e.ta);lv_ime_pinyin_set_keyboard(e.obj,e.kb);lv_obj_del(old_kb);assert(e.ime->kb==e.kb);lv_obj_del(e.kb);assert(e.ime->kb==NULL);lv_obj_t*home=lv_obj_create(NULL);lv_scr_load(home);lv_obj_del(e.screen);assert(lv_mem_test()==LV_RES_OK);
 lv_mem_monitor_t baseline,after;lv_mem_monitor(&baseline);
 for(unsigned i=0;i<30;i++){Editor t=editor();lv_ime_pinyin_set_dict(t.obj,dict);type(&t,"nihao");choose(&t,8);assert(t.ime->expanded);lv_scr_load(home);lv_obj_del(t.screen);assert(lv_mem_test()==LV_RES_OK);}lv_mem_monitor(&after);assert(after.free_size+128>=baseline.free_size);
 puts("IME PASS: consecutive syllables, guard-page 0/1/6/7/12/13/24/25/48/49 candidates, expanded pointer events, 300-letter bound, CN/EN/uppercase/password, UTF-8, independent apps, cursor/rebind/delete and 30 cleanup cycles");
}
'''
if __name__=='__main__':main()
