"""Mini-games on real LVGL. Screen transition and edge-back are boundary fakes."""
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools' / 'speech-games-20261116' / 'mini-games-host'

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include' / 'lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    unit = OUT / 'test.c'
    unit.write_text(HARNESS.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
    exe = OUT / 'mini-games.exe'
    command = [str(ROOT / '.tools' / 'host-tcc' / 'tcc' / 'tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I' + str(OUT), '-I' + str(ROOT / 'components' / 'lvgl'), '-I' + str(ROOT / 'components'),
               '-I' + str(ROOT / 'src'), str(unit),
               *[str(p) for p in (ROOT / 'components' / 'lvgl' / 'src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace'))
        raise SystemExit(result.returncode)
    subprocess.run([str(exe)], check=True, timeout=60)

HARNESS = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
static lv_event_cb_t edge;
static bool down; static int px, py;
void edge_back_bind(lv_obj_t *screen, lv_event_cb_t callback, void *user){(void)screen;(void)user; edge=callback;}
void ui_screen_open(lv_obj_t *screen){lv_scr_load(screen);}
void ui_screen_back(lv_obj_t *screen){lv_scr_load(screen);}
#include "@ROOT@/src/MiniGames.c"
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *c){(void)a;(void)c; lv_disp_flush_ready(d);}
static void input(lv_indev_drv_t *d, lv_indev_data_t *p){(void)d; p->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL; p->point=(lv_point_t){px,py};}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5); lv_timer_handler();}}
static const char *cap(lv_obj_t *button){return lv_label_get_text(lv_obj_get_child(button,0));}
static int collect(lv_obj_t **out, int max, lv_coord_t min_w, lv_coord_t max_w){
    lv_obj_update_layout(lv_scr_act());
    uint32_t n=lv_obj_get_child_cnt(lv_scr_act()); int k=0;
    for(uint32_t i=0;i<n;i++){
        lv_obj_t *child=lv_obj_get_child(lv_scr_act(), i);
        if(!lv_obj_check_type(child, &lv_btn_class)) continue;
        lv_coord_t w=lv_obj_get_width(child);
        if(w<min_w || w>max_w) continue;
        lv_area_t area; lv_obj_get_coords(child, &area);
        assert(area.x1>=16 && area.x2<=464 && area.y2<=476);
        if(k<max) out[k++]=child;
    }
    return k;
}
static int changed_from(lv_obj_t **items, int n, const char *text){int c=0; for(int i=0;i<n;i++) if(strcmp(cap(items[i]), text)) c++; return c;}
static lv_obj_t *find_btn(const char *text){
    uint32_t n=lv_obj_get_child_cnt(lv_scr_act());
    for(uint32_t i=0;i<n;i++){
        lv_obj_t *child=lv_obj_get_child(lv_scr_act(), i);
        if(lv_obj_check_type(child, &lv_btn_class) && !strcmp(cap(child), text)) return child;
    }
    return NULL;
}
static lv_obj_t *status_of(void){
    uint32_t n=lv_obj_get_child_cnt(lv_scr_act());
    for(uint32_t i=0;i<n;i++){
        lv_obj_t *child=lv_obj_get_child(lv_scr_act(), i);
        if(!lv_obj_check_type(child, &lv_label_class)) continue;
        const char *t=lv_label_get_text(child);
        if(strstr(t, "秒") || strstr(t, "回合") || strstr(t, "轮到") || strstr(t, "赢") || strstr(t, "思考") || strstr(t, "等待") || strstr(t, "剩余") || strstr(t, "槽")) return child;
    }
    return NULL;
}
int main(void){
    lv_init();
    lv_disp_draw_buf_t buf; lv_disp_draw_buf_init(&buf, pixels, NULL, 480*480);
    lv_disp_drv_t display; lv_disp_drv_init(&display); display.hor_res=480; display.ver_res=480; display.draw_buf=&buf; display.full_refresh=1; display.flush_cb=flush; lv_disp_drv_register(&display);
    lv_indev_drv_t pointer; lv_indev_drv_init(&pointer); pointer.type=LV_INDEV_TYPE_POINTER; pointer.read_cb=input; lv_indev_drv_register(&pointer);
    lv_obj_t *home=lv_obj_create(NULL); lv_scr_load(home);
    mini_games_open(home, MINI_GAME_MINESWEEPER, &lv_font_montserrat_20); advance(50);
    lv_obj_t *board=lv_scr_act(); lv_obj_t *cells[64];
    assert(collect(cells, 64, 30, 48)==64);
    assert(changed_from(cells, 64, "?")==0);
    lv_event_send(cells[0], LV_EVENT_SHORT_CLICKED, NULL);
    assert(changed_from(cells, 64, "?")>0);
    for(int i=0;i<64;i++) assert(strcmp(cap(cells[i]), "*"));
    int closed=-1; for(int i=0;i<64;i++) if(!strcmp(cap(cells[i]), "?")) {closed=i; break;}
    assert(closed>=0);
    lv_event_send(cells[closed], LV_EVENT_LONG_PRESSED, NULL);
    assert(!strcmp(cap(cells[closed]), "F"));
    lv_event_send(cells[closed], LV_EVENT_SHORT_CLICKED, NULL);
    assert(!strcmp(cap(cells[closed]), "F"));
    lv_obj_t *mode=find_btn("标记"); assert(mode);
    lv_event_send(mode, LV_EVENT_SHORT_CLICKED, NULL);
    int other=-1; for(int i=0;i<64;i++) if(!strcmp(cap(cells[i]), "?")) {other=i; break;}
    assert(other>=0);
    lv_event_send(cells[other], LV_EVENT_SHORT_CLICKED, NULL);
    assert(!strcmp(cap(cells[other]), "F"));
    advance(1100);
    lv_obj_t *status=status_of(); assert(status && strstr(lv_label_get_text(status), "1秒"));
    lv_scr_load(home); advance(2000);
    lv_scr_load(board); advance(50);
    assert(strstr(lv_label_get_text(status_of()), "1秒"));
    advance(1100);
    assert(strstr(lv_label_get_text(status_of()), "2秒"));
    lv_obj_t *restart=find_btn("重开"); assert(restart);
    lv_event_send(restart, LV_EVENT_SHORT_CLICKED, NULL);
    assert(changed_from(cells, 64, "?")==0);
    assert(strstr(lv_label_get_text(status_of()), "等待首击"));
    edge(NULL); advance(30); assert(lv_scr_act()==home);

    mini_games_open(home, MINI_GAME_GOMOKU, &lv_font_montserrat_20); advance(50);
    lv_obj_t *grid[121]; assert(collect(grid, 121, 26, 36)==121);
    lv_event_send(grid[0], LV_EVENT_SHORT_CLICKED, NULL);
    assert(active_page->board[0]==1 && active_page->ai_pending);
    lv_event_send(grid[0], LV_EVENT_SHORT_CLICKED, NULL);
    assert(active_page->board[0]==1);
    lv_event_send(grid[1], LV_EVENT_SHORT_CLICKED, NULL);
    assert(active_page->board[1]==0);
    advance(500);
    assert(active_page->board[0]==1);
    int ai_count=0; for(int i=0;i<121;i++) if(active_page->board[i]==2) ai_count++;
    assert(ai_count==1);
    assert(strstr(lv_label_get_text(status_of()), "轮到你"));
    lv_event_send(find_btn("重开"), LV_EVENT_SHORT_CLICKED, NULL); advance(500);
    for(int i=0;i<121;i++) assert(!active_page->board[i]);
    lv_event_send(grid[60], LV_EVENT_SHORT_CLICKED, NULL);
    edge(NULL); advance(500); assert(lv_scr_act()==home && !active_page);

    {
        int8_t b[121]={0};
        b[0]=b[1]=b[2]=b[3]=2; assert(gomoku_choose(b)==4);
        b[0]=b[1]=b[2]=b[3]=1; assert(gomoku_choose(b)==4);
        memset(b,0,sizeof(b)); b[0]=b[11]=b[22]=b[33]=2; assert(gomoku_choose(b)==44);
        memset(b,0,sizeof(b)); b[0]=b[12]=b[24]=b[36]=1; assert(gomoku_choose(b)==48);
        memset(b,0,sizeof(b)); b[4]=b[14]=b[24]=b[34]=2; assert(gomoku_choose(b)==44);
        memset(b,0,sizeof(b)); b[0]=b[1]=b[2]=b[3]=b[4]=1; assert(gomoku_winner(b)==1);
        memset(b,0,sizeof(b)); b[10]=b[21]=b[32]=b[43]=b[54]=2; assert(gomoku_winner(b)==2);
        memset(b,0,sizeof(b)); b[10]=b[11]=b[12]=b[13]=b[14]=1; assert(gomoku_winner(b)==0);
        memset(b,0,sizeof(b)); b[6]=b[7]=b[8]=b[9]=b[10]=1; assert(gomoku_winner(b)==1);
    }

    mini_games_open(home, MINI_GAME_MEMORY, &lv_font_montserrat_20); advance(50);
    lv_obj_t *cards[16]; assert(collect(cards, 16, 60, 80)==16);
    lv_event_send(cards[0], LV_EVENT_SHORT_CLICKED, NULL);
    lv_event_send(cards[0], LV_EVENT_SHORT_CLICKED, NULL);
    assert(strcmp(cap(cards[0]), "?") && changed_from(cards, 16, "?")==1);
    int shown=0, second=1;
    lv_event_send(cards[second], LV_EVENT_SHORT_CLICKED, NULL);
    if(!strcmp(cap(cards[shown]), cap(cards[second]))) { shown=2; second=3; lv_event_send(cards[shown], LV_EVENT_SHORT_CLICKED, NULL); lv_event_send(cards[second], LV_EVENT_SHORT_CLICKED, NULL); }
    assert(strcmp(cap(cards[shown]), cap(cards[second])));
    int blocked=shown==0?2:0;
    lv_event_send(cards[blocked], LV_EVENT_SHORT_CLICKED, NULL);
    assert(!strcmp(cap(cards[blocked]), "?"));
    advance(700);
    assert(!strcmp(cap(cards[shown]), "?") && !strcmp(cap(cards[second]), "?"));
    lv_event_send(cards[0], LV_EVENT_SHORT_CLICKED, NULL);
    int pair=-1; char face[8];snprintf(face,sizeof(face),"%s",cap(cards[0]));
    for(int i=1;i<16;i++){
        lv_event_send(cards[i], LV_EVENT_SHORT_CLICKED, NULL);
        if(!strcmp(cap(cards[i]), face)) {pair=i; break;}
        advance(700);
        assert(!strcmp(cap(cards[0]), "?") && !strcmp(cap(cards[i]), "?"));
        lv_event_send(cards[0], LV_EVENT_SHORT_CLICKED, NULL);
    }
    assert(pair>0 && !strcmp(cap(cards[0]), face) && !strcmp(cap(cards[pair]), face));
    edge(NULL); advance(800); assert(lv_scr_act()==home);

    mini_games_open(home, MINI_GAME_TILE_MATCH, &lv_font_montserrat_20); advance(50);
    lv_obj_t *tiles[100]; assert(collect(tiles, 100, 50, 58)==100);
    {
        int hist[6]={0}, reward=0, normals=0;
        for(int i=0;i<100;i++){
            assert(lv_obj_get_width(active_page->cells[i])==56 && lv_obj_get_height(active_page->cells[i])==52);
            if(active_page->tile_kind[i]==TILE_REWARD){reward++;continue;}
            assert(active_page->tile_kind[i]<6); hist[active_page->tile_kind[i]]++; normals++;
        }
        assert(reward==1 && normals==99);
        int n15=0,n18=0;
        for(int k=0;k<6;k++){assert(hist[k]%3==0); if(hist[k]==15)n15++; else if(hist[k]==18)n18++;}
        assert(n15==3 && n18==3);
    }
    /* Distance cases the layout never produces, so the oracle is not a copy of the deal. */
    {
        mini_page_t probe=*active_page;
        for(int i=0;i<100;i++) probe.tile_alive[i]=0;
        probe.tile_alive[0]=probe.tile_alive[1]=1;
        probe.tile_layer[0]=0; probe.tile_layer[1]=1;
        probe.tile_x[0]=100; probe.tile_y[0]=100;
        probe.tile_x[1]=155; probe.tile_y[1]=100; assert(!tile_is_free(&probe,0));
        probe.tile_x[1]=156; assert(tile_is_free(&probe,0));
        probe.tile_x[1]=100; probe.tile_y[1]=151; assert(!tile_is_free(&probe,0));
        probe.tile_y[1]=152; assert(tile_is_free(&probe,0));
        probe.tile_layer[1]=0; probe.tile_y[1]=100; assert(tile_is_free(&probe,0));
        probe.tile_layer[1]=1; probe.tile_kind[1]=TILE_REWARD; assert(tile_is_free(&probe,1));
        probe.tile_x[1]=100; probe.tile_y[1]=100; assert(!tile_is_free(&probe,0));
    }
    int16_t seen_x[100]={0}, seen_y[100]={0};
    for(int trial=0;trial<12;trial++){
        mini_page_t q={0};deal_tiles(&q);int counts[6]={0},seen[100]={0},reward=0,normal_run=0,prev=-1;
        int overlaps=0, free_n=0, minx=999, maxx=0, miny=999, maxy=0, uniq_x=0, uniq_y=0;
        int16_t xs[100], ys[100];
        for(int i=0;i<100;i++){
            if(q.tile_kind[i]==TILE_REWARD) reward++;
            else counts[q.tile_kind[i]]++;
            assert(q.tile_layer[i]==i);
            assert(q.tile_x[i]>=16 && q.tile_x[i]<=408 && q.tile_x[i]+56<=464);
            assert(q.tile_y[i]>=118 && q.tile_y[i]<=352 && q.tile_y[i]+52<418);
            if(q.tile_x[i]<minx)minx=q.tile_x[i]; if(q.tile_x[i]>maxx)maxx=q.tile_x[i];
            if(q.tile_y[i]<miny)miny=q.tile_y[i]; if(q.tile_y[i]>maxy)maxy=q.tile_y[i];
            xs[i]=q.tile_x[i]; ys[i]=q.tile_y[i];
            if(tile_is_free(&q,i)) free_n++;
        }
        for(int i=0;i<100;i++){
            int nx=1,ny=1;
            for(int j=0;j<i;j++){if(xs[j]==xs[i])nx=0; if(ys[j]==ys[i])ny=0;}
            uniq_x+=nx; uniq_y+=ny;
            for(int j=i+1;j<100;j++){
                assert(q.tile_x[i]!=q.tile_x[j] || q.tile_y[i]!=q.tile_y[j]);
                if(q.tile_x[i]<q.tile_x[j]+56 && q.tile_x[i]+56>q.tile_x[j] && q.tile_y[i]<q.tile_y[j]+52 && q.tile_y[i]+52>q.tile_y[j]) overlaps++;
            }
        }
        assert(reward==1);
        assert(free_n>=8 && overlaps>=60 && maxx-minx>=280 && maxy-miny>=140 && uniq_x>=25 && uniq_y>=20);
        if(trial){
            int differ=0;
            for(int i=0;i<100;i++) if(xs[i]!=seen_x[i] || ys[i]!=seen_y[i]) differ=1;
            assert(differ);
        }
        for(int i=0;i<100;i++){seen_x[i]=xs[i]; seen_y[i]=ys[i];}
        for(int k=0;k<6;k++) assert(counts[k]==15 || counts[k]==18);
        for(int step=0;step<100;step++){
            for(int a=0;a<100;a++){
                bool free=q.tile_alive[a];
                for(int b=0;b<100;b++) if(q.tile_alive[b] && q.tile_layer[b]>q.tile_layer[a] &&
                    q.tile_x[a]<q.tile_x[b]+56 && q.tile_x[a]+56>q.tile_x[b] &&
                    q.tile_y[a]<q.tile_y[b]+52 && q.tile_y[a]+52>q.tile_y[b]) free=false;
                assert(tile_is_free(&q,a)==free);
            }
            int n=q.tile_order[step]; assert(n>=0 && n<100 && !seen[n] && tile_is_free(&q,n)); seen[n]=1;
            if(q.tile_kind[n]==TILE_REWARD) {q.tile_alive[n]=0; continue;}
            if(normal_run%3) assert(q.tile_kind[n]==prev);
            prev=q.tile_kind[n]; normal_run++;
            q.tile_alive[n]=0;
        }
        assert(normal_run==99);
    }
    int covered=-1; for(int i=0;i<100;i++) if(!tile_is_free(active_page,i)){covered=i;break;}
    assert(covered>=0);
    lv_event_send(active_page->cells[covered], LV_EVENT_SHORT_CLICKED, NULL);
    assert(active_page->tile_alive[covered] && active_page->slot_n==0);
    {
        int reward=-1; for(int i=0;i<100;i++) if(active_page->tile_kind[i]==TILE_REWARD) reward=i;
        for(int i=0;i<100;i++){
            if(i==reward) continue;
            int dx=abs(active_page->tile_x[i]-active_page->tile_x[reward]);
            int dy=abs(active_page->tile_y[i]-active_page->tile_y[reward]);
            if(active_page->tile_layer[i]>active_page->tile_layer[reward] && dx<56 && dy<52) active_page->tile_alive[i]=0;
        }
        int picked=0;
        for(int i=0;i<100 && picked<6;i++){
            if(i==reward || !active_page->tile_alive[i] || active_page->tile_kind[i]==TILE_REWARD) continue;
            active_page->tile_alive[i]=0; active_page->slot[picked++]=(int8_t)i;
        }
        active_page->slot_n=6; active_page->tiles_left=0; active_page->over=false; active_page->won=false;
        for(int i=0;i<100;i++) if(active_page->tile_alive[i]) active_page->tiles_left++;
        refresh_tiles(active_page);
        assert(tile_is_free(active_page,reward) && active_page->slot_n==6);
        uint8_t left=active_page->tiles_left;
        lv_event_send(active_page->cells[reward], LV_EVENT_SHORT_CLICKED, NULL);
        assert(!active_page->tile_alive[reward] && active_page->slot_n==6 && !active_page->over && !active_page->won && active_page->tiles_left==left-1);
    }
    lv_event_send(find_btn("重开"), LV_EVENT_SHORT_CLICKED, NULL);
    assert(!active_page->over && active_page->tiles_left==100 && active_page->slot_n==0);
    for(int pass=0; pass<3; pass++){
        if(pass){lv_event_send(find_btn("重开"), LV_EVENT_SHORT_CLICKED, NULL); assert(active_page->tiles_left==100);}
        int normal_run=0;
        for(int i=0;i<100;i++){
            int idx=active_page->tile_order[i];
            assert(tile_is_free(active_page, idx));
            uint8_t slot_before=active_page->slot_n;
            lv_event_send(active_page->cells[idx], LV_EVENT_SHORT_CLICKED, NULL);
            if(active_page->tile_kind[idx]==TILE_REWARD) assert(active_page->slot_n==slot_before);
            else {normal_run++; if(normal_run%3==0) assert(active_page->slot_n==0);}
        }
        assert(normal_run==99 && active_page->won && active_page->over && active_page->tiles_left==0 && active_page->slot_n==0);
    }

    // A reward left last must be collected before the game is won.
    lv_event_send(find_btn("重开"), LV_EVENT_SHORT_CLICKED, NULL);
    {
        int reward=-1;for(int i=0;i<TILE_N;i++)if(tile_is_reward(active_page,i))reward=i;
        assert(reward>=0);memset(active_page->tile_alive,0,sizeof(active_page->tile_alive));
        active_page->tile_alive[reward]=1;active_page->tiles_left=1;refresh_tiles(active_page);
        assert(!active_page->won&&!active_page->over);
        lv_event_send(active_page->cells[reward],LV_EVENT_SHORT_CLICKED,NULL);
        assert(active_page->won&&active_page->over&&!active_page->slot_n&&!active_page->tiles_left);
    }
    lv_event_send(find_btn("重开"), LV_EVENT_SHORT_CLICKED, NULL);
    {
        /* Deterministic slot-full fixture. The scattered deal stays untouched as a solved board above. */
        static const uint8_t kinds[7] = {0,1,2,3,4,5,0};
        for(int i=0;i<100;i++) active_page->tile_alive[i]=0;
        for(int i=0;i<7;i++){
            active_page->tile_alive[i]=1;
            active_page->tile_kind[i]=kinds[i];
            active_page->tile_layer[i]=(uint8_t)i;
            active_page->tile_x[i]=(int16_t)(16+i*56);
            active_page->tile_y[i]=160;
        }
        active_page->slot_n=0; active_page->over=false; active_page->won=false; active_page->tiles_left=7;
        for(int i=0;i<7;i++) active_page->slot[i]=-1;
        refresh_tiles(active_page);
        for(int i=0;i<7;i++){
            assert(tile_is_free(active_page,i));
            lv_event_send(active_page->cells[i], LV_EVENT_SHORT_CLICKED, NULL);
        }
        assert(active_page->over && !active_page->won && active_page->slot_n==7 && active_page->tiles_left==0);
    }
    edge(NULL); advance(40); assert(lv_scr_act()==home && !active_page);

    lv_mem_monitor_t before, after; lv_mem_monitor(&before);
    for(int i=0;i<8;i++){
        mini_games_open(home, (mini_game_kind_t)(i%4), &lv_font_montserrat_20); advance(40); edge(NULL); advance(40);
        assert(lv_scr_act()==home && lv_mem_test()==LV_RES_OK);
    }
    lv_mem_monitor(&after); assert(after.free_size+256>=before.free_size);
    puts("mini games: mines timing/flag/restart, gomoku AI/win/block, tile match cover/3-clear/fail, memory lock, edge cleanup PASS");
    return 0;
}
'''

if __name__ == '__main__':
    main()
