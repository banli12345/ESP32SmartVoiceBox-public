"""Run the production direct-mode presenter in real LVGL against full redraws.

The fake RGB peripheral checks that the active scanout is never synchronized
before frame completion. This checks software pixels, not physical LCD timing.
"""
from pathlib import Path
import argparse
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/refresh-touch-20261116/display-host'
SOURCE = r'''
#include "DisplayRefresh.h"
#include "board_lcd.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static size_t sync_bytes, last_sync_bytes;
static void *watched_memcpy(void *d,const void *s,size_t n){sync_bytes+=n;return memcpy(d,s,n);}
#define memcpy watched_memcpy
#include "@ROOT@/src/DisplayRefresh.c"
#undef memcpy
enum { W=480, H=480, N=W*H };
static struct { uint32_t a; lv_color_t p[N]; uint32_t b; } fb[2];
static lv_color_t reference[N], before[N], scanout_before[N], capture[N];
static lv_color_t *front, *pending;
static unsigned submits, waits, recoveries, fail_submit, fail_wait;
static unsigned direct_pixels;
static uint32_t sequence;
#ifdef TEST_VIDEO
#include "video_draw.h"
static bool playing;
static lv_color_t video_pixels[32*24];
static lv_img_dsc_t video_frame;
bool video_playback_active(void){return playing;}
const lv_img_dsc_t *get_video_frame_image(void){return playing?&video_frame:NULL;}
static lv_obj_t *video_scene(lv_disp_t *d){
    lv_disp_set_default(d);lv_obj_t *s=lv_obj_create(NULL);lv_scr_load(s);
    lv_obj_t *v=lv_img_create(s);lv_img_set_src(v,&video_frame);lv_img_set_zoom(v,2048);
    lv_img_set_antialias(v,true);lv_obj_set_pos(v,100,110);return v;
}
#endif
uint16_t *board_lcd_get_framebuffer(int i) { return (uint16_t *)fb[i].p; }
esp_err_t board_lcd_draw_bitmap(int x,int y,int xe,int ye,const void *p) {
    assert(x==0 && y==0 && xe==W && ye==H);
    assert(!memcmp(front,scanout_before,sizeof(scanout_before)));
    assert(p==fb[0].p || p==fb[1].p); ++submits;
    if (fail_submit) { --fail_submit; return -1; }
    pending=(lv_color_t *)p;
    assert(pending!=front); memcpy(before, front, sizeof(before));
    return ESP_OK;
}
uint32_t board_lcd_read_frame_complete_count(void) {return sequence;}
bool board_lcd_wait_frame_complete(uint32_t after,unsigned timeout) {
    assert(after==sequence && timeout==100 && pending); ++waits;
    assert(!memcmp(before,front,sizeof(before)));
    if (fail_wait) { --fail_wait; return false; }
    front=pending; pending=NULL; ++sequence; return true;
}
bool board_lcd_recover_scanout(void) {++recoveries; return true;}
static void full_flush(lv_disp_drv_t *d,const lv_area_t *a,lv_color_t *p) {
    assert(a->x1==0 && a->y1==0 && a->x2==W-1 && a->y2==H-1);
    assert(p==reference); lv_disp_flush_ready(d);
}
static void monitor(lv_disp_drv_t *d,uint32_t ms,uint32_t px){(void)d;(void)ms;direct_pixels=px;}
static void render(lv_disp_t *d){memcpy(scanout_before,front,sizeof(scanout_before));lv_disp_set_default(d);size_t bytes=sync_bytes;lv_refr_now(d);last_sync_bytes=sync_bytes-bytes;}
static lv_obj_t *scene(lv_disp_t *d) {
    lv_disp_set_default(d);
    lv_obj_t *s=lv_scr_act(); lv_obj_set_style_bg_color(s,lv_color_hex(0xeaf4ed),0);
    lv_obj_t *r=lv_obj_create(s); lv_obj_set_pos(r,25,35); lv_obj_set_size(r,92,63);
    lv_obj_set_style_radius(r,14,0); lv_obj_set_style_bg_color(r,lv_color_hex(0x1d8860),0);
    lv_obj_set_style_shadow_width(r,8,0);
    lv_obj_t *label=lv_label_create(r); lv_label_set_text(label,"Breeze");
    lv_obj_t *other=lv_obj_create(s); lv_obj_set_pos(other,320,330); lv_obj_set_size(other,100,90);
    lv_obj_set_style_bg_color(other,lv_color_hex(0x32aacc),0);
    return r;
}
static void check(lv_disp_t *direct,lv_disp_t *full) {
    unsigned old=submits;
    render(direct);
    lv_disp_set_default(full); lv_refr_now(full);
    assert(submits==old+1);
    assert(!memcmp(front,reference,sizeof(reference)));
    assert(display_refresh_capture(capture,N,NULL));
    assert(!memcmp(capture,reference,sizeof(reference)));
    assert(!display_refresh_capture(NULL,N,NULL));
    assert(!display_refresh_capture(capture,N-1,NULL));
    assert(fb[0].a==0x12345678 && fb[0].b==0x98765432);
    assert(fb[1].a==0x12345678 && fb[1].b==0x98765432);
}
int main(void) {
    lv_init(); front=fb[0].p;
    for (int i=0;i<2;i++) {fb[i].a=0x12345678; fb[i].b=0x98765432;}
    static lv_disp_draw_buf_t db,rb; static lv_disp_drv_t dd,rd;
    lv_disp_draw_buf_init(&db,fb[1].p,fb[0].p,N); lv_disp_drv_init(&dd);
    dd.hor_res=W; dd.ver_res=H; dd.draw_buf=&db; dd.direct_mode=1;
    dd.monitor_cb=monitor;
    dd.render_start_cb=display_refresh_begin;
    dd.flush_cb=display_refresh_flush; lv_disp_t *direct=lv_disp_drv_register(&dd);
    lv_disp_draw_buf_init(&rb,reference,NULL,N); lv_disp_drv_init(&rd);
    rd.hor_res=W; rd.ver_res=H; rd.draw_buf=&rb; rd.full_refresh=1;
    rd.flush_cb=full_flush; lv_disp_t *full=lv_disp_drv_register(&rd);
    lv_obj_t *a=scene(direct), *b=scene(full);
    check(direct,full);
    // Many small, disjoint, overlapping, clipped and full-width changes alternate
    // both buffers. Pixel equality catches forgotten synchronization rectangles.
    for (int i=0;i<60;i++) {
        lv_obj_set_pos(a, (i*23)%420-10, (i*17)%420-10);
        lv_obj_set_pos(b, (i*23)%420-10, (i*17)%420-10);
        lv_obj_t *sa=lv_obj_get_parent(a), *sb=lv_obj_get_parent(b);
        lv_obj_t *oa=lv_obj_get_child(sa,1), *ob=lv_obj_get_child(sb,1);
        lv_obj_set_style_bg_color(oa,lv_color_hex(0x553300+i*1337),0);
        lv_obj_set_style_bg_color(ob,lv_color_hex(0x553300+i*1337),0);
        if(i%5==0) {lv_obj_set_width(a,W);lv_obj_set_width(b,W);}
        else {lv_obj_set_width(a,92);lv_obj_set_width(b,92);}
        check(direct,full);
    }
    // Consecutive whole-screen frames (animation/video) do not need a redundant
    // 450KiB synchronization copy. Returning to a small update restores history.
    for(int i=0;i<2;i++) {
        lv_obj_invalidate(lv_obj_get_parent(a));lv_obj_invalidate(lv_obj_get_parent(b));
        check(direct,full);assert(last_sync_bytes==0);
    }
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_obj_get_parent(a),1),lv_color_hex(0xdd6622),0);
    lv_obj_set_style_bg_color(lv_obj_get_child(lv_obj_get_parent(b),1),lv_color_hex(0xdd6622),0);
    check(direct,full); assert(direct_pixels<N/4);
    printf("small card redraw: %u pixels instead of %u full-screen pixels\n",direct_pixels,N);
    // Unchanged frames do not re-submit to the controller.
    unsigned old=submits; render(direct); assert(submits==old);
    // Counter wrap is harmless; timeout and SDK failures recover before syncing.
    sequence=UINT32_MAX;
    lv_obj_set_pos(a,4,5); lv_obj_set_pos(b,4,5); check(direct,full); assert(sequence==0);
    fail_wait=1;
    lv_obj_set_pos(a,7,8); lv_obj_set_pos(b,7,8);
    render(direct);
    lv_disp_set_default(full); lv_refr_now(full);
    assert(recoveries==1 && !memcmp(front,reference,sizeof(reference)));
    lv_disp_set_default(direct); display_refresh_poll(); render(direct);
    assert(!memcmp(fb[0].p,fb[1].p,sizeof(reference)));
    fail_submit=1; lv_obj_invalidate(a); lv_obj_invalidate(b);
    render(direct);
    assert(recoveries==2); display_refresh_poll(); render(direct);
    assert(!memcmp(front,reference,sizeof(reference)));
    assert(waits>=submits-1);
#ifdef TEST_VIDEO
    // Production video draw context versus stock LVGL, with partial updates,
    // opaque full-screen background elision and frame-to-frame image changes.
    playing=true; video_frame.header.cf=LV_IMG_CF_TRUE_COLOR;
    video_frame.header.w=32;video_frame.header.h=24;
    video_frame.data=(const uint8_t *)video_pixels;video_frame.data_size=sizeof(video_pixels);
    lv_obj_t *va=video_scene(direct), *vb=video_scene(full);video_draw_set_image(va);
    for(int frame=0;frame<12;frame++){
        for(int i=0;i<32*24;i++)video_pixels[i].full=(uint16_t)(i*773+frame*939);
        lv_img_cache_invalidate_src(&video_frame);
        lv_obj_set_pos(va,frame*13-20,frame*9+30);lv_obj_set_pos(vb,frame*13-20,frame*9+30);
        lv_obj_invalidate(va);lv_obj_invalidate(vb);
        if(frame%3==0){lv_obj_invalidate(lv_obj_get_parent(va));lv_obj_invalidate(lv_obj_get_parent(vb));}
        check(direct,full);
    }
    video_draw_set_image(NULL);playing=false;
    puts("production video renderer: direct/partial/full updates match stock LVGL pixels PASS");
#endif
    puts("display direct mode: partial pixels match full redraw, buffers stay coherent, one present per frame, recovery/wrap PASS");
    return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--video', action='store_true')
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    (OUT/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n', encoding='utf-8')
    (OUT/'esp_attr.h').write_text('#pragma once\n#define DRAM_ATTR\n#define IRAM_ATTR\n#define __attribute__(x)\n', encoding='utf-8')
    unit = OUT/'test.c'
    source = SOURCE.replace('@ROOT@', ROOT.as_posix())
    if args.video:
        source = source.replace('dd.monitor_cb=monitor;', 'dd.monitor_cb=monitor;dd.draw_ctx_init=video_draw_init_ctx;')
    unit.write_text(source, encoding='utf-8')
    command = [str(ROOT/'.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I', str(OUT), '-I', str(ROOT/'components/lvgl'), '-I', str(ROOT/'src'),
               str(unit),
               *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')], '-o', str(OUT/'test.exe')]
    if args.video:
        compiler = shutil.which('cl.exe')
        if not compiler:
            raise SystemExit('--video requires the MSVC developer environment.')
        includes = [OUT, ROOT/'components/lvgl', ROOT/'src', ROOT/'components/lvgl/src/extra/others/ime']
        arguments = ['/nologo', '/O2', '/utf-8', '/D_CRT_SECURE_NO_WARNINGS', '/DLV_CONF_INCLUDE_SIMPLE',
                     '/DLV_KCONFIG_IGNORE', '/DTEST_VIDEO', *['/I'+str(p) for p in includes],
                     str(unit), str(ROOT/'src/video_draw.cpp'),
                     *[str(p) for p in (ROOT/'components/lvgl/src').rglob('*.c')],
                     '/Fe:'+str(OUT/'test.exe'), '/Fo'+str(OUT)+'\\']
        response = OUT/'compile.rsp'
        response.write_text('\n'.join(subprocess.list2cmdline([a]) for a in arguments), encoding='utf-8')
        command = [compiler, '@'+str(response)]
    result = subprocess.run(command, capture_output=True, timeout=120)
    (OUT/'compile.log').write_bytes(result.stdout+result.stderr)
    if result.returncode:
        print((result.stdout+result.stderr).decode(errors='replace')[-5000:])
        raise SystemExit(result.returncode)
    subprocess.run([str(OUT/'test.exe')], check=True, timeout=45)


if __name__ == '__main__':
    main()
