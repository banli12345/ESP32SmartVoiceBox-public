#include "HotspotNameUI.h"
void hotspot_name_ui_open(lv_obj_t *previous,const lv_font_t *font){(void)previous;(void)font;}
#include "lvgl.h"
#include "QuickPanel.h"
#include "EdgeBack.h"
bool app_launcher_is_dragging(void) { return false; }
bool custom_navigation_can_back(void) { return true; }
#include <assert.h>
#include <stdio.h>
#include <string.h>

static lv_color_t pixels[480 * 480];
static quick_radio_state_t radios[QUICK_RADIO_COUNT];
static int requests[QUICK_RADIO_COUNT], brightness=100, volume=100, saves;
static bool pressed;
static int point_x, point_y;
static lv_obj_t *settings_screen, *wifi_screen;
static int header_clicks;
static void header_clicked(lv_event_t *event) {(void)event;header_clicks++;}
static const char *font_file;
void panel_test_set_home(lv_obj_t *screen);
int panel_test_take_swipe(void);
void panel_test_read(lv_indev_drv_t *driver,lv_indev_data_t *data,bool down,int x,int y);

static void *font_open(lv_fs_drv_t *driver,const char *path,lv_fs_mode_t mode)
{
    (void)driver;(void)path;(void)mode;return fopen(font_file,"rb");
}
static lv_fs_res_t font_close(lv_fs_drv_t *driver,void *file)
{
    (void)driver;return fclose(file)==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;
}
static lv_fs_res_t font_read(lv_fs_drv_t *driver,void *file,void *data,uint32_t bytes,uint32_t *read)
{
    (void)driver;*read=(uint32_t)fread(data,1,bytes,file);return LV_FS_RES_OK;
}
static lv_fs_res_t font_seek(lv_fs_drv_t *driver,void *file,uint32_t offset,lv_fs_whence_t whence)
{
    (void)driver;int origin=whence==LV_FS_SEEK_SET?SEEK_SET:whence==LV_FS_SEEK_CUR?SEEK_CUR:SEEK_END;
    return fseek(file,(long)offset,origin)==0?LV_FS_RES_OK:LV_FS_RES_FS_ERR;
}
static lv_fs_res_t font_tell(lv_fs_drv_t *driver,void *file,uint32_t *position)
{
    (void)driver;*position=(uint32_t)ftell(file);return LV_FS_RES_OK;
}

bool quick_radio_request(quick_radio_t radio, bool enabled)
{
    assert(radio >= 0 && radio < QUICK_RADIO_COUNT);
    requests[radio]++;
    radios[radio].enabled=enabled;
    return true;
}
void quick_radio_get(quick_radio_t radio, quick_radio_state_t *state) { *state=radios[radio]; }
bool music_web_is_running(void) { return true; }
void quick_hotspot_get_info(quick_hotspot_info_t *info)
{
    memset(info,0,sizeof(*info));
    strcpy(info->ssid,"ESP32S3-PREVIEW");
    strcpy(info->password,"PreviewOnly42");
    strcpy(info->ip,"192.168.4.1");
    info->clients=1;info->internet_available=true;
}
const char *quick_bluetooth_get_name(void) {return "ESP32S3-PREVIEW";}
int get_backlight_brightness(void) {return brightness;}
void set_backlight_brightness(int value) {brightness=value;}
uint8_t get_audio_volume(void) {return (uint8_t)volume;}
void set_audio_volume(int value) {volume=value;}
void save_audio_volume(void) {saves++;}
void custom_open_settings(void) {lv_scr_load(settings_screen);}
void custom_open_wifi_settings(void) {lv_scr_load(wifi_screen);}

static void flush(lv_disp_drv_t *driver,const lv_area_t *area,lv_color_t *colors)
{
    (void)area;(void)colors;lv_disp_flush_ready(driver);
}
static void read_pointer(lv_indev_drv_t *driver,lv_indev_data_t *data)
{
    panel_test_read(driver,data,pressed,point_x,point_y);
}
static void advance(unsigned milliseconds)
{
    for(unsigned i=0;i<milliseconds;i+=5) {lv_tick_inc(5);lv_timer_handler();}
}
static void touch(bool down,int x,int y,unsigned milliseconds)
{
    pressed=down;point_x=x;point_y=y;advance(milliseconds);
}
static void tap(int x,int y) {touch(true,x,y,90);touch(false,x,y,100);}
static void screenshot(const char *folder,const char *name)
{
    lv_refr_now(NULL);
    char path[1024];snprintf(path,sizeof(path),"%s/%s.rgb565",folder,name);
    FILE *file=fopen(path,"wb");assert(file);
    assert(fwrite(pixels,sizeof(pixels),1,file)==1);fclose(file);
}
int main(int argc,char **argv)
{
    assert(argc==2 || argc==3);
    lv_init();
    lv_disp_draw_buf_t buffer;lv_disp_drv_t display;
    lv_disp_draw_buf_init(&buffer,pixels,NULL,480*480);
    lv_disp_drv_init(&display);display.hor_res=480;display.ver_res=480;
    display.draw_buf=&buffer;display.flush_cb=flush;display.full_refresh=1;
    lv_disp_drv_register(&display);
    lv_indev_drv_t pointer;lv_indev_drv_init(&pointer);
    pointer.type=LV_INDEV_TYPE_POINTER;pointer.read_cb=read_pointer;
    lv_indev_drv_register(&pointer);
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x6AA168),0);
    panel_test_set_home(lv_scr_act());
    lv_obj_t *header_button=lv_btn_create(lv_scr_act());
    lv_obj_set_pos(header_button,390,0);lv_obj_set_size(header_button,90,48);
    lv_obj_add_event_cb(header_button,header_clicked,LV_EVENT_CLICKED,NULL);
    lv_obj_t *app_icon=lv_btn_create(lv_scr_act());
    lv_obj_set_pos(app_icon,200,100);lv_obj_set_size(app_icon,120,120);
    lv_obj_add_event_cb(app_icon,header_clicked,LV_EVENT_CLICKED,NULL);
    settings_screen=lv_obj_create(NULL);wifi_screen=lv_obj_create(NULL);
    radios[QUICK_RADIO_WIFI].enabled=true;
    radios[QUICK_RADIO_WIFI].connected=true;
    lv_font_t *font=NULL;
    if(argc==3) {
        font_file=argv[2];lv_fs_drv_t filesystem;lv_fs_drv_init(&filesystem);
        filesystem.letter='S';filesystem.open_cb=font_open;filesystem.close_cb=font_close;
        filesystem.read_cb=font_read;filesystem.seek_cb=font_seek;filesystem.tell_cb=font_tell;
        lv_fs_drv_register(&filesystem);font=lv_font_load("S:/font.bin");assert(font);
    }
    quick_panel_init(font);edge_back_init();advance(100);
    tap(430,20);assert(header_clicks==1 && !quick_panel_is_visible());
    touch(true,280,150,50);touch(true,120,150,80);touch(false,120,150,100);
    assert(header_clicks==1 && panel_test_take_swipe()==-1);
    touch(true,430,8,40);touch(true,430,55,50);touch(true,430,115,60);
    touch(false,430,115,300);assert(header_clicks==1);
    assert(quick_panel_is_visible());
    screenshot(argv[1],"quick-panel");
    tap(96,170);
    assert(requests[QUICK_RADIO_WIFI]==1 && !radios[QUICK_RADIO_WIFI].enabled);
    touch(true,240,170,800);touch(false,240,170,300);
    assert(!quick_panel_is_visible());
    assert(requests[QUICK_RADIO_BLUETOOTH]==0);
    tap(402,130);assert(requests[QUICK_RADIO_BLUETOOTH]==1);
    assert(radios[QUICK_RADIO_BLUETOOTH].enabled);
    advance(300);
    screenshot(argv[1],"bluetooth-settings");
    touch(true,5,210,50);touch(true,95,210,90);touch(false,95,210,150);assert(lv_scr_act()==settings_screen);
    quick_panel_open();advance(300);
    touch(true,437,297,60);touch(true,170,297,150);touch(false,170,297,150);
    assert(brightness>=5 && brightness<50 && quick_panel_is_visible());
    touch(true,437,371,60);touch(true,210,371,150);
    assert(saves==0 && volume<60);
    touch(false,210,371,150);assert(saves==1 && quick_panel_is_visible());
    touch(true,240,428,40);touch(true,240,300,90);touch(false,240,300,300);
    assert(!quick_panel_is_visible());
    quick_panel_open();advance(300);tap(240,464);advance(250);
    assert(!quick_panel_is_visible());
    quick_panel_open();advance(300);
    touch(true,96,170,800);touch(false,96,170,100);
    assert(lv_scr_act()==wifi_screen && requests[QUICK_RADIO_WIFI]==1);
    quick_panel_open_radio_settings(QUICK_RADIO_HOTSPOT);advance(300);
    tap(402,130);assert(radios[QUICK_RADIO_HOTSPOT].enabled);
    advance(300);
    screenshot(argv[1],"hotspot-settings");
    printf("Quick panel passed: edge drag, short tap, long press without toggle, settings switches, two sliders, save on release, upward dismissal, backdrop dismissal\n");
    return 0;
}
