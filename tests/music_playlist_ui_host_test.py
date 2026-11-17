"""Exercise production playlist in real LVGL with pointer input and backend fakes."""
from pathlib import Path
import subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'.tools/music-library-20261114/ui-host'
def main():
 OUT.mkdir(parents=True,exist_ok=True)
 config=(ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
 (OUT/'lv_conf.h').write_text(config)
 unit=OUT/'test.c'
 unit.write_text(HARNESS.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
 exe=OUT/'playlist.exe'
 cmd=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'components'),'-I',str(ROOT/'src'),str(ROOT/'src/UiTransition.c'),str(unit),*[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
 compiled=subprocess.run(cmd,capture_output=True)
 if compiled.returncode:
  print(compiled.stderr.decode(errors='replace')[-4000:]);raise SystemExit(compiled.returncode)
 subprocess.run([str(exe),str(ROOT/'sd_card_files/fonts/chinese_20.bin'),str(OUT)],check=True,timeout=60)
 for file in OUT.glob('*.rgb565'):
  rgb=bytearray()
  raw=file.read_bytes()
  for i in range(0,len(raw),2):
   c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
  Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))
HARNESS=r'''#define SDCAR_H
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
lv_font_t *chinese_font_20;
static bool active[12];static uint32_t generations[12],revision=1;
static unsigned plays;static int chosen=-1,status;
static lv_event_cb_t back;
static bool sdcar_copy_music_track(uint8_t i,char*p,size_t n,uint32_t*g){
 if(i>=12||!active[i])return false;
 if(p)snprintf(p,n,i==0?"/music/茉莉花 - 黄诗扶.wav":"/music/歌曲 %u - 歌手.wav",i+1);
 if(g)*g=generations[i];return true;
}
static uint8_t sdcar_get_music_track_count(void){return 12;}
static uint32_t sdcar_music_catalog_revision(void){return revision;}
static void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)p;back=cb;}
static bool music_select_library_track(uint8_t i,uint32_t g){if(!active[i]||generations[i]!=g)return false;chosen=i;return true;}
static void music_audio_play(void){++plays;}
static bool music_delete_track(uint8_t i,uint32_t g){assert(active[i]&&generations[i]==g);chosen=i;status=1;return true;}
static int music_delete_status(void){return status;}
#define SOUND_H
#define MUSIC_PLAYLIST_EDGE_TEST
#define EDGE_BACK_H
/* Prevent the external declarations from conflicting with these test fakes. */
#define edge_back_bind host_edge_bind
#include "EdgeBack.h"
#undef edge_back_bind
#include "@ROOT@/src/MusicPlaylist.c"
static bool down;static int x,y;
static void input(lv_indev_drv_t*d,lv_indev_data_t*p){(void)d;p->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;p->point=(lv_point_t){x,y};}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5);lv_timer_handler();}}
static void touch(bool pressed,int px,int py,unsigned ms){down=pressed;x=px;y=py;advance(ms);}
static void tap(int px,int py){touch(true,px,py,90);touch(false,px,py,100);}
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){(void)a;(void)c;lv_disp_flush_ready(d);}
static const char *font_file;
static void *fo(lv_fs_drv_t*d,const char*p,lv_fs_mode_t m){(void)d;(void)p;(void)m;return fopen(font_file,"rb");}
static lv_fs_res_t fc(lv_fs_drv_t*d,void*f){(void)d;return fclose(f)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t fr(lv_fs_drv_t*d,void*f,void*p,uint32_t n,uint32_t*r){(void)d;*r=fread(p,1,n,f);return LV_FS_RES_OK;}
static lv_fs_res_t fs(lv_fs_drv_t*d,void*f,uint32_t n,lv_fs_whence_t w){(void)d;return fseek(f,n,w==LV_FS_SEEK_SET?SEEK_SET:w==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t ft(lv_fs_drv_t*d,void*f,uint32_t*n){(void)d;*n=ftell(f);return LV_FS_RES_OK;}
static void screenshot(const char*root,const char*name){char p[1024];snprintf(p,sizeof(p),"%s/%s.rgb565",root,name);lv_refr_now(NULL);FILE*f=fopen(p,"wb");assert(f);fwrite(pixels,sizeof(pixels),1,f);fclose(f);}
int main(int argc,char**argv){
 assert(argc==3);font_file=argv[1];lv_init();
 lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,pixels,NULL,480*480);
 lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=480;display.ver_res=480;display.draw_buf=&buf;display.full_refresh=1;display.flush_cb=flush;lv_disp_drv_register(&display);
 lv_indev_drv_t pointer;lv_indev_drv_init(&pointer);pointer.type=LV_INDEV_TYPE_POINTER;pointer.read_cb=input;lv_indev_drv_register(&pointer);
 lv_fs_drv_t fontfs;lv_fs_drv_init(&fontfs);fontfs.letter='S';fontfs.open_cb=fo;fontfs.close_cb=fc;fontfs.read_cb=fr;fontfs.seek_cb=fs;fontfs.tell_cb=ft;lv_fs_drv_register(&fontfs);
 chinese_font_20=lv_font_load("S:/chinese.bin");assert(chinese_font_20);
 for(int i=0;i<12;++i){active[i]=true;generations[i]=i+1;}
 lv_obj_t *player=lv_obj_create(NULL);music_playlist_open(player);advance(300);
 assert(lv_obj_get_child_cnt(list)==12);screenshot(argv[2],"playlist");
 tap(120,130);assert(plays==1&&chosen==0&&lv_scr_act()==player);
 music_playlist_open(player);advance(300);
 touch(true,120,130,650);touch(false,120,130,100);assert(dialog&&plays==1);screenshot(argv[2],"confirm-delete");
 tap(120,330);assert(!dialog&&status==0&&plays==1);
 touch(true,120,370,70);touch(true,120,290,80);touch(true,120,180,80);touch(false,120,180,100);assert(lv_obj_get_scroll_y(list)>0&&plays==1);
 lv_obj_scroll_to_y(list,0,LV_ANIM_OFF);advance(100);
 touch(true,120,130,650);touch(false,120,130,100);assert(dialog&&plays==1);
 back(NULL);assert(!dialog&&lv_scr_act()==screen); /* edge-back cancels deletion */
 touch(true,120,130,650);touch(false,120,130,100);assert(dialog);tap(335,330);
 assert(!dialog&&deleting&&status==1&&plays==1);
 tap(120,130);assert(plays==1);active[0]=false;revision++;status=2;advance(300);
 assert(!deleting&&lv_obj_get_child_cnt(list)==11&&strstr(lv_label_get_text(notice),"已删除"));
 for(int i=1;i<12;++i)active[i]=false;revision++;advance(300);assert(lv_obj_get_child_cnt(list)==1&&strstr(lv_label_get_text(summary),"0"));
 screenshot(argv[2],"empty");back(NULL);assert(lv_scr_act()==player);
 puts("Production playlist LVGL: tap/long-press/cancel/confirm, scroll without playback, async deletion/refresh/empty and side-back passed");
}
'''
if __name__=='__main__':main()
