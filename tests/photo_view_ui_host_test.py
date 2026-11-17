"""Exercise production photo view in real LVGL for full and stream sources."""
from pathlib import Path
import re
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/photo-view-ui/host'


def function(source, name):
    match = re.search(r'^static[^\n]*\b' + re.escape(name) + r'\([^;{}]*\)\s*\{', source, re.M)
    assert match, name
    end, depth = match.end(), 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    source = (ROOT / 'src/ui/custom/custom.c').read_text(encoding='utf-8')
    body = '\n'.join(function(source, name) for name in (
        'photo_detach_and_release', 'photo_show_load_error', 'photo_open_cb',
        'photo_load_timer_cb', 'photo_close_cb', 'create_photo_view_screen'))
    unit = OUT / 'test.c'
    unit.write_text(HARNESS.replace('@BODY@', body).replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
    exe = OUT / 'photo.exe'
    cmd = [str(ROOT / '.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
           '-I', str(OUT), '-I', str(ROOT / 'components/lvgl'), '-I', str(ROOT / 'components'),
           '-I', str(ROOT / 'src'), str(ROOT / 'src/UiTransition.c'), str(unit),
           *[str(p) for p in (ROOT / 'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    compiled = subprocess.run(cmd, capture_output=True)
    if compiled.returncode:
        print(compiled.stderr.decode(errors='replace')[-4000:])
        raise SystemExit(compiled.returncode)
    bin_path = OUT / 'stream.bin'
    w, h = 32, 32
    px = 3
    data = bytearray(4 + w * h * px)
    hdr = 5 | (w << 10) | (h << 21)
    data[0:4] = hdr.to_bytes(4, 'little')
    for y in range(h):
        for x in range(w):
            i = 4 + (y * w + x) * px
            c = ((x & 31) << 11) | ((y & 63) << 5) | (x & 31)
            data[i] = c & 255
            data[i + 1] = c >> 8
            data[i + 2] = 255
    bin_path.write_bytes(data)
    subprocess.run([str(exe), str(ROOT / 'sd_card_files/fonts/chinese_20.bin'), str(OUT), str(bin_path)],
                   check=True, timeout=60)
    for file in OUT.glob('*.rgb565'):
        rgb = bytearray()
        raw = file.read_bytes()
        for i in range(0, len(raw), 2):
            c = raw[i] | raw[i + 1] << 8
            rgb.extend((((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(file.with_suffix('.png'))


HARNESS = r'''
#define SDCAR_H
#include "lvgl.h"
#include "UITheme.h"
#include "UiTransition.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
lv_font_t *chinese_font_16;
lv_font_t *chinese_font_20;
static lv_obj_t *main_screen_ref;
static lv_obj_t *photo_screen;
static lv_obj_t *photo_view_screen;
static lv_obj_t *photo_full_img;
static lv_obj_t *photo_loading_label;
static lv_timer_t *photo_load_timer;
static int load_status;
static const lv_img_dsc_t *loaded_image;
static const char *loaded_path;
static const char *load_error;
static unsigned releases, starts;
static const char *font_file;
static lv_img_dsc_t full_dsc;
static uint8_t full_pixels[32*32*3];
static char stream_src[1024];
static const char *photo_paths[2]={"S:/images/full.bin","S:/images/stream.bin"};
static void edge_back_bind(lv_obj_t*s,lv_event_cb_t cb,void*p){(void)s;(void)cb;(void)p;}
static lv_obj_t *create_back_button(lv_obj_t *parent,lv_align_t a,lv_coord_t x,lv_event_cb_t cb){
 (void)a;(void)x;lv_obj_t*b=lv_btn_create(parent);lv_obj_set_size(b,40,40);lv_obj_align(b,LV_ALIGN_TOP_RIGHT,-8,8);
 lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,NULL);return b;}
static const lv_font_t *ui_chinese_font_20(const lv_font_t *fallback){return chinese_font_20?chinese_font_20:fallback;}
static int get_photo_count(void){return 2;}
static const char *get_photo_path(int i){return i>=0&&i<2?photo_paths[i]:NULL;}
static bool start_photo_load(const char *path){++starts;if(!path){load_status=-1;load_error="路径无效";return false;}
 if(strstr(path,"full")){load_status=2;loaded_image=&full_dsc;loaded_path=NULL;load_error="";}
 else if(strstr(path,"stream")){load_status=2;loaded_image=NULL;loaded_path=stream_src;load_error="";}
 else {load_status=-1;loaded_image=NULL;loaded_path=NULL;load_error="找不到照片文件";return true;}
 return true;}
static int get_photo_load_status(void){return load_status;}
static const lv_img_dsc_t *get_loaded_photo_image(void){return load_status==2?loaded_image:NULL;}
static const char *get_loaded_photo_path(void){return load_status==2?loaded_path:NULL;}
static const char *get_photo_load_error(void){return load_error;}
static void release_loaded_photo(void){assert(!photo_full_img||lv_img_get_src(photo_full_img)==NULL);++releases;loaded_image=NULL;loaded_path=NULL;load_status=0;}
static void *fo(lv_fs_drv_t*d,const char*p,lv_fs_mode_t m){(void)d;(void)m;if(!p)return NULL;if(strstr(p,"chinese"))return fopen(font_file,"rb");if(p[0]=='S'&&p[1]==':')p+=2;return fopen(p,"rb");}
static lv_fs_res_t fc(lv_fs_drv_t*d,void*f){(void)d;return fclose(f)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t fr(lv_fs_drv_t*d,void*f,void*p,uint32_t n,uint32_t*r){(void)d;*r=(uint32_t)fread(p,1,n,f);return LV_FS_RES_OK;}
static lv_fs_res_t fs(lv_fs_drv_t*d,void*f,uint32_t n,lv_fs_whence_t w){(void)d;return fseek(f,n,w==LV_FS_SEEK_SET?SEEK_SET:w==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t ft(lv_fs_drv_t*d,void*f,uint32_t*n){(void)d;*n=(uint32_t)ftell(f);return LV_FS_RES_OK;}
static bool down;static int x,y;
static void input(lv_indev_drv_t*d,lv_indev_data_t*p){(void)d;p->state=down?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;p->point=(lv_point_t){x,y};}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5);lv_timer_handler();}}
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){(void)a;(void)c;lv_disp_flush_ready(d);}
static void screenshot(const char*root,const char*name){char p[1024];snprintf(p,sizeof(p),"%s/%s.rgb565",root,name);lv_refr_now(NULL);FILE*f=fopen(p,"wb");assert(f);fwrite(pixels,sizeof(pixels),1,f);fclose(f);}
static void photo_detach_and_release(void);
static void photo_show_load_error(void);
static void photo_open_cb(lv_event_t *e);
static void photo_load_timer_cb(lv_timer_t *timer);
static void photo_close_cb(lv_event_t *e);
static void create_photo_view_screen(void);
@BODY@
static void open_index(int index){lv_obj_t*b=lv_obj_create(photo_screen);lv_obj_add_event_cb(b,photo_open_cb,LV_EVENT_CLICKED,(void*)(intptr_t)index);lv_event_send(b,LV_EVENT_CLICKED,NULL);lv_obj_del(b);}
int main(int argc,char**argv){
 assert(argc==4);font_file=argv[1];snprintf(stream_src,sizeof(stream_src),"S:%s",argv[3]);
 full_dsc.header.cf=LV_IMG_CF_TRUE_COLOR_ALPHA;full_dsc.header.w=32;full_dsc.header.h=32;
 full_dsc.data_size=sizeof(full_pixels);full_dsc.data=full_pixels;memset(full_pixels,0xff,sizeof(full_pixels));
 lv_init();
 lv_disp_draw_buf_t buf;lv_disp_draw_buf_init(&buf,pixels,NULL,480*480);
 lv_disp_drv_t display;lv_disp_drv_init(&display);display.hor_res=480;display.ver_res=480;display.draw_buf=&buf;display.full_refresh=1;display.flush_cb=flush;lv_disp_drv_register(&display);
 lv_indev_drv_t pointer;lv_indev_drv_init(&pointer);pointer.type=LV_INDEV_TYPE_POINTER;pointer.read_cb=input;lv_indev_drv_register(&pointer);
 lv_fs_drv_t fontfs;lv_fs_drv_init(&fontfs);fontfs.letter='S';fontfs.open_cb=fo;fontfs.close_cb=fc;fontfs.read_cb=fr;fontfs.seek_cb=fs;fontfs.tell_cb=ft;lv_fs_drv_register(&fontfs);
 chinese_font_20=lv_font_load("S:/chinese.bin");assert(chinese_font_20);
 main_screen_ref=lv_obj_create(NULL);photo_screen=lv_obj_create(NULL);lv_scr_load(photo_screen);
 create_photo_view_screen();
 open_index(0);advance(80);
 assert(lv_img_get_src(photo_full_img)==&full_dsc&&!lv_obj_has_flag(photo_full_img,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"full");
 unsigned before=releases;photo_close_cb(NULL);assert(releases==before+1);assert(lv_img_get_src(photo_full_img)==NULL);
 open_index(1);advance(80);
 assert(lv_img_get_src(photo_full_img)&&!strcmp(lv_img_get_src(photo_full_img),stream_src)&&!lv_obj_has_flag(photo_full_img,LV_OBJ_FLAG_HIDDEN));screenshot(argv[2],"stream");
 photo_paths[0]="S:/images/missing.bin";open_index(0);advance(80);
 assert(lv_obj_has_flag(photo_full_img,LV_OBJ_FLAG_HIDDEN));
 assert(strstr(lv_label_get_text(photo_loading_label),"找不到"));screenshot(argv[2],"error");
 photo_close_cb(NULL);open_index(0);advance(80);photo_close_cb(NULL);
 for(unsigned i=0;i<30;++i){open_index(1);advance(80);photo_close_cb(NULL);}
 puts("Photo view LVGL: full/stream src, specific error, close detaches then release");
}
'''

if __name__ == '__main__':
    main()
