"""Render real LVGL timeout settings and exercise choices with pointer events."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS, ROOT
from desktop_ui_host_test import function

OUT = ROOT / '.tools/screen-idle-20261114/ui-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    source = (ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8')
    settings = '\n'.join(function(source, name) for name in (
        'settings_card_draw_sun', 'settings_card_create', 'screen_idle_btn_cb', 'create_settings_screen'))
    code = r'''#include "lvgl.h"
#include "ScreenIdleUI.h"
#include "HotspotIcon.h"
#include "ScreenIdle.h"
#include "UITheme.h"
#include "QuickPanel.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
static lv_font_t *chinese_font_20;
static unsigned selected=5,saves;static bool fail;
uint16_t screen_idle_get_timeout(void){return selected;}
bool screen_idle_set_timeout(uint16_t minutes){if(fail)return false;selected=minutes;saves++;return true;}
static lv_event_cb_t back;
void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;back=cb;}
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
static lv_obj_t *settings_screen,*restart_button,*display_restart_button,*display_restart_title;
static const lv_font_t*ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static lv_obj_t*create_back_button(lv_obj_t*p,lv_align_t a,int x,lv_event_cb_t cb){(void)p;(void)a;(void)x;(void)cb;return NULL;}
static void back_btn_cb(lv_event_t*e){(void)e;}
static void wifi_btn_cb(lv_event_t*e){(void)e;}
static void brightness_btn_cb(lv_event_t*e){(void)e;}
static void volume_btn_cb(lv_event_t*e){(void)e;}
static void wireless_settings_cb(lv_event_t*e){(void)e;}
static void device_info_btn_cb(lv_event_t*e){(void)e;}
static void system_update_btn_cb(lv_event_t*e){(void)e;}
static void ai_voice_btn_cb(lv_event_t*e){(void)e;}
static void restart_btn_cb(lv_event_t*e){(void)e;}
static void display_restart_btn_cb(lv_event_t*e){(void)e;}
''' + settings + driver + r'''
int main(int argc,char **argv){
@INIT@
    create_settings_screen();lv_obj_t *settings=settings_screen;lv_scr_load(settings);advance(100);
    lv_obj_t *menu=lv_obj_get_child(settings,1);assert(lv_obj_get_child_cnt(menu)==11);
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(lv_obj_get_child(menu,2),1)),"息屏时间"));
    lv_obj_t*hotspot=lv_obj_get_child(lv_obj_get_child(lv_obj_get_child(menu,6),0),0);
    assert(!lv_obj_check_type(hotspot,&lv_label_class));
    screenshot(argv[2],"settings-menu");
    tap(240,252);advance(100);assert(lv_scr_act()!=settings);
    lv_obj_t *screen=lv_scr_act(),*list=lv_obj_get_child(screen,-1);
    assert(lv_obj_get_child_cnt(list)==9);screenshot(argv[2],"timeout-settings");
    static const unsigned options[]={1,2,3,5,10,20,30,60,0};
    for(unsigned i=0;i<9;i++){
        lv_obj_t *row=lv_obj_get_child(list,i);lv_obj_scroll_to_view(row,LV_ANIM_OFF);advance(60);
        lv_area_t area;lv_obj_get_coords(row,&area);assert(area.y1>=124&&area.y2<469);
        tap((area.x1+area.x2)/2,(area.y1+area.y2)/2);assert(selected==options[i]&&saves==i+1);
        for(unsigned j=0;j<9;j++)assert(lv_obj_has_flag(lv_obj_get_child(lv_obj_get_child(list,j),1),LV_OBJ_FLAG_HIDDEN)==(i!=j));
    }
    screenshot(argv[2],"timeout-never");
    fail=true;lv_obj_t*row=lv_obj_get_child(list,0);lv_obj_scroll_to_view(row,LV_ANIM_OFF);advance(60);
    lv_area_t area;lv_obj_get_coords(row,&area);tap(240,(area.y1+area.y2)/2);assert(selected==0&&saves==9);
    assert(strstr(lv_label_get_text(lv_obj_get_child(screen,2)),"失败"));
    back(NULL);assert(lv_scr_act()==settings);screen_idle_ui_open(settings,chinese_font_20);assert(lv_scr_act()==screen);
    lv_scr_load(settings);lv_obj_clean(settings);lv_obj_t*icon=hotspot_icon_create(settings,80);
    lv_obj_set_style_text_color(icon,lv_color_hex(0x365A26),0);lv_obj_center(icon);screenshot(argv[2],"hotspot-icon");
    assert(lv_mem_test()==LV_RES_OK);puts("Timeout real LVGL passed: settings entry, nine touch choices, scrolling, selection, save error, back/reopen, hotspot drawing");
}
'''
    unit = OUT/'test.c'
    unit.write_text(code.replace('@INIT@', init), encoding='utf-8')
    exe = OUT/'ui.exe'
    cmd = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
           '-I', str(OUT), '-I', str(ROOT/'components/lvgl'), '-I', str(ROOT/'src'), str(ROOT/'src/UiTransition.c'),str(unit),
           str(ROOT/'src/ScreenIdleUI.c'), str(ROOT/'src/HotspotIcon.c'),
           *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(cmd, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace')[-4000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT/'sd_card_files/fonts/chinese_20.bin'), str(OUT)], check=True, timeout=45)
    for file in OUT.glob('*.rgb565'):
        raw = file.read_bytes()
        rgb = bytearray()
        for i in range(0, len(raw), 2):
            c = raw[i] | raw[i+1] << 8
            rgb.extend((((c >> 11) & 31)*255//31, ((c >> 5) & 63)*255//63, (c & 31)*255//31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(file.with_suffix('.png'))


if __name__ == '__main__':
    main()
