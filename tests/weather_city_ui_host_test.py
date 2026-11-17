"""Exercise the production city IME and weather images in real LVGL, offline."""
from pathlib import Path
import json
import re
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS, ROOT
from desktop_ui_host_test import function

OUT = ROOT / '.tools/weather-city-music-20261117/ui-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text().replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config)
    source = (ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8')
    names = ['show_text_keyboard', 'hide_text_keyboard', 'city_keyboard_show', 'kb_confirm',
             'city_keyboard_event_cb', 'city_text_event_cb', 'city_btn_cb',
             'create_keyboard_screen', 'city_keyboard_back_cb', 'get_weather_icon', 'style_weather_icon']
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    constants = '\n'.join(re.findall(r'^#define AI_(?:KEYBOARD|CANDIDATE)_HEIGHT .*$', source, re.M))
    scaffold = r'''#include "lvgl.h"
#include "lv_ime_pinyin.h"
#include "UITheme.h"
#include "UiTransition.h"
#include "WeatherIcons.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
@CONSTANTS@
static lv_color_t pixels[480*480];static lv_font_t *chinese_font_20;
static lv_obj_t *kb_screen,*kb_keyboard,*kb_textarea,*kb_ime,*kb_candidates,*kb_hint,*kb_return_screen,*main_screen_ref,*city_btn_label;
static bool kb_submitted;static unsigned submissions;static char submitted[128];
static const char *weather="晴",*temp="26";static int code=-1;
static int get_weather_code(void){return code;}
static lv_pinyin_dict_t ai_chat_ime_dict[]={@SD_DICT@,{NULL,NULL}};
static lv_pinyin_dict_t *get_sd_pinyin_dict(void){return ai_chat_ime_dict;}
static const lv_font_t *ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const char *getWeatherCityError(void){return "城市重名，请加省市";}
static bool updateWeatherCity(const char*s){submissions++;snprintf(submitted,sizeof(submitted),"%s",s);return strcmp(s,"朝阳")!=0;}
static const char *ui_city_name(void){return submitted;}
static void set_weather(const char*a,const char*b){(void)a;(void)b;}
'''.replace('@CONSTANTS@', constants)
    dictionary = dict(line.split('\t', 1) for line in (ROOT/'sd_card_files/ime/pinyin_full.txt').read_text(encoding='utf8').splitlines())
    dictionary_c = ','.join('{' + json.dumps(k, ensure_ascii=False) + ',' + json.dumps(v, ensure_ascii=False) + '}' for k,v in dictionary.items())
    scaffold = scaffold.replace('@SD_DICT@', dictionary_c)
    rare_pairs = [(next(k for k,v in dictionary.items() if len(k)>1 and c in v),c) for c in '埇氹沚浉瀍猇硚鄠']
    rare_c = 'static const char *rare_city_pinyin[][2]={' + ','.join('{' + json.dumps(k) + ',' + json.dumps(c,ensure_ascii=False) + '}' for k,c in rare_pairs) + '};\n'
    required = sorted({ord(c) for c in (ROOT/'font_assets/city_names_32.txt').read_text(encoding='utf8') if c >= ' ' and c != '\ufeff'})
    glyphs = 'static const uint32_t city_points[]={' + ','.join(map(str, required)) + '};\n'
    tail = r'''
static void key(const char *s){for(unsigned i=0;i<200;i++){const char*t=lv_btnmatrix_get_btn_text(kb_keyboard,i);if(!t)break;if(!strcmp(t,s)){lv_btnmatrix_set_selected_btn(kb_keyboard,i);lv_event_send(kb_keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}}assert(!"key missing");}
static void choose_text(const char*s){for(unsigned page=0;page<200;page++){for(unsigned i=1;i<=6;i++)if(!strcmp(lv_btnmatrix_get_btn_text(kb_candidates,i),s)){lv_btnmatrix_set_selected_btn(kb_candidates,i);lv_event_send(kb_candidates,LV_EVENT_VALUE_CHANGED,NULL);return;}unsigned before=((lv_ime_pinyin_t*)kb_ime)->py_page;lv_btnmatrix_set_selected_btn(kb_candidates,7);lv_event_send(kb_candidates,LV_EVENT_VALUE_CHANGED,NULL);if(((lv_ime_pinyin_t*)kb_ime)->py_page==before)break;}assert(!"candidate missing");}
static void geometry(void){
 lv_obj_update_layout(kb_screen);lv_area_t a,b,c;lv_obj_get_coords(kb_textarea,&a);lv_obj_get_coords(kb_candidates,&b);lv_obj_get_coords(kb_keyboard,&c);
 assert(a.x1>=0&&a.x2<480&&a.y2<b.y1&&b.y2<c.y1&&c.y1==480-AI_KEYBOARD_HEIGHT&&c.y2==479);
 assert(!lv_obj_has_flag(kb_keyboard,LV_OBJ_FLAG_HIDDEN));
}
static void check_font(const lv_font_t*f){
 for(unsigned i=0;i<sizeof(city_points)/sizeof(city_points[0]);i++){
  lv_font_glyph_dsc_t d;assert(f->get_glyph_dsc(f,&d,city_points[i],0));
  if(city_points[i]>127){assert(d.box_w>0&&d.box_h>0&&f->get_glyph_bitmap(f,city_points[i]));}
 }
}
int main(int argc,char**argv){
@INIT@
 check_font(chinese_font_20);
 char other[2048];snprintf(other,sizeof(other),"%s",argv[1]);char *slash=strrchr(other,'/');if(!slash)slash=strrchr(other,'\\');assert(slash);strcpy(slash+1,"chinese_32.bin");font_file=other;
 lv_font_t *font32=lv_font_load("S:/chinese32.bin");assert(font32);check_font(font32);font_file=argv[1];
 main_screen_ref=lv_obj_create(NULL);lv_scr_load(main_screen_ref);create_keyboard_screen();city_btn_cb(NULL);advance(100);geometry();
 key("n");key("a");key("n");assert(!lv_obj_has_flag(kb_candidates,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"city-pinyin");
 lv_btnmatrix_set_selected_btn(kb_candidates,1);lv_event_send(kb_candidates,LV_EVENT_VALUE_CHANGED,NULL);assert(!strcmp(lv_textarea_get_text(kb_textarea),"南"));
 key("n");key("i");key("n");key("g");choose_text("宁");assert(!strcmp(lv_textarea_get_text(kb_textarea),"南宁"));
 key(LV_SYMBOL_OK);advance(100);assert(submissions==1&&!strcmp(submitted,"南宁")&&lv_scr_act()==main_screen_ref);
 city_btn_cb(NULL);lv_textarea_set_text(kb_textarea,"朝阳");key(LV_SYMBOL_OK);advance(30);assert(submissions==2&&lv_scr_act()==kb_screen&&!kb_submitted);assert(strstr(lv_label_get_text(kb_hint),"重名"));screenshot(argv[2],"city-ambiguous");
 lv_textarea_set_text(kb_textarea,"北京市朝阳区");key(LV_SYMBOL_OK);advance(80);assert(submissions==3&&lv_scr_act()==main_screen_ref);
 for(int n=0;n<10;n++){city_btn_cb(NULL);geometry();city_keyboard_back_cb(NULL);assert(lv_obj_has_flag(kb_keyboard,LV_OBJ_FLAG_HIDDEN));city_keyboard_back_cb(NULL);assert(lv_scr_act()==main_screen_ref);}
 city_btn_cb(NULL);lv_ime_pinyin_set_enabled(kb_ime,false);key("b");key("e");key("i");assert(!strcmp(lv_textarea_get_text(kb_textarea),"bei"));lv_ime_pinyin_set_enabled(kb_ime,true);city_keyboard_back_cb(NULL);city_keyboard_back_cb(NULL);
 for(unsigned i=0;i<sizeof(rare_city_pinyin)/sizeof(rare_city_pinyin[0]);i++){
  city_btn_cb(NULL);for(const char *p=rare_city_pinyin[i][0];*p;p++){char letter[2]={*p,0};key(letter);}
  lv_ime_pinyin_t *ime=(lv_ime_pinyin_t*)kb_ime;assert(ime->cand_str&&strstr(ime->cand_str,rare_city_pinyin[i][1]));
  city_keyboard_back_cb(NULL);city_keyboard_back_cb(NULL);
 }
 lv_obj_t *gallery=lv_obj_create(NULL);lv_obj_set_style_pad_all(gallery,0,0);lv_obj_set_style_bg_color(gallery,lv_color_hex(UI_COLOR_BG),0);lv_scr_load(gallery);
 for(int i=0;i<40;i++){
  int provider=i==39?99:i;const lv_img_dsc_t*d=weather_icon_for_code(provider);assert(d==&weather_icon_descriptors[i]);assert(d->header.w==64&&d->header.h==64&&d->header.cf==LV_IMG_CF_TRUE_COLOR_ALPHA&&d->data_size==64*64*3);
  unsigned visible=0,empty=0;for(int p=2;p<64*64*3;p+=3){visible+=d->data[p]>0;empty+=d->data[p]==0;}assert(visible>5&&empty>5);
  lv_obj_t*image=lv_img_create(gallery);lv_img_set_src(image,d);style_weather_icon(image);lv_obj_set_pos(image,(i%8)*60,(i/8)*94);lv_img_set_zoom(image,224);
  lv_obj_t*label=lv_label_create(gallery);lv_label_set_text_fmt(label,"%d",provider);lv_obj_set_pos(label,(i%8)*60+16,(i/8)*94+66);
 }
 code=99;assert(get_weather_icon("晴")==weather_icon_for_code(99));code=-1;assert(get_weather_icon("雷阵雨伴有冰雹")==weather_icon_for_code(12));assert(weather_icon_for_code(-100)==weather_icon_for_code(99));assert(weather_icon_legacy_code("Cloudy")==4);
 screenshot(argv[2],"weather-all-codes");
 assert(lv_mem_test()==LV_RES_OK);puts("Weather UI: all city font glyphs+bitmaps, Chinese/English IME, candidate placement, confirm once, ambiguous input, back/reopen, all 40 weather images PASS");
}
'''.replace('@INIT@', init)
    unit = OUT/'test.c'
    unit.write_text(scaffold + '\n'.join(function(source,n) for n in names) + driver + glyphs + rare_c + tail, encoding='utf8')
    exe = OUT/'weather-ui.exe'
    command = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I'+str(OUT), '-I'+str(ROOT/'components/lvgl'), '-I'+str(ROOT/'components'), '-I'+str(ROOT/'src'),
               '-I'+str(ROOT/'components/lvgl/src/extra/others/ime'), str(unit), str(ROOT/'src/UiTransition.c'), str(ROOT/'src/WeatherIcons.c'), str(ROOT/'src/WeatherIconsData.c'),
               *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace'));raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT/'sd_card_files/fonts/chinese_20.bin'), str(OUT)], check=True, timeout=60)
    for path in OUT.glob('*.rgb565'):
        raw=path.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(path.with_suffix('.png'))


if __name__ == '__main__':
    main()
