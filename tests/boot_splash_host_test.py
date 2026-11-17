"""Host LVGL checks for the Breeze OS boot splash lifecycle."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/breeze-boot-polish-20261115/host-boot'
SOURCE = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int fail_timer;
static lv_timer_t *wrap_timer(lv_timer_cb_t cb, uint32_t period, void *user)
{
    if(fail_timer) return NULL;
    return lv_timer_create(cb, period, user);
}
#define lv_timer_create wrap_timer
#include "@ROOT@/src/BootSplash.c"
#undef lv_timer_create
static struct {unsigned guard1; lv_color_t data[480*24]; unsigned guard2;} buffer;
static lv_color_t frame[480*480];
static unsigned flushes;
static unsigned desktop_draws;
static void flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color)
{
    assert(buffer.guard1==0x12345678 && buffer.guard2==0x87654321);
    int width = lv_area_get_width(area);
    for(int y = area->y1; y <= area->y2; ++y)
        memcpy(frame + y*480 + area->x1, color + (y - area->y1)*width, (size_t)width*sizeof(*color));
    flushes++;
    lv_disp_flush_ready(drv);
}
static void advance(unsigned ms)
{
    for(unsigned n = 0; n < ms; n += 10) { lv_tick_inc(10); lv_timer_handler(); }
}
static void count_draw(lv_event_t *event) { (void)event; desktop_draws++; }
static int timers(void)
{
    int count = 0;
    for(lv_timer_t *timer = lv_timer_get_next(NULL); timer != NULL; timer = lv_timer_get_next(timer)) count++;
    return count;
}
int main(void)
{
    lv_init();
    buffer.guard1 = 0x12345678; buffer.guard2 = 0x87654321;
    boot_splash_start("1.0");
    assert(!boot_splash_busy());
    lv_disp_draw_buf_t buf; lv_disp_draw_buf_init(&buf, buffer.data, NULL, 480*24);
    lv_disp_drv_t drv; lv_disp_drv_init(&drv);
    drv.hor_res = drv.ver_res = 480; drv.draw_buf = &buf; drv.flush_cb = flush;
    lv_disp_drv_register(&drv);
    lv_obj_t *desktop = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(desktop, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_bg_opa(desktop, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(desktop, count_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_scr_load(desktop);
    advance(50);
    assert(lv_scr_act()==desktop);
    int baseline = timers();
    boot_splash_start(NULL);
    assert(boot_splash_busy() && lv_scr_act()==desktop);
    assert(strcmp(lv_label_get_text(splash.brand_label), "青禾")==0);
    const lv_font_t *brand_font=lv_obj_get_style_text_font(splash.brand_label,LV_PART_MAIN);
    lv_font_glyph_dsc_t glyph;
    assert(brand_font==&lv_font_breeze_brand_24);
    assert(lv_font_get_glyph_dsc(brand_font,&glyph,0x9752,0)&&glyph.box_w>0);
    assert(lv_font_get_glyph_dsc(brand_font,&glyph,0x79be,0)&&glyph.box_w>0);
    lv_obj_t *overlay = lv_obj_get_child(lv_layer_top(), 0);
    assert(overlay != NULL);
    lv_refr_now(NULL);
    unsigned painted = desktop_draws;
    assert(frame[0].full != 0xF800);
    assert(frame[240*480+240].full != 0xF800);
    advance(8000);
    assert(boot_splash_busy() && splash.phase==BOOT_IDLE && lv_scr_act()==desktop);
    assert(desktop_draws==painted);
    assert(frame[0].full != 0xF800);
    boot_splash_ready();
    assert(boot_splash_busy() && splash.phase==BOOT_RUN);
    boot_splash_ready();
    assert(splash.phase==BOOT_RUN && splash.elapsed<100);
    advance(2900);
    assert(boot_splash_busy() && lv_scr_act()==desktop);
    advance(300);
    assert(!boot_splash_busy() && splash.phase==BOOT_OFF && splash.timer==NULL && splash.overlay==NULL);
    assert(lv_scr_act()==desktop && frame[240*480+20].full==0xF800);
    assert(timers()==baseline && lv_anim_count_running()==0 && lv_mem_test()==LV_RES_OK);
    boot_splash_ready();
    assert(!boot_splash_busy());
    boot_splash_start("v2");
    boot_splash_ready();
    advance(400);
    assert(boot_splash_busy());
    boot_splash_cancel();
    assert(!boot_splash_busy() && splash.overlay==NULL && timers()==baseline);
    boot_splash_cancel();
    lv_refr_now(NULL);
    assert(lv_scr_act()==desktop && frame[10].full==0xF800);
    boot_splash_start("v3");
    overlay = lv_obj_get_child(lv_layer_top(), 0);
    boot_splash_ready();
    lv_obj_del(overlay);
    assert(!boot_splash_busy() && splash.timer==NULL && splash.overlay==NULL);
    advance(3000);
    assert(!boot_splash_busy() && lv_scr_act()==desktop && timers()==baseline);
    fail_timer = 1;
    boot_splash_start("v4");
    assert(boot_splash_busy());
    boot_splash_ready();
    assert(!boot_splash_busy() && splash.overlay==NULL);
    fail_timer = 0;
    lv_refr_now(NULL);
    assert(frame[0].full==0xF800);
    boot_splash_start("v5");
    boot_splash_ready();
    advance(3200);
    assert(!boot_splash_busy() && timers()==baseline && lv_mem_test()==LV_RES_OK && lv_scr_act()==desktop);
    /* Main initialization may create/load its desktop behind the top overlay. */
    boot_splash_start("1.19.0");
    lv_obj_t *next=lv_obj_create(NULL);lv_obj_set_style_bg_color(next,lv_color_hex(0x00FF00),0);lv_scr_load(next);
    lv_obj_t *sibling=lv_obj_create(lv_layer_top());lv_obj_set_pos(sibling,450,450);lv_obj_set_size(sibling,10,10);
    boot_splash_ready();assert(lv_obj_get_child(lv_layer_top(),-1)==splash.overlay);advance(3200);
    assert(lv_scr_act()==next&&lv_obj_is_valid(sibling)&&!boot_splash_busy());lv_obj_del(sibling);lv_scr_load(desktop);lv_obj_del(next);
    /* Restart/clock rollover and repeated teardown must not retain resources. */
    lv_mem_monitor_t before,after;lv_mem_monitor(&before);
    for(unsigned i=0;i<20;i++){boot_splash_start("first");boot_splash_ready();advance(90);boot_splash_start("replacement");assert(boot_splash_busy()&&splash.phase==BOOT_IDLE&&splash.timer==NULL);boot_splash_ready();advance(3200);assert(!boot_splash_busy()&&timers()==baseline);}
    lv_mem_monitor(&after);assert(after.free_size+128>=before.free_size);
    lv_tick_inc(0xffffffffu-lv_tick_get()-100);boot_splash_start("wrap");boot_splash_ready();advance(3200);assert(!boot_splash_busy()&&timers()==baseline&&lv_scr_act()==desktop);
    puts("Breeze boot splash PASS: built-in Qinghe glyphs, 3-second playback, held initialization, handoff, cancel/restart/external delete/OOM, sibling preservation, 20 cleanup cycles and tick wrap");
    return 0;
}
'''

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    unit = OUT / 'grok-boot-test.c'
    unit.write_text(SOURCE.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
    exe = OUT / 'grok-boot-test.exe'
    command = [str(ROOT / '.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I', str(OUT), '-I', str(ROOT / 'components/lvgl'), '-I', str(ROOT / 'src'), str(unit),
               str(ROOT / 'src/lv_font_breeze_brand_24.c'),
               *[str(p) for p in (ROOT / 'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors='replace')[-5000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(exe)], check=True, timeout=60)

if __name__ == '__main__':
    main()
