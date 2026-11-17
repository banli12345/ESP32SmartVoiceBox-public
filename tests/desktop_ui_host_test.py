"""Drive production pointer arbitration, launcher, flip clock and edge-back in real LVGL.
SDK/NVS operations are fault fakes; production icons and SD fonts are rendered.
"""
from pathlib import Path
import argparse
import re
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/desktop-ui-20261114/host'


def function(source, name):
    match = re.search(r'^static[^\n]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.output:
        OUT = args.output.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    (OUT/'nvs.h').write_text('''#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int nvs_handle_t; typedef int esp_err_t;
enum {ESP_OK=0, NVS_READONLY=1, NVS_READWRITE=2};
int nvs_open(const char*,int,nvs_handle_t*);
int nvs_get_blob(nvs_handle_t,const char*,void*,size_t*);
int nvs_set_blob(nvs_handle_t,const char*,const void*,size_t);
int nvs_commit(nvs_handle_t); void nvs_close(nvs_handle_t);
''', encoding='utf-8')
    (OUT/'host_time.h').write_text('''#include <time.h>
#include <string.h>
static struct tm *host_localtime(const time_t*t, struct tm*r){struct tm*v=localtime(t);if(v)memcpy(r,v,sizeof(*r));return v?r:NULL;}
#define localtime_r host_localtime
''', encoding='utf-8')
    source = (ROOT/'src/main.cpp').read_text(encoding='utf-8')
    read = function(source, 'touch_read').replace('nullptr', 'NULL').replace('lv_indev_drv_t *,', 'lv_indev_drv_t *driver,')
    (OUT/'input.c').write_text('''#include "lvgl.h"
#include "QuickPanel.h"
#include "EdgeBack.h"
#include "AppLauncher.h"
#include "AppIcons.h"
#include "WeatherIcons.h"
#include "UiTransition.h"
#include "LifeData.h"
#include "LocalLunar.h"
#include "HomeInfo.h"
#include <stdlib.h>
bool custom_navigation_can_back(void);
static struct {lv_obj_t *screen;} guider_ui;
static bool touch_pressed;
static int touch_point_x,touch_point_y,current_page;
static int8_t swipe_dir;
static uint32_t app_millis(void){return lv_tick_get();}
static uint32_t touch_snapshot_load(void){return (touch_pressed?1u:0u)|((uint32_t)touch_point_x<<1)|((uint32_t)touch_point_y<<10);}
static bool idle_blocked;
static bool boot_blocked;
static bool boot_splash_busy(void){return boot_blocked;}
static bool alarm_ui_alert_visible(void){return false;}
void desktop_block_boot(bool blocked){boot_blocked=blocked;}
static bool screen_idle_pointer(bool down,int x,int y,uint32_t now){(void)down;(void)x;(void)y;(void)now;return idle_blocked;}
''' + read + '''
void desktop_input(lv_indev_drv_t*d,lv_indev_data_t*p,bool down,int x,int y){touch_pressed=down;touch_point_x=x;touch_point_y=y;touch_read(d,p);}
void desktop_home(lv_obj_t*screen){guider_ui.screen=screen;}
void desktop_block_input(bool blocked){idle_blocked=blocked;}
void desktop_set_page(int page){current_page=page;}
int desktop_get_page(void){return current_page;}
int desktop_take_swipe(void){int n=swipe_dir;swipe_dir=0;return n;}
''', encoding='utf-8')
    ui = (ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8')
    scaffold = r'''
#include "lvgl.h"
#include "UITheme.h"
#include "HomeInfo.h"
#include "LifeData.h"
#include "LocalLunar.h"
#include "FlipClock.h"
#include "AppLauncher.h"
#include "AppIcons.h"
#include "WeatherIcons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
static float host_cosf(float v){return (float)cos(v);}
static float host_sinf(float v){return (float)sin(v);}
static long host_lroundf(float v){return (long)(v>=0?floor(v+0.5f):ceil(v-0.5f));}
#define cosf host_cosf
#define sinf host_sinf
#define lroundf host_lroundf
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
#define GYRO_ICON_SIZE 56
#define GAME_ICON_SIZE 64
#define NUMPAD_ICON_SIZE 56
static uint8_t gyro_icon_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(56,56)];
static uint8_t game_icon_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(64,64)];
static uint8_t numpad_icon_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(56,56)];
static lv_img_dsc_t gyro_img,game_img,numpad_img;
static lv_img_dsc_t sd_icon_music,sd_icon_weather,sd_icon_clock,sd_icon_settings,sd_icon_photos,sd_icon_browser,sd_icon_video,sd_icon_calendar,sd_icon_ai_assistant;
static lv_obj_t *wallpaper_img,*weather_icon_img,*weather_text,*weather_temp_label,*time_label,*city_btn_label,*home_gyro_temp_label,*detail_info;
static flip_clock_t *home_flip_clock;
static const lv_font_t *chinese_font_20;
LV_IMG_DECLARE(img_sunny);
static const char *weather="晴";
static int get_weather_code(void){return 0;}
extern int app_opens[12];
#define CB(name,id) static void name(lv_event_t*e){(void)e;app_opens[id]++;}
CB(gyro_btn_cb,0) CB(games_app_btn_cb,1) CB(music_app_btn_cb,2)
CB(weather_app_btn_cb,3) CB(clock_btn_cb,4) CB(photo_app_btn_cb,5) CB(browser_app_btn_cb,6)
CB(video_app_btn_cb,7) CB(settings_btn_cb,8) CB(calendar_app_btn_cb,9) CB(ai_assistant_app_btn_cb,10) CB(words_app_btn_cb,11)
static void wallpaper_btn_cb(lv_event_t*e){(void)e;}
static const lv_font_t *ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_font_t *ui_chinese_font_16(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_img_dsc_t *get_wallpaper_image(void){return NULL;}
static const char *get_wallpaper_path(void){return "S:/wallpapers/wallpaper1.bin";}
bool life_data_get_day(int y,int m,int d,life_calendar_day_t*out){return local_lunar_get(y,m,d,out);}
static life_indices_t host_indices={.dressing="舒适",.uv="弱",.flu="少发",.sport="较不宜"};
static bool host_have_indices=true;
bool life_data_get_indices(life_indices_t*out){memset(out,0,sizeof(*out));if(!host_have_indices)return false;*out=host_indices;return true;}
static void load_icon(lv_img_dsc_t*image,const char*root,const char*name){
 char path[1024];snprintf(path,sizeof(path),"%s/icons/%s.bin",root,name);FILE*f=fopen(path,"rb");if(!f)abort();
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);fread(&image->header,4,1,f);image->data_size=n-4;
 uint8_t*data=malloc(n-4);fread(data,n-4,1,f);image->data=data;fclose(f);
}
'''
    names = ['get_weather_icon', 'style_weather_icon', 'gyro_draw_ellipse', 'create_gyro_icon', 'setup_home_page', 'setup_icon_page']
    factory = '''
void desktop_build(lv_obj_t*home,lv_obj_t*apps,const lv_font_t*font,const char*sd){
 chinese_font_20=font;create_gyro_icon();
'''
    for name in ['music', 'weather', 'clock', 'settings', 'photos', 'browser', 'video', 'calendar', 'ai_assistant']:
        factory += f' load_icon(&sd_icon_{name},sd,"{name}");\n'
    factory += ''' setup_home_page(home);setup_icon_page(apps);
 struct tm date={0};date.tm_year=126;date.tm_mon=10;date.tm_mday=14;mktime(&date);home_info_update(&date);
}
void desktop_rebuild_apps(lv_obj_t*page){setup_icon_page(page);}
flip_clock_t*desktop_clock(void){return home_flip_clock;}
void desktop_home_sample(const char*condition,const char*weather_temp,const char*device_temp){
 lv_label_set_text(weather_text,condition);lv_label_set_text(weather_temp_label,weather_temp);lv_label_set_text(home_gyro_temp_label,device_temp);
}
void desktop_life_sample(int y,int m,int d){struct tm date={0};date.tm_year=y-1900;date.tm_mon=m-1;date.tm_mday=d;mktime(&date);home_info_update(y?&date:NULL);}
const char*desktop_lunar_text(void){return lv_label_get_text(lunar_label);}
const char*desktop_weekday_text(void){return lv_label_get_text(weekday_label);}
const char*desktop_date_text(void){return lv_label_get_text(gregorian_label);}
const char*desktop_term_text(void){return lv_obj_has_flag(term_label,LV_OBJ_FLAG_HIDDEN)?"":lv_label_get_text(term_label);}
const char*desktop_festival_text(void){return lv_obj_has_flag(festival_label,LV_OBJ_FLAG_HIDDEN)?"":lv_label_get_text(festival_label);}
const char*desktop_index_text(int i){return lv_label_get_text(life_values[i]);}
void desktop_indices_sample(bool available,bool long_text){host_have_indices=available;strcpy(host_indices.sport,long_text?"较不适宜长时间运动":"较不宜");}
void desktop_three_character_fit(void){
 lv_obj_update_layout(life_panel);
 for(int i=0;i<4;i++){
  lv_point_t needed;lv_txt_get_size(&needed,"：较不宜",lv_obj_get_style_text_font(life_values[i],0),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);
  printf("life reading required=%d available=%d\\n",needed.x,lv_obj_get_content_width(life_values[i]));
  assert(needed.x<=lv_obj_get_content_width(life_values[i]));
 }
 assert(!strcmp(lv_label_get_text(life_values[3]),"：较不宜"));
}
void desktop_home_info_bounds(void){
 lv_obj_update_layout(life_panel);lv_obj_update_layout(date_panel);
 lv_area_t a,b,c;lv_obj_get_coords(life_panel,&a);lv_obj_get_coords(date_panel,&b);
 assert(a.x1>=200&&a.x2<480&&a.y1>=0&&a.y2<=102&&b.y1>300&&b.y2<465);
 for(int i=0;i<4;i++){lv_obj_get_coords(life_values[i],&c);assert(c.x1>a.x1&&c.x2<a.x2&&c.y1>a.y1&&c.y2<a.y2);}
 lv_obj_get_coords(date_row,&c);assert(abs((c.x1+c.x2)-(b.x1+b.x2))<=2);
 lv_obj_get_coords(gregorian_label,&a);lv_obj_get_coords(weekday_label,&c);assert(c.x1>a.x2);
 assert(abs((a.x1+a.x2)-(b.x1+b.x2))<=2);
 assert(lv_obj_get_style_text_font(gregorian_label,0)==&lv_font_montserrat_28);
 lv_obj_t *rows[]={lunar_label,term_label,festival_label};
 for(int i=0;i<3;i++)if(!lv_obj_has_flag(rows[i],LV_OBJ_FLAG_HIDDEN)){lv_obj_get_coords(rows[i],&c);assert(c.y1>b.y1&&c.y2<b.y2);}
}
'''
    (OUT/'production_pages.c').write_text(scaffold + '\n'.join(function(ui, n) for n in names) + '\n#include "'+(ROOT/'src/HomeInfo.c').as_posix()+'"\n' + factory, encoding='utf-8')
    lvgl = ROOT/'components/lvgl'
    sources = sorted(str(p) for p in (lvgl/'src').rglob('*.c'))
    exe = OUT/'desktop-test.exe'
    command = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I', str(OUT), '-I', str(lvgl), '-I', str(ROOT/'components'), '-I', str(ROOT/'src'),
               '-include', str(OUT/'host_time.h'), str(ROOT/'tests/desktop_ui_host_test.c'), str(OUT/'input.c'),
               str(OUT/'production_pages.c'), str(ROOT/'src/FlipClock.c'), str(ROOT/'src/flip_digits_72.c'), str(ROOT/'src/AppLauncher.c'),
               str(ROOT/'src/EdgeBack.c'), str(ROOT/'src/QuickPanel.c'), str(ROOT/'src/HotspotIcon.c'), str(ROOT/'src/LocalLunar.c'),
               str(ROOT/'src/UiTransition.c'), str(ROOT/'src/AppIconGame.c'), str(ROOT/'src/AppIconEnglish.c'), str(ROOT/'src/WeatherIcons.c'), str(ROOT/'src/WeatherIconsData.c'), str(ROOT/'src/ui/generated/images/sunny.c'), *sources, '-o', str(exe)]
    if args.cc:
        compiler = Path(args.cc)
        if compiler.name.lower() in ('cl', 'cl.exe'):
            units = command[command.index(str(ROOT/'tests/desktop_ui_host_test.c')):-2]
            arguments = ['/nologo','/std:c11','/O2','/utf-8','/D_CRT_SECURE_NO_WARNINGS',
                         '/DLV_CONF_INCLUDE_SIMPLE','/DLV_KCONFIG_IGNORE',
                         f'/I{OUT}',f'/I{lvgl}',f'/I{ROOT / "components"}',f'/I{ROOT / "src"}',
                         f'/FI{OUT / "host_time.h"}',*units,f'/Fe:{exe}',f'/Fo{OUT}\\']
            response=OUT/'msvc.rsp'
            response.write_text('\n'.join(subprocess.list2cmdline([s]) for s in arguments),encoding='utf-8')
            command=[str(compiler),'@'+str(response)]
        else:
            command[0]=str(compiler)
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print((result.stdout+result.stderr).decode(errors='replace')[-5000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT/'sd_card_files'), str(OUT)], check=True, timeout=60)
    for file in OUT.glob('*.rgb565'):
        raw = file.read_bytes()
        rgb = bytearray()
        for i in range(0, len(raw), 2):
            color = raw[i] | raw[i+1] << 8
            rgb.extend((((color >> 11) & 31) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(file.with_suffix('.png'))
    frames = [Image.open(p) for p in sorted(OUT.glob('flip-frame-*.png'))]
    if frames:
        frames[0].save(OUT/'flip-clock.gif',save_all=True,append_images=frames[1:],duration=80,loop=0)
    transition_frames = [Image.open(p) for p in sorted(OUT.glob('transition-frame-*.png'))]
    if transition_frames:
        transition_frames[0].save(OUT/'page-transition.gif',save_all=True,append_images=transition_frames[1:],duration=[40]*(len(transition_frames)-1)+[500],loop=0)
    print('Actual production LVGL previews:', OUT)


if __name__ == '__main__':
    main()
