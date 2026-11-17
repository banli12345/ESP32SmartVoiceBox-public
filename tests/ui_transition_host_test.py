"""Actual LVGL transition lifecycle, deletion and allocation-failure checks."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/ui-smooth-20261115/host-transition'
SOURCE = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail_create,fail_anim,starts,fail_alloc,allocation_count,fail_capture,held,fail_context,contexts,captures;
static unsigned desktop_draws,app_draws,nested_draws;
static bool slow_preparation;
int64_t transition_fake_us;
static lv_color_t last_frame[480*480];
static lv_obj_t *create(lv_obj_t *p){return fail_create?NULL:lv_obj_create(p);}
static lv_anim_t *start(const lv_anim_t*a){return ++starts==fail_anim?NULL:lv_anim_start(a);}
static void *allocate(size_t n){if(++allocation_count==fail_alloc)return NULL;void*p=malloc(n);if(p)held++;return p;}
static void release(void*p){if(p){assert(held>0);held--;}free(p);}
static void *context_allocate(size_t n){if(fail_context)return NULL;void*p=lv_mem_alloc(n);if(p)contexts++;return p;}
static void context_release(void*p){if(p){assert(contexts>0);contexts--;}lv_mem_free(p);}
static bool capture(lv_color_t*out,size_t pixels,void*unused){(void)unused;captures++;if(fail_capture)return false;assert(pixels==480*480);memcpy(out,last_frame,sizeof(last_frame));return true;}
#define lv_obj_create create
#define lv_anim_start start
#define malloc allocate
#define free release
#define lv_mem_alloc context_allocate
#define lv_mem_free context_release
#include "@ROOT@/src/UiTransition.c"
#undef lv_obj_create
#undef lv_anim_start
#undef malloc
#undef free
#undef lv_mem_alloc
#undef lv_mem_free
static struct {unsigned guard1;lv_color_t data[480*20];unsigned guard2;} buffer;
static unsigned flushes;
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){
 assert(buffer.guard1==0x12345678&&buffer.guard2==0x87654321);int w=lv_area_get_width(a);
 for(int y=a->y1;y<=a->y2;y++)memcpy(last_frame+y*480+a->x1,c+(y-a->y1)*w,w*sizeof(*c));
 flushes++;lv_disp_flush_ready(d);
}
static void advance(unsigned ms){for(unsigned n=0;n<ms;n+=10){lv_tick_inc(10);lv_timer_handler();}}
static void count_draw(lv_event_t*e){(*(unsigned*)lv_event_get_user_data(e))++;if(slow_preparation)transition_fake_us+=100000;}
static lv_obj_t *make_screen(unsigned*count,unsigned color){lv_obj_t*s=lv_obj_create(NULL);lv_obj_set_style_bg_color(s,lv_color_hex(color),0);lv_obj_set_style_pad_all(s,0,0);lv_obj_set_style_radius(s,0,0);lv_obj_add_event_cb(s,count_draw,LV_EVENT_DRAW_MAIN,count);return s;}
static void pattern(lv_obj_t*parent,unsigned left,unsigned right){for(int n=0;n<2;n++){lv_obj_t*c=lv_obj_create(parent);lv_obj_set_pos(c,n*240,0);lv_obj_set_size(c,240,480);lv_obj_set_style_bg_opa(c,LV_OPA_COVER,0);lv_obj_set_style_bg_color(c,lv_color_hex(n?right:left),0);lv_obj_set_style_border_width(c,0,0);lv_obj_set_style_radius(c,0,0);}}
static void orientation(void){
 lv_refr_now(NULL);
 /* At 1:1 size, vertical sheets must preserve texture direction and scale.
  * The contact shadow is the only place where texture colors may darken. */
 assert(!g.back&&g.progress>0&&g.progress<1024);
 int split=480*(1024-g.progress)/1024,shift=(480-split)/5;
 for(int x=0;x<480;x++){
  if(x>=split-8&&x<split)continue;
  unsigned expected=x<split?(x+shift<240?0xF800:0x001F):(x-split<240?0xFFE0:0x07FF);
  assert(last_frame[240*480+x].full==expected);
 }
}
static void settled(){advance(600);assert(!ui_transition_busy()&&held==0&&contexts==0);assert(lv_mem_test()==LV_RES_OK);}
int main(void){
 lv_init();buffer.guard1=0x12345678;buffer.guard2=0x87654321;lv_disp_draw_buf_t b;lv_disp_draw_buf_init(&b,buffer.data,NULL,480*20);
 lv_disp_drv_t d;lv_disp_drv_init(&d);d.hor_res=d.ver_res=480;d.draw_buf=&b;d.flush_cb=flush;lv_disp_drv_register(&d);
 lv_obj_t *a=make_screen(&desktop_draws,0x80A060),*app=make_screen(&app_draws,0xB3C6A2),*nested=make_screen(&nested_draws,0xFFFFFF);pattern(a,0xFF0000,0x0000FF);pattern(app,0xFFFF00,0x00FFFF);lv_scr_load(a);advance(100);
 ui_screen_open(NULL);ui_screen_open(a);ui_transition_set_capture(capture,NULL);
 // Unregistered fixtures and internal application pages always switch directly.
 ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0&&captures==0);ui_screen_back(a);
 ui_transition_set_desktop(a);
 fail_capture=1;ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0);fail_capture=0;ui_transition_set_capture(NULL,NULL);ui_screen_back(a);ui_transition_set_capture(capture,NULL);
 for(int n=1;n<=2;n++){allocation_count=0;fail_alloc=n;ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0);fail_alloc=0;ui_transition_set_capture(NULL,NULL);ui_screen_back(a);ui_transition_set_capture(capture,NULL);}
 fail_create=1;ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0);fail_create=0;ui_transition_set_capture(NULL,NULL);ui_screen_back(a);ui_transition_set_capture(capture,NULL);
 fail_context=1;ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0&&contexts==0);fail_context=0;ui_transition_set_capture(NULL,NULL);ui_screen_back(a);ui_transition_set_capture(capture,NULL);
#if defined(ESP_PLATFORM)
 // A slow target render must be immediately usable, with no animation buffers.
 slow_preparation=true;ui_screen_open(app);slow_preparation=false;
 assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0&&contexts==0);
 ui_transition_set_capture(NULL,NULL);ui_screen_back(a);ui_transition_set_capture(capture,NULL);
#endif
 ui_screen_open(app);assert(lv_scr_act()==app&&ui_transition_busy()&&held==2&&contexts==0);unsigned drawn=app_draws,old_flush=flushes;advance(140);assert(ui_transition_busy()&&app_draws==drawn&&flushes>old_flush);orientation();settled();
 // No more live widget repaint after the single preparation snapshot.
 unsigned previous_captures=captures;ui_screen_open(nested);assert(lv_scr_act()==nested&&!ui_transition_busy()&&captures==previous_captures&&held==0);ui_screen_back(app);assert(!ui_transition_busy()&&captures==previous_captures);
 ui_screen_back(a);assert(ui_transition_busy()&&held==2);drawn=desktop_draws;advance(100);assert(desktop_draws==drawn);settled();
 // Internal navigation can interrupt a desktop launch and release both buffers.
 ui_screen_open(app);ui_screen_open(nested);assert(!ui_transition_busy()&&held==0&&lv_scr_act()==nested);ui_screen_back(a);settled();
 ui_screen_open(app);advance(40);ui_screen_back(a);assert(ui_transition_busy()&&held==2);settled();
 // Exiting can immediately destroy the old screen/media; only pixels survive.
 ui_screen_open(nested);settled();ui_screen_back(a);lv_obj_del(nested);settled();nested=make_screen(&nested_draws,0xFFFFFF);
 ui_screen_open(app);lv_obj_t*overlay=lv_obj_get_child(app,-1);assert(lv_obj_get_parent(overlay)==app);lv_obj_del(overlay);assert(!ui_transition_busy()&&held==0);ui_screen_back(a);settled();
 ui_screen_open(app);lv_scr_load(a);lv_obj_del(app);assert(!ui_transition_busy()&&held==0);app=make_screen(&app_draws,0xB3C6A2);
 starts=0;fail_anim=1;ui_screen_open(app);assert(lv_scr_act()==app&&!ui_transition_busy()&&held==0);fail_anim=0;ui_screen_back(a);settled();
 lv_obj_t *page=lv_obj_create(a);lv_obj_set_size(page,480,480);ui_transition_reveal(page,false);assert(ui_transition_busy()&&held==2);settled();
 ui_transition_reveal(page,true);assert(ui_transition_busy());lv_obj_del(page);assert(!ui_transition_busy()&&held==0);
 lv_obj_t *padded=make_screen(&nested_draws,0xEAF0DF);lv_obj_set_style_pad_all(padded,13,0);lv_obj_set_style_border_width(padded,3,0);ui_screen_open(padded);assert(ui_transition_busy());lv_obj_update_layout(padded);lv_obj_t*cover=lv_obj_get_child(padded,-1);lv_area_t bounds;lv_obj_get_coords(cover,&bounds);assert(bounds.x1==0&&bounds.y1==0&&bounds.x2==479&&bounds.y2==479);drawn=nested_draws;advance(100);assert(nested_draws==drawn);settled();ui_screen_back(a);settled();lv_obj_del(padded);
 for(int n=0;n<30;n++){ui_screen_open(app);settled();ui_screen_back(a);settled();}
 assert(lv_anim_count_running()==0&&lv_mem_test()==LV_RES_OK);
 ui_screen_open(app);ui_transition_set_desktop(NULL);assert(!ui_transition_busy()&&held==0);ui_screen_back(a);assert(!ui_transition_busy());
 ui_transition_set_desktop(a);ui_screen_open(app);settled();lv_obj_del(a);ui_screen_open(nested);assert(!ui_transition_busy()&&held==0);assert(contexts==0);
 puts("Layered slide transition PASS: desktop/app-boundary policy, instant internal pages, single offscreen preparation, opaque overlay skips live UI repaint, 1:1 directional textures and partial-buffer guards, OOM/provider/context/object/animation failures, cancellation/deletion and repeated cleanup");
}
'''

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    unit=OUT/'test.c';unit.write_text(SOURCE.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
    # SDK stubs exercise ESP-only preparation deadline behavior too.
    (OUT/'esp_heap_caps.h').write_text('#pragma once\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nstatic inline void *heap_caps_malloc(size_t n,int caps){(void)caps;return malloc(n); }\nstatic inline void heap_caps_free(void *p){free(p); }\n')
    (OUT/'esp_timer.h').write_text('#pragma once\nextern int64_t transition_fake_us;\nstatic inline int64_t esp_timer_get_time(void){return transition_fake_us;}\n')
    (OUT/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(...) ((void)0)\n')
    for variant in ('host','sdk-clock'):
        exe=OUT/(variant+'.exe')
        command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE',
                 '-I',str(OUT),'-I',str(ROOT/'components/lvgl'),'-I',str(ROOT/'src'),str(unit),
                 *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
        if variant=='sdk-clock':command.insert(1,'-DESP_PLATFORM=1')
        result=subprocess.run(command,capture_output=True)
        if result.returncode:
            print((result.stdout+result.stderr).decode(errors='replace')[-4000:])
            raise SystemExit(result.returncode)
        subprocess.run([str(exe)],check=True,timeout=60)

if __name__=='__main__':
    main()
