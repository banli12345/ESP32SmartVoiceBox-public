"""Render the production weather page in real LVGL and check layout."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS, ROOT
from desktop_ui_host_test import function

OUT = ROOT / '.tools/weather-page-ui'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    unit = OUT/'test.c'
    custom = (ROOT/'src/ui/custom/custom.c').read_text(encoding='utf8')
    start = custom.index('    fps_label = lv_label_create(screen);')
    end = custom.index('\n', custom.index('lv_obj_set_style_text_color(fps_label,', start))
    fps = custom[start:end].replace('screen', 'desktop')
    unit.write_text(SOURCE.replace('@DRIVER@', driver).replace('@INIT@', init)
                    .replace('@INDICATORS@', function(custom, 'create_page_indicators'))
                    .replace('@UPDATE_PAGE_DOTS@', function(custom, 'update_page_dots'))
                    .replace('@FPS@', fps), encoding='utf-8')
    exe = OUT/'weather-page.exe'
    command = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I'+str(OUT), '-I'+str(ROOT/'components/lvgl'), '-I'+str(ROOT/'components'), '-I'+str(ROOT/'src'),
               str(unit), str(ROOT/'src/WeatherPageUI.c'), str(ROOT/'src/WeatherIcons.c'), str(ROOT/'src/WeatherIconsData.c'),
               *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace')[-4000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT/'sd_card_files/fonts/chinese_20.bin'), str(OUT)], check=True, timeout=60)
    for path in OUT.glob('*.rgb565'):
        raw = path.read_bytes()
        rgb = bytearray()
        for i in range(0, len(raw), 2):
            color = raw[i] | raw[i+1] << 8
            rgb.extend((((color >> 11) & 31) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(path.with_suffix('.png'))


SOURCE = r'''#include "lvgl.h"
#include "UITheme.h"
#include "WeatherPageUI.h"
#include "WeatherIcons.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
char *weather; char *temp; char *updateTime;
char WEATHER_CITY[32]; char WEATHER_CITY_DISPLAY[96];
char WEATHER_FORECAST_DATE[3][12];
char WEATHER_FORECAST_DAY_TEXT[3][32];
char WEATHER_FORECAST_NIGHT_TEXT[3][32];
char WEATHER_FORECAST_HIGH[3][8];
char WEATHER_FORECAST_LOW[3][8];
uint8_t WEATHER_FORECAST_COUNT;
static int weather_code; static const char *status_message = "";
int get_weather_code(void){return weather_code;}
const char *get_weather_status_message(void){return status_message;}
lv_font_t *chinese_font_20; lv_font_t *chinese_font_32;
static lv_color_t pixels[480*480];
static int city_clicks;
static void on_city(lv_event_t *event){if(lv_event_get_code(event)==LV_EVENT_CLICKED) city_clicks++;}
#define NUM_PAGES 3
static lv_obj_t *desktop,*page_dots[3],*fps_label;
@UPDATE_PAGE_DOTS@
@INDICATORS@
@DRIVER@
static int overlaps(const lv_area_t *a, const lv_area_t *b){
 return !(a->x2 < b->x1 || b->x2 < a->x1 || a->y2 < b->y1 || b->y2 < a->y1);
}
static void collect(lv_obj_t *obj, lv_obj_t **out, int *n){
 if(lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return;
 if(lv_obj_check_type(obj, &lv_label_class) || lv_obj_check_type(obj, &lv_img_class)){
  assert(*n < 32); out[(*n)++] = obj;
 }
 uint32_t count = lv_obj_get_child_cnt(obj);
 for(uint32_t i=0;i<count;i++) collect(lv_obj_get_child(obj,i), out, n);
}
static lv_obj_t *find_btn(lv_obj_t *obj){
 if(lv_obj_check_type(obj, &lv_btn_class)) return obj;
 uint32_t count = lv_obj_get_child_cnt(obj);
 for(uint32_t i=0;i<count;i++){lv_obj_t *found=find_btn(lv_obj_get_child(obj,i)); if(found) return found;}
 return NULL;
}
static void assert_inside(lv_obj_t *obj){
 lv_area_t area; lv_obj_get_coords(obj,&area);
 assert(area.x1>=0 && area.y1>=0 && area.x2<480 && area.y2<480);
 lv_obj_t *parent = lv_obj_get_parent(obj);
 if(parent && parent != lv_obj_get_screen(obj)){
  lv_area_t box; lv_obj_get_coords(parent,&box);
  assert(area.x1>=box.x1 && area.y1>=box.y1 && area.x2<=box.x2 && area.y2<=box.y2);
 }
 if(!lv_obj_check_type(obj, &lv_label_class)) return;
 const char *text = lv_label_get_text(obj);
 assert(!strstr(text,"湿度") && !strstr(text,"风速"));
 if(lv_label_get_long_mode(obj)==LV_LABEL_LONG_DOT) return;
 lv_point_t size;
 lv_txt_get_size(&size, text, lv_obj_get_style_text_font(obj,0),
  lv_obj_get_style_text_letter_space(obj,0), lv_obj_get_style_text_line_space(obj,0),
  lv_obj_get_width(obj), LV_TEXT_FLAG_NONE);
 assert(size.x <= lv_obj_get_width(obj));
 assert(size.y <= lv_obj_get_height(obj));
}
static void check_layout(lv_obj_t *page){
 lv_obj_update_layout(page);
 lv_obj_t *leaves[32]; int count=0; collect(page, leaves, &count);
 assert(count>=6);
 for(int i=0;i<count;i++){
  assert_inside(leaves[i]);
  for(int j=i+1;j<count;j++){
   lv_area_t a,b; lv_obj_get_coords(leaves[i],&a); lv_obj_get_coords(leaves[j],&b);
   assert(!overlaps(&a,&b));
  }
 }
 lv_obj_t *button=find_btn(page); assert(button);
 lv_obj_t *city=lv_obj_get_child(button,0);
 assert(lv_label_get_long_mode(city)==LV_LABEL_LONG_DOT);
 const lv_font_t *font=lv_obj_get_style_text_font(city,0);
 assert(font->line_height==chinese_font_32->line_height&&font->dsc==chinese_font_32->dsc);
 lv_font_glyph_dsc_t dot;assert(lv_font_get_glyph_dsc(font,&dot,'.',0)&&dot.resolved_font==&lv_font_montserrat_32);
 int before=city_clicks; lv_event_send(button, LV_EVENT_CLICKED, NULL); assert(city_clicks==before+1);
 lv_obj_t *icon=NULL;
 for(int i=0;i<count;i++) if(lv_obj_check_type(leaves[i], &lv_img_class)) icon=leaves[i];
 assert(icon && lv_img_get_src(icon)==weather_icon_for_code(weather_code<0?39:weather_code==99?39:weather_code));
 lv_color_t plate=lv_obj_get_style_bg_color(icon,0);
 assert(plate.full==lv_color_hex(UI_COLOR_ACCENT_DARK).full);
 assert(lv_obj_get_style_bg_opa(icon,0)==LV_OPA_COVER);
 for(int i=0;i<count;i++){
  lv_area_t data,chrome;lv_obj_get_coords(leaves[i],&data);lv_obj_get_coords(fps_label,&chrome);assert(!overlaps(&data,&chrome));
  for(int d=0;d<3;d++){lv_obj_get_coords(page_dots[d],&chrome);assert(!overlaps(&data,&chrome));}
 }
}
static void clear_forecast(void){
 WEATHER_FORECAST_COUNT=0;
 memset(WEATHER_FORECAST_DATE,0,sizeof WEATHER_FORECAST_DATE);
 memset(WEATHER_FORECAST_DAY_TEXT,0,sizeof WEATHER_FORECAST_DAY_TEXT);
 memset(WEATHER_FORECAST_NIGHT_TEXT,0,sizeof WEATHER_FORECAST_NIGHT_TEXT);
 memset(WEATHER_FORECAST_HIGH,0,sizeof WEATHER_FORECAST_HIGH);
 memset(WEATHER_FORECAST_LOW,0,sizeof WEATHER_FORECAST_LOW);
}
static void put_forecast(int index,const char *date,const char *day,const char *night,const char *high,const char *low){
 snprintf(WEATHER_FORECAST_DATE[index],12,"%s",date);
 snprintf(WEATHER_FORECAST_DAY_TEXT[index],32,"%s",day);
 snprintf(WEATHER_FORECAST_NIGHT_TEXT[index],32,"%s",night);
 snprintf(WEATHER_FORECAST_HIGH[index],8,"%s",high);
 snprintf(WEATHER_FORECAST_LOW[index],8,"%s",low);
}
static int label_has(lv_obj_t *obj, const char *needle){
 if(lv_obj_check_type(obj,&lv_label_class) && strstr(lv_label_get_text(obj), needle)) return 1;
 uint32_t count=lv_obj_get_child_cnt(obj);
 for(uint32_t i=0;i<count;i++) if(label_has(lv_obj_get_child(obj,i), needle)) return 1;
 return 0;
}
int main(int argc,char**argv){
@INIT@
 char font32_path[2048]; snprintf(font32_path,sizeof font32_path,"%s",argv[1]);
 char *slash=strrchr(font32_path,'/'); if(!slash) slash=strrchr(font32_path,'\\'); assert(slash);
 strcpy(slash+1,"chinese_32.bin"); font_file=font32_path;
 chinese_font_32=lv_font_load("S:/chinese32.bin"); assert(chinese_font_32); font_file=argv[1];
 desktop=lv_obj_create(NULL);lv_obj_set_style_pad_all(desktop,0,0);lv_obj_clear_flag(desktop,LV_OBJ_FLAG_SCROLLABLE);lv_scr_load(desktop);
 lv_obj_t *page=lv_obj_create(desktop);lv_obj_set_pos(page,0,0);lv_obj_set_size(page,480,480);lv_obj_set_style_border_width(page,0,0);lv_obj_set_style_radius(page,0,0);
 create_page_indicators(desktop);
@FPS@
 update_page_dots(1);
 strcpy(WEATHER_CITY,"beijing"); strcpy(WEATHER_CITY_DISPLAY,"北京");
 weather="晴"; temp="26"; updateTime="08:30"; weather_code=0; status_message="";
 put_forecast(0,"11-17","晴","晴间多云","28","16");
 put_forecast(1,"11-18","雷阵雨伴有冰雹","中雨","21","14");
 put_forecast(2,"11-19","多云","阴","19","9");
 WEATHER_FORECAST_COUNT=3;
 weather_page_ui_mount(page, on_city); advance(30); check_layout(page);
 assert(label_has(page,"晴") && label_has(page,"26°C") && label_has(page,"雷阵雨伴有冰雹"));
 assert(label_has(page,"更新于 08:30"));
 screenshot(argv[2],"sunny");
 updateTime="2026-11-17T10:23:00+08:00";weather_page_ui_refresh_status();advance(20);check_layout(page);
 assert(label_has(page,"更新于 10:23"));screenshot(argv[2],"updated-time");
 strcpy(WEATHER_CITY_DISPLAY,"新疆维吾尔自治区伊犁哈萨克自治州");
 weather="大部多云转雷阵雨"; temp="-18"; updateTime=NULL; weather_code=1; status_message="";
 put_forecast(0,"2026-11-17","雷阵雨伴有冰雹","强沙尘暴","-9","-18");
 put_forecast(1,"11-18","特大暴雨","暴雪","2","-12");
 put_forecast(2,"11-19","晴间多云","大部多云","0","-1");
 weather_page_ui_refresh(); advance(30); check_layout(page);
 assert(label_has(page,"-18°C") && label_has(page,"高-9℃") && label_has(page,"低-18℃"));
 assert(label_has(page,"数据来源：心知天气"));
 screenshot(argv[2],"night-long-city");
 lv_mem_monitor_t baseline;
 for(int n=0;n<6;n++){
  weather_page_ui_clear(); lv_obj_clean(page);
  weather_page_ui_refresh(); weather_page_ui_refresh_status();
  weather_page_ui_mount(page, on_city); advance(20);
  if(n==0) lv_mem_monitor(&baseline);
 }
 check_layout(page);
 lv_mem_monitor_t after; lv_mem_monitor(&after);
 assert(after.free_size==baseline.free_size);
 clear_forecast(); weather=NULL; temp=NULL; updateTime=NULL; weather_code=-1; status_message="";
 weather_page_ui_refresh(); advance(20); check_layout(page);
 assert(label_has(page,"待更新")); assert(!label_has(page,"湿度"));
 screenshot(argv[2],"pending");
 weather="阴"; temp="3"; weather_code=9; status_message="天气接口无权限";
 weather_page_ui_refresh(); advance(20); check_layout(page);
 assert(label_has(page,"天气接口无权限"));
 screenshot(argv[2],"no-permission");
 status_message="网络失败"; weather_page_ui_refresh(); advance(20); check_layout(page);
 assert(label_has(page,"网络失败") && label_has(page,"3°C"));
 screenshot(argv[2],"network-failed");
 weather_page_ui_clear(); weather_page_ui_refresh(); lv_obj_clean(page); weather_page_ui_refresh_status();
 assert(lv_mem_test()==LV_RES_OK);
 puts("Weather page LVGL: green layout, forecast text, city callback, reopen and failed status PASS");
}
'''

if __name__ == '__main__':
    main()
