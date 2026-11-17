"""Independent geometry oracle for production scatter deals and drawing order."""
from pathlib import Path
import subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCE=r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void edge_back_bind(lv_obj_t *s,lv_event_cb_t cb,void *u){(void)s;(void)cb;(void)u;}
void ui_screen_open(lv_obj_t *s){(void)s;}
void ui_screen_back(lv_obj_t *s){(void)s;}
#include "@ROOT@/src/MiniGames.c"
// Refresh draws remaining cards in increasing index order. This oracle uses
// the painted rectangles, independently of tile_layer / tile_overlap helpers.
static bool intersects(const mini_page_t *p,int a,int b){
 return p->tile_x[a]<p->tile_x[b]+56 && p->tile_x[a]+56>p->tile_x[b] &&
        p->tile_y[a]<p->tile_y[b]+52 && p->tile_y[a]+52>p->tile_y[b];
}
static bool accessible(const mini_page_t *p,int a){
 if(!p->tile_alive[a])return false;
 for(int b=a+1;b<100;b++)if(p->tile_alive[b]&&intersects(p,a,b))return false;
 return true;
}
int main(){
 lv_init();mini_page_t p={0};int16_t old_x[100],old_y[100];
 for(int trial=0;trial<64;trial++){
  deal_tiles(&p);unsigned unique_x=0,unique_y=0,pairs=0,free_count=0,changed=0,rewards=0;
  unsigned kinds[6]={0};
  for(int i=0;i<100;i++){
   assert(p.tile_x[i]>=16&&p.tile_x[i]+56<=464&&p.tile_y[i]>=118&&p.tile_y[i]+52<418);
   if(trial)changed+=p.tile_x[i]!=old_x[i]||p.tile_y[i]!=old_y[i];
   old_x[i]=p.tile_x[i];old_y[i]=p.tile_y[i];
   bool nx=true,ny=true;
   for(int j=0;j<i;j++){
    if(p.tile_x[i]==p.tile_x[j])nx=false;if(p.tile_y[i]==p.tile_y[j])ny=false;
    assert(p.tile_x[i]!=p.tile_x[j]||p.tile_y[i]!=p.tile_y[j]);pairs+=intersects(&p,i,j);
   }
   unique_x+=nx;unique_y+=ny;free_count+=accessible(&p,i);
   if(tile_is_reward(&p,i))rewards++;else {assert(p.tile_kind[i]<6);kinds[p.tile_kind[i]]++;}
  }
  assert(unique_x>=40&&unique_y>=35&&pairs>=100&&free_count>=8);
  assert(rewards==1);for(int k=0;k<6;k++)assert(kinds[k]==15||kinds[k]==18);
  if(trial)assert(changed>=60);
  bool used[100]={0};unsigned normals=0;int last_kind=-1;
  for(int step=0;step<100;step++){
   for(int a=0;a<100;a++)assert(tile_is_free(&p,a)==accessible(&p,a));
   int a=p.tile_order[step];assert(a>=0&&a<100&&!used[a]&&accessible(&p,a));used[a]=true;
   if(!tile_is_reward(&p,a)){
    if(normals%3)assert(last_kind==p.tile_kind[a]);last_kind=p.tile_kind[a];normals++;
   }
   p.tile_alive[a]=0;
  }
  assert(normals==99);
 }
 puts("Scatter: 64 non-grid changing deals, 100 cards, overlap, painted depth, 99+1 and legal solution PASS");
}
'''
def main():
 with tempfile.TemporaryDirectory(prefix='tile-scatter-')as temp:
  d=Path(temp)
  config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
  (d/'lv_conf.h').write_text(config,encoding='utf-8');unit=d/'test.c';unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=d/'test.exe'
  cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(d),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
  r=subprocess.run(cmd,capture_output=True)
  if r.returncode:print(r.stderr.decode(errors='replace'));raise SystemExit(r.returncode)
  subprocess.run([str(exe)],check=True,timeout=60)
if __name__=='__main__':main()
