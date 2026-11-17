"""Exercise production WiFi list/password handlers in real LVGL with fake radio state."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import ROOT, HARNESS
from desktop_ui_host_test import function

OUT = ROOT / '.tools/wifi-profiles-20261115/ui-host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    source = (ROOT / 'src/ui/custom/custom.c').read_text(encoding='utf-8')
    start = source.index('static lv_obj_t *wifi_list_screen;')
    fields = source[start:source.index('static EXT_RAM_BSS_ATTR', start)]
    names = ['wifi_open_password', 'wifi_request_selected', 'wifi_list_long_cb', 'wifi_list_btn_cb',
             'wifi_update_connection_info', 'wifi_render_scan_results', 'wifi_refresh_cb', 'wifi_btn_cb',
             'wifi_pwd_confirm_cb', 'wifi_enabled_event', 'wifi_settings_state_timer',
             'wifi_pwd_ready_cb', 'wifi_pwd_click_cb', 'wifi_pwd_long_cb',
             'create_wifi_list_screen', 'create_wifi_password_screen']
    driver = HARNESS[HARNESS.index('static bool down;'):HARNESS.index('int main(')]
    init = HARNESS[HARNESS.index(' assert(argc==3);'):HARNESS.index(' for(int i=0;i<12;')]
    scaffold = r'''
#include "lvgl.h"
#include "UITheme.h"
#include "QuickSettingsService.h"
#include "setwifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
static lv_color_t pixels[480*480];
static lv_font_t *chinese_font_20;
static lv_obj_t *main_screen_ref;
static bool enabled=true, scanning, busy, connected=true, failed, newly_saved;
static uint32_t revision=1;
static unsigned saved_requests, password_requests, scan_requests;
static char current[33]="Home Demo", target[33], submitted[33], submitted_password[64];
static const char *networks[]={"Home Demo","Office Demo","New Demo","Open Demo","12345678901234567890123456789012"};
bool get_wifi_connected(void){return connected;}
const char *get_wifi_connected_ssid(void){return connected?current:"";}
bool wifi_connection_is_busy(void){return busy;}
const char *wifi_connection_target(void){return target;}
bool wifi_last_connection_failed(void){return failed;}
bool wifi_network_is_saved(const char*s){return !strcmp(s,networks[0])||!strcmp(s,networks[1])||(newly_saved&&!strcmp(s,networks[2]));}
bool do_wifi_connect_saved(const char*s){assert(wifi_network_is_saved(s));saved_requests++;snprintf(target,sizeof(target),"%s",s);snprintf(submitted,sizeof(submitted),"%s",s);busy=true;failed=false;return true;}
bool do_wifi_connect(const char*s,const char*p){assert(s&&p);password_requests++;snprintf(target,sizeof(target),"%s",s);snprintf(submitted,sizeof(submitted),"%s",s);snprintf(submitted_password,sizeof(submitted_password),"%s",p);busy=true;failed=false;return true;}
bool wifi_scan_is_busy(void){return scanning;}
uint32_t wifi_scan_revision(void){return revision;}
bool wifi_scan_network_is_open(int i){return i==3;}
int do_wifi_scan(void){scan_requests++;scanning=true;return 5;}
int get_wifi_scan_count(void){return enabled?5:0;}
const char *get_wifi_ssid(int i){return i>=0&&i<5?networks[i]:"";}
void quick_radio_get(quick_radio_t r,quick_radio_state_t*s){assert(r==QUICK_RADIO_WIFI);memset(s,0,sizeof(*s));s->enabled=enabled;}
bool quick_radio_request(quick_radio_t r,bool on){assert(r==QUICK_RADIO_WIFI);enabled=on;return true;}
static const lv_font_t *ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_font_t *ui_chinese_font_16(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static void settings_btn_cb(lv_event_t*e){(void)e;}
static lv_obj_t *create_back_button(lv_obj_t*p,lv_align_t a,int x,lv_event_cb_t cb){(void)p;(void)a;(void)x;(void)cb;return NULL;}
static void ui_screen_open(lv_obj_t*s){lv_scr_load(s);}
static void ui_screen_back(lv_obj_t*s){lv_scr_load(s);}
static void hide_text_keyboard(lv_obj_t*k){lv_obj_add_flag(k,LV_OBJ_FLAG_HIDDEN);}
static void text_keyboard_event_cb(lv_event_t*e){(void)e;}
static void text_input_event_cb(lv_event_t*e){(void)e;}
'''
    cases = r'''
static lv_obj_t *row(unsigned i){return lv_obj_get_child(wifi_list,i);}
static const char *badge(unsigned i){return lv_label_get_text(lv_obj_get_child(row(i),1));}
static void tap_row(unsigned i){lv_obj_t*b=row(i);lv_obj_scroll_to_view(b,LV_ANIM_OFF);lv_obj_update_layout(wifi_list_screen);lv_area_t a;lv_obj_get_coords(b,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
int main(int argc,char**argv){
@INIT@
 main_screen_ref=lv_obj_create(NULL);
 create_wifi_list_screen();create_wifi_password_screen();wifi_btn_cb(NULL);advance(100);
 assert(lv_scr_act()==wifi_list_screen&&scan_requests==1&&lv_obj_get_child_cnt(wifi_list)==5);
 assert(!strcmp(badge(0),"已连接")&&!strcmp(badge(1),"已保存"));
 scanning=false;revision++;advance(600);screenshot(argv[2],"saved-networks");
 lv_area_t list_area,refresh_area;lv_obj_get_coords(wifi_list,&list_area);lv_obj_get_coords(lv_obj_get_parent(wifi_refresh_label),&refresh_area);assert(list_area.y2<refresh_area.y1&&refresh_area.y2<480);
 tap_row(0);assert(!saved_requests&&!password_requests);
 tap_row(1);assert(saved_requests==1&&!strcmp(submitted,"Office Demo")&&lv_scr_act()==wifi_list_screen);
 wifi_settings_state_timer(NULL);assert(!strcmp(badge(1),"连接中")&&strstr(lv_label_get_text(wifi_connection_ssid_label),"Office Demo"));
 screenshot(argv[2],"switching-network");
 busy=false;strcpy(current,"Office Demo");advance(600);assert(!strcmp(badge(1),"已连接"));
 lv_obj_t *button=row(0);lv_obj_update_layout(wifi_list_screen);lv_area_t a;lv_obj_get_coords(button,&a);touch(true,(a.x1+a.x2)/2,(a.y1+a.y2)/2,700);touch(false,(a.x1+a.x2)/2,(a.y1+a.y2)/2,100);
 assert(lv_scr_act()==wifi_pwd_screen&&saved_requests==1&&!password_requests&&!strcmp(wifi_selected_ssid,"Home Demo"));
 assert(!lv_textarea_get_text(wifi_pwd_textarea)[0]&&lv_textarea_get_password_mode(wifi_pwd_textarea));
 lv_textarea_set_text(wifi_pwd_textarea,"Demo!Pass9");lv_event_send(wifi_pwd_textarea,LV_EVENT_READY,NULL);lv_event_send(wifi_pwd_textarea,LV_EVENT_READY,NULL);
 assert(password_requests==1&&!strcmp(submitted_password,"Demo!Pass9")&&lv_scr_act()==wifi_list_screen);
 busy=false;failed=true;connected=false;advance(600);assert(strstr(lv_label_get_text(wifi_connection_status_label),"失败"));
 tap_row(2);assert(lv_scr_act()==wifi_pwd_screen&&!strcmp(wifi_selected_ssid,"New Demo")&&!lv_textarea_get_text(wifi_pwd_textarea)[0]);
 lv_textarea_set_text(wifi_pwd_textarea,"New!Pass9");lv_event_send(wifi_pwd_textarea,LV_EVENT_READY,NULL);assert(password_requests==2&&!strcmp(submitted,"New Demo"));
 busy=false;connected=true;newly_saved=true;strcpy(current,"New Demo");advance(600);assert(!strcmp(badge(2),"已连接"));
 tap_row(3);assert(password_requests==3&&!submitted_password[0]&&!strcmp(submitted,"Open Demo")&&lv_scr_act()==wifi_list_screen);
 busy=false;connected=false;advance(600);
 lv_event_send(row(4),LV_EVENT_SHORT_CLICKED,NULL);assert(lv_scr_act()==wifi_pwd_screen&&strlen(wifi_selected_ssid)==32&&!strcmp(wifi_selected_ssid,networks[4]));
 ui_screen_back(wifi_list_screen);enabled=false;advance(600);assert(lv_obj_get_child_cnt(wifi_list)==1&&strstr(lv_label_get_text(lv_obj_get_child(wifi_list,0)),"开启"));
 enabled=true;advance(600);assert(scan_requests>=2);scanning=false;
 for(unsigned i=0;i<30;i++){revision++;advance(600);assert(lv_mem_test()==LV_RES_OK);}
 puts("WiFi real LVGL passed: badges, one-tap switch, long-press password without accidental join, new/open networks, 32-byte SSID, duplicate-submit guard, async refresh and footer geometry");
}
'''.replace('@INIT@', init)
    unit = OUT / 'test.c'
    unit.write_text(scaffold + fields + '\n' + '\n'.join(function(source, n) for n in names) + driver + cases, encoding='utf-8')
    exe = OUT / 'wifi-ui.exe'
    command = [str(ROOT / '.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I', str(OUT), '-I', str(ROOT / 'components/lvgl'), '-I', str(ROOT / 'components'),
               '-I', str(ROOT / 'src'), str(unit),
               *[str(p) for p in (ROOT / 'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace')[-4000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT / 'sd_card_files/fonts/chinese_20.bin'), str(OUT)], check=True, timeout=60)
    for path in OUT.glob('*.rgb565'):
        data = path.read_bytes()
        rgb = bytearray()
        for i in range(0, len(data), 2):
            value = data[i] | data[i + 1] << 8
            rgb.extend((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63, (value & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(path.with_suffix('.png'))


if __name__ == '__main__':
    main()
