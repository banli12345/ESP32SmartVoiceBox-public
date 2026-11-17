#include "MediaActions.h"
#include "SDcar.h"
#include "UITheme.h"
#include <stdio.h>
#include <string.h>

typedef struct {media_kind_t kind;char path[96];lv_obj_t *owner;void (*changed)(void);} MediaRow;
static MediaRow selected;
static lv_obj_t *modal,*input,*keyboard,*ime,*candidate_panel,*message,*save_button,*cancel_button;
static lv_timer_t *status_timer;
static bool working;
static const lv_font_t *font(void){return chinese_font_20?chinese_font_20:&lv_font_montserrat_20;}
static void style_text(lv_obj_t *obj,uint32_t color){lv_obj_set_style_text_font(obj,font(),0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);}
static lv_pinyin_dict_t fallback_dict[]={{"ming","名明"},{"pian","片"},{"shi","视时"},{"tu","图"},{"xin","新心"},{NULL,NULL}};
static void close_modal(void){
 if(ime){lv_obj_del(ime);ime=NULL;} /* IME owns the keyboard and candidate panel; delete it before the parent. */
 if(modal)lv_obj_del(modal);
 modal=input=keyboard=candidate_panel=message=save_button=cancel_button=NULL;
}
static void keyboard_show(lv_event_t *event){
 (void)event;if(keyboard&&!working){lv_obj_clear_flag(keyboard,LV_OBJ_FLAG_HIDDEN);if(ime)lv_ime_pinyin_refresh(ime);}
}
static void keyboard_hide(void){if(ime)lv_ime_pinyin_clear(ime);if(keyboard)lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);if(candidate_panel)lv_obj_add_flag(candidate_panel,LV_OBJ_FLAG_HIDDEN);}
bool media_actions_back(lv_obj_t *screen){
 if(!modal||selected.owner!=screen)return false;
 if(working)return true;
 if(keyboard&&!lv_obj_has_flag(keyboard,LV_OBJ_FLAG_HIDDEN))keyboard_hide();else close_modal();return true;
}
static void cancel(lv_event_t *event){(void)event;if(!working)close_modal();}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int width){
 lv_obj_t *l=lv_label_create(parent);lv_label_set_text(l,text);style_text(l,UI_COLOR_TEXT);lv_obj_set_pos(l,x,y);lv_obj_set_width(l,width);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);return l;
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,lv_event_cb_t cb,bool danger){
 lv_obj_t *b=lv_btn_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,50);lv_obj_set_style_radius(b,14,0);
 lv_obj_set_style_bg_color(b,lv_color_hex(danger?UI_COLOR_ERROR:UI_COLOR_ACCENT),0);
 lv_obj_add_event_cb(b,cb,LV_EVENT_SHORT_CLICKED,NULL);lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);style_text(l,UI_COLOR_ON_ACCENT);lv_obj_center(l);return b;
}
static void create_modal(void){
 close_modal();modal=lv_obj_create(selected.owner);lv_obj_set_pos(modal,0,0);lv_obj_set_size(modal,480,480);
 lv_obj_set_style_pad_all(modal,0,0);lv_obj_set_style_radius(modal,0,0);lv_obj_set_style_border_width(modal,0,0);
 lv_obj_clear_flag(modal,LV_OBJ_FLAG_SCROLLABLE);ui_theme_apply_background(modal);lv_obj_move_foreground(modal);
}
static void poll(lv_timer_t *timer){
 (void)timer;if(!working)return;media_file_status_t state;media_files_get_status(&state);if(state.state==1)return;
 working=false;void(*changed)(void)=selected.changed;
 if(state.state==2){close_modal();if(changed)changed();return;}
 if(changed)changed();
 if(message)lv_label_set_text(message,state.message);
 if(save_button)lv_obj_clear_state(save_button,LV_STATE_DISABLED);
 if(cancel_button)lv_obj_clear_state(cancel_button,LV_STATE_DISABLED);
 if(input)lv_obj_clear_state(input,LV_STATE_DISABLED);
}
static void submit(bool remove){
 const char *name=remove?NULL:lv_textarea_get_text(input);
 if(!media_files_request(selected.kind,selected.path,name,remove)){lv_label_set_text(message,"文件正在处理或名称无效，请稍后重试");return;}
 working=true;keyboard_hide();
 if(input)lv_obj_add_state(input,LV_STATE_DISABLED);
 lv_obj_add_state(save_button,LV_STATE_DISABLED);lv_obj_add_state(cancel_button,LV_STATE_DISABLED);
 lv_label_set_text(message,remove?"正在删除文件…":"正在重命名…");
 if(!status_timer)status_timer=lv_timer_create(poll,100,NULL);
}
static void save(lv_event_t *event){(void)event;submit(false);}
static void erase(lv_event_t *event){(void)event;submit(true);}
static void keyboard_event(lv_event_t *event){
 if(lv_event_get_code(event)==LV_EVENT_READY||lv_event_get_code(event)==LV_EVENT_CANCEL)keyboard_hide();
}
static void show_rename(lv_event_t *event){
 (void)event;if(working)return;create_modal();
 label(modal,selected.kind==MEDIA_PHOTO?"重命名照片":"重命名视频",24,20,432);
 label(modal,"只修改名称，文件格式自动保留",24,62,432);
 input=lv_textarea_create(modal);lv_obj_set_pos(input,24,96);lv_obj_set_size(input,432,52);
 style_text(input,UI_COLOR_TEXT);lv_textarea_set_one_line(input,true);lv_textarea_set_max_length(input,80);
 const char *leaf=strrchr(selected.path,'/');char name[96];snprintf(name,sizeof(name),"%s",leaf?leaf+1:selected.path);char *dot=strrchr(name,'.');if(dot)*dot=0;
 lv_textarea_set_text(input,name);lv_textarea_set_cursor_pos(input,LV_TEXTAREA_CURSOR_LAST);
 message=label(modal,"",24,158,432);
 cancel_button=button(modal,"取消",24,220,204,cancel,false);save_button=button(modal,"保存名称",252,220,204,save,false);
 keyboard=lv_keyboard_create(modal);lv_obj_set_align(keyboard,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(keyboard,0,316);lv_obj_set_size(keyboard,480,164);
 lv_keyboard_set_textarea(keyboard,input);lv_obj_set_style_bg_color(keyboard,lv_color_hex(UI_COLOR_PANEL),0);
 lv_obj_set_style_bg_color(keyboard,lv_color_hex(UI_COLOR_SURFACE),LV_PART_ITEMS);lv_obj_set_style_text_color(keyboard,lv_color_hex(UI_COLOR_TEXT),LV_PART_ITEMS);
 ime=lv_ime_pinyin_create(modal);lv_ime_pinyin_set_keyboard(ime,keyboard);
 lv_pinyin_dict_t *dict=get_sd_pinyin_dict();lv_ime_pinyin_set_dict(ime,dict?dict:fallback_dict);lv_ime_pinyin_set_mode(ime,LV_IME_PINYIN_MODE_K26);
 lv_obj_clear_flag(ime,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_GESTURE_BUBBLE);lv_obj_set_size(ime,1,1);
 candidate_panel=lv_ime_pinyin_get_cand_panel(ime);
 if(candidate_panel){lv_obj_set_parent(candidate_panel,modal);lv_obj_set_size(candidate_panel,480,34);lv_obj_set_align(candidate_panel,LV_ALIGN_TOP_LEFT);lv_obj_set_pos(candidate_panel,0,282);lv_obj_set_style_text_font(candidate_panel,font(),LV_PART_ITEMS);lv_obj_set_style_bg_color(candidate_panel,lv_color_hex(UI_COLOR_SURFACE),0);}
 lv_obj_add_event_cb(input,keyboard_show,LV_EVENT_CLICKED,NULL);lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_READY,NULL);lv_obj_add_event_cb(keyboard,keyboard_event,LV_EVENT_CANCEL,NULL);
}
static void show_delete(lv_event_t *event){
 (void)event;if(working)return;create_modal();label(modal,selected.kind==MEDIA_PHOTO?"删除这张照片？":"删除这个视频？",24,66,432);
 const char*leaf=strrchr(selected.path,'/');label(modal,leaf?leaf+1:selected.path,24,122,432);
 message=label(modal,selected.kind==MEDIA_VIDEO?"将删除视频及已有的同名配套音频。":"将从 SD 卡删除这张照片。",24,200,432);
 cancel_button=button(modal,"取消",24,310,204,cancel,false);save_button=button(modal,"删除文件",252,310,204,erase,true);
}
static void show_menu(lv_event_t *event){
 if(working)return;
 MediaRow*r=lv_event_get_user_data(event);selected=*r;create_modal();
 label(modal,"文件操作",24,60,432);const char*leaf=strrchr(selected.path,'/');label(modal,leaf?leaf+1:selected.path,24,112,432);
 button(modal,"重命名",24,188,432,show_rename,false);button(modal,"删除文件",24,254,432,show_delete,true);button(modal,"取消",24,330,432,cancel,false);
}
static void row_event(lv_event_t *event){
 MediaRow*r=lv_event_get_user_data(event);
 if(lv_event_get_code(event)==LV_EVENT_DELETE){lv_mem_free(r);return;}
 if(lv_event_get_code(event)==LV_EVENT_LONG_PRESSED&&!working){selected=*r;show_rename(NULL);}
}
static void dots(lv_event_t *event){
 lv_obj_t *obj=lv_event_get_target(event);lv_area_t area;lv_obj_get_coords(obj,&area);
 lv_draw_rect_dsc_t dsc;lv_draw_rect_dsc_init(&dsc);dsc.bg_color=lv_color_hex(UI_COLOR_TEXT_MUTED);dsc.bg_opa=LV_OPA_COVER;dsc.radius=LV_RADIUS_CIRCLE;
 int x=(area.x1+area.x2)/2,y=(area.y1+area.y2)/2;
 for(int i=-1;i<=1;i++){lv_area_t dot={x-2,y+i*8-2,x+2,y+i*8+2};lv_draw_rect(lv_event_get_draw_ctx(event),&dsc,&dot);}
}
lv_obj_t *media_actions_row(lv_obj_t *gallery,int index,media_kind_t kind,const char*path,const char*name,lv_event_cb_t open,void(*changed)(void)){
 MediaRow*r=lv_mem_alloc(sizeof(*r));if(!r)return NULL;r->kind=kind;r->owner=lv_obj_get_screen(gallery);r->changed=changed;snprintf(r->path,sizeof(r->path),"%s",path?path:"");
 lv_obj_t *row=lv_btn_create(gallery);lv_obj_set_size(row,448,60);lv_obj_set_pos(row,16,8+index*68);
 lv_obj_set_style_bg_color(row,lv_color_hex(UI_COLOR_SURFACE),0);lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_radius(row,12,0);lv_obj_set_style_pad_hor(row,14,0);
 lv_obj_add_event_cb(row,row_event,LV_EVENT_ALL,r);lv_obj_add_event_cb(row,open,LV_EVENT_SHORT_CLICKED,(void*)(intptr_t)index);
 lv_obj_t *filename=lv_label_create(row);lv_label_set_text(filename,name?name:"");style_text(filename,UI_COLOR_TEXT);lv_obj_set_width(filename,348);lv_obj_set_height(filename,lv_font_get_line_height(font()));lv_label_set_long_mode(filename,LV_LABEL_LONG_DOT);lv_obj_align(filename,LV_ALIGN_LEFT_MID,0,0);
 lv_obj_t *menu=lv_btn_create(row);lv_obj_set_size(menu,44,48);lv_obj_align(menu,LV_ALIGN_RIGHT_MID,6,0);lv_obj_set_style_pad_all(menu,0,0);lv_obj_set_style_radius(menu,10,0);lv_obj_set_style_bg_opa(menu,LV_OPA_TRANSP,0);lv_obj_set_style_bg_color(menu,lv_color_hex(UI_COLOR_PRESSED),LV_STATE_PRESSED);lv_obj_set_style_bg_opa(menu,LV_OPA_COVER,LV_STATE_PRESSED);lv_obj_set_style_shadow_width(menu,0,0);
 lv_obj_clear_flag(menu,LV_OBJ_FLAG_EVENT_BUBBLE);lv_obj_add_event_cb(menu,dots,LV_EVENT_DRAW_MAIN,NULL);lv_obj_add_event_cb(menu,show_menu,LV_EVENT_SHORT_CLICKED,r);return row;
}
