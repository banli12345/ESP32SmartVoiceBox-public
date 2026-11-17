"""Drive production BrowserUI -> OwnMusicUI -> BrowserUI with real LVGL/edge gestures.
The HTTP, song, SD and audio boundaries are fakes; both UIs and their routing execute.
"""
from pathlib import Path
import subprocess
from PIL import Image
from own_music_ui_host_test import HARNESS as MUSIC
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/browser-local-open-fix-20261115/host-integrated'
EXTRA=r'''
#include "WebBrowser.h"
#include "BrowserLocalPage.h"
#include "browser_local_url.h"
#include "EdgeBack.h"
static unsigned http_starts;static web_browser_view_t webview={.state=BROWSER_IDLE,.revision=1};
bool browser_is_own_music_page(const char*s){return browser_is_local_music_url(s,"172.31.42.1",wifi?"192.168.1.43":NULL);}
bool web_browser_start(const char*s){http_starts++;webview.busy=false;webview.state=wifi?BROWSER_LOADING:BROWSER_ERROR;++webview.revision;snprintf(webview.url,sizeof(webview.url),"%s",s);return wifi;}
void web_browser_release(void){webview.busy=false;webview.state=BROWSER_IDLE;++webview.revision;}
void web_browser_cancel(void){web_browser_release();}
void web_browser_get_view(web_browser_view_t*v){*v=webview;}
bool web_browser_copy_page(unsigned p,char*s,size_t n){(void)p;if(n)s[0]=0;return false;}
bool web_browser_copy_link(unsigned p,browser_link_t*l){(void)p;(void)l;return false;}
#include "@ROOT@/src/BrowserUI.c"
'''
MAIN=r'''
static lv_obj_t*label_named(lv_obj_t*o,const char*t){if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),t))return o;for(unsigned k=0;k<lv_obj_get_child_cnt(o);k++){lv_obj_t*x=label_named(lv_obj_get_child(o,k),t);if(x)return x;}return NULL;}
static lv_obj_t*by_type(lv_obj_t*o,const lv_obj_class_t*c){if(lv_obj_check_type(o,c))return o;for(unsigned k=0;k<lv_obj_get_child_cnt(o);k++){lv_obj_t*x=by_type(lv_obj_get_child(o,k),c);if(x)return x;}return NULL;}
static void edge_return(void){touch(true,7,150,80);touch(true,110,150,90);touch(false,110,150,100);advance(250);}
static void complete_search(void){preparing=false;result_count=3;for(unsigned k=0;k<3;k++){snprintf(results[k].id,sizeof(results[k].id),"id-%u",k);snprintf(results[k].title,sizeof(results[k].title),k==0?"茉莉花":"示例歌曲 %u",k+1);snprintf(results[k].artist,sizeof(results[k].artist),"示例歌手");}snprintf(reply,sizeof(reply),"找到三首歌曲，点击播放");result_revision++;advance(400);}
int main(int argc,char**argv){
@INIT@
 lv_obj_t*desktop=lv_obj_create(NULL);lv_scr_load(desktop);edge_back_init();wifi=false;
 browser_ui_open(desktop,chinese_font_20,true,NULL);advance(200);lv_obj_t*browser=ui->screen;assert(!strcmp(lv_textarea_get_text(ui->address),"http://172.31.42.1/")&&!http_starts);
 tap(420,80);advance(200);lv_obj_t*music=lv_scr_act();assert(music!=browser&&label_named(music,"随身音乐")&&!http_starts&&ui&&ui->timer->paused);
 screenshot(argv[2],"native-open-offline");lv_obj_t*query=by_type(music,&lv_textarea_class);assert(query);lv_textarea_set_text(query,"茉莉花");tap(420,78);assert(!commands&&!http_starts);
 wifi=true;tap(420,78);assert(commands==1&&!last_save&&!strcmp(last_command,"搜歌 茉莉花"));complete_search();assert(label_named(music,"茉莉花"));screenshot(argv[2],"native-search-results");
 tap(120,235);assert(commands==2&&!last_save&&!strcmp(last_command,"播放第1首")&&downloads==0);preparing=false;playing=true;saved_index=2;snprintf(reply,sizeof(reply),"WAV 已保存：/music/茉莉花 - 示例歌手.wav\nLRC 已保存：/music/茉莉花 - 示例歌手.lrc");result_revision++;advance(400);tap(410,410);assert(selected==2&&plays==1);screenshot(argv[2],"native-saved-playback");
 unsigned stopped=stops;edge_return();assert(ui&&lv_scr_act()==browser&&!ui->timer->paused&&stops==stopped&&!http_starts);
 /* An external page remains a generic request, then local refresh uses local UI. */
 lv_textarea_set_text(ui->address,"https://example.com/");tap(420,80);assert(http_starts==1);web_browser_release();advance(200);
 lv_textarea_set_text(ui->address,"http://172.31.42.1/");tap(180,125);advance(200);assert(label_named(lv_scr_act(),"随身音乐")&&http_starts==1);edge_return();assert(lv_scr_act()==browser);
 ui->history_count=0;ui->history_position=-1;edge_return();assert(!ui&&lv_scr_act()==desktop);
 /* Repeated real open/back chains reclaim both pages, timers and IME objects. */
 lv_mem_monitor_t before,after;lv_mem_monitor(&before);for(unsigned k=0;k<16;k++){wifi=false;browser_ui_open(desktop,chinese_font_20,true,NULL);advance(80);tap(420,80);advance(80);assert(label_named(lv_scr_act(),"随身音乐"));edge_return();assert(ui&&lv_scr_act()==ui->screen);edge_return();assert(!ui&&lv_scr_act()==desktop&&lv_mem_test()==LV_RES_OK);}lv_mem_monitor(&after);assert(after.free_size+256>=before.free_size);
 puts("Browser/music integration PASS: default Open offline, real native search/play/save controls, generic external request, local refresh, real edge returns, background music preservation and 16 cleanup cycles");return 0;
}
'''
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)');(OUT/'lv_conf.h').write_text(config,encoding='utf-8')
    prefix=MUSIC[:MUSIC.index('#include "@ROOT@/src/OwnMusicUI.c"')]
    prefix=prefix.replace('void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;edge=cb;}','')
    driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')]
    driver=driver.replace('p->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;','bool used=edge_back_pointer(down,x,y,true);p->state=down&&!used?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;')
    init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    unit=OUT/'test.c';unit.write_text((prefix+EXTRA+driver+MAIN.replace('@INIT@',init)).replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'integration.exe'
    command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),'-I'+str(ROOT/'components/lvgl/src/extra/others/ime'),str(unit),str(ROOT/'src/OwnMusicUI.c'),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),str(ROOT/'src/browser_local_url.c'),str(ROOT/'src/browser_document.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    r=subprocess.run(command,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace');subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))
if __name__=='__main__':main()
