"""Game hub and new games in real LVGL with real edge-back, SD font and pointer input.

Legacy 2048/balance entry callbacks are boundary fakes here; game rules are tested
separately. No simulated data is presented as device screenshots or hardware FPS.
"""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS as DRIVER
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/tile-scatter-20261116/game-host'

def main():
    OUT.mkdir(parents=True,exist_ok=True)
    config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)');(OUT/'lv_conf.h').write_text(config)
    driver=DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')]
    init=DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    unit=OUT/'test.c';unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()).replace('@DRIVER@',driver).replace('@INIT@',init),encoding='utf-8');exe=OUT/'games.exe'
    command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    r=subprocess.run(command,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace');subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))

SOURCE=r'''
#include "lvgl.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static lv_color_t pixels[480*480];static lv_font_t *chinese_font_20;
static bool fail_timer;
static lv_timer_t *timer_create(lv_timer_cb_t cb,uint32_t ms,void*data){if(fail_timer){fail_timer=false;return NULL;}return lv_timer_create(cb,ms,data);}
#include "@ROOT@/src/GameHub.c"
#define lv_timer_create timer_create
#include "@ROOT@/src/MiniGames.c"
#undef lv_timer_create
@DRIVER@
static lv_obj_t*legacy[2];static unsigned opened[2];
static void old_2048(lv_event_t*e){(void)e;opened[0]++;lv_scr_load(legacy[0]);}
static void old_balance(lv_event_t*e){(void)e;opened[1]++;lv_scr_load(legacy[1]);}
static void tap_obj(lv_obj_t*o){lv_obj_update_layout(o);lv_area_t a;lv_obj_get_coords(o,&a);tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}
static void long_obj(lv_obj_t*o){lv_obj_update_layout(o);lv_area_t a;lv_obj_get_coords(o,&a);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;touch(true,x,y,650);touch(false,x,y,100);}
static lv_obj_t*named(lv_obj_t*p,const char*name){for(unsigned i=0;i<lv_obj_get_child_cnt(p);i++){lv_obj_t*c=lv_obj_get_child(p,i);if(lv_obj_check_type(c,&lv_label_class)&&!strcmp(lv_label_get_text(c),name))return lv_obj_get_parent(c);lv_obj_t*r=named(c,name);if(r)return r;}return NULL;}
static void edge_return(void){assert(edge_back_pointer(true,1,220,true));assert(edge_back_pointer(true,88,220,true));assert(edge_back_pointer(false,88,220,true));advance(100);}
static void glyphs(lv_obj_t*p){for(unsigned i=0;i<lv_obj_get_child_cnt(p);i++)glyphs(lv_obj_get_child(p,i));if(!lv_obj_check_type(p,&lv_label_class))return;const char*t=lv_label_get_text(p);uint32_t i=0;while(t[i]){uint32_t c=_lv_txt_encoded_next(t,&i);if(c<128)continue;lv_font_glyph_dsc_t g;const lv_font_t*f=lv_obj_get_style_text_font(p,0);if(!lv_font_get_glyph_dsc(f,&g,c,0)){printf("missing glyph U+%04X in %s\\n",(unsigned)c,t);assert(0);}}}
static unsigned cell_count(mini_page_t*p){return p->kind==MINI_GAME_MINESWEEPER?64:p->kind==MINI_GAME_GOMOKU?121:p->kind==MINI_GAME_MEMORY?16:100;}
static void bounds(mini_page_t*p){lv_obj_update_layout(p->screen);unsigned n=cell_count(p);for(unsigned i=0;i<n;i++){lv_area_t a;lv_obj_get_coords(p->cells[i],&a);assert(a.x1>=16&&a.x2<464&&a.y1>=100&&a.y2<476);}glyphs(p->screen);}
int main(int argc,char**argv){
@INIT@
 lv_obj_t*home=lv_obj_create(NULL);lv_scr_load(home);legacy[0]=lv_obj_create(NULL);legacy[1]=lv_obj_create(NULL);edge_back_init();
 games_ui_open(home,chinese_font_20,true,old_2048,old_balance);advance(80);lv_obj_t*hub=games_ui_get_screen();assert(hub&&lv_scr_act()==hub);glyphs(hub);screenshot(argv[2],"games-home");
 assert(named(hub,"五子棋")&&named(hub,"叠牌三消")&&!named(hub,"井字棋"));
 tap_obj(named(hub,"2048"));assert(opened[0]==1&&lv_scr_act()==legacy[0]);ui_screen_back(hub);tap_obj(named(hub,"平衡球"));assert(opened[1]==1&&lv_scr_act()==legacy[1]);ui_screen_back(hub);
 tap_obj(named(hub,"扫雷"));mini_page_t*p=active_page;assert(p&&p->kind==MINI_GAME_MINESWEEPER);bounds(p);advance(1500);assert(p->seconds==0&&!p->placed);long_obj(p->cells[0]);assert(p->flags==1&&!p->open[0]&&!p->placed);long_obj(p->cells[0]);assert(!p->flags);tap_obj(p->cells[27]);assert(p->placed&&!p->over&&!p->mine[27]);unsigned mines=0;for(unsigned i=0;i<64;i++)mines+=p->mine[i];assert(mines==10);for(int y=2;y<=4;y++)for(int x=2;x<=4;x++)assert(!p->mine[y*8+x]);screenshot(argv[2],"mines-playing");advance(1500);assert(p->seconds>=1);uint32_t secs=p->seconds;lv_scr_load(hub);advance(3000);assert(p->seconds==secs);lv_scr_load(p->screen);advance(1200);assert(p->seconds>=secs+1);
 int mine=-1;for(int i=0;i<64;i++)if(p->mine[i]){mine=i;break;}tap_obj(p->cells[mine]);assert(p->over&&!p->won);secs=p->seconds;advance(2000);assert(p->seconds==secs);screenshot(argv[2],"mines-over");tap_obj(p->restart);assert(!p->placed&&!p->over&&!p->seconds);tap_obj(p->cells[0]);for(int i=0;i<64;i++)if(!p->mine[i])dig_cell(p,i);assert(p->won&&p->over);edge_return();assert(!active_page&&lv_scr_act()==hub);
 for(int seed=0;seed<8;seed++)for(int safe=0;safe<64;safe++){mini_page_t q={0};place_mines(&q,safe);unsigned count=0;for(int i=0;i<64;i++){count+=q.mine[i];if(abs(i%8-safe%8)<=1&&abs(i/8-safe/8)<=1)assert(!q.mine[i]);if(!q.mine[i]){unsigned n=0;for(int j=0;j<64;j++)if(abs(i%8-j%8)<=1&&abs(i/8-j/8)<=1)n+=q.mine[j];assert(q.adj[i]==n);}}assert(count==10);}
 tap_obj(named(hub,"五子棋"));p=active_page;bounds(p);tap_obj(p->cells[0]);assert(p->board[0]==1&&p->ai_pending);tap_obj(p->cells[1]);assert(p->board[1]==0);advance(450);int white=0;for(int i=0;i<121;i++)white+=p->board[i]==2;assert(white==1&&!p->ai_pending);screenshot(argv[2],"gomoku");tap_obj(p->cells[0]);assert(p->board[0]==1);tap_obj(p->cells[8]);assert(p->ai_pending);tap_obj(p->restart);advance(500);for(int i=0;i<121;i++)assert(!p->board[i]);fail_timer=true;tap_obj(p->cells[60]);assert(!p->ai_pending&&p->board[60]==1);int whites=0;for(int i=0;i<121;i++)whites+=p->board[i]==2;assert(whites==1);
 {int8_t b[121]={0};b[0]=b[1]=b[2]=b[3]=2;assert(gomoku_choose(b)==4);b[0]=b[1]=b[2]=b[3]=1;assert(gomoku_choose(b)==4);memset(b,0,sizeof(b));b[0]=b[11]=b[22]=b[33]=2;assert(gomoku_choose(b)==44);memset(b,0,sizeof(b));b[4]=b[14]=b[24]=b[34]=2;assert(gomoku_choose(b)==44);memset(b,0,sizeof(b));b[0]=b[1]=b[2]=b[3]=b[4]=1;assert(gomoku_winner(b)==1);memset(b,0,sizeof(b));b[10]=b[11]=b[12]=b[13]=b[14]=1;assert(gomoku_winner(b)==0);}edge_return();assert(!active_page&&lv_scr_act()==hub);
 tap_obj(named(hub,"记忆翻牌"));p=active_page;bounds(p);unsigned hist[8]={0};for(int i=0;i<16;i++)hist[p->deck[i]]++;for(int i=0;i<8;i++)assert(hist[i]==2);int other=1;while(p->deck[other]==p->deck[0])other++;tap_obj(p->cells[0]);tap_obj(p->cells[0]);assert(p->turns==0&&p->first==0);tap_obj(p->cells[other]);assert(p->lock&&p->turns==1);int third=1;while(third==other)third++;tap_obj(p->cells[third]);assert(!p->face[third]);screenshot(argv[2],"memory-playing");tap_obj(p->restart);advance(750);assert(!p->lock&&!p->turns);other=1;while(p->deck[other]==p->deck[0])other++;tap_obj(p->cells[0]);fail_timer=true;tap_obj(p->cells[other]);assert(!p->lock&&p->first==-1);
 for(int val=0;val<8;val++){int a=-1,b=-1;for(int i=0;i<16;i++)if(p->deck[i]==val){if(a<0)a=i;else b=i;}tap_obj(p->cells[a]);tap_obj(p->cells[b]);}assert(p->pairs==8&&p->over);screenshot(argv[2],"memory-win");edge_return();assert(!active_page&&lv_scr_act()==hub);
 tap_obj(named(hub,"叠牌三消"));p=active_page;assert(p->kind==MINI_GAME_TILE_MATCH);bounds(p);screenshot(argv[2],"tile-match-100");
 {int kinds[6]={0},reward=0,overlaps=0,free_n=0,minx=999,maxx=0,miny=999,maxy=0;int16_t sx[100],sy[100];
  for(int i=0;i<100;i++){assert(lv_obj_get_width(p->cells[i])==56&&lv_obj_get_height(p->cells[i])==52);assert(p->tile_layer[i]==(uint8_t)i);assert(p->tile_x[i]>=16&&p->tile_x[i]<=408&&p->tile_y[i]>=118&&p->tile_y[i]<=352);sx[i]=p->tile_x[i];sy[i]=p->tile_y[i];if(p->tile_x[i]<minx)minx=p->tile_x[i];if(p->tile_x[i]>maxx)maxx=p->tile_x[i];if(p->tile_y[i]<miny)miny=p->tile_y[i];if(p->tile_y[i]>maxy)maxy=p->tile_y[i];if(tile_is_free(p,i))free_n++;if(p->tile_kind[i]==TILE_REWARD){reward++;assert(!strcmp(lv_label_get_text(lv_obj_get_child(p->cells[i],0)),"奖"));}else{assert(p->tile_kind[i]<6);kinds[p->tile_kind[i]]++;}}
  for(int i=0;i<100;i++)for(int j=i+1;j<100;j++){assert(p->tile_x[i]!=p->tile_x[j]||p->tile_y[i]!=p->tile_y[j]);if(abs(p->tile_x[i]-p->tile_x[j])<56&&abs(p->tile_y[i]-p->tile_y[j])<52)overlaps++;}
  assert(reward==1&&free_n>=8&&overlaps>=60&&maxx-minx>=280&&maxy-miny>=140);for(int k=0;k<6;k++)assert(kinds[k]%3==0&&(kinds[k]==15||kinds[k]==18));
  int paint[100],np=0;for(unsigned c=0;c<lv_obj_get_child_cnt(p->screen);c++){lv_obj_t*o=lv_obj_get_child(p->screen,c);for(int i=0;i<100;i++)if(o==p->cells[i]&&p->tile_alive[i])paint[np++]=i;}for(int i=1;i<np;i++)assert(paint[i]>paint[i-1]);
  for(int step=0;step<18;step++){int idx=p->tile_order[step];assert(tile_is_free(p,idx));tap_obj(p->cells[idx]);}assert(p->tiles_left==82);screenshot(argv[2],"tile-match");
  tap_obj(p->restart);assert(p->tiles_left==100&&!p->slot_n);int moved=0;for(int i=0;i<100;i++)if(p->tile_x[i]!=sx[i]||p->tile_y[i]!=sy[i])moved=1;assert(moved);
  int covered=-1;for(int i=0;i<100;i++)if(!tile_is_free(p,i)){covered=i;break;}assert(covered>=0);tap_obj(p->cells[covered]);assert(p->tile_alive[covered]&&!p->slot_n);
  mini_page_t probe=*p;for(int i=0;i<100;i++)probe.tile_alive[i]=0;probe.tile_alive[0]=probe.tile_alive[1]=1;probe.tile_layer[0]=0;probe.tile_layer[1]=1;probe.tile_x[0]=80;probe.tile_y[0]=140;probe.tile_x[1]=135;probe.tile_y[1]=140;assert(!tile_is_free(&probe,0));probe.tile_x[1]=136;assert(tile_is_free(&probe,0));probe.tile_x[1]=80;probe.tile_y[1]=191;assert(!tile_is_free(&probe,0));probe.tile_y[1]=192;assert(tile_is_free(&probe,0));
  int reward_i=-1;for(int i=0;i<100;i++)if(p->tile_kind[i]==TILE_REWARD)reward_i=i;
  for(int i=0;i<100;i++){if(i==reward_i)continue;int dx=abs(p->tile_x[i]-p->tile_x[reward_i]),dy=abs(p->tile_y[i]-p->tile_y[reward_i]);if(p->tile_layer[i]>p->tile_layer[reward_i]&&dx<56&&dy<52)p->tile_alive[i]=0;}
  int picked=0;for(int i=0;i<100&&picked<6;i++){if(i==reward_i||!p->tile_alive[i])continue;p->tile_alive[i]=0;p->slot[picked++]=(int8_t)i;}
  p->slot_n=6;p->over=false;p->won=false;p->tiles_left=0;for(int i=0;i<100;i++)if(p->tile_alive[i])p->tiles_left++;refresh_tiles(p);uint8_t left=p->tiles_left;tap_obj(p->cells[reward_i]);assert(!p->tile_alive[reward_i]&&p->slot_n==6&&!p->over&&!p->won&&p->tiles_left==left-1);
 }
 tap_obj(p->restart);assert(!p->over&&p->tiles_left==100&&p->slot_n==0);
 for(int pass=0;pass<3;pass++){if(pass){tap_obj(p->restart);assert(p->tiles_left==100);}int normal=0;for(int i=0;i<100;i++){int idx=p->tile_order[i];assert(tile_is_free(p,idx));uint8_t before=p->slot_n;tap_obj(p->cells[idx]);if(p->tile_kind[idx]==TILE_REWARD)assert(p->slot_n==before);else{normal++;if(normal%3==0)assert(p->slot_n==0);}}assert(normal==99&&p->won&&p->over&&!p->slot_n);}
 tap_obj(p->restart);
 {static const uint8_t kinds[7]={0,1,2,3,4,5,0};for(int i=0;i<100;i++)p->tile_alive[i]=0;for(int i=0;i<7;i++){p->tile_alive[i]=1;p->tile_kind[i]=kinds[i];p->tile_layer[i]=(uint8_t)i;p->tile_x[i]=(int16_t)(16+i*56);p->tile_y[i]=160;}p->slot_n=0;p->over=false;p->won=false;p->tiles_left=7;for(int i=0;i<7;i++)p->slot[i]=-1;refresh_tiles(p);for(int i=0;i<7;i++){assert(tile_is_free(p,i));tap_obj(p->cells[i]);}assert(p->over&&!p->won&&p->slot_n==7);}edge_return();assert(!active_page&&lv_scr_act()==hub);
 lv_mem_monitor_t before,after;lv_mem_monitor(&before);for(unsigned i=0;i<45;i++){mini_games_open(hub,(mini_game_kind_t)(i%4),chinese_font_20);advance(50);p=active_page;if(p->kind==MINI_GAME_GOMOKU)tap_obj(p->cells[0]);else if(p->kind==MINI_GAME_MEMORY){int j=1;while(p->deck[j]==p->deck[0])j++;tap_obj(p->cells[0]);tap_obj(p->cells[j]);}else if(p->kind==MINI_GAME_TILE_MATCH){for(int t=0;t<100;t++)if(tile_is_free(p,t)){tap_obj(p->cells[t]);break;}}edge_return();assert(!active_page&&lv_scr_act()==hub&&lv_mem_test()==LV_RES_OK);}lv_mem_monitor(&after);assert(after.free_size+256>=before.free_size);
 edge_return();assert(lv_scr_act()==home);lv_obj_del(hub);assert(!games_ui_get_screen());games_ui_open(home,chinese_font_20,true,old_2048,old_balance);advance(80);edge_return();assert(lv_scr_act()==home);lv_obj_del(games_ui_get_screen());
 puts("Games UI PASS: six cards/legacy callbacks, gomoku/tile-match, real edge back, Chinese glyphs/bounds, mines/memory, 45 timed cleanup cycles");return 0;
}
'''
if __name__=='__main__':main()
