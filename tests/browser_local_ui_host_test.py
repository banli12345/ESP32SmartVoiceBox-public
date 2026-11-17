"""Real browser UI prefills the local address and preserves ordinary editable URL browsing."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS as PLAYLIST_DRIVER

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/browser-local-open-fix-20261115/host-routing'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    # Adapter is a boundary fake here; its full interaction tests are separate.
    (OUT/'OwnMusicUI.h').write_text('#pragma once\n#include "lvgl.h"\nvoid own_music_ui_open(lv_obj_t *return_screen);\n', encoding='utf-8')
    driver = PLAYLIST_DRIVER[PLAYLIST_DRIVER.index('static bool down;'):PLAYLIST_DRIVER.index('int main(')]
    init = PLAYLIST_DRIVER[PLAYLIST_DRIVER.index(' assert(argc==3);'):PLAYLIST_DRIVER.index(' for(int i=0;i<12;')]
    (OUT/'test.c').write_text(HARNESS.replace('@ROOT@', ROOT.as_posix()).replace('@DRIVER@',driver).replace('@INIT@',init), encoding='utf-8')
    exe = OUT/'browser-ui.exe'
    command = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I'+str(OUT), '-I'+str(ROOT/'components/lvgl'), '-I'+str(ROOT/'components'), '-I'+str(ROOT/'src'),
               str(OUT/'test.c'), str(ROOT/'src/browser_local_url.c'), str(ROOT/'src/browser_document.c'), str(ROOT/'src/UiTransition.c'),
               *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')], '-o',str(exe)]
    subprocess.run(command, check=True, capture_output=True)
    subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)], check=True, timeout=30)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes(); rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))

HARNESS=r'''
#include "lvgl.h"
#include "BrowserUI.h"
#include "WebBrowser.h"
#include "browser_local_url.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
lv_font_t *chinese_font_20;
static lv_obj_t *local_screen,*local_return;
static unsigned opens,starts,releases;
static lv_event_cb_t edge;
static web_browser_view_t view={.state=BROWSER_IDLE,.revision=1};
bool browser_is_own_music_page(const char *s){return browser_is_local_music_url(s,"172.31.42.1","192.168.1.43");}
void own_music_ui_open(lv_obj_t *s){++opens;local_return=s;lv_scr_load(local_screen);}
static char requested[BROWSER_URL_CAP];
bool web_browser_start(const char*s){snprintf(requested,sizeof(requested),"%s",s);++starts;view.busy=true;view.state=BROWSER_LOADING;++view.revision;return true;}
void web_browser_release(void){++releases;view.busy=false;view.state=BROWSER_IDLE;++view.revision;}
void web_browser_cancel(void){web_browser_release();}
void web_browser_get_view(web_browser_view_t *v){*v=view;}
bool web_browser_copy_page(unsigned p,char*t,size_t n){(void)p;if(n)t[0]=0;return false;}
bool web_browser_copy_link(unsigned p,browser_link_t*l){(void)p;(void)l;return false;}
void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;edge=cb;}
#include "@ROOT@/src/BrowserUI.c"
@DRIVER@
int main(int argc,char**argv){
@INIT@
 lv_obj_t *desktop=lv_obj_create(NULL);local_screen=lv_obj_create(NULL);lv_scr_load(desktop);
 browser_ui_open(desktop,chinese_font_20,true,NULL);advance(200);
 assert(ui&&lv_scr_act()==ui->screen&&!strcmp(lv_textarea_get_text(ui->address),"http://172.31.42.1/")&&starts==0);
 lv_obj_t*header=lv_obj_get_child(ui->screen,0);assert(lv_obj_get_child_cnt(header)==1);
 screenshot(argv[2],"browser-home");tap(400,25);assert(opens==0&&starts==0);
 tap(420,80);assert(opens==1&&starts==0&&lv_scr_act()==local_screen&&local_return==ui->screen&&ui->timer->paused);
 lv_scr_load(local_return);advance(200);assert(!ui->timer->paused&&ui->history_position==-1);
 const char*aliases[]={"http://127.0.0.1/","localhost","http://192.168.1.43/"};
 for(unsigned i=0;i<3;i++){lv_textarea_set_text(ui->address,aliases[i]);tap(420,80);assert(lv_scr_act()==local_screen&&starts==0);lv_scr_load(local_return);advance(200);}assert(opens==4);
 lv_textarea_set_text(ui->address,"https://example.com/");tap(420,80);assert(starts==1&&view.busy&&!strcmp(requested,"https://example.com/"));web_browser_release();advance(200);
 /* A local refresh must not reload the older external history URL. */
 lv_textarea_set_text(ui->address,"http://172.31.42.1/");tap(182,125);assert(opens==5&&starts==1&&lv_scr_act()==local_screen);lv_scr_load(local_return);advance(200);
 lv_textarea_set_text(ui->address,"http://172.31.42.1/api/status");tap(420,80);assert(starts==2&&opens==5&&!strcmp(requested,"http://172.31.42.1/api/status"));web_browser_release();advance(200);
 lv_textarea_set_text(ui->address,"https://example.org/edited");browser_ui_open(desktop,chinese_font_20,true,NULL);advance(200);assert(!strcmp(lv_textarea_get_text(ui->address),"https://example.org/edited")&&starts==2);
 tap(100,80);assert(!lv_obj_has_flag(ui->keyboard,LV_OBJ_FLAG_HIDDEN));edge(NULL);assert(lv_obj_has_flag(ui->keyboard,LV_OBJ_FLAG_HIDDEN)&&ui);
 ui->history_count=0;ui->history_position=-1;edge(NULL);advance(100);assert(!ui&&lv_scr_act()==desktop);
 browser_ui_open(desktop,chinese_font_20,true,NULL);advance(200);assert(ui&&!strcmp(lv_textarea_get_text(ui->address),"http://172.31.42.1/")&&starts==2);edge(NULL);advance(100);assert(!ui&&lv_scr_act()==desktop);
 puts("browser routing PASS: default local adapter, aliases, no shortcut/auto HTTP, refresh after external history, subpaths external, edit/reopen/keyboard/back lifecycle");return 0;
}
'''

if __name__ == '__main__': main()
