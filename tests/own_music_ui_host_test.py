"""Production local-music UI in real LVGL; network/SD/audio are explicit boundary fakes."""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/own-music-browser-20261115/music-ui-host'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (OUT/'lv_conf.h').write_text(config,encoding='utf-8')
    driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')]
    init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    unit=OUT/'test.c'
    unit.write_text(HARNESS.replace('@ROOT@',ROOT.as_posix()).replace('@DRIVER@',driver).replace('@INIT@',init),encoding='utf-8')
    exe=OUT/'music-ui.exe'
    command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE',
             '-I'+str(OUT),'-I'+str(ROOT/'components/lvgl'),'-I'+str(ROOT/'components'),'-I'+str(ROOT/'src'),
             '-I'+str(ROOT/'components/lvgl/src/extra/others/ime'),str(unit),str(ROOT/'src/UiTransition.c'),
             *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    result=subprocess.run(command,capture_output=True)
    if result.returncode: print(result.stderr.decode(errors='replace'));raise SystemExit(result.returncode)
    subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))

HARNESS=r'''
#include "lvgl.h"
#include "SDcar.h"
#include "OnlineMusic.h"
#include "Sound.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
lv_font_t *chinese_font_20,*chinese_font_16,*chinese_font_32;
static bool wifi=true,sd=true,preparing,playing,dict_ready=true;
static unsigned commands,pauses,plays,stops,list_opens,downloads;
static int saved_index=-1,selected=-1;
static uint32_t result_revision=1;
static char last_command[256],reply[1536]="准备就绪";
static bool last_save,can_download=false;
static online_music_song_t results[5];static unsigned result_count;
static lv_obj_t *list_screen,*list_return;
static lv_event_cb_t edge;
static lv_pinyin_dict_t dict[]={{"mo","茉末"},{"li","莉里"},{NULL,NULL}};
bool get_wifi_connected(void){return wifi;}
bool sdcar_is_ready(void){return sd;}
lv_pinyin_dict_t *get_sd_pinyin_dict(void){return dict_ready?dict:NULL;}
bool online_music_is_preparing(void){return preparing;}
unsigned online_music_copy_songs(online_music_song_t *out,unsigned n){if(n>result_count)n=result_count;memcpy(out,results,n*sizeof(*out));return n;}
uint32_t online_music_get_result(char*out,size_t n){if(out&&n)snprintf(out,n,"%s",reply);return result_revision;}
bool online_music_handle_web_command(const char*text,bool save){snprintf(last_command,sizeof(last_command),"%s",text);last_save=save;commands++;preparing=true;snprintf(reply,sizeof(reply),"正在准备");result_revision++;return true;}
int online_music_saved_track(char*p,size_t n){if(p&&n)snprintf(p,n,"%s",saved_index<0?"":"/music/茉莉花 - 示例歌手.wav");return saved_index;}
bool online_music_can_download_current(void){return can_download&&!preparing;}
bool online_music_download_current(void){if(!online_music_can_download_current())return false;downloads++;preparing=true;snprintf(reply,sizeof(reply),"正在把当前歌曲保存到歌单");result_revision++;return true;}
void music_audio_pause(void){pauses++;playing=false;}
void music_audio_play(void){plays++;playing=true;}
void music_audio_stop(void){stops++;playing=false;preparing=false;}
bool music_audio_is_playing(void){return playing;}
static char shown_title[320];
void music_audio_copy_title(char*p,size_t n){snprintf(p,n,"%s",shown_title);}
bool sdcar_copy_music_track(uint8_t i,char*p,size_t n,uint32_t*g){if(i!=2||saved_index<0)return false;snprintf(p,n,"/music/茉莉花 - 示例歌手.wav");if(g)*g=42;return true;}
bool music_select_library_track(uint8_t i,uint32_t g){if(i!=2||g!=42)return false;selected=i;return true;}
void music_playlist_open(lv_obj_t*p){list_opens++;list_return=p;lv_scr_load(list_screen);}
void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;edge=cb;}
#include "@ROOT@/src/OwnMusicUI.c"
@DRIVER@
static void key(const char*t){for(unsigned i=0;i<200;i++){const char*s=lv_btnmatrix_get_btn_text(music_page->keyboard,i);if(!s)break;if(!strcmp(s,t)){lv_btnmatrix_set_selected_btn(music_page->keyboard,i);lv_event_send(music_page->keyboard,LV_EVENT_VALUE_CHANGED,NULL);return;}}assert(!"missing key");}
static void geometry(void){lv_obj_update_layout(music_page->screen);lv_area_t field,panel,kb;lv_obj_get_coords(music_page->query,&field);lv_obj_get_coords(music_page->candidates,&panel);lv_obj_get_coords(music_page->keyboard,&kb);assert(field.y1==198&&field.y2<panel.y1&&panel.y2<kb.y1&&kb.y2==479);assert(lv_obj_get_parent(music_page->candidates)==music_page->screen);}
static void complete_search(void){preparing=false;result_count=3;for(unsigned i=0;i<3;i++){snprintf(results[i].id,sizeof(results[i].id),"id-%u",i);snprintf(results[i].title,sizeof(results[i].title),i==0?"茉莉花":"示例歌曲 %u",i+1);snprintf(results[i].artist,sizeof(results[i].artist),"示例歌手");}snprintf(reply,sizeof(reply),"找到三首歌曲，点击播放");result_revision++;advance(400);}
int main(int argc,char**argv){
@INIT@
 char path16[1024];snprintf(path16,sizeof(path16),"%s",argv[1]);char *suffix=strstr(path16,"chinese_20.bin");assert(suffix);strcpy(suffix,"chinese_16.bin");font_file=path16;chinese_font_16=lv_font_load("S:/small.bin");assert(chinese_font_16);font_file=argv[1];
 lv_obj_t *browser=lv_obj_create(NULL);list_screen=lv_obj_create(NULL);lv_scr_load(browser);
 own_music_ui_open(browser);advance(150);assert(music_page&&music_page->download&&lv_obj_has_state(music_page->download,LV_STATE_DISABLED));screenshot(argv[2],"music-home");
 tap(70,78);assert(music_page->editing);geometry();
 key("m");key("o");assert(!lv_obj_has_flag(music_page->candidates,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"music-pinyin");
 lv_btnmatrix_set_selected_btn(music_page->candidates,1);lv_event_send(music_page->candidates,LV_EVENT_VALUE_CHANGED,NULL);
 assert(!strcmp(lv_textarea_get_text(music_page->query),"茉"));screenshot(argv[2],"music-composer");
 lv_textarea_set_text(music_page->query,"茉莉花");tap(420,218);assert(commands==1&&!last_save&&!strcmp(last_command,"搜歌 茉莉花")&&!music_page->editing);
 complete_search();assert(music_page->count==3);screenshot(argv[2],"music-results");
 /* A different client can update the shared result list before our next poll. */
 strcpy(results[0].id,"replacement");lv_event_send(lv_obj_get_child(music_page->list,0),LV_EVENT_CLICKED,NULL);assert(commands==1&&strstr(music_page->reply,"已更新"));advance(400);
 tap(120,235);assert(commands==2&&!last_save&&!strcmp(last_command,"播放第1首")&&downloads==0);
 tap(290,410);assert(stops==1&&!preparing);tap(60,410);tap(175,410);assert(pauses==1&&plays==1);
 advance(400);tap(120,235);assert(commands==3&&!last_save&&downloads==0);
 preparing=false;saved_index=2;snprintf(reply,sizeof(reply),"WAV 已保存：/music/茉莉花 - 示例歌手.wav\nLRC 已保存：/music/茉莉花 - 示例歌手.lrc");result_revision++;advance(400);
 tap(410,410);assert(selected==2&&plays==2);tap(100,455);assert(music_page->details);screenshot(argv[2],"music-status");edge(NULL);assert(!music_page->details);
 wifi=false;tap(420,78);assert(commands==3&&strstr(music_page->reply,"WiFi"));wifi=true;
 sd=false;tap(120,235);assert(commands==3&&strstr(music_page->reply,"SD"));
 /* Search itself remains available without SD; cache/playback do not. */
 tap(420,78);assert(commands==4);preparing=false;sd=true;advance(400);
 tap(390,25);assert(list_opens==1&&lv_scr_act()==list_screen);lv_scr_load(list_return);advance(400);assert(lv_scr_act()==music_page->screen);
 playing=true;snprintf(shown_title,sizeof(shown_title),"%s","茉莉花-一个很长很长很长很长很长很长很长很长的歌名用来检查省略");can_download=true;advance(400);
 lv_obj_update_layout(music_page->screen);lv_area_t now,dl,phase,list,controls;lv_obj_get_coords(music_page->now,&now);lv_obj_get_coords(music_page->download,&dl);lv_obj_get_coords(music_page->phase,&phase);lv_obj_get_coords(music_page->list,&list);lv_obj_get_coords(music_page->controls[0],&controls);
 assert(now.x2<dl.x1&&now.y2<phase.y1&&dl.y2<phase.y1&&phase.y1==180&&list.y1==208&&list.y2<=controls.y1&&controls.y1==392&&dl.x2<480&&!lv_obj_has_state(music_page->download,LV_STATE_DISABLED));
 unsigned plays_before=plays,pauses_before=pauses;tap(398,128);assert(downloads==1&&plays==plays_before&&pauses==pauses_before&&preparing);
 preparing=false;advance(400);tap(70,78);assert(music_page->editing&&lv_obj_has_flag(music_page->download,LV_OBJ_FLAG_HIDDEN));edge(NULL);assert(!music_page->editing&&!lv_obj_has_flag(music_page->download,LV_OBJ_FLAG_HIDDEN));
 tap(70,78);assert(music_page->editing);edge(NULL);assert(!music_page->editing);preparing=true;unsigned stopped_before=stops;edge(NULL);advance(100);assert(!music_page&&stops==stopped_before&&lv_scr_act()==browser);
 preparing=false;own_music_ui_open(browser);advance(100);edge(NULL);advance(100);lv_mem_monitor_t baseline,after;lv_mem_monitor(&baseline);
 for(unsigned i=0;i<12;i++){own_music_ui_open(browser);advance(80);edge(NULL);advance(100);assert(!music_page&&lv_scr_act()==browser);assert(lv_mem_test()==LV_RES_OK);}lv_mem_monitor(&after);assert(after.free_size+128>=baseline.free_size);
 dict_ready=false;own_music_ui_open(browser);advance(100);assert(!music_page->ime&&strstr(music_page->reply,"词库"));edge(NULL);advance(100);
 puts("own music UI: Chinese IME/geometry/search/save selection/controls/stale results/offline SD/list/status/exit and repeated cleanup PASS");return 0;
}
'''
if __name__=='__main__':main()
