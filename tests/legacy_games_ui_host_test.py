"""Existing 2048 and gyro-balance UI keep working behind the game hub."""
from pathlib import Path
import subprocess
from PIL import Image
from desktop_ui_host_test import function
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/game-hub-20261115/host-legacy'

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)');(OUT/'lv_conf.h').write_text(config)
 custom=(ROOT/'src/ui/custom/custom.c').read_text(encoding='utf-8');a=custom.index('/* ==================== 2048 GAME');b=custom.index('/* Hand-drawn gear icon',a)
 driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')];init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
 source=HEADER+custom[a:b]+'\n'+function(custom,'game_restart_cb')+'\n'+function(custom,'game_btn_cb')+'\n'+function(custom,'game_back_cb')+'\n'+driver+TAIL.replace('@INIT@',init)
 unit=OUT/'test.c';unit.write_text(source,encoding='utf-8');exe=OUT/'legacy.exe';cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
 r=subprocess.run(cmd,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace');subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=45)
 for f in OUT.glob('*.rgb565'):
  raw=f.read_bytes();rgb=bytearray()
  for i in range(0,len(raw),2):
   c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
  Image.frombytes('RGB',(480,480),bytes(rgb)).save(f.with_suffix('.png'))
HEADER=r'''
#include "lvgl.h"
#include "UITheme.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static float host_abs(float v){return v<0?-v:v;}
#define fabsf host_abs
static float host_sign(float v,float s){return s<0?-host_abs(v):host_abs(v);}
#define copysignf host_sign
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 480
static lv_color_t pixels[480*480];static lv_font_t *chinese_font_20;
static lv_obj_t *main_screen_ref,*hub;
static float gyro_pending_roll,gyro_pending_pitch;
static lv_obj_t *games_ui_get_screen(void){return hub;}
static const lv_font_t*ui_chinese_font_20(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static const lv_font_t*ui_chinese_font_16(const lv_font_t*f){return chinese_font_20?chinese_font_20:f;}
static lv_obj_t*create_back_button(lv_obj_t*p,lv_align_t a,lv_coord_t x,lv_event_cb_t cb){(void)a;(void)x;edge_back_bind(lv_obj_get_screen(p),cb,NULL);return NULL;}
static lv_obj_t*create_legacy_back_button(lv_obj_t*p,lv_align_t a,lv_coord_t x,lv_event_cb_t cb){return create_back_button(p,a,x,cb);}
static void game_restart_cb(lv_event_t*e);
static void game_back_cb(lv_event_t*e);
static void create_balance_game_screen(void);
static void balance_game_reset(bool reset);
static void balance_game_update_timer_cb(lv_timer_t*t);
'''
TAIL=r'''
static void edge_return(void){edge_back_pointer(true,1,220,true);edge_back_pointer(true,88,220,true);edge_back_pointer(false,88,220,true);advance(80);}
int main(int argc,char**argv){
@INIT@
 main_screen_ref=lv_obj_create(NULL);hub=lv_obj_create(NULL);lv_scr_load(hub);edge_back_init();assert(!game_screen&&!balance_screen&&!balance_game_timer);
 game_btn_cb(NULL);advance(80);assert(lv_scr_act()==game_screen);memset(game_grid,0,sizeof(game_grid));game_grid[0][0]=2;game_grid[0][1]=2;game_grid[0][2]=4;game_score=0;game_update_tiles();touch(true,340,250,80);touch(true,120,250,100);touch(false,120,250,100);assert(game_score==4&&game_grid[0][0]==4&&game_grid[0][1]==4);screenshot(argv[2],"legacy-2048");edge_return();assert(lv_scr_act()==hub);
 balance_game_btn_cb(NULL);advance(50);assert(lv_scr_act()==balance_screen&&balance_game_active&&balance_game_timer);float x=balance_ball_x,y=balance_ball_y;gyro_pending_pitch+=15;gyro_pending_roll+=10;advance(300);assert(balance_ball_x!=x||balance_ball_y!=y);screenshot(argv[2],"legacy-balance");edge_return();assert(lv_scr_act()==hub&&!balance_game_active&&balance_game_timer->paused);x=balance_ball_x;y=balance_ball_y;advance(600);assert(balance_ball_x==x&&balance_ball_y==y);
 balance_game_btn_cb(NULL);advance(50);assert(balance_game_active&&!balance_score);lv_scr_load(main_screen_ref);advance(500);assert(!balance_game_active&&balance_game_timer->paused);lv_scr_load(balance_screen);advance(50);assert(balance_game_active&&!balance_game_timer->paused);edge_return();lv_obj_del(balance_screen);assert(!balance_screen&&!balance_game_timer);lv_obj_del(game_screen);assert(!game_screen);
 game_btn_cb(NULL);advance(50);edge_return();balance_game_btn_cb(NULL);advance(50);edge_return();assert(lv_scr_act()==hub);lv_obj_del(game_screen);lv_obj_del(balance_screen);assert(lv_mem_test()==LV_RES_OK);
 puts("Legacy games PASS: lazy creation, real 2048 swipe/single merge, gyro movement, real edge return to hub, hidden pause and clean recreate");return 0;
}
'''
if __name__=='__main__':main()
