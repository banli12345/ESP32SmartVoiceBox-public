#include "UiTransition.h"
#include "SystemUpdateUI.h"
#include "SystemUpdate.h"
#include "SDcar.h"
#include "UITheme.h"
#include "EdgeBack.h"
#include <stdio.h>
static lv_obj_t *screen,*settings_screen,*current,*candidate,*message,*progress,*check_button,*install_button,*confirm;
static const lv_font_t *font(void){return chinese_font_20?chinese_font_20:&lv_font_montserrat_20;}
static void back(lv_event_t*event){(void)event;if(confirm){lv_obj_del(confirm);confirm=NULL;}else ui_screen_back(settings_screen);}
static void text(lv_obj_t*obj){lv_obj_set_style_text_font(obj,font(),0);lv_obj_set_style_text_color(obj,lv_color_hex(UI_COLOR_TEXT),0);}
static void checked(lv_event_t*event){(void)event;system_update_check();}
static void confirmed(lv_event_t*event){
 (void)event;if(confirm){lv_obj_del(confirm);confirm=NULL;}system_update_install();
}
static void cancelled(lv_event_t*event){(void)event;if(confirm){lv_obj_del(confirm);confirm=NULL;}}
static lv_obj_t*button(lv_obj_t*parent,const char*label,int x,int y,int width,lv_event_cb_t cb){
 lv_obj_t*b=lv_btn_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,width,52);lv_obj_set_style_radius(b,14,0);
 lv_obj_set_style_bg_color(b,lv_color_hex(UI_COLOR_ACCENT),0);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,NULL);
 lv_obj_t*l=lv_label_create(b);lv_label_set_text(l,label);text(l);lv_obj_set_style_text_color(l,lv_color_white(),0);lv_obj_center(l);return b;
}
static void install(lv_event_t*event){
 (void)event;if(confirm)return;
 confirm=lv_obj_create(screen);lv_obj_set_size(confirm,480,480);lv_obj_set_pos(confirm,0,0);lv_obj_set_style_bg_opa(confirm,LV_OPA_80,0);lv_obj_set_style_bg_color(confirm,lv_color_hex(UI_COLOR_BG),0);lv_obj_clear_flag(confirm,LV_OBJ_FLAG_SCROLLABLE);
 lv_obj_t*l=lv_label_create(confirm);lv_obj_set_width(l,400);lv_obj_set_pos(l,20,110);text(l);lv_label_set_text(l,"安装新固件并重启？\n\n请保持设备供电。更新期间\n请勿拔出 SD 卡或复位设备。");
 button(confirm,"取消",20,280,188,cancelled);button(confirm,"安装并重启",224,280,188,confirmed);
}
static void refresh(lv_timer_t*timer){
 (void)timer;if(!screen||lv_scr_act()!=screen)return;
 system_update_status_t s;system_update_get_status(&s);char label[80];
 snprintf(label,sizeof(label),"当前版本  %s",s.current_version);lv_label_set_text(current,label);
 snprintf(label,sizeof(label),"上传版本  %s",s.candidate_version[0]?s.candidate_version:"未检查");lv_label_set_text(candidate,label);
 lv_label_set_text(message,s.message);lv_bar_set_value(progress,s.progress,LV_ANIM_OFF);
 if(s.busy)lv_obj_add_state(check_button,LV_STATE_DISABLED);else lv_obj_clear_state(check_button,LV_STATE_DISABLED);
 if(s.available&&!s.busy)lv_obj_clear_state(install_button,LV_STATE_DISABLED);else lv_obj_add_state(install_button,LV_STATE_DISABLED);
}
void system_update_ui_open(lv_obj_t*settings){
 settings_screen=settings;
 if(!screen){
  screen=lv_obj_create(NULL);ui_theme_apply_background(screen);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);edge_back_bind(screen,back,NULL);
  lv_obj_t*title=lv_label_create(screen);text(title);lv_label_set_text(title,"系统更新");lv_obj_align(title,LV_ALIGN_TOP_MID,0,18);
  lv_obj_t*card=lv_obj_create(screen);lv_obj_set_pos(card,20,70);lv_obj_set_size(card,440,296);lv_obj_set_style_radius(card,20,0);lv_obj_set_style_bg_color(card,lv_color_hex(UI_COLOR_SURFACE),0);lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
  current=lv_label_create(card);text(current);lv_obj_set_pos(current,4,2);
  candidate=lv_label_create(card);text(candidate);lv_obj_set_pos(candidate,4,40);
  message=lv_label_create(card);text(message);lv_obj_set_width(message,392);lv_obj_set_pos(message,4,88);
  progress=lv_bar_create(card);lv_obj_set_size(progress,388,12);lv_obj_set_pos(progress,4,234);lv_obj_set_style_bg_color(progress,lv_color_hex(UI_COLOR_ACCENT),LV_PART_INDICATOR);
  check_button=button(screen,"检查更新",20,388,210,checked);install_button=button(screen,"安装并重启",250,388,210,install);
  lv_obj_t*hint=lv_label_create(screen);text(hint);lv_obj_set_style_text_color(hint,lv_color_hex(UI_COLOR_TEXT_MUTED),0);lv_label_set_text(hint,"先在热点网页上传应用固件");lv_obj_align(hint,LV_ALIGN_BOTTOM_MID,0,-9);
  lv_timer_create(refresh,200,NULL);
 }
 ui_screen_open(screen);refresh(NULL);
}
