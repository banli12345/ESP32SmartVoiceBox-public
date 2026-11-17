"""Real LVGL, supplied SD dictionary/font, pointer matching, edge-back and cleanup."""
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/word-match-20261115/host-ui'
SOURCE = r'''
#include "lvgl.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *book_file,*font_file;
static bool font_missing,fail_timer,fail_nvs,saved_valid;
static uint32_t saved_cursor;static unsigned commits,font_allocs,font_frees,book_handles;
int nvs_open(const char*n,int mode,int*h){assert(!strcmp(n,"word_match"));if(fail_nvs)return 2;if(mode==0&&!saved_valid)return 1;*h=1;return 0;}
int nvs_get_u32(int h,const char*k,uint32_t*v){(void)h;assert(!strcmp(k,"cursor"));if(!saved_valid)return 1;*v=saved_cursor;return 0;}
int nvs_set_u32(int h,const char*k,uint32_t v){(void)h;assert(!strcmp(k,"cursor"));saved_cursor=v;saved_valid=true;return 0;}
int nvs_commit(int h){(void)h;commits++;return 0;}void nvs_close(int h){(void)h;}
static FILE*tracked_open(const char*p,const char*m){FILE*f=fopen(p,m);if(f)book_handles++;return f;}
static int tracked_close(FILE*f){assert(book_handles);book_handles--;return fclose(f);}
#define fopen tracked_open
#define fclose tracked_close
#include "@ROOT@/src/WordBook.c"
#undef fopen
#undef fclose
static lv_font_t*owned_load(const char*p){if(font_missing)return NULL;lv_font_t*f=lv_font_load(p);if(f)font_allocs++;return f;}
static void owned_free(lv_font_t*f){assert(f);font_frees++;lv_font_free(f);}
static lv_timer_t*checked_timer(lv_timer_cb_t cb,uint32_t ms,void*u){return fail_timer?NULL:lv_timer_create(cb,ms,u);}
#undef WORD_BOOK_SD_PATH
#define WORD_BOOK_SD_PATH book_file
#define lv_font_load owned_load
#define lv_font_free owned_free
#define lv_timer_create checked_timer
#include "@ROOT@/src/WordMatchUI.c"
#undef lv_font_load
#undef lv_font_free
#undef lv_timer_create
static lv_color_t pixels[480*480];static bool down;static int px,py;
static void flush(lv_disp_drv_t*d,const lv_area_t*a,lv_color_t*c){(void)a;(void)c;lv_disp_flush_ready(d);}
static void input(lv_indev_drv_t*d,lv_indev_data_t*p){(void)d;bool consumed=edge_back_pointer(down,px,py,true);p->state=down&&!consumed?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;p->point=(lv_point_t){px,py};}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5);lv_timer_handler();}}
static void touch(bool v,int x,int y,unsigned ms){down=v;px=x;py=y;advance(ms);}
static void tap(lv_obj_t*o){lv_obj_update_layout(o);lv_area_t a;lv_obj_get_coords(o,&a);int x=(a.x1+a.x2)/2,y=(a.y1+a.y2)/2;touch(true,x,y,70);touch(false,x,y,80);}
static void edge_return(void){touch(true,7,235,80);touch(true,110,235,90);touch(false,110,235,100);advance(250);}
static lv_obj_t*named(lv_obj_t*o,const char*t){if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),t))return lv_obj_get_parent(o);for(unsigned i=0;i<lv_obj_get_child_cnt(o);i++){lv_obj_t*r=named(lv_obj_get_child(o,i),t);if(r)return r;}return NULL;}
static void *fo(lv_fs_drv_t*d,const char*p,lv_fs_mode_t m){(void)d;(void)m;assert(!strcmp(p,"/fonts/cet4_20.bin"));return fopen(font_file,"rb");}
static lv_fs_res_t fc(lv_fs_drv_t*d,void*f){(void)d;return fclose(f)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t fr(lv_fs_drv_t*d,void*f,void*p,uint32_t n,uint32_t*r){(void)d;*r=fread(p,1,n,f);return LV_FS_RES_OK;}
static lv_fs_res_t fs(lv_fs_drv_t*d,void*f,uint32_t n,lv_fs_whence_t w){(void)d;return fseek(f,n,w==LV_FS_SEEK_SET?SEEK_SET:w==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END)?LV_FS_RES_FS_ERR:LV_FS_RES_OK;}
static lv_fs_res_t ft(lv_fs_drv_t*d,void*f,uint32_t*n){(void)d;*n=ftell(f);return LV_FS_RES_OK;}
static bool capture(lv_color_t*dst,size_t n,void*u){(void)u;memcpy(dst,pixels,n*sizeof(*dst));return true;}
static void shot(const char*dir,const char*name){char p[1024];snprintf(p,sizeof(p),"%s/%s.rgb565",dir,name);lv_refr_now(NULL);FILE*f=fopen(p,"wb");assert(f);fwrite(pixels,sizeof(pixels),1,f);fclose(f);}
#define MARK(s) do{puts(s);fflush(stdout);}while(0)
static void finish(word_match_page_t*p){for(int i=0;i<p->pairs;i++)if(!p->en_done[i]){int j=0;while(p->zh_pair[j]!=p->en_pair[i])j++;tap(p->en_btn[i]);tap(p->zh_btn[j]);}}
static void unique(word_match_page_t*p){assert(p&&p->pairs>0&&p->pairs<=5);for(int i=0;i<p->pairs;i++)for(int j=0;j<i;j++){assert(strcmp(p->english[i],p->english[j]));assert(strcmp(p->chinese_text[i],p->chinese_text[j]));}}
static void validate_glyphs(const char*s,const lv_font_t*f){while(*s){unsigned c=(unsigned char)*s++,n=0;if(c>=0xF0){c&=7;n=3;}else if(c>=0xE0){c&=15;n=2;}else if(c>=0xC0){c&=31;n=1;}while(n--)c=(c<<6)|((unsigned char)*s++&63);lv_font_glyph_dsc_t g;assert(lv_font_get_glyph_dsc(f,&g,c,0)&&!g.is_placeholder);}}
int main(int argc,char**argv){
 assert(argc==6);book_file=argv[1];font_file=argv[2];lv_init();lv_disp_draw_buf_t b;lv_disp_draw_buf_init(&b,pixels,NULL,480*480);lv_disp_drv_t d;lv_disp_drv_init(&d);d.hor_res=d.ver_res=480;d.full_refresh=1;d.draw_buf=&b;d.flush_cb=flush;lv_disp_drv_register(&d);
 lv_indev_drv_t i;lv_indev_drv_init(&i);i.type=LV_INDEV_TYPE_POINTER;i.read_cb=input;lv_indev_drv_register(&i);lv_fs_drv_t f;lv_fs_drv_init(&f);f.letter='S';f.open_cb=fo;f.close_cb=fc;f.read_cb=fr;f.seek_cb=fs;f.tell_cb=ft;lv_fs_drv_register(&f);
 lv_font_t*fallback=lv_font_load("S:/fonts/cet4_20.bin");assert(fallback);lv_obj_t*home=lv_obj_create(NULL);lv_scr_load(home);edge_back_init();ui_transition_set_desktop(home);ui_transition_set_capture(capture,NULL);advance(50);
 word_match_ui_open(home,fallback,true);advance(250);word_match_page_t*p=active_page;assert(p&&p->pairs==5&&p->count==4377&&p->cursor==0&&commits==0&&p->nvs_ready);unique(p);shot(argv[3],"word-match");
 lv_obj_t*measure=lv_label_create(home);lv_obj_set_width(measure,196);lv_label_set_long_mode(measure,LV_LABEL_LONG_WRAP);
 for(uint32_t k=0;k<p->count;k++){word_book_entry_t e;assert(word_book_read(&p->book,k,&e));validate_glyphs(e.chinese,p->zh_font);lv_point_t size;lv_txt_get_size(&size,e.chinese,p->zh_font,0,0,196,LV_TEXT_FLAG_NONE);if(size.y>44){fprintf(stderr,"meaning too tall at %u: %d\n",k,size.y);return 2;}lv_txt_get_size(&size,e.english,&lv_font_montserrat_20,0,0,196,LV_TEXT_FLAG_NONE);assert(size.y<=44);}
 lv_obj_del(measure);
 /* Every source entry participates in sequential rounds, including duplicate meanings. */
 uint32_t traversed=0;while(traversed<p->count){p->cursor=traversed;int n=collect_round(p);assert(n>0);p->pairs=n;unique(p);uint32_t next=p->pending_cursor?p->pending_cursor:p->count;assert(next==traversed+(uint32_t)n);traversed=next;}p->cursor=0;fill_screen(p);advance(50);
 tap(p->en_btn[0]);assert(p->en_sel==0);tap(p->en_btn[0]);assert(p->en_sel==-1);tap(p->en_btn[1]);tap(p->en_btn[0]);assert(p->en_sel==0);
 int wrong=0;while(p->zh_pair[wrong]==p->en_pair[0])wrong++;tap(p->zh_btn[wrong]);assert(p->lock&&p->flash&&!p->matched);shot(argv[3],"word-match-wrong");tap(p->en_btn[2]);assert(p->en_sel==0);advance(700);assert(!p->lock&&p->en_sel==-1&&p->zh_sel==-1);
 int right=0;while(p->zh_pair[right]!=p->en_pair[0])right++;tap(p->zh_btn[right]);tap(p->en_btn[0]);assert(p->matched==1&&lv_obj_has_state(p->en_btn[0],LV_STATE_DISABLED));tap(p->en_btn[0]);assert(p->matched==1);shot(argv[3],"word-match-correct");finish(p);assert(p->round_complete&&p->matched==p->pairs&&commits==1&&saved_valid&&saved_cursor==5);shot(argv[3],"word-match-done");
 MARK("after complete, next group");tap(named(p->screen,"换一组"));MARK("next group ready");assert(p->cursor==5&&!p->matched&&commits==1);int j=0;while(p->zh_pair[j]==p->en_pair[0])j++;tap(p->en_btn[0]);tap(p->zh_btn[j]);assert(p->lock);MARK("before first edge return");edge_return();MARK("after first edge return");assert(!active_page&&lv_scr_act()==home&&!book_handles&&font_allocs==font_frees);
 MARK("reopen resumed group");word_match_ui_open(home,fallback,true);advance(250);p=active_page;assert(p->cursor==5&&commits==1);fail_timer=true;j=0;while(p->zh_pair[j]==p->en_pair[0])j++;tap(p->en_btn[0]);tap(p->zh_btn[j]);assert(!p->lock&&!p->flash);fail_timer=false;fail_nvs=true;MARK("before failed-save finish");finish(p);MARK("after failed-save finish");assert(p->round_complete&&commits==1&&p->note);MARK("before note expiry");advance(1500);MARK("after note expiry");fail_nvs=false;edge_return();MARK("after second return");
 font_missing=true;word_match_ui_open(home,fallback,true);advance(250);assert(!active_page->pairs&&named(active_page->screen,"重试"));shot(argv[3],"word-match-font-missing");font_missing=false;tap(named(active_page->screen,"重试"));assert(active_page->pairs==5);edge_return();
 book_file="missing.bwv";word_match_ui_open(home,fallback,true);advance(250);assert(!active_page->pairs);book_file=argv[1];tap(named(active_page->screen,"重试"));assert(active_page->pairs==5);edge_return();
 /* External unload, reopen in the same turn, then delete the queued old page. */
 word_match_ui_open(home,fallback,true);advance(250);lv_obj_t*old=active_page->screen;lv_scr_load(home);assert(!active_page);word_match_ui_open(home,fallback,true);lv_obj_del(old);advance(250);assert(active_page&&lv_scr_act()==active_page->screen);edge_return();
 /* Small and duplicate-meaning decks have fewer cards but no lost words. */
 for(int a=4;a<6;a++){saved_valid=false;book_file=argv[a];word_match_ui_open(home,fallback,true);advance(250);p=active_page;assert(p->pairs==(a==4?1:2));unique(p);finish(p);assert(p->round_complete);edge_return();}book_file=argv[1];saved_valid=false;
 lv_mem_monitor_t before,after;lv_mem_monitor(&before);for(unsigned k=0;k<20;k++){word_match_ui_open(home,fallback,true);advance(200);p=active_page;if(k%2){int z=0;while(p->zh_pair[z]==p->en_pair[0])z++;tap(p->en_btn[0]);tap(p->zh_btn[z]);}edge_return();assert(!active_page&&!book_handles&&font_allocs==font_frees&&lv_mem_test()==LV_RES_OK);}lv_mem_monitor(&after);assert(after.free_size+256>=before.free_size);lv_font_free(fallback);
 puts("Word match UI PASS: 4377 glyphs/layout/source coverage, shuffled match/retry/complete, NVS resume/failure, missing files, timer fault, queued external delete/reopen, small decks, real side-back/transition and 20 cleanup cycles");return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT/'include/lv_conf.h').read_text(encoding='utf-8').replace('#define LV_TICK_CUSTOM 1','#define LV_TICK_CUSTOM 0').replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>','#define LV_MEM_POOL_INCLUDE <stdlib.h>').replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)','malloc(size)')
    (OUT/'lv_conf.h').write_text(config, encoding='utf-8')
    (OUT/'nvs.h').write_text('#pragma once\n#include <stdint.h>\ntypedef int nvs_handle_t;typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NVS_NOT_FOUND 1\n#define NVS_READONLY 0\n#define NVS_READWRITE 1\nint nvs_open(const char*,int,int*);int nvs_get_u32(int,const char*,uint32_t*);int nvs_set_u32(int,const char*,uint32_t);int nvs_commit(int);void nvs_close(int);\n')
    unit=OUT/'test.c';unit.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=OUT/'word-match.exe'
    command=[str(ROOT/'.tools/host-tcc/tcc/tcc.exe'),'-DLV_CONF_INCLUDE_SIMPLE','-DLV_KCONFIG_IGNORE','-I'+str(OUT),'-I'+str(ROOT/'src'),'-I'+str(ROOT/'components/lvgl'),str(unit),str(ROOT/'src/EdgeBack.c'),str(ROOT/'src/UiTransition.c'),*[str(p)for p in(ROOT/'components/lvgl/src').rglob('*.c')],'-o',str(exe)]
    r=subprocess.run(command,capture_output=True);assert not r.returncode,r.stderr.decode(errors='replace')
    import sys
    sys.path.insert(0,str(ROOT/'scripts'))
    from convert_word_book import encode_book
    (OUT/'one.bwv').write_bytes(encode_book([('hello','你好')]))
    (OUT/'duplicates.bwv').write_bytes(encode_book([('one','一'),('two','二'),('second','二'),('three','三')]))
    subprocess.run([str(exe),str(ROOT/'sd_card_files/words/cet4.bwv'),str(ROOT/'sd_card_files/fonts/cet4_20.bin'),str(OUT),str(OUT/'one.bwv'),str(OUT/'duplicates.bwv')],check=True,timeout=90)
    for file in OUT.glob('*.rgb565'):
        raw=file.read_bytes();rgb=bytearray()
        for i in range(0,len(raw),2):
            c=raw[i]|raw[i+1]<<8;rgb.extend((((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31))
        Image.frombytes('RGB',(480,480),bytes(rgb)).save(file.with_suffix('.png'))


if __name__=='__main__':
    main()
