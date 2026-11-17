"""Real LVGL check for the shared phone QWERTY keyboard and CN/EN slot.

No network. Builds with the repo host tcc and Python 3.13.
"""
from pathlib import Path
import subprocess
from PIL import Image
from music_playlist_ui_host_test import HARNESS as DRIVER

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / '.tools/mobile-keyboard-20261117/host'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    config = (ROOT / 'include/lv_conf.h').read_text(encoding='utf-8')
    config = config.replace('#define LV_TICK_CUSTOM 1', '#define LV_TICK_CUSTOM 0')
    config = config.replace('#define LV_MEM_POOL_INCLUDE <esp_heap_caps.h>', '#define LV_MEM_POOL_INCLUDE <stdlib.h>')
    config = config.replace('heap_caps_malloc((size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)', 'malloc(size)')
    (OUT / 'lv_conf.h').write_text(config, encoding='utf-8')
    driver = DRIVER[DRIVER.index('static bool down;'):DRIVER.index('int main(')]
    init = DRIVER[DRIVER.index(' assert(argc==3);'):DRIVER.index(' for(int i=0;i<12;')]
    (OUT / 'test.c').write_text(HARNESS.replace('@DRIVER@', driver).replace('@INIT@', init), encoding='utf-8')
    exe = OUT / 'mobile-keyboard.exe'
    command = [str(ROOT / '.tools/host-tcc/tcc/tcc.exe'), '-DLV_CONF_INCLUDE_SIMPLE', '-DLV_KCONFIG_IGNORE',
               '-I' + str(OUT), '-I' + str(ROOT / 'components/lvgl'),
               str(OUT / 'test.c'),
               *[str(p) for p in (ROOT / 'components/lvgl/src').rglob('*.c')], '-o', str(exe)]
    result = subprocess.run(command, capture_output=True)
    if result.returncode:
        print(result.stderr.decode(errors='replace'))
        raise SystemExit(result.returncode)
    subprocess.run([str(exe), str(ROOT / 'sd_card_files/fonts/chinese_20.bin'), str(OUT)], check=True, timeout=60)
    for file in OUT.glob('*.rgb565'):
        raw = file.read_bytes()
        rgb = bytearray()
        for i in range(0, len(raw), 2):
            c = raw[i] | raw[i + 1] << 8
            rgb.extend((((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31))
        Image.frombytes('RGB', (480, 480), bytes(rgb)).save(file.with_suffix('.png'))


HARNESS = r'''
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static lv_color_t pixels[480*480];
static lv_font_t *chinese_font_20;
@DRIVER@
static lv_obj_t *screen, *ta, *kb, *ime, *panel;
static unsigned submits;
static bool accepted;

static void ready_cb(lv_event_t *e){(void)e; if(accepted) return; accepted=true; submits++;}

static int find_key(lv_obj_t *obj, const char *text){
    if(!strcmp(text," ")) text="Space";
    for(int i=0;i<80;i++){const char *s=lv_btnmatrix_get_btn_text(obj,i); if(!s) return -1; if(!strcmp(s,text)) return i;}
    return -1;
}
static void key(lv_obj_t *obj, const char *text){
    int id=find_key(obj,text); assert(id>=0);
    lv_btnmatrix_set_selected_btn(obj,(uint16_t)id);
    lv_event_send(obj,LV_EVENT_VALUE_CHANGED,NULL);
}
static lv_area_t abs_btn(lv_obj_t *obj, int id){
    lv_btnmatrix_t *b=(lv_btnmatrix_t*)obj; lv_area_t k; lv_obj_get_coords(obj,&k); lv_area_t a=b->button_areas[id];
    a.x1+=k.x1; a.x2+=k.x1; a.y1+=k.y1; a.y2+=k.y1; return a;
}
static bool hidden_btn(lv_obj_t *obj, int id){return (((lv_btnmatrix_t*)obj)->ctrl_bits[id] & LV_BTNMATRIX_CTRL_HIDDEN)!=0;}
static int btn_count(lv_obj_t *obj){int n=0; while(lv_btnmatrix_get_btn_text(obj,(uint16_t)n)) n++; return n;}
static bool hit(const lv_area_t *a, const lv_area_t *b){return a->x1<=b->x2 && b->x1<=a->x2 && a->y1<=b->y2 && b->y1<=a->y2;}
static void tap_btn(lv_obj_t *obj, int id){lv_area_t a=abs_btn(obj,id); tap((a.x1+a.x2)/2,(a.y1+a.y2)/2);}

static void check_letters(lv_obj_t *obj, int min_h){
    lv_obj_update_layout(obj);
    int n=btn_count(obj), rows[4][16], row_n[4]={0}, rc=0, i;
    assert(n<=64);
    for(i=0;i<n;i++){
        if(hidden_btn(obj,i)) continue;
        lv_area_t a=abs_btn(obj,i);
        for(int j=0;j<i;j++){ if(hidden_btn(obj,j)) continue; lv_area_t b=abs_btn(obj,j); assert(!hit(&a,&b)); }
        int placed=0;
        for(int r=0;r<rc;r++){ lv_area_t first=abs_btn(obj,rows[r][0]); if(LV_ABS(a.y1-first.y1)<4){ rows[r][row_n[r]++]=i; placed=1; break; } }
        if(!placed){ assert(rc<4); rows[rc][0]=i; row_n[rc++]=1; }
    }
    assert(rc==4);
    const char *top[]={"q","w","e","r","t","y","u","i","o","p"};
    const char *mid[]={"a","s","d","f","g","h","j","k","l"};
    const char *low[]={"ABC","z","x","c","v","b","n","m"};
    assert(row_n[0]==10 && row_n[1]==9 && row_n[2]==9 && row_n[3]==7);
    for(i=0;i<10;i++) assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[0][i]), top[i]));
    for(i=0;i<9;i++) assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[1][i]), mid[i]));
    for(i=0;i<8;i++) assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[2][i]), low[i]));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[2][8]), LV_SYMBOL_BACKSPACE));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][0]), "1#"));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][1]), "123"));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][2]), ","));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][3]), "Space"));
    assert(!hidden_btn(obj,rows[3][3]) && !lv_btnmatrix_has_btn_ctrl(obj,rows[3][3],LV_BTNMATRIX_CTRL_DISABLED));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][4]), "."));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][5]), "CN"));
    assert(!strcmp(lv_btnmatrix_get_btn_text(obj,rows[3][6]), LV_SYMBOL_OK));
    lv_area_t q=abs_btn(obj,rows[0][0]), p=abs_btn(obj,rows[0][9]), a=abs_btn(obj,rows[1][0]), l=abs_btn(obj,rows[1][8]);
    lv_area_t space=abs_btn(obj,rows[3][3]), back=abs_btn(obj,rows[2][8]), cn=abs_btn(obj,rows[3][5]);
    int qw=lv_area_get_width(&q), qh=lv_area_get_height(&q);
    printf("letter %dx%d keyboard %d\n", qw, qh, lv_obj_get_height(obj));
    assert(qw>=43 && qw<=46 && qh>=min_h);
    assert(lv_area_get_width(&a)>=43 && lv_area_get_width(&a)<=46);
    assert(a.x1>q.x1+8 && l.x2<p.x2-8);
    assert(lv_area_get_width(&space)>qw*2 && lv_area_get_width(&back)>qw);
    if(ime && lv_keyboard_get_textarea(obj)==ta && !lv_textarea_get_password_mode(ta)){
        lv_obj_t *lang=((lv_ime_pinyin_t*)ime)->language_button;
        assert(lang && !lv_obj_has_flag(lang, LV_OBJ_FLAG_HIDDEN));
        lv_area_t la; lv_obj_get_coords(lang,&la);
        assert(LV_ABS(la.x1-cn.x1)<=2 && LV_ABS(la.y1-cn.y1)<=2);
        assert(LV_ABS(lv_area_get_width(&la)-lv_area_get_width(&cn))<=2);
        assert(LV_ABS(lv_area_get_height(&la)-lv_area_get_height(&cn))<=2);
        assert(!hit(&la,&space) && !hit(&la,&q) && !hit(&la,&a));
    }
}

static void paint(void){
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xE7F6EE), 0);
    lv_obj_set_style_bg_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xD7EBDF), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0x285843), LV_PART_ITEMS);
}

int main(int argc, char **argv){
@INIT@
    screen=lv_obj_create(NULL); lv_obj_set_style_pad_all(screen,0,0); lv_obj_set_style_bg_color(screen,lv_color_hex(0xF1F8F3),0); lv_scr_load(screen);
    ta=lv_textarea_create(screen); lv_obj_set_pos(ta,16,188); lv_obj_set_size(ta,448,48); lv_textarea_set_one_line(ta,true);
    lv_obj_set_style_text_font(ta, chinese_font_20, 0);
    kb=lv_keyboard_create(screen); lv_obj_set_align(kb,LV_ALIGN_TOP_LEFT); lv_obj_set_pos(kb,0,288); lv_obj_set_size(kb,480,192); paint();
    lv_keyboard_set_textarea(kb, ta);
    ime=lv_ime_pinyin_create(screen); lv_obj_set_size(ime,1,1); lv_obj_clear_flag(ime, LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_pinyin_dict_t dict[]={{"ni","你呢泥拟逆妮尼倪"},{"hao","好号"},{NULL,NULL}};
    lv_ime_pinyin_set_dict(ime, dict); lv_ime_pinyin_set_keyboard(ime, kb);
    panel=lv_ime_pinyin_get_cand_panel(ime); lv_obj_set_parent(panel, screen); lv_obj_set_align(panel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(panel,0,248); lv_obj_set_size(panel,480,36); lv_obj_set_style_text_font(panel, chinese_font_20, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, ready_cb, LV_EVENT_READY, NULL); lv_obj_add_event_cb(ta, ready_cb, LV_EVENT_READY, NULL);
    advance(40); check_letters(kb, 40); screenshot(argv[2], "letters-192");

    tap_btn(kb, find_key(kb,"q")); assert(!strcmp(lv_textarea_get_text(ta),"q"));
    tap_btn(kb, find_key(kb, LV_SYMBOL_BACKSPACE)); assert(!lv_textarea_get_text(ta)[0]);

    key(kb,"n"); key(kb,"i"); assert(!lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_btnmatrix_get_btn_text(panel,1),"你"));
    lv_btnmatrix_set_selected_btn(panel, LV_IME_PINYIN_CAND_TEXT_NUM+2); lv_event_send(panel, LV_EVENT_VALUE_CHANGED, NULL);
    assert(((lv_ime_pinyin_t*)ime)->expanded); screenshot(argv[2], "pinyin-expanded");
    lv_btnmatrix_set_selected_btn(panel, 3); lv_event_send(panel, LV_EVENT_VALUE_CHANGED, NULL);advance(20);
    assert(!((lv_ime_pinyin_t*)ime)->expanded && lv_obj_get_height(panel)==36);
    lv_btnmatrix_set_selected_btn(panel, 1); lv_event_send(panel, LV_EVENT_VALUE_CHANGED, NULL);
    assert(!strcmp(lv_textarea_get_text(ta),"你") && !((lv_ime_pinyin_t*)ime)->ta_count);

    key(kb,"n"); key(kb,"i");
    lv_obj_t *lang=((lv_ime_pinyin_t*)ime)->language_button; assert(!strcmp(lv_label_get_text(lv_obj_get_child(lang,0)),"CN"));
    lv_area_t before; lv_obj_get_coords(lang,&before); tap((before.x1+before.x2)/2,(before.y1+before.y2)/2);
    assert(!lv_ime_pinyin_get_enabled(ime) && !strcmp(lv_textarea_get_text(ta),"你ni"));
    assert(lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN) && !strcmp(lv_label_get_text(lv_obj_get_child(lang,0)),"EN"));
    tap_btn(kb,find_key(kb,"Space")); key(kb,"h"); key(kb,"i"); assert(!strcmp(lv_textarea_get_text(ta),"你ni hi") && !((lv_ime_pinyin_t*)ime)->ta_count);
    bool english=!lv_ime_pinyin_get_enabled(ime);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER); lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    assert(lv_ime_pinyin_get_enabled(ime)==!english && !strcmp(lv_textarea_get_text(ta),"你ni hi"));
    tap((before.x1+before.x2)/2,(before.y1+before.y2)/2);
    assert(lv_ime_pinyin_get_enabled(ime) && !strcmp(lv_label_get_text(lv_obj_get_child(lang,0)),"CN"));
    screenshot(argv[2], "english");

    lv_textarea_set_text(ta,""); lv_ime_pinyin_clear(ime);
    key(kb,"ABC"); key(kb,"N"); assert(!strcmp(lv_textarea_get_text(ta),"N") && ((lv_ime_pinyin_t*)ime)->ta_count==1);
    lv_btnmatrix_set_selected_btn(panel,1); lv_event_send(panel, LV_EVENT_VALUE_CHANGED, NULL); assert(!strcmp(lv_textarea_get_text(ta),"你"));
    key(kb,"abc");

    lv_textarea_set_text(ta,"");lv_ime_pinyin_clear(ime);key(kb,"n");key(kb,"i");
    tap_btn(kb,find_key(kb,"Space"));
    assert(!strcmp(lv_textarea_get_text(ta),"ni ")&&!((lv_ime_pinyin_t*)ime)->ta_count&&lv_obj_has_flag(panel,LV_OBJ_FLAG_HIDDEN));

    lv_textarea_set_text(ta,""); lv_ime_pinyin_clear(ime);
    key(kb,"1#"); assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_SPECIAL); key(kb,"!"); key(kb,"@"); key(kb,"/"); key(kb,"_");
    key(kb,"123"); assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_USER_2); key(kb,":"); key(kb,"$"); key(kb,"("); key(kb,"\"");
    key(kb,"abc"); assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_TEXT_LOWER);
    assert(!strcmp(lv_textarea_get_text(ta), "!@/_:$(\""));
    screenshot(argv[2], "symbols");

    /* Legacy number map stays 4 columns, not the phone digit page. */
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    assert(find_key(kb,"q")<0 && find_key(kb,"1")>=0 && find_key(kb,LV_SYMBOL_KEYBOARD)>=0);
    int cols=0, y0=-1, count=btn_count(kb);
    for(int i=0;i<count;i++){ lv_area_t a=abs_btn(kb,i); if(y0<0) y0=a.y1; if(LV_ABS(a.y1-y0)<4) cols++; }
    assert(cols==4 && lv_obj_has_flag(lang, LV_OBJ_FLAG_HIDDEN));
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); assert(!lv_obj_has_flag(lang, LV_OBJ_FLAG_HIDDEN));

    /* URL and password keyboards have no IME. The CN slot still changes case. */
    lv_obj_t *url_ta=lv_textarea_create(screen); lv_obj_add_flag(url_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *url=lv_keyboard_create(screen); lv_obj_set_size(url,480,192); lv_keyboard_set_textarea(url, url_ta);
    assert(((lv_ime_pinyin_t*)ime)->kb==kb);
    key(url,"h"); key(url,"ABC"); key(url,"W"); key(url,"abc"); key(url,"1#"); key(url,"123"); key(url,":"); key(url,"1#"); key(url,"/");
    assert(!strcmp(lv_textarea_get_text(url_ta),"hW:/"));
    lv_textarea_set_password_mode(ta, true); lv_event_send(kb, LV_EVENT_REFRESH, NULL);
    assert(lv_obj_has_flag(lang, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(ta,""); key(kb,"a"); key(kb,"CN"); key(kb,"B"); assert(!strcmp(lv_textarea_get_text(ta),"aB"));
    assert(!((lv_ime_pinyin_t*)ime)->ta_count && lv_obj_has_flag(panel, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_password_mode(ta, false); lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER); lv_ime_pinyin_set_enabled(ime, true);

    int h192;
    { lv_area_t q=abs_btn(kb, find_key(kb,"q")); h192=lv_area_get_height(&q); }
    lv_obj_set_height(kb, 220); lv_obj_set_pos(kb,0,260); advance(30); check_letters(kb, 40);
    lv_area_t q220=abs_btn(kb, find_key(kb,"q")); assert(lv_area_get_height(&q220)>h192);
    lv_area_t lang220; lv_obj_get_coords(lang,&lang220); assert(lv_area_get_height(&lang220)>h192);
    screenshot(argv[2], "letters-220");
    lv_obj_set_size(kb,480,192); lv_obj_set_pos(kb,0,288); advance(20);

    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_obj_t *other=lv_keyboard_create(screen); lv_obj_set_size(other,480,192); lv_obj_t *other_ta=lv_textarea_create(screen);
    lv_keyboard_set_textarea(other, other_ta);
    assert(lv_keyboard_get_mode(other)==LV_KEYBOARD_MODE_TEXT_LOWER);
    assert(lv_keyboard_get_mode(kb)==LV_KEYBOARD_MODE_TEXT_UPPER);
    assert(find_key(kb,"Q")>=0 && find_key(other,"Q")<0 && find_key(other,"q")>=0);
    key(other,"z"); assert(!strcmp(lv_textarea_get_text(other_ta),"z") && !strcmp(lv_textarea_get_text(ta),"aB"));
    lv_obj_del(kb); kb=NULL; assert(((lv_ime_pinyin_t*)ime)->kb==NULL);
    key(other,"z"); assert(!strcmp(lv_textarea_get_text(other_ta),"zz"));
    lv_obj_t *third=lv_keyboard_create(screen); assert(find_key(third,"q")>=0 && find_key(other,"q")>=0);
    assert(lv_btnmatrix_get_map(third)==lv_btnmatrix_get_map(other));
    lv_obj_del(third); lv_obj_del(other); lv_obj_del(screen);
    assert(submits==0); /* OK not pressed yet on a live guard; press a fresh keyboard */
    screen=lv_obj_create(NULL); lv_scr_load(screen); kb=lv_keyboard_create(screen); ta=lv_textarea_create(screen);
    lv_keyboard_set_textarea(kb, ta); lv_obj_add_event_cb(kb, ready_cb, LV_EVENT_READY, NULL); lv_obj_add_event_cb(ta, ready_cb, LV_EVENT_READY, NULL);
    /* Real pointer taps, including all four phone maps and password input.
     * Programmatically dispatching a hidden key would miss the original fault. */
    lv_obj_set_style_pad_all(screen,0,0);lv_obj_set_pos(ta,16,188);lv_obj_set_size(ta,448,48);
    lv_obj_set_size(kb,480,192);paint();
    const lv_keyboard_mode_t modes[]={LV_KEYBOARD_MODE_TEXT_LOWER,LV_KEYBOARD_MODE_TEXT_UPPER,LV_KEYBOARD_MODE_SPECIAL,LV_KEYBOARD_MODE_USER_2};
    for(unsigned mode=0;mode<4;mode++)for(unsigned password=0;password<2;password++){
        lv_keyboard_set_mode(kb,modes[mode]);lv_textarea_set_password_mode(ta,password);lv_textarea_set_text(ta,"A");advance(40);
        int space=find_key(kb,"Space");assert(space>=0&&!hidden_btn(kb,space)&&!lv_btnmatrix_has_btn_ctrl(kb,space,LV_BTNMATRIX_CTRL_DISABLED));
        lv_area_t a=abs_btn(kb,space);assert(lv_area_get_width(&a)>=100&&a.y1>=288&&a.y2<480);
        tap_btn(kb,space);assert(!strcmp(lv_textarea_get_text(ta),"A "));
        tap_btn(kb,space);assert(!strcmp(lv_textarea_get_text(ta),"A  "));
        assert(!strstr(lv_textarea_get_text(ta),"Space"));
    }
    lv_textarea_set_password_mode(ta,false);lv_keyboard_set_mode(kb,LV_KEYBOARD_MODE_TEXT_LOWER);lv_textarea_set_text(ta,"");
    screenshot(argv[2],"visible-space");
    key(kb,"a"); key(kb, LV_SYMBOL_OK); assert(submits==1 && !strcmp(lv_textarea_get_text(ta),"a"));
    key(kb, LV_SYMBOL_OK); assert(submits==1);
    assert(lv_mem_test()==LV_RES_OK);
    puts("mobile keyboard PASS");
    return 0;
}
'''

if __name__ == '__main__':
    main()
