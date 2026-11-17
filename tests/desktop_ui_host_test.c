#include "lvgl.h"
#include "UITheme.h"
#include "QuickPanel.h"
#include "AppLauncher.h"
#include "FlipClock.h"
#include "EdgeBack.h"
#include "UiTransition.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static lv_color_t pixels[480 * 480], outgoing_frame[480 * 480];
static bool capture_pixels(lv_color_t *out,size_t count,void *context){if(count!=480*480)return false;memcpy(out,context?context:pixels,sizeof(pixels));return true;}
void hotspot_name_ui_open(lv_obj_t *previous,const lv_font_t *font){(void)previous;(void)font;}
static lv_obj_t *home, *home_page, *apps_page, *settings, *wifi_screen;
static bool pressed, fail_commit;
static int point_x, point_y, back_count, widget_clicks;
static uint8_t committed[14], pending_bytes[14];
static size_t committed_length, pending_length;
static unsigned commits;
static char sd_root[1024];
int app_opens[12];
static quick_radio_state_t radios[QUICK_RADIO_COUNT];
static int brightness = 60, volume = 50, saves;

void desktop_input(lv_indev_drv_t*, lv_indev_data_t*, bool, int, int);
void desktop_home(lv_obj_t*);
void desktop_block_input(bool);
void desktop_block_boot(bool);
void desktop_set_page(int);
int desktop_get_page(void);
int desktop_take_swipe(void);
void desktop_build(lv_obj_t*,lv_obj_t*,const lv_font_t*,const char*);
void desktop_rebuild_apps(lv_obj_t*);
void desktop_home_sample(const char*,const char*,const char*);
void desktop_life_sample(int,int,int);
const char *desktop_lunar_text(void);
const char *desktop_weekday_text(void);
const char *desktop_date_text(void);
const char *desktop_term_text(void);
const char *desktop_festival_text(void);
const char *desktop_index_text(int);
void desktop_indices_sample(bool,bool);
void desktop_home_info_bounds(void);
void desktop_three_character_fit(void);
flip_clock_t *desktop_clock(void);

int nvs_open(const char *name, int mode, int *handle) {(void)mode;assert(!strcmp(name,"app_layout"));*handle=1;return 0;}
int nvs_get_blob(int handle,const char *key,void *data,size_t *length)
{(void)handle;assert(!strcmp(key,"order"));if(!committed_length)return 1;assert(*length>=committed_length);memcpy(data,committed,committed_length);*length=committed_length;return 0;}
int nvs_set_blob(int handle,const char *key,const void *data,size_t length)
{(void)handle;assert(!strcmp(key,"order")&&length<=14);memcpy(pending_bytes,data,length);pending_length=length;return 0;}
int nvs_commit(int handle){(void)handle;if(fail_commit)return 1;memcpy(committed,pending_bytes,pending_length);committed_length=pending_length;++commits;return 0;}
void nvs_close(int handle){(void)handle;}
bool quick_radio_request(quick_radio_t radio,bool enabled){radios[radio].enabled=enabled;return true;}
void quick_radio_get(quick_radio_t radio,quick_radio_state_t *state){*state=radios[radio];}
bool music_web_is_running(void){return true;}
void quick_hotspot_get_info(quick_hotspot_info_t *info){memset(info,0,sizeof(*info));strcpy(info->ssid,"Preview");strcpy(info->password,"PreviewOnly");strcpy(info->ip,"172.31.42.1");}
const char *quick_bluetooth_get_name(void){return "ESP32S3-PREVIEW";}
int get_backlight_brightness(void){return brightness;}
void set_backlight_brightness(int value){brightness=value;}
uint8_t get_audio_volume(void){return volume;}
void set_audio_volume(int value){volume=value;}
void save_audio_volume(void){++saves;}
void custom_open_settings(void){lv_scr_load(settings);}
void custom_open_wifi_settings(void){lv_scr_load(wifi_screen);}
bool custom_navigation_can_back(void){return lv_scr_act()!=home||desktop_get_page()!=0;}
static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *data){(void)area;(void)data;lv_disp_flush_ready(driver);}
static void read_pointer(lv_indev_drv_t *driver,lv_indev_data_t *data){desktop_input(driver,data,pressed,point_x,point_y);}
static void advance(unsigned ms){for(unsigned i=0;i<ms;i+=5){lv_tick_inc(5);lv_timer_handler();}}
static void touch(bool down,int x,int y,unsigned ms){pressed=down;point_x=x;point_y=y;advance(ms);}
static void tap(int x,int y){touch(true,x,y,90);touch(false,x,y,150);}
static void left_back(void){touch(true,5,210,50);touch(true,90,210,90);touch(false,90,210,150);}
static void right_back(void){touch(true,475,210,50);touch(true,385,210,90);touch(false,385,210,150);}
static void screenshot(const char *folder,const char *name)
{char path[1200];snprintf(path,sizeof(path),"%s/%s.rgb565",folder,name);lv_refr_now(NULL);FILE*f=fopen(path,"wb");assert(f);assert(fwrite(pixels,sizeof(pixels),1,f)==1);fclose(f);}
static void *file_open(lv_fs_drv_t*d,const char*p,lv_fs_mode_t m){(void)d;(void)m;char path[1200];snprintf(path,sizeof(path),"%s/%s",sd_root,p);return fopen(path,"rb");}
static lv_fs_res_t file_close(lv_fs_drv_t*d,void*f){(void)d;return fclose(f)==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;}
static lv_fs_res_t file_read(lv_fs_drv_t*d,void*f,void*out,uint32_t n,uint32_t*read){(void)d;*read=fread(out,1,n,f);return LV_FS_RES_OK;}
static lv_fs_res_t file_seek(lv_fs_drv_t*d,void*f,uint32_t n,lv_fs_whence_t w){(void)d;return fseek(f,n,w==LV_FS_SEEK_SET?SEEK_SET:w==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END)==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;}
static lv_fs_res_t file_tell(lv_fs_drv_t*d,void*f,uint32_t*n){(void)d;*n=ftell(f);return LV_FS_RES_OK;}
static lv_obj_t *page(lv_obj_t *parent){lv_obj_t*p=lv_obj_create(parent);lv_obj_set_size(p,480,480);lv_obj_set_pos(p,0,0);lv_obj_set_style_pad_all(p,0,0);lv_obj_set_style_border_width(p,0,0);lv_obj_clear_flag(p,LV_OBJ_FLAG_SCROLLABLE);ui_theme_apply_background(p);return p;}
static void show_apps(void){lv_scr_load(home);desktop_set_page(1);lv_obj_add_flag(home_page,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(apps_page,LV_OBJ_FLAG_HIDDEN);advance(100);}
static void show_home(void){lv_scr_load(home);desktop_set_page(0);lv_obj_add_flag(apps_page,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(home_page,LV_OBJ_FLAG_HIDDEN);advance(100);}
static lv_obj_t *find_card(lv_obj_t *parent,const char *name)
{
 for(uint32_t i=0;i<lv_obj_get_child_cnt(parent);++i){lv_obj_t*c=lv_obj_get_child(parent,i);
  if(lv_obj_check_type(c,&lv_btn_class))for(uint32_t j=0;j<lv_obj_get_child_cnt(c);++j){lv_obj_t*l=lv_obj_get_child(c,j);if(lv_obj_check_type(l,&lv_label_class)&&!strcmp(lv_label_get_text(l),name))return c;}
  lv_obj_t *nested=find_card(c,name);if(nested)return nested;
 }return NULL;
}
static void screen_back(lv_event_t *event){assert(lv_event_get_user_data(event)==(void*)7);++back_count;lv_scr_load(home);}
static void root_back(lv_event_t *event){(void)event;++back_count;show_home();}
static void clicked(lv_event_t *event){(void)event;++widget_clicks;}
static void assert_time(flip_clock_t *clock,const char *expected)
{
 lv_obj_t *object=flip_clock_object(clock);
 for(unsigned i=0;i<6;++i){lv_obj_t*card=lv_obj_get_child(object,i);lv_obj_t*upper=lv_obj_get_child(card,0);const char*s=lv_label_get_text(lv_obj_get_child(upper,0));assert(s[0]==expected[i]);}
}
int main(int argc,char **argv)
{
 assert(argc==3);snprintf(sd_root,sizeof(sd_root),"%s",argv[1]);lv_init();
 lv_disp_draw_buf_t buffer;lv_disp_drv_t display;lv_disp_draw_buf_init(&buffer,pixels,NULL,480*480);
 lv_disp_drv_init(&display);display.hor_res=480;display.ver_res=480;display.draw_buf=&buffer;display.flush_cb=flush;display.full_refresh=1;lv_disp_drv_register(&display);ui_transition_set_capture(capture_pixels,NULL);
 lv_indev_drv_t pointer;lv_indev_drv_init(&pointer);pointer.type=LV_INDEV_TYPE_POINTER;pointer.read_cb=read_pointer;lv_indev_drv_register(&pointer);
 lv_fs_drv_t filesystem;lv_fs_drv_init(&filesystem);filesystem.letter='S';filesystem.open_cb=file_open;filesystem.close_cb=file_close;filesystem.read_cb=file_read;filesystem.seek_cb=file_seek;filesystem.tell_cb=file_tell;lv_fs_drv_register(&filesystem);
 lv_font_t *font=lv_font_load("S:/fonts/chinese_20.bin");assert(font);
 home=lv_obj_create(NULL);lv_obj_set_style_pad_all(home,0,0);lv_obj_clear_flag(home,LV_OBJ_FLAG_SCROLLABLE);lv_scr_load(home);desktop_home(home);ui_transition_set_desktop(home);
 home_page=page(home);apps_page=page(home);settings=page(NULL);wifi_screen=page(NULL);
 desktop_build(home_page,apps_page,font,sd_root);quick_panel_init(font);edge_back_init();
 edge_back_bind(home,root_back,NULL);edge_back_bind(settings,screen_back,(void*)7);
 show_home();flip_clock_t *clock=desktop_clock();assert(clock);flip_clock_set_time(clock,10,39,58,false);
 for(int size=16;size<=32;size+=size==16?4:12){char path[64];snprintf(path,sizeof(path),"S:/fonts/chinese_%d.bin",size);lv_font_t *punct=lv_font_load(path);assert(punct);lv_font_glyph_dsc_t glyph;assert(lv_font_get_glyph_dsc(punct,&glyph,0xff1a,0)&&glyph.box_w>0);assert(lv_font_get_glyph_dsc(punct,&glyph,0x00b7,0)&&glyph.box_w>0);lv_font_free(punct);}
 desktop_home_sample("晴","25°C","36.8°C");advance(100);screenshot(argv[2],"home-flip-clock");
 desktop_home_info_bounds();assert(!strcmp(desktop_date_text(),"2026-11-14")&&!strcmp(desktop_weekday_text(),"周六"));
 desktop_three_character_fit();
 assert(strstr(desktop_index_text(0),"舒适")&&strstr(desktop_index_text(1),"弱")&&strstr(desktop_index_text(2),"少发")&&strstr(desktop_index_text(3),"较不宜"));
 desktop_life_sample(2026,2,17);advance(80);assert(strstr(desktop_festival_text(),"春节")&&strstr(desktop_lunar_text(),"正月初一"));desktop_home_info_bounds();screenshot(argv[2],"home-spring-festival");
 desktop_life_sample(2026,11,22);advance(80);assert(strstr(desktop_term_text(),"小雪")&&!desktop_festival_text()[0]);desktop_home_info_bounds();screenshot(argv[2],"home-solar-term");
 desktop_life_sample(2026,4,5);advance(80);assert(strstr(desktop_term_text(),"清明")&&strstr(desktop_festival_text(),"清明节")&&!strcmp(desktop_weekday_text(),"周日"));desktop_home_info_bounds();screenshot(argv[2],"home-four-date-rows");
 desktop_life_sample(2020,10,1);advance(80);assert(strstr(desktop_festival_text(),"国庆节")&&strstr(desktop_festival_text(),"中秋节"));desktop_home_info_bounds();screenshot(argv[2],"home-overlapping-festivals");
 desktop_life_sample(2026,11,14);assert(!desktop_term_text()[0]&&!desktop_festival_text()[0]);
 desktop_indices_sample(false,false);desktop_life_sample(0,0,0);advance(80);for(int i=0;i<4;i++)assert(!strcmp(desktop_index_text(i),"：--"));assert(!desktop_weekday_text()[0]&&!desktop_term_text()[0]&&!desktop_festival_text()[0]);screenshot(argv[2],"home-offline-no-clock");
 desktop_indices_sample(true,true);desktop_life_sample(2026,11,14);advance(80);desktop_home_info_bounds();screenshot(argv[2],"home-long-life-index");
 desktop_indices_sample(true,false);desktop_life_sample(2026,11,14);
 desktop_home_sample("小到中雨转雷阵雨","-12°C","--.-°C");advance(80);screenshot(argv[2],"home-long-weather");
 desktop_home_sample("晴","25°C","36.8°C");advance(80);
 flip_clock_set_time(clock,10,39,59,true);advance(100);screenshot(argv[2],"clock-flip-midpoint");advance(400);assert_time(clock,"103959");
 flip_clock_set_time(clock,10,40,0,true);
 for(unsigned frame=0;frame<9;++frame){char name[40];snprintf(name,sizeof(name),"flip-frame-%02u",frame);screenshot(argv[2],name);advance(40);}
 assert_time(clock,"104000");
 flip_clock_set_time(clock,23,59,59,false);flip_clock_set_time(clock,0,0,0,true);advance(400);assert_time(clock,"000000");
 flip_clock_set_time(clock,-1,-1,-1,true);assert_time(clock,"------");
 flip_clock_set_time(clock,10,39,58,false);
 memcpy(outgoing_frame,pixels,sizeof(pixels));show_apps();ui_transition_set_capture(capture_pixels,outgoing_frame);ui_transition_reveal(apps_page,false);ui_transition_set_capture(capture_pixels,NULL);
 for(int n=0;n<14;n++){char name[48];snprintf(name,sizeof(name),"transition-frame-%02d",n);screenshot(argv[2],name);advance(40);}
 assert(!ui_transition_busy()&&lv_obj_get_x(apps_page)==0);
 unsigned animations=lv_anim_count_running();flip_clock_set_time(clock,10,40,0,true);assert(lv_anim_count_running()==animations);assert_time(clock,"104000");
 screenshot(argv[2],"apps-default");
 /* Boot splash input stays blocked through a held finger's final release. */
 desktop_block_boot(true);tap(69,140);touch(true,69,140,600);desktop_block_boot(false);touch(true,69,140,100);touch(false,69,140,100);assert(app_opens[0]==0&&!app_launcher_is_dragging()&&desktop_take_swipe()==0);
 /* The real input arbitration must swallow sleep/wake taps and swipes. */
 desktop_block_input(true);tap(69,140);tap(69,140);
 touch(true,69,140,650);touch(true,300,140,120);touch(false,300,140,100);
 assert(app_opens[0]==0&&!app_launcher_is_dragging()&&desktop_take_swipe()==0);
 desktop_block_input(false);advance(100);
 for(int i=0;i<12;++i){tap(69+(i%4)*114,140+(i/4)*114);assert(app_opens[i]==1);}
 /* A held icon follows a real pointer across rows; it never launches or flips pages. */
 memset(app_opens,0,sizeof(app_opens));desktop_take_swipe();
 touch(true,69,140,650);assert(app_launcher_is_dragging());
 touch(true,183,254,180);screenshot(argv[2],"apps-dragging");touch(false,183,254,180);
 assert(!app_launcher_is_dragging()&&commits==1&&committed[7]==0&&desktop_take_swipe()==0&&app_opens[0]==0);
 lv_obj_t *moved=find_card(apps_page,"陀螺仪");assert(moved&&lv_obj_get_x(moved)==132&&lv_obj_get_y(moved)==202);
 tap(183,254);assert(app_opens[0]==1);screenshot(argv[2],"apps-reordered");
 /* Recreating the page reads the saved permutation rather than resetting it. */
 lv_obj_clean(apps_page);desktop_rebuild_apps(apps_page);advance(100);moved=find_card(apps_page,"陀螺仪");assert(lv_obj_get_x(moved)==132&&lv_obj_get_y(moved)==202);
 fail_commit=true;touch(true,183,254,650);touch(true,69,140,160);touch(false,69,140,150);assert(commits==1&&committed[7]==0);fail_commit=false;
 lv_obj_clean(apps_page);desktop_rebuild_apps(apps_page);advance(100);assert(lv_obj_get_y(find_card(apps_page,"陀螺仪"))==202);
 touch(true,183,254,650);touch(true,69,140,120);app_launcher_cancel_drag();touch(false,69,140,150);assert(commits==1&&lv_obj_get_y(find_card(apps_page,"陀螺仪"))==202);
 uint8_t saved[14];memcpy(saved,committed,14);committed[3]=committed[2];
 lv_obj_clean(apps_page);desktop_rebuild_apps(apps_page);advance(100);assert(lv_obj_get_x(find_card(apps_page,"陀螺仪"))==18);memcpy(committed,saved,14);
 /* Moving before the long-press timeout remains a desktop swipe, not a drag/click. */
 int opens=app_opens[0];touch(true,69,140,70);touch(true,300,140,130);touch(false,300,140,160);
 assert(!app_launcher_is_dragging()&&desktop_take_swipe()==1&&app_opens[0]==opens&&commits==1);
 /* Side reservation, cancellation, both edges, and safe delayed navigation. */
 lv_obj_t *trap=lv_btn_create(settings);lv_obj_set_pos(trap,0,150);lv_obj_set_size(trap,120,100);lv_obj_add_event_cb(trap,clicked,LV_EVENT_CLICKED,NULL);
 ui_screen_open(settings);assert(lv_scr_act()==settings&&ui_transition_busy());
 touch(true,60,200,650);touch(false,60,200,100);assert(widget_clicks==0&&!ui_transition_busy());
 lv_scr_load(settings);advance(100);touch(true,5,210,60);touch(true,35,210,90);touch(false,35,210,150);assert(back_count==0&&widget_clicks==0);
 touch(true,5,210,60);touch(true,8,320,90);touch(false,90,320,150);assert(back_count==0);
 touch(true,5,210,60);touch(true,90,210,100);screenshot(argv[2],"edge-back-preview");touch(false,90,210,150);assert(back_count==1&&lv_scr_act()==home&&widget_clicks==0);
 lv_scr_load(settings);advance(60);right_back();assert(back_count==2&&lv_scr_act()==home);
 lv_scr_load(settings);advance(60);touch(true,5,210,50);touch(true,90,210,60);lv_scr_load(home);touch(false,90,210,150);assert(back_count==2);
 lv_scr_load(settings);advance(60);static const char*options[]={"Cancel","OK",""};lv_obj_t *dialog=lv_msgbox_create(NULL,"Confirm","Cancel this first",options,false);advance(60);left_back();advance(100);assert(back_count==2&&!lv_obj_is_valid(dialog)&&lv_scr_act()==settings);
 lv_obj_t *slider=lv_slider_create(settings);lv_obj_set_pos(slider,40,300);lv_obj_set_size(slider,400,24);advance(60);touch(true,180,312,80);touch(true,370,312,100);touch(false,370,312,120);assert(back_count==2&&lv_slider_get_value(slider)>70);
 /* The top quick panel keeps priority over side-back. */
 touch(true,6,8,40);touch(true,6,100,90);touch(false,6,100,300);assert(quick_panel_is_visible()&&back_count==2);
 quick_panel_close();advance(300);assert(!quick_panel_is_visible());
 lv_scr_load(settings);touch(true,5,210,50);touch(true,90,210,60);lv_scr_load(home);lv_obj_del(settings);touch(false,90,210,150);assert(back_count==2);
 show_apps();left_back();assert(desktop_get_page()==0&&back_count==3);
 /* Deleting a clock during its animation cancels all callbacks referencing it. */
 for(unsigned i=0;i<40;++i){flip_clock_t *temporary=flip_clock_create(home_page);assert(temporary);flip_clock_set_time(temporary,1,2,3,false);flip_clock_set_time(temporary,4,5,6,true);advance(20);lv_obj_del(flip_clock_object(temporary));advance(20);}
 flip_clock_t *second=flip_clock_create(home_page);flip_clock_set_time(second,12,34,56,false);flip_clock_set_time(clock,10,40,1,false);assert_time(second,"123456");assert_time(clock,"104001");lv_obj_del(flip_clock_object(second));
 puts("Desktop UI passed: real touch long-press/reorder/persistence/failure, page-swipe arbitration, both-edge back/cancel/modal/delete, slider/top-panel isolation, flip rollover/hidden updates/deletion.");
 return 0;
}
