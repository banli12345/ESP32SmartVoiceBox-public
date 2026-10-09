/*
 * Custom UI — fixed for ESP32-S3 pure LVGL
 * GUI Guider integration: swipe pages + weather + gyro
 */

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "lvgl.h"
#include "lv_ime_pinyin.h"
#include "custom.h"
#include "video_draw.h"
#include "DeviceInfo.h"
#include "DeviceControl.h"
#include "DisplayControl.h"
#include "BrowserUI.h"
#include "UITheme.h"

// C 兼容声明
extern char WEATHER_CITY[32];
extern char WEATHER_CITY_DISPLAY[32];
extern char WEATHER_FORECAST_DATE[3][12];
extern char WEATHER_FORECAST_DAY_TEXT[3][32];
extern char WEATHER_FORECAST_NIGHT_TEXT[3][32];
extern char WEATHER_FORECAST_HIGH[3][8];
extern char WEATHER_FORECAST_LOW[3][8];
extern uint8_t WEATHER_FORECAST_COUNT;
extern char* weather;
extern char* temp;
extern lv_font_t *chinese_font_16;
extern lv_font_t *chinese_font_20;
extern lv_font_t *chinese_font_32;
extern lv_img_dsc_t sd_icon_music;
extern lv_img_dsc_t sd_icon_settings;
extern lv_img_dsc_t sd_icon_weather;
extern lv_img_dsc_t sd_icon_clock;
extern lv_img_dsc_t sd_icon_photos;
extern lv_img_dsc_t sd_icon_browser;
extern lv_img_dsc_t sd_icon_video;
extern lv_img_dsc_t sd_icon_calendar;
extern lv_img_dsc_t sd_icon_ai_assistant;
extern volatile uint32_t gui_fps;  // FPS 计数器（main.cpp 维护）
bool updateWeatherCity(const char* newCity);
bool fetchWeatherForecast(void);
bool syncNTPTime(void);
const char *get_wallpaper_path(void);
const char *get_wallpaper_choice_path(int choice);
const lv_img_dsc_t *get_wallpaper_image(void);
const lv_img_dsc_t *get_wallpaper_choice_image(int choice);
void set_wallpaper_choice(int choice);
void music_audio_play(void);
void play_alarm_wav(void);
void music_audio_pause(void);
void music_audio_stop(void);
bool music_audio_is_playing(void);
uint8_t get_music_track_count(void);
uint8_t get_music_track_index(void);
uint8_t get_music_play_mode(void);
void set_music_play_mode(uint8_t mode);
const char *get_music_track_name(uint8_t index);
void music_select_track(uint8_t index);
bool ai_chat_submit(const char *message);
bool ai_chat_is_busy(void);
bool ai_audio_is_playing(void);
bool ai_websocket_is_connected(void);
int ai_chat_get_result(char *response, size_t response_size,
                       char *error, size_t error_size);
const char *get_music_lyric(uint32_t position_ms);
uint32_t get_music_elapsed_ms(void);

// WiFi 函数（定义在 main.cpp，extern "C"）
int do_wifi_scan(void);
int get_wifi_scan_count(void);
const char* get_wifi_ssid(int idx);
bool get_wifi_connected(void);
const char* get_wifi_connected_ssid(void);
bool do_wifi_connect(const char* ssid, const char* password);
const char* get_wifi_status(void);
void set_backlight_brightness(int percent);
uint8_t get_audio_volume(void);
void set_audio_volume(int percent);
void save_audio_volume(void);
void get_clock_reminder(int *hour, int *minute, bool *enabled);
void save_clock_reminder(int hour, int minute, bool enabled);
int scan_photo_files(void);
int get_photo_count(void);
const char *get_photo_path(int index);
const char *get_photo_name(int index);
bool start_photo_load(const char *path);
int get_photo_load_status(void);
const lv_img_dsc_t *get_loaded_photo_image(void);
int scan_video_files(void);
int get_video_count(void);
const char *get_video_name(int index);
bool open_video_file(int index);
void close_video_file(void);
const char *get_video_path(int index);
const lv_img_dsc_t *get_video_frame_image(void);
uint32_t get_video_frame_delay_ms(void);
uint32_t get_video_frame_count(void);
uint32_t get_video_source_fps_milli(void);
bool read_next_video_frame(void);
void set_video_vsync_sync(bool enabled);

/* Weather icon image declarations */
LV_IMG_DECLARE(img_sunny)
LV_IMG_DECLARE(img_cloudy)
LV_IMG_DECLARE(img_overcast)
LV_IMG_DECLARE(img_bigrain)
LV_IMG_DECLARE(img_clock_icon)
extern const lv_font_t lv_font_chinese_tianqi_16;
extern const lv_font_t lv_font_chinese_20_builtin;

/*********************
 *      DEFINES
 *********************/
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   480
#define AI_CANDIDATE_HEIGHT 44
#define AI_KEYBOARD_HEIGHT 242
#define NUM_PAGES       3
#define SWIPE_THRESHOLD 50
#define WEATHER_FORECAST_DAYS 3

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void time_update(lv_timer_t * timer);
static void gyro_ui_update_timer_cb(lv_timer_t *timer);
static void clock_reminder_check(const struct tm *tm_buf);
static void clock_reminder_timeout(lv_timer_t *timer);
void animate_to_page(int page);
static void update_page_dots(int page);
// 屏幕/按钮回调
static void gyro_btn_cb(lv_event_t *e);
static void settings_btn_cb(lv_event_t *e);
static void back_btn_cb(lv_event_t *e);
static void game_restart_cb(lv_event_t *e);
static void game_restart(void);
static void balance_game_btn_cb(lv_event_t *e);
static void balance_game_reset(bool reset_score);
static void create_balance_game_screen(void);
static void balance_game_update_timer_cb(lv_timer_t *timer);
static lv_obj_t * make_icon_btn(lv_obj_t *parent, const char *label, const void *img, uint16_t zoom, uint32_t color, int x, int y, int w, int h);
static lv_obj_t * make_theme_icon_btn(lv_obj_t *parent, const char *label, const void *img, uint16_t zoom, uint32_t color, int x, int y, int w, int h);
static void create_keyboard_screen(void);
static void create_gyro_screen(void);
static void create_game_screen(void);
static void create_settings_screen(void);
static void create_device_info_screen(void);
static void device_info_btn_cb(lv_event_t *e);
static void device_info_screen_event_cb(lv_event_t *e);
static void device_info_timer_cb(lv_timer_t *timer);
static void create_wifi_list_screen(void);
static void create_wifi_password_screen(void);
static void create_brightness_screen(void);
static void create_volume_screen(void);
static void create_wallpaper_screen(void);
static void create_clock_screen(void);
static void create_calendar_screen(void);
static void create_music_screen(void);
static void create_photo_screen(void);
static void create_photo_view_screen(void);
static void photo_render_gallery(void);
static void create_video_screen(void);
static void create_video_view_screen(void);
static void video_render_gallery(void);
static void wallpaper_btn_cb(lv_event_t *e);
static void wallpaper_select_cb(lv_event_t *e);
static void clock_btn_cb(lv_event_t *e);
static void clock_save_cb(lv_event_t *e);
static void calendar_app_btn_cb(lv_event_t *e);
static void ai_assistant_app_btn_cb(lv_event_t *e);
static void browser_app_btn_cb(lv_event_t *e);
static void ai_chat_send_cb(lv_event_t *e);
static void ai_chat_poll_cb(lv_timer_t *timer);
static void ai_chat_show_keyboard(void);
static void ai_chat_keyboard_cb(lv_event_t *e);
static void text_input_event_cb(lv_event_t *e);
static void text_keyboard_event_cb(lv_event_t *e);
static void show_text_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea);
static void hide_text_keyboard(lv_obj_t *keyboard);
static void calendar_back_cb(lv_event_t *e);
static void calendar_gesture_cb(lv_event_t *e);
static void calendar_build_grid(void);
static void calendar_render_month(void);
static void music_app_btn_cb(lv_event_t *e);
static void music_back_cb(lv_event_t *e);
static void music_play_pause_cb(lv_event_t *e);
static void music_previous_cb(lv_event_t *e);
static void music_next_cb(lv_event_t *e);
static void music_mode_cb(lv_event_t *e);
static void music_update_mode_label(void);
static void music_animation_tick_cb(lv_timer_t *timer);
static void music_update_tonearm(void);
static void music_stop(void);
static void photo_app_btn_cb(lv_event_t *e);
static void photo_open_cb(lv_event_t *e);
static void photo_close_cb(lv_event_t *e);
static void photo_load_timer_cb(lv_timer_t *timer);
static void video_app_btn_cb(lv_event_t *e);
static void video_open_cb(lv_event_t *e);
static void video_close_cb(lv_event_t *e);
static void video_frame_timer_cb(lv_timer_t *timer);
static void brightness_btn_cb(lv_event_t *e);
static void brightness_slider_cb(lv_event_t *e);
static void volume_btn_cb(lv_event_t *e);
static void volume_slider_cb(lv_event_t *e);
static void volume_slider_release_cb(lv_event_t *e);
static void weather_app_btn_cb(lv_event_t *e);
static void weather_back_cb(lv_event_t *e);
static void weather_city_btn_cb(lv_event_t *e);
static void setup_weather_page(lv_obj_t *page);
static void refresh_weather_page(void);
static lv_obj_t *create_back_button(lv_obj_t *parent, lv_align_t align,
                                    lv_coord_t x_offset, lv_event_cb_t callback);

/**********************
 *  STATIC VARIABLES
 **********************/
/* Page management — 页面直接放 screen 上，切换=显隐 */
static lv_obj_t * pages[NUM_PAGES];
static lv_obj_t * page_dots[NUM_PAGES];
int current_page = 0;

/* FPS counter */
static lv_obj_t * fps_label;

/* Home page (Page 0) */
static lv_obj_t * time_label;
static lv_obj_t * date_label;
static lv_obj_t * digital_digits[6][7];
static lv_obj_t * digital_colons[2][2];
static lv_obj_t * weather_icon_img;
static lv_obj_t * weather_text;
static lv_obj_t * weather_temp_label;
static lv_obj_t * wallpaper_img;
static lv_obj_t * wallpaper_screen;
static lv_obj_t *wallpaper_preview_objs[3];
static lv_obj_t *wallpaper_preview_labels[3];
static lv_obj_t *clock_screen;
static lv_obj_t *clock_hour_roller;
static lv_obj_t *clock_minute_roller;
static lv_obj_t *clock_enabled_switch;
static lv_obj_t *clock_status_label;
static lv_obj_t *clock_reminder_label;
static int clock_last_reminder_day = -1;
static int clock_last_reminder_minute = -1;
static lv_obj_t *calendar_screen;
static lv_obj_t *calendar_month_label;
static lv_obj_t *calendar_grid;
static lv_obj_t *calendar_day_labels[42];
static int calendar_view_year;
static int calendar_view_month;
static lv_coord_t calendar_swipe_start_x;
static lv_coord_t calendar_swipe_start_y;
static lv_obj_t *photo_screen;
static lv_obj_t *photo_gallery;
static lv_obj_t *photo_view_screen;
static lv_obj_t *photo_full_img;
static lv_obj_t *photo_loading_label;
static lv_timer_t *photo_load_timer;
static lv_obj_t *video_screen;
static lv_obj_t *video_gallery;
static lv_obj_t *video_view_screen;
static lv_obj_t *video_frame_img;
static lv_timer_t *video_frame_timer;
static uint32_t video_frame_period_ms;
static uint32_t video_next_frame_ms;

extern bool video_audio_prepare(const char *rvf_path, uint32_t frame_count, uint32_t fps_milli);
extern bool video_audio_start(void);
extern void video_audio_release(void);
extern bool video_audio_is_active(void);
extern void video_audio_get_clock(uint32_t *position_samples, uint32_t *duration_samples,
                                  uint32_t *cycle, uint32_t *underruns);
extern bool video_present_for_audio(uint32_t position_samples, uint32_t duration_samples,
                                    uint32_t cycle, uint32_t fps_milli);
static uint32_t video_saved_refr_period_ms;
static bool video_fast_refresh;
static lv_obj_t *music_screen;
static lv_obj_t *music_record_label;
static lv_obj_t *music_track_label;
static lv_obj_t *music_record_title;
static lv_obj_t *music_play_label;
static lv_obj_t *music_mode_label;
static lv_obj_t *music_lyrics_label;
static lv_obj_t *music_tonearm;
static lv_obj_t *music_tonearm_shadow;
static lv_obj_t *music_tonearm_head;
static lv_timer_t *music_animation_timer;
static lv_point_t music_tonearm_points[3];
static lv_point_t music_tonearm_shadow_points[3];
static int music_rotation_angle;
static int music_needle_progress;
static int music_track_index = 0;
static bool music_playing;

static const uint8_t digital_digit_masks[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66,
    0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static lv_obj_t *create_digital_segment(lv_obj_t *parent, int x, int y,
                                        int width, int height, int thickness)
{
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_set_pos(segment, x, y);
    lv_obj_set_size(segment, width, height);
    /* Keep the clock's green accent, but deepen it slightly for stronger contrast. */
    lv_obj_set_style_bg_color(segment, lv_color_hex(0x4ED2A0), 0);
    lv_obj_set_style_bg_opa(segment, LV_OPA_20, 0);
    lv_obj_set_style_border_width(segment, 0, 0);
    lv_obj_set_style_radius(segment, thickness / 2, 0);
    lv_obj_set_style_pad_all(segment, 0, 0);
    lv_obj_clear_flag(segment, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return segment;
}

static void create_digital_digit(lv_obj_t *parent, lv_obj_t *segments[7],
                                 int x, int y, int width, int height, int thickness)
{
    const int mid = height / 2;
    segments[0] = create_digital_segment(parent, x + thickness, y, width - 2 * thickness, thickness, thickness);
    segments[1] = create_digital_segment(parent, x + width - thickness, y + thickness,
                                         thickness, mid - thickness, thickness);
    segments[2] = create_digital_segment(parent, x + width - thickness, y + mid,
                                         thickness, mid - thickness, thickness);
    segments[3] = create_digital_segment(parent, x + thickness, y + height - thickness,
                                         width - 2 * thickness, thickness, thickness);
    segments[4] = create_digital_segment(parent, x, y + mid,
                                         thickness, mid - thickness, thickness);
    segments[5] = create_digital_segment(parent, x, y + thickness,
                                         thickness, mid - thickness, thickness);
    segments[6] = create_digital_segment(parent, x + thickness, y + mid - thickness / 2,
                                         width - 2 * thickness, thickness, thickness);
}

static void set_digital_digit(lv_obj_t *segments[7], int value)
{
    const uint8_t mask = (value >= 0 && value <= 9) ? digital_digit_masks[value] : 0;
    for (int i = 0; i < 7; ++i) {
        lv_obj_set_style_bg_opa(segments[i], (mask & (1U << i)) ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void set_digital_colon(lv_obj_t *dots[2], bool enabled)
{
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_style_bg_opa(dots[i], enabled ? LV_OPA_COVER : LV_OPA_20, 0);
    }
}

static void update_digital_time(int hour, int minute, int second)
{
    const int values[6] = {hour >= 0 ? hour / 10 : -1, hour >= 0 ? hour % 10 : -1,
                           minute >= 0 ? minute / 10 : -1, minute >= 0 ? minute % 10 : -1,
                           second >= 0 ? second / 10 : -1, second >= 0 ? second % 10 : -1};
    for (int i = 0; i < 6; ++i) set_digital_digit(digital_digits[i], values[i]);
    set_digital_colon(digital_colons[0], true);
    set_digital_colon(digital_colons[1], true);
}

/* Detail page (Page 1) — now an icon grid */
static lv_obj_t * detail_info;
static lv_obj_t * sd_status_label;

/* Gyro screen — separate full screen loaded on button click */
static lv_obj_t * gyro_screen;
static lv_obj_t * gyro_arc_roll, * gyro_arc_pitch;
static lv_obj_t * gyro_roll_label, * gyro_pitch_label;
static lv_obj_t * gyro_temp_label;
static volatile float gyro_pending_roll;
static volatile float gyro_pending_pitch;
static volatile float gyro_pending_temp;
static volatile bool gyro_pending_valid;
static char gyro_roll_text[16];
static char gyro_pitch_text[16];
static char gyro_temp_text[32];
static lv_obj_t *home_gyro_temp_icon;
static lv_obj_t *home_gyro_temp_label;
static char home_gyro_temp_text[16];
static bool gyro_ui_was_visible;
static bool home_gyro_temp_was_visible;
static bool gyro_temp_has_update;
static bool home_gyro_temp_has_update;
static uint32_t gyro_temp_update_tick;
static uint32_t home_gyro_temp_update_tick;
static lv_obj_t * main_screen_ref; // 保存主屏幕引用，用于返回
static lv_obj_t * ai_assistant_screen;
static lv_obj_t *ai_chat_input;
static lv_obj_t *ai_chat_reply;
static lv_obj_t *ai_chat_status;
static lv_obj_t *ai_chat_send_button;
static lv_obj_t *ai_chat_keyboard;
static lv_obj_t *ai_chat_ime;
static lv_timer_t *ai_chat_poll_timer;
static char ai_chat_last_reply[3073];
static char ai_chat_last_error[128];
extern lv_pinyin_dict_t *get_sd_pinyin_dict(void);

#if 1 /* Used only when the SD dictionary is missing. */
static lv_pinyin_dict_t ai_chat_ime_dict[] = {
    {"a", "啊阿"}, {"ai", "爱矮挨哎碍"}, {"an", "安按暗岸案"},
    {"ba", "八吧巴拔把爸"}, {"bai", "白百摆败"}, {"ban", "办半班般"},
    {"bang", "帮棒榜"}, {"bao", "包保报宝抱"}, {"bei", "北被倍备杯"},
    {"bi", "比必笔闭"}, {"bian", "边变便编"}, {"bie", "别"},
    {"bing", "并病冰"}, {"bo", "波播博"}, {"bu", "不布步部"},
    {"cha", "查茶差"}, {"chang", "长常厂"}, {"cheng", "成程城"},
    {"chi", "吃持迟尺"}, {"chu", "出初处"}, {"ci", "次词此"},
    {"da", "大打答达"}, {"dai", "代带待"}, {"dan", "但单蛋"},
    {"dao", "到道导岛"}, {"de", "的得德"}, {"di", "地第低底"},
    {"dian", "点电店"}, {"dong", "东动懂"}, {"du", "读度独"},
    {"duo", "多朵"}, {"e", "饿额"}, {"er", "而儿耳二"},
    {"fa", "发法罚"}, {"fan", "反饭烦"}, {"fang", "方放房"},
    {"fei", "非飞费"}, {"feng", "风封"}, {"fu", "服福父付"},
    {"ge", "个各哥歌"}, {"gong", "工公功"}, {"gou", "够狗"},
    {"gu", "古故鼓"}, {"guan", "关馆管"}, {"guang", "光广"},
    {"guo", "国过果"}, {"hai", "还海"}, {"han", "汉寒"},
    {"hao", "好号豪"}, {"he", "和何合喝"}, {"hen", "很恨"},
    {"hong", "红洪"}, {"hou", "后厚"}, {"hu", "湖虎户"},
    {"hua", "花话画"}, {"huan", "换环欢"}, {"hui", "会回惠"},
    {"huo", "或火活"}, {"ji", "几机记己"}, {"jia", "家加假"},
    {"jian", "见间建件"}, {"jiang", "将讲江"}, {"jiao", "交叫教"},
    {"jie", "接节姐解"}, {"jin", "今进近"}, {"jing", "经京静"},
    {"jiu", "就九久"}, {"ju", "居局句"}, {"jue", "觉决"},
    {"jun", "军均"}, {"kai", "开"}, {"kan", "看"}, {"ke", "可客课"},
    {"kou", "口"}, {"kuai", "快"}, {"lai", "来"}, {"lan", "蓝兰"},
    {"lao", "老"}, {"le", "了乐"}, {"lei", "雷累类"}, {"li", "里力利李"},
    {"lian", "连脸练"}, {"liang", "两量亮"}, {"lin", "林"}, {"ling", "零另"},
    {"liu", "六流"}, {"long", "龙"}, {"lou", "楼"}, {"lu", "路录"},
    {"lv", "绿"}, {"ma", "吗马妈麻"}, {"mai", "买卖"}, {"man", "满慢"},
    {"mao", "毛猫"}, {"mei", "没每美"}, {"men", "门们"}, {"mi", "米密"},
    {"mian", "面"}, {"ming", "明名"}, {"mu", "目木"}, {"na", "那拿"},
    {"nan", "南难"}, {"nei", "内"}, {"neng", "能"}, {"ni", "你尼泥拟逆"},
    {"nian", "年念"}, {"nin", "您"}, {"ning", "宁"}, {"niu", "牛"},
    {"nong", "农"}, {"nu", "怒"}, {"nv", "女"}, {"pa", "怕爬"},
    {"qi", "起其气期"}, {"qian", "前钱千"}, {"qing", "请清情"}, {"qiu", "求秋"},
    {"qu", "去取"}, {"quan", "全"}, {"que", "却确"}, {"ran", "然"},
    {"rang", "让"}, {"re", "热"}, {"ren", "人认"}, {"ri", "日"},
    {"rong", "容"}, {"ru", "如入"}, {"san", "三"}, {"sha", "沙"},
    {"shan", "山善"}, {"shang", "上商"}, {"she", "设社"}, {"shen", "什身"},
    {"sheng", "生声"}, {"shi", "是时事十使"}, {"shou", "手收"}, {"shu", "书数"},
    {"shui", "水"}, {"shuo", "说"}, {"si", "四思"}, {"song", "送"},
    {"ta", "他她它"}, {"tai", "太台"}, {"tan", "谈"}, {"tao", "讨"},
    {"ti", "体题"}, {"tian", "天田"}, {"tiao", "条"}, {"ting", "听"},
    {"tong", "同通"}, {"tou", "头"}, {"tu", "图土"}, {"tui", "退"},
    {"wan", "完晚万"}, {"wang", "王网"}, {"wei", "为位未"}, {"wen", "问文"},
    {"wo", "我窝握"}, {"wu", "无五"}, {"xi", "西希习喜"}, {"xia", "下夏"},
    {"xian", "先现线"}, {"xiang", "想向香"}, {"xiao", "小笑"}, {"xie", "些写谢"},
    {"xin", "心新"}, {"xing", "行性"}, {"xiong", "兄"}, {"xu", "需许"},
    {"xue", "学"}, {"ya", "呀"}, {"yan", "眼言"}, {"yang", "样阳"},
    {"yao", "要药"}, {"ye", "也夜"}, {"yi", "一以已意衣"}, {"yin", "因音"},
    {"ying", "应影"}, {"yong", "用永"}, {"you", "有又友"}, {"yu", "于鱼"},
    {"yuan", "元原远"}, {"yue", "月"}, {"yun", "云运"}, {"zai", "在再"},
    {"zao", "早"}, {"zhan", "站"}, {"zhang", "张长"}, {"zhao", "找照"},
    {"zhe", "这着"}, {"zhen", "真"}, {"zheng", "正整"}, {"zhi", "之只知直"},
    {"zhong", "中种重众"}, {"zhou", "周"}, {"zhu", "主住"}, {"zhuang", "装"},
    {"zhuo", "着"}, {"zi", "子字"}, {"zong", "总"}, {"zu", "组"},
    {"zuo", "做作坐"}, {NULL, NULL}
};
#endif

typedef struct {
    const char *pinyin;
    const char *text;
} ai_chat_phrase_t;

static const ai_chat_phrase_t ai_chat_phrases[] = {
    {"ni", "你"}, {"ni", "尼"}, {"ni", "泥"}, {"ni", "拟"}, {"ni", "逆"},
    {"nihao", "你好"}, {"ninhao", "您好"}, {"nimen", "你们"},
    {"nishi", "你是"}, {"nixiang", "你想"}, {"nengbuneng", "能不能"},
    {"women", "我们"}, {"woshi", "我是"}, {"woxiang", "我想"},
    {"woya", "我要"}, {"woxuyao", "我需要"}, {"wozhidao", "我知道"},
    {"wobuzhidao", "我不知道"}, {"wenti", "问题"}, {"huida", "回答"},
    {"bangzhu", "帮助"}, {"xiexie", "谢谢"}, {"duoxie", "多谢"},
    {"zaijian", "再见"}, {"qingwen", "请问"}, {"qingbangwo", "请帮我"},
    {"qinggeiwo", "请给我"}, {"haode", "好的"}, {"keyi", "可以"},
    {"meiguanxi", "没关系"}, {"meiwenti", "没问题"}, {"bukeqi", "不客气"},
    {"buyao", "不要"}, {"bukeyi", "不可以"}, {"buzhidao", "不知道"},
    {"buhuida", "不会的"}, {"zenme", "怎么"}, {"zenmeyang", "怎么样"},
    {"shenme", "什么"}, {"shenmeshihou", "什么时候"}, {"weishenme", "为什么"},
    {"zheli", "这里"}, {"nali", "哪里"}, {"xianzai", "现在"},
    {"jintian", "今天"}, {"mingtian", "明天"}, {"zuotian", "昨天"},
    {"shijian", "时间"}, {"tianqi", "天气"}, {"yuyin", "语音"},
    {"yinpin", "音频"}, {"dianhua", "电话"}, {"diannao", "电脑"},
    {"shouji", "手机"}, {"wangluo", "网络"}, {"fuwuqi", "服务器"},
    {"chengxu", "程序"}, {"xuexi", "学习"}, {"gongzuo", "工作"},
    {"pengyou", "朋友"}, {"jiating", "家庭"}, {"kaixin", "开心"},
    {"zaoan", "早安"}, {"zaoshanghao", "早上好"}, {"wanshanghao", "晚上好"},
    {"renzhineng", "人工智能"}, {"aizhushou", "AI助手"}, {"dakai", "打开"},
    {"guanbi", "关闭"}, {"chaxun", "查询"}, {"bangwochaxun", "帮我查询"},
    {"bofang", "播放"}, {"zanting", "暂停"}, {"jixu", "继续"},
    {"shuru", "输入"}, {"fasong", "发送"}, {"xianshi", "显示"},
    {"zhuanhuan", "转换"}, {"shengcheng", "生成"}, {"wenda", "问答"},
    {"zixun", "咨询"}, {"yonghu", "用户"}, {"xiangqing", "详情"},
    {"duqu", "读取"}, {"wenjian", "文件"}, {"shuju", "数据"},
    {"jishu", "技术"}, {"xiaoxi", "消息"}, {"zhongwen", "中文"},
    {"jiantizhongwen", "简体中文"}, {"pinyin", "拼音"}, {"shuchu", "输出"},
    {"qingchu", "清楚"}, {"xiexieni", "谢谢你"}, {"bangwoxie", "帮我写"},
    {"duoshao", "多少"}, {"zenmezuo", "怎么做"}, {"zaima", "在吗"},
    {"wozaizheli", "我在这里"}, {"woxiangwen", "我想问"}, {"woxiangliaojie", "我想了解"}
};

static uint32_t ai_chat_glyph_cache_codepoints[192];
static int8_t ai_chat_glyph_cache_results[192];
static uint16_t ai_chat_glyph_cache_count;

/* Keyboard screen for city input */
static lv_obj_t *kb_screen;
static lv_obj_t *kb_textarea;
static lv_obj_t *kb_keyboard;
static lv_obj_t *city_btn_label;
static lv_obj_t *weather_page_city_label;
static lv_obj_t *weather_page_icon;
static lv_obj_t *weather_page_condition;
static lv_obj_t *weather_page_temp;
static lv_obj_t *weather_forecast_labels[3];

/* Settings & WiFi screens */
static lv_obj_t *settings_screen;
#define DEVICE_INFO_ROW_COUNT 7
static lv_obj_t *device_info_screen;
static lv_obj_t *device_info_values[DEVICE_INFO_ROW_COUNT];
static lv_timer_t *device_info_timer;
static lv_obj_t *wifi_list_screen;
static lv_obj_t *wifi_pwd_screen;
static lv_obj_t *wifi_pwd_textarea;
static lv_obj_t *wifi_pwd_keyboard;
static lv_obj_t *wifi_list;
static lv_obj_t *wifi_connection_status_label;
static lv_obj_t *wifi_connection_ssid_label;
static lv_obj_t *wifi_password_ssid_label;
static char wifi_selected_ssid[33];
static lv_obj_t *brightness_screen;
static lv_obj_t *brightness_label;
static lv_obj_t *brightness_slider;
static lv_obj_t *volume_screen;
static lv_obj_t *volume_label;
static lv_obj_t *volume_slider;

#define WIFI_ICON_SIZE 56
static LV_ATTRIBUTE_MEM_ALIGN uint8_t wifi_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(WIFI_ICON_SIZE, WIFI_ICON_SIZE)];
static lv_img_dsc_t wifi_img;
#define VOLUME_ICON_SIZE 56
static LV_ATTRIBUTE_MEM_ALIGN uint8_t volume_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(VOLUME_ICON_SIZE, VOLUME_ICON_SIZE)];
static lv_img_dsc_t volume_img;

static lv_obj_t *create_back_button(lv_obj_t *parent, lv_align_t align,
                                    lv_coord_t x_offset, lv_event_cb_t callback)
{
    static lv_point_t return_curve[] = {
        {7, 29}, {29, 29}, {37, 27}, {42, 22}, {44, 16},
        {42, 10}, {37, 6}, {29, 4}, {13, 4}
    };
    static lv_point_t arrow_head[] = {
        {19, 0}, {12, 4}, {19, 10}
    };

    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 62, 42);
    lv_obj_align(button, align, x_offset, 0);
    lv_obj_set_style_radius(button, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_PRESSED), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *curve = lv_line_create(button);
    lv_line_set_points(curve, return_curve, sizeof(return_curve) / sizeof(return_curve[0]));
    lv_obj_set_pos(curve, 6, 5);
    lv_obj_set_style_line_width(curve, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(curve, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(curve, true, LV_PART_MAIN);

    lv_obj_t *head = lv_line_create(button);
    lv_line_set_points(head, arrow_head, sizeof(arrow_head) / sizeof(arrow_head[0]));
    lv_obj_set_pos(head, 6, 5);
    lv_obj_set_style_line_width(head, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(head, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(head, true, LV_PART_MAIN);

    lv_obj_clear_flag(curve, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
    return button;
}

static lv_obj_t *create_legacy_back_button(lv_obj_t *parent, lv_align_t align,
                                    lv_coord_t x_offset, lv_event_cb_t callback)
{
    static lv_point_t return_curve[] = {
        {7, 29}, {29, 29}, {37, 27}, {42, 22}, {44, 16},
        {42, 10}, {37, 6}, {29, 4}, {13, 4}
    };
    static lv_point_t arrow_head[] = {
        {19, 0}, {12, 4}, {19, 10}
    };

    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, 62, 42);
    lv_obj_align(button, align, x_offset, 0);
    lv_obj_set_style_radius(button, 9, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *curve = lv_line_create(button);
    lv_line_set_points(curve, return_curve, sizeof(return_curve) / sizeof(return_curve[0]));
    lv_obj_set_pos(curve, 6, 5);
    lv_obj_set_style_line_width(curve, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(curve, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(curve, true, LV_PART_MAIN);

    lv_obj_t *head = lv_line_create(button);
    lv_line_set_points(head, arrow_head, sizeof(arrow_head) / sizeof(arrow_head[0]));
    lv_obj_set_pos(head, 6, 5);
    lv_obj_set_style_line_width(head, 6, LV_PART_MAIN);
    lv_obj_set_style_line_color(head, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(head, true, LV_PART_MAIN);

    lv_obj_clear_flag(curve, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_CLICKABLE);
    return button;
}

static const lv_font_t *ui_chinese_font_16(const lv_font_t *fallback)
{
    /* Keep the existing call sites, but use the clearer 20 px SD font. */
    return chinese_font_20 ? chinese_font_20 :
           (chinese_font_16 ? chinese_font_16 : &lv_font_chinese_tianqi_16);
}

static const lv_font_t *ui_chinese_font_20(const lv_font_t *fallback)
{
    return chinese_font_20 ? chinese_font_20 : fallback;
}

static const lv_font_t *ui_chinese_font_32(const lv_font_t *fallback)
{
    return chinese_font_32 ? chinese_font_32 : fallback;
}

/*
 * LVGL's keyboard edits its assigned textarea, but it does not manage the
 * keyboard's visibility.  Keep that focus-to-keyboard bridge consistent for
 * every text input in the UI.
 */
static void show_text_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea)
{
    if (!keyboard || !textarea) return;

    lv_keyboard_set_textarea(keyboard, textarea);
    lv_coord_t height = lv_obj_get_height(keyboard);
    if (height <= 0 || height > SCREEN_HEIGHT) height = 220;
    /* lv_keyboard is created with BOTTOM_MID alignment.  Clear that style
     * alignment before using absolute screen coordinates, otherwise set_pos
     * is interpreted as an offset and the keyboard remains partly off-screen. */
    lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(keyboard, 0, SCREEN_HEIGHT - height);
    lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(keyboard);
}

static void hide_text_keyboard(lv_obj_t *keyboard)
{
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void text_input_event_cb(lv_event_t *e)
{
    lv_obj_t *textarea = lv_event_get_target(e);
    lv_obj_t *keyboard = (lv_obj_t *)lv_event_get_user_data(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        show_text_keyboard(keyboard, textarea);
    } else if (code == LV_EVENT_DEFOCUSED) {
        hide_text_keyboard(keyboard);
    }
}

static void text_keyboard_event_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        hide_text_keyboard(lv_event_get_target(e));
    }
}

static const char *ui_city_name(void)
{
    return chinese_font_32 ? WEATHER_CITY_DISPLAY : WEATHER_CITY;
}

/* Hand-drawn gyro icon (circle + crossing ellipses + dot) */
#define GYRO_ICON_SIZE 56
static LV_ATTRIBUTE_MEM_ALIGN uint8_t gyro_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(GYRO_ICON_SIZE, GYRO_ICON_SIZE)];
static lv_img_dsc_t gyro_img;

static void create_gyro_icon_legacy(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, gyro_icon_buf, GYRO_ICON_SIZE, GYRO_ICON_SIZE,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    int cx = GYRO_ICON_SIZE / 2, cy = GYRO_ICON_SIZE / 2;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(0x4444FF);
    arc.rounded = 1;

    // 外圈
    arc.width = 3;
    lv_canvas_draw_arc(canvas, cx, cy, 23, 0, 3600, &arc);

    // 两个交叉扁椭圆（厚弧线）
    arc.width = 8;
    lv_canvas_draw_arc(canvas, cx, cy, 15, 300, 2100, &arc);   // 左上→右下
    lv_canvas_draw_arc(canvas, cx, cy, 15, 1200, 3000, &arc);  // 右上→左下

    // 中心黑点
    arc.color = lv_color_hex(0x000000);
    arc.width = 9;
    lv_canvas_draw_arc(canvas, cx, cy, 4, 0, 3600, &arc);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&gyro_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

static void gyro_draw_ellipse(lv_obj_t *canvas, float angle,
                              float radius_x, float radius_y,
                              lv_color_t color, uint8_t width)
{
    lv_point_t points[25];
    const float cos_angle = cosf(angle);
    const float sin_angle = sinf(angle);
    const float two_pi = 6.283185307f;
    for (int i = 0; i < 25; ++i) {
        const float t = two_pi * (float)i / 24.0f;
        const float ex = cosf(t) * radius_x;
        const float ey = sinf(t) * radius_y;
        points[i].x = (lv_coord_t)lroundf(28.0f + ex * cos_angle - ey * sin_angle);
        points[i].y = (lv_coord_t)lroundf(28.0f + ex * sin_angle + ey * cos_angle);
    }

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = width;
    line.round_start = 1;
    line.round_end = 1;
    lv_canvas_draw_line(canvas, points, 25, &line);
}

static void create_gyro_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, gyro_icon_buf, GYRO_ICON_SIZE, GYRO_ICON_SIZE,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    const int cx = GYRO_ICON_SIZE / 2;
    const int cy = GYRO_ICON_SIZE / 2;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.rounded = 1;

    /* Outer orbit. */
    arc.color = lv_color_hex(0x1677E8);
    arc.width = 4;
    lv_canvas_draw_arc(canvas, cx, cy, 23, 0, 3600, &arc);

    /* Diagonal spin axis with two blue end caps. */
    lv_draw_line_dsc_t axis;
    lv_draw_line_dsc_init(&axis);
    axis.color = lv_color_hex(0x0B4DA5);
    axis.width = 4;
    axis.round_start = 1;
    axis.round_end = 1;
    static const lv_point_t axis_points[] = {{7, 49}, {49, 7}};
    lv_canvas_draw_line(canvas, axis_points, 2, &axis);
    arc.color = lv_color_hex(0x2F9BFF);
    arc.width = 8;
    lv_canvas_draw_arc(canvas, 7, 49, 1, 0, 3600, &arc);
    lv_canvas_draw_arc(canvas, 49, 7, 1, 0, 3600, &arc);

    /* Crossing tilted rings, matching the reference silhouette. */
    gyro_draw_ellipse(canvas, -0.55f, 19.0f, 9.0f, lv_color_hex(0x55C7FF), 4);
    gyro_draw_ellipse(canvas, 0.55f, 19.0f, 9.0f, lv_color_hex(0x0D5EC3), 4);

    /* Layered blue center sphere. */
    arc.color = lv_color_hex(0x0A4C9F);
    arc.width = 15;
    lv_canvas_draw_arc(canvas, cx, cy, 7, 0, 3600, &arc);
    arc.color = lv_color_hex(0x8DE4FF);
    arc.width = 7;
    lv_canvas_draw_arc(canvas, cx - 1, cy - 1, 3, 0, 3600, &arc);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&gyro_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* Compact reference-style thermometer beside the live MPU temperature. */
#define HOME_TEMP_ICON_SIZE 40
static LV_ATTRIBUTE_MEM_ALIGN uint8_t home_temp_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(HOME_TEMP_ICON_SIZE, HOME_TEMP_ICON_SIZE)];
static lv_img_dsc_t home_temp_img;

static void create_home_temp_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, home_temp_icon_buf, HOME_TEMP_ICON_SIZE,
                         HOME_TEMP_ICON_SIZE, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;

    /* Six alternating long/short, square-ended scale marks. */
    rect.bg_color = lv_color_hex(0x232B2B);
    rect.radius = 0;
    lv_canvas_draw_rect(canvas, 8, 6, 10, 2, &rect);
    lv_canvas_draw_rect(canvas, 12, 10, 6, 2, &rect);
    lv_canvas_draw_rect(canvas, 8, 13, 10, 2, &rect);
    lv_canvas_draw_rect(canvas, 12, 17, 6, 2, &rect);
    lv_canvas_draw_rect(canvas, 8, 20, 10, 2, &rect);
    lv_canvas_draw_rect(canvas, 12, 24, 6, 2, &rect);

    /* Paint both outer shapes before their white interiors for a joined neck. */
    rect.radius = 5;
    lv_canvas_draw_rect(canvas, 20, 1, 10, 29, &rect);
    rect.radius = 8;
    lv_canvas_draw_rect(canvas, 17, 23, 16, 16, &rect);
    rect.bg_color = lv_color_hex(0xFFFFFF);
    rect.radius = 3;
    lv_canvas_draw_rect(canvas, 22, 3, 6, 28, &rect);
    rect.radius = 6;
    lv_canvas_draw_rect(canvas, 19, 25, 12, 12, &rect);

    /* Continuous rounded mercury column and circular red bulb. */
    rect.bg_color = lv_color_hex(0xFF241C);
    rect.radius = 2;
    lv_canvas_draw_rect(canvas, 23, 13, 4, 19, &rect);
    rect.radius = 5;
    lv_canvas_draw_rect(canvas, 20, 26, 10, 10, &rect);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&home_temp_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* Hand-drawn game controller icon for the balance-ball app entry. */
#define GAME_ICON_SIZE 64
static LV_ATTRIBUTE_MEM_ALIGN uint8_t game_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(GAME_ICON_SIZE, GAME_ICON_SIZE)];
static lv_img_dsc_t game_img;

static void create_game_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, game_icon_buf, GAME_ICON_SIZE, GAME_ICON_SIZE,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    static const lv_point_t shell[] = {
        {8, 47}, {7, 38}, {10, 28}, {15, 17}, {21, 10}, {30, 10},
        {34, 17}, {42, 17}, {46, 10}, {54, 10}, {59, 22}, {62, 37},
        {60, 48}, {55, 55}, {50, 55}, {43, 43}, {21, 43}, {14, 54},
        {9, 54}, {8, 47}
    };
    static const lv_point_t left_shell[] = {
        {8, 47}, {7, 38}, {10, 28}, {15, 17}, {21, 10}, {30, 10}, {34, 17}
    };
    static const lv_point_t right_shell[] = {
        {34, 17}, {42, 17}, {46, 10}, {54, 10}, {59, 22}, {62, 37},
        {60, 48}, {55, 55}, {50, 55}, {43, 43}
    };

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.width = 9;
    line.round_start = 1;
    line.round_end = 1;
    line.color = lv_color_hex(0xEF6A3E);
    lv_canvas_draw_line(canvas, shell, sizeof(shell) / sizeof(shell[0]), &line);
    line.color = lv_color_hex(0xE84C3D);
    lv_canvas_draw_line(canvas, left_shell, sizeof(left_shell) / sizeof(left_shell[0]), &line);
    line.color = lv_color_hex(0xF7B92E);
    lv_canvas_draw_line(canvas, right_shell, sizeof(right_shell) / sizeof(right_shell[0]), &line);

    /* Direction pad. */
    static const lv_point_t dpad_h[] = {{13, 29}, {27, 29}};
    static const lv_point_t dpad_v[] = {{20, 22}, {20, 36}};
    line.width = 8;
    line.color = lv_color_hex(0xE9573F);
    lv_canvas_draw_line(canvas, dpad_h, 2, &line);
    lv_canvas_draw_line(canvas, dpad_v, 2, &line);

    /* Four rounded face buttons. */
    lv_draw_arc_dsc_t dot;
    lv_draw_arc_dsc_init(&dot);
    dot.width = 10;
    dot.rounded = 1;
    dot.color = lv_color_hex(0xF2A832);
    lv_canvas_draw_arc(canvas, 43, 27, 5, 0, 3600, &dot);
    lv_canvas_draw_arc(canvas, 50, 34, 5, 0, 3600, &dot);
    lv_canvas_draw_arc(canvas, 43, 41, 5, 0, 3600, &dot);
    lv_canvas_draw_arc(canvas, 36, 34, 5, 0, 3600, &dot);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&game_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* Four-tile 2048 icon with a transparent background. */
#define NUMPAD_ICON_SIZE 56
static LV_ATTRIBUTE_MEM_ALIGN uint8_t numpad_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(NUMPAD_ICON_SIZE, NUMPAD_ICON_SIZE)];
static lv_img_dsc_t numpad_img;

static void numpad_draw_digit(lv_obj_t *canvas, int x, int y, int digit)
{
    static const uint8_t digit_segments[10] = {
        0x3F, /* 0: a b c d e f */
        0x06, /* 1 */
        0x5B, /* 2: a b g e d */
        0x4F, /* 3 */
        0x66, /* 4: f g b c */
        0x6D, /* 5 */
        0x7D, /* 6 */
        0x07, /* 7 */
        0x7F, /* 8 */
        0x6F  /* 9 */
    };
    static const lv_point_t segments[7][2] = {
        {{6, 4},  {18, 4}},  /* a */
        {{18, 4}, {18, 12}}, /* b */
        {{18, 12}, {18, 20}},/* c */
        {{6, 20}, {18, 20}}, /* d */
        {{6, 12}, {6, 20}},  /* e */
        {{6, 4},  {6, 12}},  /* f */
        {{6, 12}, {18, 12}}  /* g */
    };
    if (digit < 0 || digit > 9) return;

    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(0xFFFFFF);
    line.width = 3;
    line.round_start = 1;
    line.round_end = 1;
    const uint8_t mask = digit_segments[digit];
    for (int i = 0; i < 7; ++i) {
        if (!(mask & (1U << i))) continue;
        lv_point_t points[2] = {
            {(lv_coord_t)(x + segments[i][0].x),
             (lv_coord_t)(y + segments[i][0].y)},
            {(lv_coord_t)(x + segments[i][1].x),
             (lv_coord_t)(y + segments[i][1].y)}
        };
        lv_canvas_draw_line(canvas, points, 2, &line);
    }
}

static void create_numpad_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, numpad_icon_buf, NUMPAD_ICON_SIZE,
                         NUMPAD_ICON_SIZE, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_opa = LV_OPA_COVER;
    rect.radius = 6;
    const uint32_t colors[4] = {0xF2B179, 0xEDC53F, 0xF67C5F, 0xEDC22E};
    const int digits[4] = {2, 0, 4, 8};
    for (int i = 0; i < 4; ++i) {
        const int x = 3 + (i & 1) * 27;
        const int y = 3 + (i >> 1) * 27;
        rect.bg_color = lv_color_hex(colors[i]);
        lv_canvas_draw_rect(canvas, x, y, 24, 24, &rect);
        numpad_draw_digit(canvas, x, y, digits[i]);
    }

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&numpad_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* ==================== 2048 GAME ==================== */

static lv_obj_t *game_screen;
static lv_obj_t *game_tiles[4][4];
static lv_obj_t *game_tile_boxes[4][4];
static lv_obj_t *game_score_label;
static int game_grid[4][4];
static int game_score;

/* Gyro-controlled balance-ball game. */
static lv_obj_t *balance_screen;
static lv_obj_t *balance_table;
static lv_obj_t *balance_ball;
static lv_obj_t *balance_hole;
static lv_obj_t *balance_score_label;
static lv_obj_t *balance_angle_label;
static float balance_ball_x;
static float balance_ball_y;
static float balance_ball_vx;
static float balance_ball_vy;
static float balance_zero_roll;
static float balance_zero_pitch;
static int balance_score;
static bool balance_game_active;

#define BALANCE_TABLE_W 420
#define BALANCE_TABLE_H 320
#define BALANCE_BALL_SIZE 22
#define BALANCE_HOLE_SIZE 38

/* Warm 2048 palette: the value blocks become darker as numbers increase. */
static const uint32_t tile_colors[] = {
    0xCDC1B4, /* empty */
    0xEEE4DA, /* 2 */
    0xE8DCC8, /* 4 */
    0xDFC7A4, /* 8 */
    0xD5A97A, /* 16 */
    0xC78B5C, /* 32 */
    0xB96F48, /* 64 */
    0xA8563E, /* 128 */
    0x914133, /* 256 */
    0x773329, /* 512 */
    0x5D2723, /* 1024 */
    0x45201F, /* 2048 */
    0x30191B  /* 4096 and above */
};

static int tile_level(int val) {
    if (val <= 0) return 0;
    int level = 1;
    while (val > 2 && level < 12) {
        val >>= 1;
        level++;
    }
    return level;
}

static uint32_t tile_bg(int val) {
    return tile_colors[tile_level(val)];
}

static uint32_t tile_border(int val) {
    static const uint32_t borders[] = {
        0xB3A79B, 0xD6C7B9, 0xCBB99F, 0xB48F68, 0xA17155,
        0x945E45, 0x874A3B, 0x77382F, 0x652D27, 0x52231F,
        0x401D1C, 0x33181A, 0x241316
    };
    return borders[tile_level(val)];
}

static uint32_t tile_fg(int val) {
    return (val <= 4) ? 0x776E65 : 0xF9F6F2;
}

static const lv_font_t *game_tile_font(int val) {
    if (val >= 10000) return &lv_font_montserrat_16;
    if (val >= 1000) return &lv_font_montserrat_20;
    if (val >= 100) return &lv_font_montserrat_24;
    return &lv_font_montserrat_28;
}

static void game_add_random(void) {
    int empty[16][2], n = 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (game_grid[r][c] == 0) { empty[n][0] = r; empty[n][1] = c; n++; }
    if (n == 0) return;
    int idx = rand() % n;
    game_grid[empty[idx][0]][empty[idx][1]] = (rand() % 10 < 9) ? 2 : 4;
}

static void game_update_tiles(void) {
    static char buf[8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            int v = game_grid[r][c];
            /* The background belongs to the tile container, not its label. */
            lv_obj_set_style_bg_color(game_tile_boxes[r][c], lv_color_hex(tile_bg(v)), 0);
            lv_obj_set_style_bg_opa(game_tile_boxes[r][c], LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(game_tile_boxes[r][c], lv_color_hex(tile_border(v)), 0);
            if (v > 0) {
                snprintf(buf, sizeof(buf), "%d", v);
                lv_label_set_text(game_tiles[r][c], buf);
                lv_obj_set_style_text_color(game_tiles[r][c], lv_color_hex(tile_fg(v)), 0);
                lv_obj_set_style_text_font(game_tiles[r][c], game_tile_font(v), 0);
            } else {
                lv_label_set_text(game_tiles[r][c], "");
            }
        }
    }
    snprintf(buf, sizeof(buf), "SCORE  %d", game_score);
    lv_label_set_text(game_score_label, buf);
}

static bool game_move_line(int *line) {
    int merged[4] = {0};
    int pos = 0;
    bool moved = false;
    for (int i = 0; i < 4; i++) {
        if (line[i] == 0) continue;
        if (pos > 0 && line[i] == merged[pos - 1]) {
            merged[pos - 1] *= 2;
            game_score += merged[pos - 1];
            line[i] = 0;
            moved = true;
        } else {
            if (pos != i) moved = true;
            merged[pos++] = line[i];
        }
    }
    for (int i = 0; i < 4; i++) line[i] = (i < pos) ? merged[i] : 0;
    return moved;
}

static bool game_move(int dr, int dc) {
    bool moved = false;
    int line[4];
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 4; i++) {
            int rr = r, cc = i;
            if (dr != 0) { rr = (dr > 0) ? 3 - i : i; cc = r; }
            else          { cc = (dc > 0) ? 3 - i : i; }
            line[i] = game_grid[rr][cc];
        }
        if (game_move_line(line)) moved = true;
        for (int i = 0; i < 4; i++) {
            int rr = r, cc = i;
            if (dr != 0) { rr = (dr > 0) ? 3 - i : i; cc = r; }
            else          { cc = (dc > 0) ? 3 - i : i; }
            game_grid[rr][cc] = line[i];
        }
    }
    return moved;
}

static void balance_place_hole(void)
{
    if (!balance_hole) return;
    /* The target is the fixed hole in the middle of the tabletop. */
    lv_obj_set_pos(balance_hole,
                   (BALANCE_TABLE_W - BALANCE_HOLE_SIZE) / 2,
                   (BALANCE_TABLE_H - BALANCE_HOLE_SIZE) / 2);
}

static void balance_render_labels(float roll_delta, float pitch_delta)
{
    static char score_text[24];
    static char angle_text[64];
    snprintf(score_text, sizeof(score_text), "SCORE  %d", balance_score);
    snprintf(angle_text, sizeof(angle_text), "相对R %+5.1f   相对P %+5.1f",
             roll_delta, pitch_delta);
    if (balance_score_label) lv_label_set_text_static(balance_score_label, score_text);
    if (balance_angle_label) lv_label_set_text_static(balance_angle_label, angle_text);
}

static void balance_game_reset(bool reset_score)
{
    balance_zero_roll = gyro_pending_roll;
    balance_zero_pitch = gyro_pending_pitch;
    balance_ball_vx = 0.0f;
    balance_ball_vy = 0.0f;
    balance_ball_x = 24.0f;
    balance_ball_y = (BALANCE_TABLE_H - BALANCE_BALL_SIZE) * 0.5f;
    if (reset_score) balance_score = 0;
    if (balance_ball) {
        lv_obj_set_pos(balance_ball, (int)balance_ball_x, (int)balance_ball_y);
    }
    balance_place_hole();
    balance_render_labels(0.0f, 0.0f);
}

static void balance_center_cb(lv_event_t *e)
{
    (void)e;
    balance_game_reset(false);
}

static void balance_back_cb(lv_event_t *e)
{
    (void)e;
    balance_game_active = false;
    lv_scr_load(main_screen_ref);
}

static void balance_game_btn_cb(lv_event_t *e)
{
    (void)e;
    balance_game_active = true;
    balance_game_reset(true);
    lv_scr_load(balance_screen);
}

static void balance_game_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!balance_game_active || !balance_ball || !balance_hole) return;

    const float roll_delta = gyro_pending_roll - balance_zero_roll;
    const float pitch_delta = gyro_pending_pitch - balance_zero_pitch;
    /*
     * Convert the relative tilt into acceleration.  The sensor has a small
     * amount of idle noise, so ignore only the first half degree and preserve
     * the rest of the tilt.  The response is intentionally strong enough that
     * a 1-3 degree hand tilt is visible on the 420 px tabletop.
     */
    const float deadzone = 0.5f;
    const float roll_abs = fabsf(roll_delta);
    const float pitch_abs = fabsf(pitch_delta);
    const float roll_control = (roll_abs > deadzone)
        ? copysignf((roll_abs - deadzone) * 1.8f, roll_delta) : 0.0f;
    const float pitch_control = (pitch_abs > deadzone)
        ? copysignf((pitch_abs - deadzone) * 1.8f, pitch_delta) : 0.0f;
    /* The sensor is mounted 90 degrees from the screen axes: swap X/Y. */
    /* Pitch direction is inverted for this sensor mounting orientation. */
    balance_ball_vx -= pitch_control * 0.045f;
    balance_ball_vy += roll_control * 0.045f;
    balance_ball_vx *= 0.94f;
    balance_ball_vy *= 0.94f;
    if (balance_ball_vx > 8.0f) balance_ball_vx = 8.0f;
    if (balance_ball_vx < -8.0f) balance_ball_vx = -8.0f;
    if (balance_ball_vy > 8.0f) balance_ball_vy = 8.0f;
    if (balance_ball_vy < -8.0f) balance_ball_vy = -8.0f;

    balance_ball_x += balance_ball_vx;
    balance_ball_y += balance_ball_vy;
    const float max_x = BALANCE_TABLE_W - BALANCE_BALL_SIZE - 4.0f;
    const float max_y = BALANCE_TABLE_H - BALANCE_BALL_SIZE - 4.0f;
    if (balance_ball_x < 4.0f) {
        balance_ball_x = 4.0f;
        balance_ball_vx *= -0.35f;
    } else if (balance_ball_x > max_x) {
        balance_ball_x = max_x;
        balance_ball_vx *= -0.35f;
    }
    if (balance_ball_y < 4.0f) {
        balance_ball_y = 4.0f;
        balance_ball_vy *= -0.35f;
    } else if (balance_ball_y > max_y) {
        balance_ball_y = max_y;
        balance_ball_vy *= -0.35f;
    }

    lv_obj_set_pos(balance_ball, (int)balance_ball_x, (int)balance_ball_y);

    const float hole_x = (float)lv_obj_get_x(balance_hole) + BALANCE_HOLE_SIZE * 0.5f;
    const float hole_y = (float)lv_obj_get_y(balance_hole) + BALANCE_HOLE_SIZE * 0.5f;
    const float ball_x = balance_ball_x + BALANCE_BALL_SIZE * 0.5f;
    const float ball_y = balance_ball_y + BALANCE_BALL_SIZE * 0.5f;
    const float hit_x = ball_x - hole_x;
    const float hit_y = ball_y - hole_y;
    if (hit_x * hit_x + hit_y * hit_y < 14.0f * 14.0f) {
        balance_score += 100;
        balance_ball_x = 24.0f;
        balance_ball_y = (BALANCE_TABLE_H - BALANCE_BALL_SIZE) * 0.5f;
        balance_ball_vx = 0.0f;
        balance_ball_vy = 0.0f;
        lv_obj_set_pos(balance_ball, (int)balance_ball_x, (int)balance_ball_y);
        balance_place_hole();
    }
    balance_render_labels(roll_delta, pitch_delta);
}

static void create_balance_game_screen(void)
{
    balance_screen = lv_obj_create(NULL);
    lv_obj_set_size(balance_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(balance_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_background(balance_screen);

    lv_obj_t *title = lv_label_create(balance_screen);
    lv_label_set_text(title, "平衡球  /  BALANCE BALL");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 18, 12);
    create_back_button(balance_screen, LV_ALIGN_TOP_RIGHT, -8, balance_back_cb);

    balance_table = lv_obj_create(balance_screen);
    lv_obj_set_size(balance_table, BALANCE_TABLE_W, BALANCE_TABLE_H);
    lv_obj_set_pos(balance_table, 30, 68);
    lv_obj_set_style_bg_color(balance_table, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_grad_color(balance_table, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_grad_dir(balance_table, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(balance_table, 2, 0);
    lv_obj_set_style_border_color(balance_table, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(balance_table, 22, 0);
    lv_obj_set_style_shadow_width(balance_table, 16, 0);
    lv_obj_set_style_shadow_color(balance_table, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_shadow_opa(balance_table, LV_OPA_60, 0);
    lv_obj_set_style_pad_all(balance_table, 0, 0);
    lv_obj_clear_flag(balance_table, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Subtle table grid. */
    for (int i = 1; i < 4; i++) {
        lv_obj_t *line = lv_obj_create(balance_table);
        lv_obj_set_size(line, 1, BALANCE_TABLE_H - 28);
        lv_obj_set_pos(line, i * BALANCE_TABLE_W / 4, 14);
        lv_obj_set_style_bg_color(line, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_20, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    for (int i = 1; i < 3; i++) {
        lv_obj_t *line = lv_obj_create(balance_table);
        lv_obj_set_size(line, BALANCE_TABLE_W - 28, 1);
        lv_obj_set_pos(line, 14, i * BALANCE_TABLE_H / 3);
        lv_obj_set_style_bg_color(line, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(line, LV_OPA_20, 0);
        lv_obj_set_style_border_width(line, 0, 0);
        lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    balance_hole = lv_obj_create(balance_table);
    lv_obj_set_size(balance_hole, BALANCE_HOLE_SIZE, BALANCE_HOLE_SIZE);
    lv_obj_set_style_radius(balance_hole, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(balance_hole, lv_color_hex(0x02070B), 0);
    lv_obj_set_style_border_width(balance_hole, 4, 0);
    lv_obj_set_style_border_color(balance_hole, lv_color_hex(0xF0A22E), 0);
    lv_obj_set_style_shadow_width(balance_hole, 10, 0);
    lv_obj_set_style_shadow_color(balance_hole, lv_color_hex(0xF07832), 0);
    lv_obj_set_style_shadow_opa(balance_hole, LV_OPA_60, 0);
    lv_obj_clear_flag(balance_hole, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    balance_ball = lv_obj_create(balance_table);
    lv_obj_set_size(balance_ball, BALANCE_BALL_SIZE, BALANCE_BALL_SIZE);
    lv_obj_set_style_radius(balance_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(balance_ball, lv_color_hex(0xFF5A5F), 0);
    lv_obj_set_style_border_width(balance_ball, 3, 0);
    lv_obj_set_style_border_color(balance_ball, lv_color_hex(0xFFE7B0), 0);
    lv_obj_set_style_shadow_width(balance_ball, 12, 0);
    lv_obj_set_style_shadow_color(balance_ball, lv_color_hex(0xFF4E50), 0);
    lv_obj_set_style_shadow_opa(balance_ball, LV_OPA_70, 0);
    lv_obj_clear_flag(balance_ball, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    balance_score_label = lv_label_create(balance_screen);
    lv_label_set_text(balance_score_label, "SCORE  0");
    lv_obj_set_style_text_font(balance_score_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(balance_score_label, lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_align(balance_score_label, LV_ALIGN_TOP_LEFT, 30, 402);

    balance_angle_label = lv_label_create(balance_screen);
    lv_label_set_text(balance_angle_label, "相对R +00.0°   相对P +00.0°");
    lv_obj_set_style_text_font(balance_angle_label, ui_chinese_font_16(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(balance_angle_label, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(balance_angle_label, LV_ALIGN_TOP_LEFT, 30, 432);

    lv_obj_t *center = lv_btn_create(balance_screen);
    lv_obj_set_size(center, 124, 38);
    lv_obj_align(center, LV_ALIGN_TOP_RIGHT, -18, 404);
    lv_obj_set_style_bg_color(center, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(center, 1, 0);
    lv_obj_set_style_border_color(center, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(center, 10, 0);
    lv_obj_add_event_cb(center, balance_center_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *center_label = lv_label_create(center);
    lv_label_set_text(center_label, "校准中心");
    lv_obj_set_style_text_font(center_label, ui_chinese_font_16(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(center_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_center(center_label);

    balance_game_active = false;
    balance_game_reset(true);
}

static void game_restart(void) {
    memset(game_grid, 0, sizeof(game_grid));
    game_score = 0;
    game_add_random();
    game_add_random();
    game_update_tiles();
}

static void game_swipe_cb(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    static lv_coord_t sx, sy;
    lv_indev_t *indev = lv_indev_get_act();
    lv_point_t pt;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(indev, &pt);
        sx = pt.x; sy = pt.y;
    } else if (code == LV_EVENT_RELEASED) {
        lv_indev_get_point(indev, &pt);
        int dx = pt.x - sx, dy = pt.y - sy;
        if (abs(dx) < 20 && abs(dy) < 20) return;
        bool moved = false;
        if (abs(dx) > abs(dy))
            moved = game_move(0, dx > 0 ? 1 : -1);
        else
            moved = game_move(dy > 0 ? 1 : -1, 0);
        if (moved) {
            game_add_random();
            game_update_tiles();
        }
    }
}

static void create_game_screen(void)
{
    game_screen = lv_obj_create(NULL);
    lv_obj_set_size(game_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(game_screen, lv_color_hex(0xFAF8EF), 0);
    lv_obj_set_style_bg_opa(game_screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(game_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(game_screen, LV_OBJ_FLAG_SCROLLABLE);

    // 标题栏
    lv_obj_t *header = lv_obj_create(game_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 66);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xEDC22E), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    // ✕ 退出（右）
    create_legacy_back_button(header, LV_ALIGN_RIGHT_MID, -8, back_btn_cb);

    // 标题
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "2048");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 18, -6);

    lv_obj_t *mode = lv_label_create(header);
    lv_label_set_text(mode, "DEEP COLOR");
    lv_obj_set_style_text_font(mode, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mode, lv_color_hex(0x7A5A20), 0);
    lv_obj_align(mode, LV_ALIGN_LEFT_MID, 20, 22);

    // 分数
    game_score_label = lv_label_create(header);
    lv_label_set_text(game_score_label, "SCORE  0");
    lv_obj_set_style_text_font(game_score_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(game_score_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(game_score_label, LV_ALIGN_CENTER, 0, 0);

    // 重新开始
    lv_obj_t *restart = lv_btn_create(game_screen);
    lv_obj_set_size(restart, 116, 34);
    lv_obj_align(restart, LV_ALIGN_TOP_RIGHT, -78, 76);
    lv_obj_set_style_bg_color(restart, lv_color_hex(0x8F7A66), 0);
    lv_obj_set_style_bg_opa(restart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(restart, 0, 0);
    lv_obj_set_style_radius(restart, 5, 0);
    lv_obj_add_event_cb(restart, game_restart_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *rl = lv_label_create(restart);
    lv_label_set_text(rl, "Restart");
    lv_obj_set_style_text_font(rl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(rl);

    // 4×4 网格
    int tile_sz = 78, gap = 9;
    int gx = (SCREEN_WIDTH - (tile_sz * 4 + gap * 3)) / 2;
    int gy = 122;

    // 背景盘（滑动检测绑定到这里）
    lv_obj_t *board = lv_obj_create(game_screen);
    lv_obj_set_size(board, tile_sz * 4 + gap * 5, tile_sz * 4 + gap * 5);
    lv_obj_set_pos(board, gx - gap, gy - gap);
    lv_obj_set_style_bg_color(board, lv_color_hex(0xBBADA0), 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(board, 0, 0);
    lv_obj_set_style_radius(board, 8, 0);
    lv_obj_set_style_pad_all(board, 0, 0);
    lv_obj_add_flag(board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board, game_swipe_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(board, game_swipe_cb, LV_EVENT_RELEASED, NULL);

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            lv_obj_t *tile = lv_obj_create(game_screen);
            lv_obj_set_size(tile, tile_sz, tile_sz);
            lv_obj_set_pos(tile, gx + c * (tile_sz + gap), gy + r * (tile_sz + gap));
            lv_obj_set_style_bg_color(tile, lv_color_hex(tile_bg(0)), 0);
            lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(tile, 0, 0);
            lv_obj_set_style_border_color(tile, lv_color_hex(tile_border(0)), 0);
            lv_obj_set_style_radius(tile, 6, 0);
            lv_obj_set_style_pad_all(tile, 0, 0);
            lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE); // 不拦截触摸

            game_tile_boxes[r][c] = tile;
            game_tiles[r][c] = lv_label_create(tile);
            lv_obj_center(game_tiles[r][c]);
            lv_obj_set_style_text_font(game_tiles[r][c], &lv_font_montserrat_28, 0);
            lv_obj_set_style_text_color(game_tiles[r][c], lv_color_hex(0x776E65), 0);
        }
    }

    game_restart();
}

/* Hand-drawn gear icon (Settings) */
#define GEAR_SIZE 56
static LV_ATTRIBUTE_MEM_ALIGN lv_color_t gear_buf[GEAR_SIZE * GEAR_SIZE];
static lv_img_dsc_t gear_img;

static void create_gear_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, gear_buf, GEAR_SIZE, GEAR_SIZE, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    int cx = GEAR_SIZE / 2, cy = GEAR_SIZE / 2;
    int body_r = 18, tooth_r = 24, hole_r = 7;

    // 齿数 8 → 每个齿 45°
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.rounded = 1;

    for (int i = 0; i < 8; i++) {
        int start = i * 450 + 50;   // 齿从 5° 开始，宽 35°
        int end = start + 350;

        // 齿（从 body_r 延伸到 tooth_r）
        arc.color = lv_color_hex(0xAAAAAA);
        arc.width = (tooth_r - body_r) * 2;
        lv_canvas_draw_arc(canvas, cx, cy, body_r + (tooth_r - body_r) / 2, start, end, &arc);
    }

    // 外圈（齿轮体）
    arc.color = lv_color_hex(0xAAAAAA);
    arc.width = (body_r - hole_r - 5) * 2;
    lv_canvas_draw_arc(canvas, cx, cy, hole_r + 5 + arc.width / 4, 0, 3600, &arc);

    // 内圈镂空（背景色覆盖）
    arc.color = lv_color_hex(0x0a0a1a);
    arc.width = (body_r - hole_r - 12) * 2;
    lv_canvas_draw_arc(canvas, cx, cy, hole_r + 6 + arc.width / 4, 0, 3600, &arc);

    // 中心小圆
    arc.color = lv_color_hex(0xAAAAAA);
    arc.width = 8;
    lv_canvas_draw_arc(canvas, cx, cy, hole_r - 2, 0, 3600, &arc);

    // 导出图片，删除 canvas
    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&gear_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* ==================== HAND-DRAWN WIFI ICON ==================== */

static void create_wifi_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, wifi_icon_buf, WIFI_ICON_SIZE, WIFI_ICON_SIZE,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    const int cx = WIFI_ICON_SIZE / 2;
    const int cy = 45;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(0x54B9FF);
    arc.width = 6;
    arc.rounded = 1;

    // Three clean signal arcs, opening upward, plus the connection dot.
    lv_canvas_draw_arc(canvas, cx, cy, 25, 225, 315, &arc);
    arc.width = 5;
    lv_canvas_draw_arc(canvas, cx, cy, 17, 225, 315, &arc);
    arc.width = 4;
    lv_canvas_draw_arc(canvas, cx, cy, 9, 225, 315, &arc);

    lv_draw_arc_dsc_t dot;
    lv_draw_arc_dsc_init(&dot);
    dot.color = lv_color_hex(0x54B9FF);
    dot.width = 9;
    lv_canvas_draw_arc(canvas, cx, cy, 4, 0, 360, &dot);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&wifi_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

static void create_volume_icon(void)
{
    lv_obj_t *canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(canvas, volume_icon_buf, VOLUME_ICON_SIZE,
                         VOLUME_ICON_SIZE, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

    lv_draw_rect_dsc_t rect;
    lv_draw_rect_dsc_init(&rect);
    rect.bg_color = lv_color_hex(0x70D6FF);
    rect.bg_opa = LV_OPA_COVER;
    rect.radius = 3;
    static const lv_point_t speaker[] = {
        {8, 22}, {18, 22}, {31, 11}, {31, 45}, {18, 34}, {8, 34}
    };
    lv_canvas_draw_polygon(canvas, speaker, sizeof(speaker) / sizeof(speaker[0]),
                           &rect);

    lv_draw_line_dsc_t wave;
    lv_draw_line_dsc_init(&wave);
    wave.color = lv_color_hex(0x70D6FF);
    wave.width = 4;
    wave.round_start = 1;
    wave.round_end = 1;
    static const lv_point_t wave_one[] = {{37, 22}, {41, 27}, {37, 34}};
    static const lv_point_t wave_two[] = {{43, 16}, {50, 27}, {43, 40}};
    lv_canvas_draw_line(canvas, wave_one, 3, &wave);
    lv_canvas_draw_line(canvas, wave_two, 3, &wave);

    const lv_img_dsc_t *src = lv_canvas_get_img(canvas);
    memcpy(&volume_img, src, sizeof(lv_img_dsc_t));
    lv_obj_del(canvas);
}

/* ==================== SETTINGS SCREEN ==================== */

static void wifi_list_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    const char *txt = lv_label_get_text(lv_obj_get_child(btn, 0));
    if (txt) {
        strncpy(wifi_selected_ssid, txt, 32);
        wifi_selected_ssid[32] = '\0';
    }
    if (wifi_password_ssid_label) {
        char network_text[64];
        snprintf(network_text, sizeof(network_text), "SSID: %s",
                 wifi_selected_ssid[0] ? wifi_selected_ssid : "--");
        lv_label_set_text(wifi_password_ssid_label, network_text);
    }
    lv_textarea_set_text(wifi_pwd_textarea, "");
    lv_scr_load(wifi_pwd_screen);
}

static void wifi_update_connection_info(void)
{
    if (!wifi_connection_status_label || !wifi_connection_ssid_label) return;

    const bool connected = get_wifi_connected();
    const char *ssid = get_wifi_connected_ssid();
    lv_label_set_text(wifi_connection_status_label,
                      connected ? (chinese_font_20 ? "状态: 已连接" : "Status: Connected") :
                                   (chinese_font_20 ? "状态: 未连接" : "Status: Not connected"));
    char network_text[64];
    if (connected && ssid[0]) {
        snprintf(network_text, sizeof(network_text), "%s%s",
                 chinese_font_20 ? "WIFI: " : "Network: ", ssid);
    } else {
        snprintf(network_text, sizeof(network_text), "%s",
                 chinese_font_20 ? "WIFI: --" : "Network: --");
    }
    lv_label_set_text(wifi_connection_ssid_label, network_text);
    lv_obj_set_style_text_color(wifi_connection_status_label,
                                lv_color_hex(connected ? UI_COLOR_SUCCESS : UI_COLOR_ERROR), 0);
}

static void wifi_render_scan_results(void)
{
    if (!wifi_list) return;
    wifi_update_connection_info();
    lv_obj_clean(wifi_list);

    const int count = get_wifi_scan_count();
    if (count <= 0) {
        lv_obj_t *empty = lv_list_add_text(wifi_list,
            chinese_font_20 ? "没有发现 WiFi" : "No WiFi networks found");
        lv_obj_set_style_text_font(empty, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        return;
    }

    for (int i = 0; i < count; i++) {
        const char *ssid = get_wifi_ssid(i);
        lv_obj_t *btn = lv_list_add_btn(wifi_list, NULL, ssid);
        lv_obj_set_style_text_font(btn, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_add_event_cb(btn, wifi_list_btn_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void wifi_refresh_cb(lv_event_t *e)
{
    lv_obj_t *button = lv_event_get_target(e);
    lv_obj_t *label = lv_obj_get_child(button, 0);
    if (label) lv_label_set_text(label, chinese_font_20 ? "扫描中..." : "Scanning...");
    do_wifi_scan();
    wifi_render_scan_results();
    if (label) lv_label_set_text(label, chinese_font_20 ? "刷新" : "Refresh");
}

static void wifi_btn_cb(lv_event_t *e)
{
    do_wifi_scan();
    wifi_render_scan_results();
    lv_scr_load(wifi_list_screen);
}

static void wifi_pwd_confirm_cb(lv_event_t *e)
{
    const char *pwd = lv_textarea_get_text(wifi_pwd_textarea);
    const bool started = do_wifi_connect(wifi_selected_ssid, pwd);
    lv_obj_t *msg = lv_msgbox_create(NULL,
        started ? "WiFi Connecting" : "Failed",
        started ? "Please wait for the connection to finish" :
                  "Another connection is in progress",
        NULL, true);
    lv_obj_center(msg);
    lv_obj_add_event_cb(msg, (lv_event_cb_t)lv_obj_del, LV_EVENT_VALUE_CHANGED, msg);
}

static lv_obj_t *restart_button;
static lv_obj_t *restart_dialog;
static lv_timer_t *restart_timer;
static bool restart_dialog_closing;

static void restart_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    restart_timer = NULL;
    device_soft_restart();
}

static void restart_dialog_event_cb(lv_event_t *e)
{
    lv_obj_t *dialog = lv_event_get_current_target(e);
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_DELETE) {
        if (dialog == restart_dialog && lv_event_get_target(e) == dialog) {
            restart_dialog = NULL;
            restart_dialog_closing = false;
        }
        return;
    }
    if (code != LV_EVENT_VALUE_CHANGED || dialog != restart_dialog ||
        restart_dialog_closing || restart_timer) return;

    const uint16_t selected = lv_msgbox_get_active_btn(dialog);
    if (selected == 0) {
        restart_dialog_closing = true;
        lv_msgbox_close_async(dialog);
        return;
    }
    if (selected != 1) return;

    restart_timer = lv_timer_create(restart_timer_cb, 200, NULL);
    if (!restart_timer) {
        lv_label_set_text(lv_msgbox_get_text(dialog), chinese_font_20 ?
            "暂时无法重启，请稍后重试" : "Unable to restart. Please try again.");
        return;
    }
    lv_timer_set_repeat_count(restart_timer, 1);
    lv_btnmatrix_set_btn_ctrl_all(lv_msgbox_get_btns(dialog), LV_BTNMATRIX_CTRL_DISABLED);
    lv_obj_add_state(restart_button, LV_STATE_DISABLED);
    lv_label_set_text(lv_msgbox_get_text(dialog),
                      chinese_font_20 ? "正在重启..." : "Restarting...");
}

static void restart_btn_cb(lv_event_t *e)
{
    (void)e;
    if (restart_dialog || restart_timer) return;
    static const char *buttons_zh[] = {"取消", "重启", ""};
    static const char *buttons_en[] = {"Cancel", "Restart", ""};
    restart_dialog = lv_msgbox_create(NULL,
        chinese_font_20 ? "重启设备" : "Restart device",
        chinese_font_20 ? "确定重启设备？已保存的配置和 SD 文件会保留。" :
            "Restart the device? Saved settings and SD files are kept.",
        chinese_font_20 ? buttons_zh : buttons_en, false);
    if (!restart_dialog) return;

    const lv_font_t *font = ui_chinese_font_20(&lv_font_montserrat_16);
    lv_obj_set_width(restart_dialog, 400);
    lv_obj_set_style_radius(restart_dialog, 16, 0);
    lv_obj_set_style_bg_color(restart_dialog, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_color(restart_dialog, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(restart_dialog, 1, 0);
    lv_obj_set_style_pad_all(restart_dialog, 20, 0);
    lv_obj_t *title = lv_msgbox_get_title(restart_dialog);
    lv_obj_set_style_text_font(title, font, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_t *text = lv_msgbox_get_text(restart_dialog);
    lv_obj_set_style_text_font(text, font, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_t *buttons = lv_msgbox_get_btns(restart_dialog);
    lv_obj_set_width(buttons, lv_pct(100));
    lv_obj_set_height(buttons, 56);
    lv_obj_set_style_text_font(buttons, font, LV_PART_ITEMS);
    lv_obj_set_style_text_color(buttons, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(buttons, lv_color_hex(UI_COLOR_PRESSED), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(restart_dialog, restart_dialog_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_center(restart_dialog);
}

static lv_obj_t *display_restart_button;
static lv_obj_t *display_restart_title;
static lv_timer_t *display_restart_timer;

static void display_restart_timer_cb(lv_timer_t *timer)
{
    const display_restart_status_t status = display_restart_get_status();
    if (status != DISPLAY_RESTART_SUCCEEDED && status != DISPLAY_RESTART_FAILED) return;

    lv_label_set_text(display_restart_title, status == DISPLAY_RESTART_SUCCEEDED ?
        (chinese_font_20 ? "重启屏幕" : "Restart screen") :
        (chinese_font_20 ? "重启失败，请重试" : "Failed. Tap to retry"));
    lv_obj_clear_state(display_restart_button, LV_STATE_DISABLED);
    display_restart_timer = NULL;
    lv_timer_del(timer);
}

static void display_restart_btn_cb(lv_event_t *e)
{
    (void)e;
    if (display_restart_timer) return;
    display_restart_timer = lv_timer_create(display_restart_timer_cb, 50, NULL);
    if (!display_restart_timer || !display_restart_request()) {
        if (display_restart_timer) lv_timer_del(display_restart_timer);
        display_restart_timer = NULL;
        lv_label_set_text(display_restart_title,
                          chinese_font_20 ? "请稍后重试" : "Retry later");
        return;
    }
    lv_obj_add_state(display_restart_button, LV_STATE_DISABLED);
    lv_label_set_text(display_restart_title,
                      chinese_font_20 ? "正在重启..." : "Restarting...");
}

static void create_settings_screen(void)
{
    settings_screen = lv_obj_create(NULL);
    ui_theme_apply_background(settings_screen);

    lv_obj_t *header = lv_obj_create(settings_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, back_btn_cb);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "设置" : "Settings");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // WiFi button
    lv_obj_t *wifi_btn = make_theme_icon_btn(settings_screen, "WiFi", &wifi_img, 320, 0x4444FF,
        30, 80, 120, 140);
    lv_obj_set_style_text_font(wifi_btn, &lv_font_montserrat_16, 0);
    lv_obj_add_event_cb(wifi_btn, wifi_btn_cb, LV_EVENT_CLICKED, NULL);

    // Brightness button opens a dedicated adjustment screen.
    lv_obj_t *brightness_btn = make_theme_icon_btn(settings_screen,
        chinese_font_20 ? "亮度" : "Brightness", &img_sunny, 320, 0xFFAA00,
        180, 80, 120, 140);
    lv_obj_set_style_text_font(brightness_btn, ui_chinese_font_20(&lv_font_montserrat_14), 0);
    lv_obj_add_event_cb(brightness_btn, brightness_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *volume_btn = make_theme_icon_btn(settings_screen,
        chinese_font_20 ? "音量" : "Volume", &volume_img, 320, 0x35B8D4,
        330, 80, 120, 140);
    lv_obj_set_style_text_font(volume_btn, ui_chinese_font_20(&lv_font_montserrat_14), 0);
    lv_obj_add_event_cb(volume_btn, volume_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *info_btn = lv_btn_create(settings_screen);
    lv_obj_set_pos(info_btn, 30, 250);
    lv_obj_set_size(info_btn, 420, 64);
    lv_obj_set_style_pad_all(info_btn, 0, 0);
    lv_obj_set_style_radius(info_btn, 14, 0);
    lv_obj_set_style_bg_color(info_btn, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(info_btn, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(info_btn, 1, 0);
    lv_obj_set_style_border_color(info_btn, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_shadow_width(info_btn, 0, 0);
    lv_obj_add_event_cb(info_btn, device_info_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *info_icon = lv_obj_create(info_btn);
    lv_obj_set_size(info_icon, 32, 32);
    lv_obj_align(info_icon, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_pad_all(info_icon, 0, 0);
    lv_obj_set_style_radius(info_icon, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(info_icon, 0, 0);
    lv_obj_set_style_bg_color(info_icon, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_clear_flag(info_icon, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *info_symbol = lv_label_create(info_icon);
    lv_label_set_text(info_symbol, "i");
    lv_obj_set_style_text_font(info_symbol, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(info_symbol, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
    lv_obj_center(info_symbol);

    lv_obj_t *info_title = lv_label_create(info_btn);
    lv_label_set_text(info_title, chinese_font_20 ? "设备信息" : "Device information");
    lv_obj_set_style_text_font(info_title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(info_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(info_title, LV_ALIGN_LEFT_MID, 72, 0);
    lv_obj_t *info_arrow = lv_label_create(info_btn);
    lv_label_set_text(info_arrow, ">");
    lv_obj_set_style_text_font(info_arrow, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(info_arrow, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(info_arrow, LV_ALIGN_RIGHT_MID, -18, 0);

    restart_button = lv_btn_create(settings_screen);
    lv_obj_set_pos(restart_button, 30, 336);
    lv_obj_set_size(restart_button, 420, 64);
    lv_obj_set_style_pad_all(restart_button, 0, 0);
    lv_obj_set_style_radius(restart_button, 14, 0);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(restart_button, 1, 0);
    lv_obj_set_style_border_color(restart_button, lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_set_style_shadow_width(restart_button, 0, 0);
    lv_obj_add_event_cb(restart_button, restart_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *restart_icon = lv_label_create(restart_button);
    lv_label_set_text(restart_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(restart_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(restart_icon, lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_align(restart_icon, LV_ALIGN_LEFT_MID, 22, 0);
    lv_obj_t *restart_title = lv_label_create(restart_button);
    lv_label_set_text(restart_title, chinese_font_20 ? "重启设备" : "Restart device");
    lv_obj_set_style_text_font(restart_title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(restart_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(restart_title, LV_ALIGN_LEFT_MID, 72, 0);

    display_restart_button = lv_btn_create(settings_screen);
    lv_obj_set_pos(display_restart_button, 30, 416);
    lv_obj_set_size(display_restart_button, 420, 48);
    lv_obj_set_style_pad_all(display_restart_button, 0, 0);
    lv_obj_set_style_radius(display_restart_button, 14, 0);
    lv_obj_set_style_bg_color(display_restart_button, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(display_restart_button, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(display_restart_button, 1, 0);
    lv_obj_set_style_border_color(display_restart_button, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_shadow_width(display_restart_button, 0, 0);
    lv_obj_add_event_cb(display_restart_button, display_restart_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *display_restart_icon = lv_label_create(display_restart_button);
    lv_label_set_text(display_restart_icon, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_font(display_restart_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(display_restart_icon, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(display_restart_icon, LV_ALIGN_LEFT_MID, 22, 0);
    display_restart_title = lv_label_create(display_restart_button);
    lv_label_set_text(display_restart_title, chinese_font_20 ? "重启屏幕" : "Restart screen");
    lv_obj_set_style_text_font(display_restart_title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(display_restart_title, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(display_restart_title, LV_ALIGN_LEFT_MID, 72, 0);
}

static void device_info_set_value(unsigned row, const char *text)
{
    if (row < DEVICE_INFO_ROW_COUNT && device_info_values[row] &&
        strcmp(lv_label_get_text(device_info_values[row]), text) != 0) {
        lv_label_set_text(device_info_values[row], text);
    }
}

static void device_info_update(bool log_snapshot)
{
    device_info_snapshot_t info;
    device_info_get_snapshot(&info);
    char value[96];

    device_info_set_value(0, info.device_model);
    device_info_set_value(1, info.firmware_version);
    snprintf(value, sizeof(value), chinese_font_20 ? "%u 核 / %u MHz" : "%u cores / %u MHz",
             (unsigned)info.cpu_cores, (unsigned)info.cpu_mhz);
    device_info_set_value(2, value);
    snprintf(value, sizeof(value), "%.2f / %.2f MiB",
             info.flash_firmware_bytes / 1048576.0, info.flash_bytes / 1048576.0);
    device_info_set_value(3, value);
    uint32_t heap_used = info.heap_total_bytes -
        (info.heap_free_bytes < info.heap_total_bytes ? info.heap_free_bytes : info.heap_total_bytes);
    snprintf(value, sizeof(value), "%.1f / %.1f KiB",
             heap_used / 1024.0, info.heap_total_bytes / 1024.0);
    device_info_set_value(4, value);
    if (info.psram_total_bytes) {
        uint32_t psram_used = info.psram_total_bytes -
            (info.psram_free_bytes < info.psram_total_bytes ? info.psram_free_bytes : info.psram_total_bytes);
        snprintf(value, sizeof(value), "%.2f / %.2f MiB",
                 psram_used / 1048576.0, info.psram_total_bytes / 1048576.0);
    } else {
        snprintf(value, sizeof(value), "%s", chinese_font_20 ? "未检测到" : "Unavailable");
    }
    device_info_set_value(5, value);
    snprintf(value, sizeof(value), "%u FPS", (unsigned)gui_fps);
    device_info_set_value(6, value);

    if (log_snapshot) device_info_log_snapshot(&info);
}

static void device_info_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (device_info_screen && lv_scr_act() == device_info_screen) {
        device_info_update(false);
    }
}

static void device_info_screen_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        device_info_update(true);
        if (device_info_timer) {
            lv_timer_reset(device_info_timer);
            lv_timer_resume(device_info_timer);
        }
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED) {
        if (device_info_timer) lv_timer_pause(device_info_timer);
    }
}

static void create_device_info_screen(void)
{
    device_info_screen = lv_obj_create(NULL);
    lv_obj_set_size(device_info_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(device_info_screen, 0, 0);
    lv_obj_set_style_border_width(device_info_screen, 0, 0);
    ui_theme_apply_background(device_info_screen);
    lv_obj_clear_flag(device_info_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(device_info_screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, settings_btn_cb);
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "设备信息" : "Device information");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_center(title);

    const char *names_cn[DEVICE_INFO_ROW_COUNT] = {
        "芯片型号", "固件版本", "CPU性能", "Flash固件", "内部堆", "PSRAM", "屏幕刷新"
    };
    const char *names_en[DEVICE_INFO_ROW_COUNT] = {
        "Chip model", "Firmware", "CPU", "Flash image", "Internal heap", "PSRAM", "LCD refresh"
    };
    for (unsigned i = 0; i < DEVICE_INFO_ROW_COUNT; ++i) {
        lv_obj_t *row = lv_obj_create(device_info_screen);
        lv_obj_set_pos(row, 20, 66 + i * 46);
        lv_obj_set_size(row, 440, 44);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_radius(row, 8, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, chinese_font_20 ? names_cn[i] : names_en[i]);
        lv_obj_set_width(name, 150);
        lv_obj_set_style_text_font(name, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(name, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 12, 0);

        device_info_values[i] = lv_label_create(row);
        lv_obj_set_width(device_info_values[i], 242);
        lv_label_set_text(device_info_values[i], "--");
        lv_label_set_long_mode(device_info_values[i], LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(device_info_values[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(device_info_values[i], ui_chinese_font_20(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(device_info_values[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(device_info_values[i], LV_ALIGN_RIGHT_MID, -12, 0);
    }

    lv_obj_t *hint = lv_label_create(device_info_screen);
    lv_label_set_text(hint, chinese_font_20 ? "容量数值: 已用 / 总量" : "Capacity: used / total");
    lv_obj_set_style_text_font(hint, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 412);

    lv_obj_t *flash_hint = lv_label_create(device_info_screen);
    lv_label_set_text(flash_hint, chinese_font_20 ? "Flash已用仅统计当前固件" : "Flash usage = current firmware image");
    lv_obj_set_style_text_font(flash_hint, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(flash_hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(flash_hint, LV_ALIGN_TOP_MID, 0, 442);

    lv_obj_add_event_cb(device_info_screen, device_info_screen_event_cb, LV_EVENT_ALL, NULL);
    device_info_timer = lv_timer_create(device_info_timer_cb, 2000, NULL);
    if (device_info_timer) lv_timer_pause(device_info_timer);
}

static void device_info_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!device_info_screen) create_device_info_screen();
    lv_scr_load(device_info_screen);
}

static void brightness_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load(brightness_screen);
}

static void create_brightness_screen(void)
{
    brightness_screen = lv_obj_create(NULL);
    ui_theme_apply_background(brightness_screen);

    lv_obj_t *header = lv_obj_create(brightness_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, settings_btn_cb);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "亮度" : "Brightness");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(brightness_screen);
    lv_label_set_text(hint, chinese_font_20 ? "调节屏幕亮度" : "Adjust display brightness");
    lv_obj_set_style_text_font(hint, ui_chinese_font_16(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 135);

    brightness_label = lv_label_create(brightness_screen);
    lv_label_set_text(brightness_label, "100%");
    lv_obj_set_style_text_font(brightness_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(brightness_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(brightness_label, LV_ALIGN_TOP_MID, 0, 185);

    brightness_slider = lv_slider_create(brightness_screen);
    lv_obj_set_width(brightness_slider, 330);
    lv_obj_set_height(brightness_slider, 22);
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 235);
    lv_slider_set_range(brightness_slider, 5, 100);
    lv_slider_set_value(brightness_slider, 100, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brightness_slider, lv_color_hex(UI_COLOR_ON_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(brightness_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(brightness_slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    set_backlight_brightness(percent);

    if (brightness_label) {
        char text[8];
        snprintf(text, sizeof(text), "%d%%", percent);
        lv_label_set_text(brightness_label, text);
    }
}

static void volume_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load(volume_screen);
}

static void create_volume_screen(void)
{
    volume_screen = lv_obj_create(NULL);
    ui_theme_apply_background(volume_screen);

    lv_obj_t *header = lv_obj_create(volume_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, settings_btn_cb);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "音量" : "Volume");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *hint = lv_label_create(volume_screen);
    lv_label_set_text(hint, chinese_font_20 ? "调节音乐和语音音量" :
                      "Adjust music and voice volume");
    lv_obj_set_style_text_font(hint, ui_chinese_font_16(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 135);

    volume_label = lv_label_create(volume_screen);
    lv_obj_set_style_text_font(volume_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(volume_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(volume_label, LV_ALIGN_TOP_MID, 0, 185);

    volume_slider = lv_slider_create(volume_screen);
    lv_obj_set_width(volume_slider, 330);
    lv_obj_set_height(volume_slider, 22);
    lv_obj_align(volume_slider, LV_ALIGN_TOP_MID, 0, 235);
    lv_slider_set_range(volume_slider, 0, 100);
    lv_slider_set_value(volume_slider, get_audio_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(volume_slider, lv_color_hex(UI_COLOR_ON_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_add_event_cb(volume_slider, volume_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(volume_slider, volume_slider_release_cb, LV_EVENT_RELEASED, NULL);

    char text[8];
    snprintf(text, sizeof(text), "%u%%", (unsigned)get_audio_volume());
    lv_label_set_text(volume_label, text);
}

static void volume_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    const int percent = lv_slider_get_value(slider);
    set_audio_volume(percent);

    if (volume_label) {
        char text[8];
        snprintf(text, sizeof(text), "%d%%", percent);
        lv_label_set_text(volume_label, text);
    }
}

static void volume_slider_release_cb(lv_event_t *e)
{
    (void)e;
    save_audio_volume();
}

/* ==================== WIFI LIST SCREEN ==================== */

static void create_wifi_list_screen(void)
{
    wifi_list_screen = lv_obj_create(NULL);
    ui_theme_apply_background(wifi_list_screen);

    lv_obj_t *header = lv_obj_create(wifi_list_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 50);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);

    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, settings_btn_cb);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "WiFi 网络" : "WiFi Networks");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *connection_panel = lv_obj_create(wifi_list_screen);
    lv_obj_set_size(connection_panel, SCREEN_WIDTH - 20, 72);
    lv_obj_set_pos(connection_panel, 10, 58);
    lv_obj_set_style_bg_color(connection_panel, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(connection_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(connection_panel, 1, 0);
    lv_obj_set_style_border_color(connection_panel, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(connection_panel, 8, 0);
    lv_obj_set_style_pad_left(connection_panel, 14, 0);
    lv_obj_set_style_pad_top(connection_panel, 7, 0);

    wifi_connection_status_label = lv_label_create(connection_panel);
    lv_obj_set_style_text_font(wifi_connection_status_label,
                               ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_align(wifi_connection_status_label, LV_ALIGN_TOP_LEFT, 0, 0);
    wifi_connection_ssid_label = lv_label_create(connection_panel);
    lv_obj_set_style_text_font(wifi_connection_ssid_label,
                               ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_width(wifi_connection_ssid_label, lv_pct(100));
    lv_obj_set_height(wifi_connection_ssid_label,
                      lv_font_get_line_height(ui_chinese_font_20(&lv_font_montserrat_20)));
    lv_label_set_long_mode(wifi_connection_ssid_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(wifi_connection_ssid_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(wifi_connection_ssid_label, LV_ALIGN_BOTTOM_LEFT, 0, -3);
    wifi_update_connection_info();

    wifi_list = lv_list_create(wifi_list_screen);
    lv_obj_set_size(wifi_list, SCREEN_WIDTH - 20, 310);
    lv_obj_set_pos(wifi_list, 10, 138);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(wifi_list, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_width(wifi_list, 1, 0);
    lv_obj_set_style_border_color(wifi_list, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(wifi_list, 8, 0);

    lv_obj_t *refresh_btn = lv_btn_create(wifi_list_screen);
    lv_obj_set_size(refresh_btn, 190, 46);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_add_event_cb(refresh_btn, wifi_refresh_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(refresh_label, chinese_font_20 ? "刷新" : "Refresh");
    lv_obj_set_style_text_font(refresh_label, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(refresh_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(refresh_label);
}

/* ==================== WIFI PASSWORD SCREEN ==================== */

// ✓ 键确认连接
static void wifi_pwd_ready_cb(lv_event_t *e)
{
    const char *pwd = lv_textarea_get_text(wifi_pwd_textarea);
    if (pwd && strlen(pwd) > 0) {
        const bool started = do_wifi_connect(wifi_selected_ssid, pwd);
        lv_obj_t *msg = lv_msgbox_create(NULL,
            started ? "WiFi Connecting" : "Failed",
            started ? "Please wait for the connection to finish" :
                      "Another connection is in progress",
            NULL, true);
        lv_obj_center(msg);
        lv_obj_add_event_cb(msg, (lv_event_cb_t)lv_obj_del, LV_EVENT_VALUE_CHANGED, msg);
        lv_scr_load(started ? main_screen_ref : wifi_list_screen);
    }
}

// 点击键盘 ✓ 键
static void wifi_pwd_click_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    const char *btn_text = lv_btnmatrix_get_btn_text(kb, btn_id);
    if (btn_text && strstr(btn_text, LV_SYMBOL_OK)) {
        wifi_pwd_ready_cb(e);
    }
}

// 长按 ⌫ → 输入框为空时返回
static void wifi_pwd_long_cb(lv_event_t *e)
{
    const char *txt = lv_textarea_get_text(wifi_pwd_textarea);
    if (!txt || strlen(txt) == 0) {
        lv_scr_load(wifi_list_screen);
    }
}

static void create_wifi_password_screen(void)
{
    wifi_pwd_screen = lv_obj_create(NULL);
    ui_theme_apply_background(wifi_pwd_screen);

    lv_obj_t *title = lv_label_create(wifi_pwd_screen);
    lv_label_set_text(title, "WiFi Password");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *hint = lv_label_create(wifi_pwd_screen);
    lv_label_set_text(hint, "Press ✓ to connect | Hold ⌫ to back");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 38);

    // SSID 显示
    wifi_password_ssid_label = lv_label_create(wifi_pwd_screen);
    char buf[64];
    snprintf(buf, sizeof(buf), "SSID: %s", wifi_selected_ssid[0] ? wifi_selected_ssid : "--");
    lv_label_set_text(wifi_password_ssid_label, buf);
    lv_obj_set_style_text_font(wifi_password_ssid_label,
                               ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_width(wifi_password_ssid_label, SCREEN_WIDTH - 40);
    lv_obj_set_height(wifi_password_ssid_label,
                      lv_font_get_line_height(ui_chinese_font_20(&lv_font_montserrat_20)));
    lv_label_set_long_mode(wifi_password_ssid_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(wifi_password_ssid_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(wifi_password_ssid_label, LV_ALIGN_TOP_MID, 0, 60);

    wifi_pwd_textarea = lv_textarea_create(wifi_pwd_screen);
    lv_obj_set_style_bg_color(wifi_pwd_textarea, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(wifi_pwd_textarea, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_color(wifi_pwd_textarea, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_size(wifi_pwd_textarea, 400, 45);
    lv_obj_align(wifi_pwd_textarea, LV_ALIGN_TOP_MID, 0, 85);
    lv_textarea_set_placeholder_text(wifi_pwd_textarea, "Password");
    lv_textarea_set_max_length(wifi_pwd_textarea, 63);
    lv_textarea_set_one_line(wifi_pwd_textarea, true);
    lv_textarea_set_password_mode(wifi_pwd_textarea, true);
    lv_obj_add_event_cb(wifi_pwd_textarea, wifi_pwd_ready_cb, LV_EVENT_READY, NULL);

    wifi_pwd_keyboard = lv_keyboard_create(wifi_pwd_screen);
    lv_keyboard_set_textarea(wifi_pwd_keyboard, wifi_pwd_textarea);
    lv_obj_set_style_bg_color(wifi_pwd_keyboard, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(wifi_pwd_keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(wifi_pwd_keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_add_event_cb(wifi_pwd_keyboard, wifi_pwd_click_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(wifi_pwd_keyboard, wifi_pwd_long_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_add_event_cb(wifi_pwd_keyboard, text_keyboard_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(wifi_pwd_keyboard, text_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wifi_pwd_textarea, text_input_event_cb, LV_EVENT_CLICKED,
                        wifi_pwd_keyboard);
    lv_obj_add_event_cb(wifi_pwd_textarea, text_input_event_cb, LV_EVENT_FOCUSED,
                        wifi_pwd_keyboard);
    lv_obj_add_event_cb(wifi_pwd_textarea, text_input_event_cb, LV_EVENT_DEFOCUSED,
                        wifi_pwd_keyboard);
    hide_text_keyboard(wifi_pwd_keyboard);
}

/* Extras page (Page 3) */

static bool ntp_synced = false;

/* Weather icon by condition — 中文天气匹配 */
static const lv_img_dsc_t * get_weather_icon(const char * condition)
{
    if (!condition) return &img_sunny;

    // 晴 → sunny
    if (strstr(condition, "晴") ||
        strstr(condition, "sunny") || strstr(condition, "Sunny") ||
        strstr(condition, "clear")  || strstr(condition, "Clear"))
        return &img_sunny;

    // 多云 → cloudy
    if (strstr(condition, "多云") || strstr(condition, "间晴") ||
        strstr(condition, "少云") || strstr(condition, "云") ||
        strstr(condition, "cloudy") || strstr(condition, "Cloudy") ||
        strstr(condition, "cloud")  || strstr(condition, "Cloud"))
        return &img_cloudy;

    // 阴/雾/霾/沙尘 → overcast
    if (strstr(condition, "阴")   || strstr(condition, "雾") ||
        strstr(condition, "霾")   || strstr(condition, "尘") ||
        strstr(condition, "沙")   || strstr(condition, "浮尘") ||
        strstr(condition, "overcast") || strstr(condition, "Overcast"))
        return &img_overcast;

    // 雨/雪/雷/雹 → rain
    if (strstr(condition, "雨")   || strstr(condition, "雪") ||
        strstr(condition, "雷")   || strstr(condition, "雹") ||
        strstr(condition, "rain") || strstr(condition, "Rain") ||
        strstr(condition, "snow") || strstr(condition, "Snow"))
        return &img_bigrain;

    // 默认晴天
    return &img_sunny;
}

void set_weather(const char * condition, const char * temp)
{
    if (city_btn_label)
        if (city_btn_label) lv_label_set_text(city_btn_label, ui_city_name());
    if (weather_icon_img)
        lv_img_set_src(weather_icon_img, get_weather_icon(condition));
    if (weather_text) {
        lv_label_set_text(weather_text, condition ? condition : "--");
    }
    if (weather_temp_label) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s°C", temp ? temp : "--");
        lv_label_set_text(weather_temp_label, buf);
    }
    if (detail_info) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Weather: %s\nTemperature: %s", condition, temp);
        lv_label_set_text(detail_info, buf);
    }
    refresh_weather_page();
}

static void refresh_weather_page(void)
{
    if (weather_page_city_label)
        lv_label_set_text(weather_page_city_label, ui_city_name());
    if (weather_page_icon)
        lv_img_set_src(weather_page_icon, get_weather_icon(weather));
    if (weather_page_condition)
        lv_label_set_text(weather_page_condition, weather ? weather : "--");
    if (weather_page_temp) {
        char temp_text[16];
        snprintf(temp_text, sizeof(temp_text), "%s°C", temp ? temp : "--");
        lv_label_set_text(weather_page_temp, temp_text);
    }

    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        if (!weather_forecast_labels[i]) continue;
        char forecast_text[160];
        if (i < WEATHER_FORECAST_COUNT) {
            snprintf(forecast_text, sizeof(forecast_text),
                     "%s  白天 %s   高%s℃\n夜间 %s   低%s℃",
                     WEATHER_FORECAST_DATE[i],
                     WEATHER_FORECAST_DAY_TEXT[i], WEATHER_FORECAST_HIGH[i],
                     WEATHER_FORECAST_NIGHT_TEXT[i], WEATHER_FORECAST_LOW[i]);
        } else {
            snprintf(forecast_text, sizeof(forecast_text), "%s", chinese_font_20 ? "暂无预报数据" : "Forecast unavailable");
        }
        lv_label_set_text(weather_forecast_labels[i], forecast_text);
    }
}

/* ==================== PAGE HELPERS ==================== */

static lv_obj_t * create_page(lv_obj_t * parent, int idx)
{
    lv_obj_t * page = lv_obj_create(parent);
    lv_obj_set_size(page, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a1a), 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    if (idx > 0) ui_theme_apply_background(page);
    // 彻底禁用所有滚动/弹跳/惯性
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);
    // 非首页先隐藏
    if (idx > 0) lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    (void)idx;
    return page;
}

/* ==================== KEYBOARD SCREEN ==================== */

static void kb_confirm(void)
{
    const char *txt = lv_textarea_get_text(kb_textarea);
    if (txt && strlen(txt) > 0) {
        bool ok = updateWeatherCity(txt);

        // 城市名总是更新为显示名
        if (city_btn_label) lv_label_set_text(city_btn_label, ui_city_name());

        // 更新天气（成功 → 新数据，失败 → 显示旧数据或占位）
        if (ok && weather && temp) {
            set_weather(weather, temp);
        } else if (!ok) {
            // 天气获取失败，显示提示
            set_weather("--", "--");
        }
    }
    lv_scr_load(main_screen_ref);
}

// 键盘 ✓ 键确认
static void kb_ready_cb(lv_event_t *e) { kb_confirm(); }

// 点击键盘：检测右下角 ✓ 键 (LV_SYMBOL_OK)
static void kb_click_cb(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    const char *btn_text = lv_btnmatrix_get_btn_text(kb, btn_id);
    if (btn_text && strstr(btn_text, LV_SYMBOL_OK)) {
        kb_confirm();
    }
}

// 长按键盘 → 输入框为空时退出
static void kb_long_press_cb(lv_event_t *e)
{
    const char *txt = lv_textarea_get_text(kb_textarea);
    if (!txt || strlen(txt) == 0) {
        lv_scr_load(main_screen_ref);
    }
}

static void city_btn_cb(lv_event_t *e)
{
    lv_textarea_set_text(kb_textarea, "");
    lv_scr_load(kb_screen);
}

static void weather_city_btn_cb(lv_event_t *e)
{
    city_btn_cb(e);
}

static void create_keyboard_screen(void)
{
    kb_screen = lv_obj_create(NULL);
    ui_theme_apply_background(kb_screen);

    /* 标题 */
    lv_obj_t *title = lv_label_create(kb_screen);
    lv_label_set_text(title, "Enter City (pinyin)");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    /* 提示 */
    lv_obj_t *hint = lv_label_create(kb_screen);
    lv_label_set_text(hint, "Press ✓ to confirm | Hold ⌫ to exit");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 42);

    /* 输入框 */
    kb_textarea = lv_textarea_create(kb_screen);
    lv_obj_set_style_bg_color(kb_textarea, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(kb_textarea, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_color(kb_textarea, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_size(kb_textarea, 400, 45);
    lv_obj_align(kb_textarea, LV_ALIGN_TOP_MID, 0, 65);
    lv_textarea_set_placeholder_text(kb_textarea, "e.g. beijing");
    lv_textarea_set_max_length(kb_textarea, 30);
    lv_textarea_set_one_line(kb_textarea, true);
    lv_obj_add_event_cb(kb_textarea, kb_ready_cb, LV_EVENT_READY, NULL);

    /* 键盘 */
    kb_keyboard = lv_keyboard_create(kb_screen);
    lv_keyboard_set_textarea(kb_keyboard, kb_textarea);
    lv_obj_set_style_bg_color(kb_keyboard, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb_keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    lv_obj_add_event_cb(kb_keyboard, kb_click_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb_keyboard, kb_long_press_cb, LV_EVENT_LONG_PRESSED_REPEAT, NULL);
    lv_obj_add_event_cb(kb_keyboard, text_keyboard_event_cb, LV_EVENT_CANCEL, NULL);
    lv_obj_add_event_cb(kb_keyboard, text_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_textarea, text_input_event_cb, LV_EVENT_CLICKED,
                        kb_keyboard);
    lv_obj_add_event_cb(kb_textarea, text_input_event_cb, LV_EVENT_FOCUSED,
                        kb_keyboard);
    lv_obj_add_event_cb(kb_textarea, text_input_event_cb, LV_EVENT_DEFOCUSED,
                        kb_keyboard);
    hide_text_keyboard(kb_keyboard);
}

/* ==================== PAGE 0: HOME ==================== */

static void setup_home_page(lv_obj_t * page)
{
    wallpaper_img = lv_img_create(page);
    const lv_img_dsc_t *wallpaper_src = get_wallpaper_image();
    if (wallpaper_src) lv_img_set_src(wallpaper_img, wallpaper_src);
    else lv_img_set_src(wallpaper_img, get_wallpaper_path());
    lv_obj_set_pos(wallpaper_img, 0, 0);
    lv_obj_set_size(wallpaper_img, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_opa(wallpaper_img, LV_OPA_COVER, 0);
    lv_obj_clear_flag(wallpaper_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(wallpaper_img);

    /* Restore the wallpaper selector on the home page. */
    lv_obj_t *wallpaper_btn = lv_btn_create(page);
    lv_obj_set_size(wallpaper_btn, 88, 34);
    lv_obj_set_pos(wallpaper_btn, 10, 10);
    lv_obj_set_style_bg_color(wallpaper_btn, lv_color_hex(0x102A43), 0);
    lv_obj_set_style_bg_opa(wallpaper_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(wallpaper_btn, 1, 0);
    lv_obj_set_style_border_color(wallpaper_btn, lv_color_hex(0x65F6C0), 0);
    lv_obj_set_style_radius(wallpaper_btn, 8, 0);
    lv_obj_set_style_shadow_width(wallpaper_btn, 0, 0);
    lv_obj_add_event_cb(wallpaper_btn, wallpaper_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wallpaper_btn_label = lv_label_create(wallpaper_btn);
    lv_label_set_text(wallpaper_btn_label, chinese_font_20 ? "壁纸" : "Wallpaper");
    lv_obj_set_style_text_font(wallpaper_btn_label, ui_chinese_font_16(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(wallpaper_btn_label, lv_color_hex(0x65F6C0), 0);
    lv_obj_center(wallpaper_btn_label);

    /* Transparent layout container: content is drawn directly over wallpaper. */
    lv_obj_t *clock_card = lv_obj_create(page);
    lv_obj_set_size(clock_card, 440, 238);
    lv_obj_align(clock_card, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_bg_opa(clock_card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_card, 0, 0);
    lv_obj_set_style_pad_all(clock_card, 0, 0);
    lv_obj_clear_flag(clock_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 左上角：仅显示当前天气，不显示城市。 */
    lv_obj_t *weather_box = lv_obj_create(clock_card);
    lv_obj_set_size(weather_box, 190, 58);
    lv_obj_set_pos(weather_box, 8, 6);
    lv_obj_set_style_bg_opa(weather_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(weather_box, 0, 0);
    lv_obj_set_style_pad_all(weather_box, 0, 0);
    lv_obj_clear_flag(weather_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    weather_icon_img = lv_img_create(weather_box);
    lv_img_set_src(weather_icon_img, &img_sunny);
    lv_img_set_zoom(weather_icon_img, 105);
    lv_obj_align(weather_icon_img, LV_ALIGN_LEFT_MID, 0, 0);

    city_btn_label = NULL;
    weather_text = lv_label_create(weather_box);
    lv_label_set_text(weather_text, "晴");
    lv_obj_set_style_text_font(weather_text, ui_chinese_font_16(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(weather_text, lv_color_hex(0x65F6C0), 0);
    lv_obj_align(weather_text, LV_ALIGN_LEFT_MID, 48, 0);

    weather_temp_label = lv_label_create(weather_box);
    lv_label_set_text(weather_temp_label, "25°C");
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(0x65F6C0), 0);
    lv_obj_align(weather_temp_label, LV_ALIGN_LEFT_MID, 104, 0);

    /* 40x40 gyro thermometer and its live sensor temperature. */
    home_gyro_temp_icon = lv_img_create(page);
    lv_img_set_src(home_gyro_temp_icon, &home_temp_img);
    lv_obj_set_size(home_gyro_temp_icon, HOME_TEMP_ICON_SIZE, HOME_TEMP_ICON_SIZE);
    lv_obj_align(home_gyro_temp_icon, LV_ALIGN_TOP_RIGHT, -20, 8);
    lv_obj_clear_flag(home_gyro_temp_icon, LV_OBJ_FLAG_CLICKABLE);

    home_gyro_temp_label = lv_label_create(page);
    lv_label_set_text(home_gyro_temp_label, "--.-°C");
    lv_obj_set_width(home_gyro_temp_label, 72);
    lv_obj_set_style_text_align(home_gyro_temp_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(home_gyro_temp_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(home_gyro_temp_label, lv_color_hex(0xFF241C), 0);
    lv_obj_align_to(home_gyro_temp_label, home_gyro_temp_icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 3);
    lv_obj_clear_flag(home_gyro_temp_label, LV_OBJ_FLAG_CLICKABLE);

    /* 右上角：星期和年月日。 */
    date_label = lv_label_create(clock_card);
    lv_label_set_text(date_label, "THU   2026-10-01");
    lv_obj_set_width(date_label, 330);
    lv_obj_set_style_text_font(date_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_color(date_label, lv_color_hex(0x4ED2A0), 0);
    lv_obj_set_pos(date_label, 8, 190);

    /* 主时间：七段数码管，大号时分 + 右侧小号秒。 */
    time_label = NULL;
    const int main_x = 4, main_y = 76, main_w = 62, main_h = 108, main_t = 9;
    create_digital_digit(clock_card, digital_digits[0], main_x, main_y, main_w, main_h, main_t);
    create_digital_digit(clock_card, digital_digits[1], main_x + 68, main_y, main_w, main_h, main_t);
    create_digital_digit(clock_card, digital_digits[2], main_x + 156, main_y, main_w, main_h, main_t);
    create_digital_digit(clock_card, digital_digits[3], main_x + 224, main_y, main_w, main_h, main_t);

    digital_colons[0][0] = create_digital_segment(clock_card, main_x + 139, main_y + 31, 10, 10, 10);
    digital_colons[0][1] = create_digital_segment(clock_card, main_x + 139, main_y + 68, 10, 10, 10);

    const int seconds_x = main_x + 316, seconds_y = main_y + 47;
    create_digital_digit(clock_card, digital_digits[4], seconds_x, seconds_y, 32, 60, 6);
    create_digital_digit(clock_card, digital_digits[5], seconds_x + 38, seconds_y, 32, 60, 6);
    digital_colons[1][0] = create_digital_segment(clock_card, seconds_x - 17, seconds_y + 16, 7, 7, 7);
    digital_colons[1][1] = create_digital_segment(clock_card, seconds_x - 17, seconds_y + 38, 7, 7, 7);
    update_digital_time(12, 0, 0);

}

static void wallpaper_btn_cb(lv_event_t *e)
{
    (void)e;
    for (int i = 0; i < 3; ++i) {
        const char *preview_path = get_wallpaper_choice_path(i);
        if (preview_path) {
            // Display the original SD image through LVGL's file decoder; zoom it
            // down to fit the card instead of loading/creating a RAM thumbnail.
            const lv_img_dsc_t *preview_image = get_wallpaper_choice_image(i);
            if (preview_image) lv_img_set_src(wallpaper_preview_objs[i], preview_image);
            else lv_img_set_src(wallpaper_preview_objs[i], preview_path);
            lv_img_set_zoom(wallpaper_preview_objs[i], 72); /* 480 px -> 135 px */
            lv_obj_align(wallpaper_preview_objs[i], LV_ALIGN_CENTER, 0, -12);
            lv_obj_clear_flag(wallpaper_preview_objs[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(wallpaper_preview_labels[i], "");
        } else {
            lv_obj_add_flag(wallpaper_preview_objs[i], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(wallpaper_preview_labels[i], chinese_font_20 ? "预览不可用" : "Preview unavailable");
        }
    }
    lv_scr_load(wallpaper_screen);
}

static void wallpaper_select_cb(lv_event_t *e)
{
    const int choice = (int)(intptr_t)lv_event_get_user_data(e);
    set_wallpaper_choice(choice);
    if (wallpaper_img) {
        const lv_img_dsc_t *wallpaper_src = get_wallpaper_choice_image(choice);
        if (wallpaper_src) lv_img_set_src(wallpaper_img, wallpaper_src);
        else lv_img_set_src(wallpaper_img, get_wallpaper_path());
    }
    lv_scr_load(main_screen_ref);
}

static void create_wallpaper_screen(void)
{
    wallpaper_screen = lv_obj_create(NULL);
    ui_theme_apply_background(wallpaper_screen);
    create_back_button(wallpaper_screen, LV_ALIGN_TOP_RIGHT, -8, back_btn_cb);

    lv_obj_t *title = lv_label_create(wallpaper_screen);
    lv_label_set_text(title, chinese_font_20 ? "选择壁纸" : "Wallpaper");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *button = lv_btn_create(wallpaper_screen);
        lv_obj_set_size(button, 140, 170);
        lv_obj_set_pos(button, 20 + i * 155, 75);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_pad_all(button, 0, 0);
        lv_obj_add_event_cb(button, wallpaper_select_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        wallpaper_preview_objs[i] = lv_img_create(button);
        lv_obj_add_flag(wallpaper_preview_objs[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(wallpaper_preview_objs[i], 480, 480);
        lv_obj_align(wallpaper_preview_objs[i], LV_ALIGN_CENTER, 0, -12);

        wallpaper_preview_labels[i] = lv_label_create(button);
        lv_obj_set_style_text_font(wallpaper_preview_labels[i], ui_chinese_font_16(&lv_font_montserrat_14), 0);
        lv_obj_set_style_text_color(wallpaper_preview_labels[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(wallpaper_preview_labels[i], LV_ALIGN_CENTER, 0, 0);

        lv_obj_t *label = lv_label_create(button);
        char name[16];
            snprintf(name, sizeof(name), chinese_font_20 ? "壁纸 %d" : "Wallpaper %d", i + 1);
        lv_label_set_text(label, name);
        lv_obj_set_style_text_font(label, ui_chinese_font_16(&lv_font_montserrat_14), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -8);
    }
}

static void clock_btn_cb(lv_event_t *e)
{
    (void)e;
    int hour = 0, minute = 0;
    bool enabled = false;
    get_clock_reminder(&hour, &minute, &enabled);
    lv_roller_set_selected(clock_hour_roller, (uint16_t)hour, LV_ANIM_OFF);
    lv_roller_set_selected(clock_minute_roller, (uint16_t)minute, LV_ANIM_OFF);
    if (enabled) lv_obj_add_state(clock_enabled_switch, LV_STATE_CHECKED);
    else lv_obj_clear_state(clock_enabled_switch, LV_STATE_CHECKED);
    lv_scr_load(clock_screen);
}

static void clock_save_cb(lv_event_t *e)
{
    (void)e;
    int hour = (int)lv_roller_get_selected(clock_hour_roller);
    int minute = (int)lv_roller_get_selected(clock_minute_roller);
    bool enabled = lv_obj_has_state(clock_enabled_switch, LV_STATE_CHECKED);
    save_clock_reminder(hour, minute, enabled);
    clock_last_reminder_day = -1;
    clock_last_reminder_minute = -1;
    lv_obj_set_style_text_color(clock_status_label,
                                lv_color_hex(enabled ? UI_COLOR_SUCCESS : UI_COLOR_TEXT_MUTED), 0);
    lv_label_set_text(clock_status_label,
                      chinese_font_20 ? (enabled ? "提醒已保存" : "提醒已关闭") :
                                        (enabled ? "Reminder saved" : "Reminder disabled"));
}

static void clock_reminder_timeout(lv_timer_t *timer)
{
    (void)timer;
    if (clock_reminder_label) lv_obj_add_flag(clock_reminder_label, LV_OBJ_FLAG_HIDDEN);
}

static void create_clock_screen(void)
{
    static const char hours[] = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23";
    static const char minutes[] = "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59";
    clock_screen = lv_obj_create(NULL);
    ui_theme_apply_background(clock_screen);
    create_back_button(clock_screen, LV_ALIGN_TOP_RIGHT, -8, back_btn_cb);
    lv_obj_t *title = lv_label_create(clock_screen);
    lv_label_set_text(title, chinese_font_20 ? "时钟提醒" : "Clock reminder");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    clock_hour_roller = lv_roller_create(clock_screen);
    clock_minute_roller = lv_roller_create(clock_screen);
    lv_roller_set_options(clock_hour_roller, hours, LV_ROLLER_MODE_INFINITE);
    /* Infinite mode duplicates all options seven times. With 60 minute rows
     * that creates a 420-row label and makes touch dragging unstable. */
    lv_roller_set_options(clock_minute_roller, minutes, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(clock_hour_roller, 3);
    lv_roller_set_visible_row_count(clock_minute_roller, 3);
    lv_obj_set_size(clock_hour_roller, 100, 110);
    lv_obj_set_size(clock_minute_roller, 100, 110);
    lv_obj_set_pos(clock_hour_roller, 130, 110);
    lv_obj_set_pos(clock_minute_roller, 250, 110);
    lv_obj_set_style_text_font(clock_hour_roller, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_font(clock_minute_roller, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_t *colon = lv_label_create(clock_screen);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_32, 0);
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, 150);

    clock_enabled_switch = lv_switch_create(clock_screen);
    lv_obj_align(clock_enabled_switch, LV_ALIGN_TOP_MID, 0, 260);
    lv_obj_set_style_bg_color(clock_enabled_switch, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(clock_enabled_switch, lv_color_hex(UI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_t *enable = lv_label_create(clock_screen);
    lv_label_set_text(enable, chinese_font_20 ? "启用提醒" : "Enable reminder");
    lv_obj_set_style_text_font(enable, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(enable, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(enable, LV_ALIGN_TOP_MID, -75, 265);

    lv_obj_t *save = lv_btn_create(clock_screen);
    lv_obj_set_size(save, 180, 50);
    lv_obj_align(save, LV_ALIGN_TOP_MID, 0, 330);
    lv_obj_set_style_bg_color(save, lv_color_hex(UI_COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(save, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(save, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
    lv_obj_add_event_cb(save, clock_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *save_text = lv_label_create(save);
    lv_label_set_text(save_text, chinese_font_20 ? "保存" : "Save");
    lv_obj_set_style_text_font(save_text, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_center(save_text);

    clock_status_label = lv_label_create(clock_screen);
    lv_label_set_text(clock_status_label, "");
    lv_obj_set_style_text_font(clock_status_label, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_align(clock_status_label, LV_ALIGN_TOP_MID, 0, 405);

    clock_reminder_label = lv_label_create(lv_layer_top());
    lv_label_set_text(clock_reminder_label, chinese_font_20 ? "到时间了" : "Time's up");
    lv_obj_set_size(clock_reminder_label, 320, 100);
    lv_obj_set_style_text_font(clock_reminder_label, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(clock_reminder_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_align(clock_reminder_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(clock_reminder_label, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(clock_reminder_label, LV_OPA_90, 0);
    lv_obj_set_style_border_width(clock_reminder_label, 2, 0);
    lv_obj_set_style_border_color(clock_reminder_label, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(clock_reminder_label, 12, 0);
    lv_obj_set_style_pad_all(clock_reminder_label, 12, 0);
    lv_obj_center(clock_reminder_label);
    lv_obj_add_flag(clock_reminder_label, LV_OBJ_FLAG_HIDDEN);
}

static bool calendar_get_local_time(struct tm *tm_buf)
{
    const time_t now = time(NULL);
    return now > 1000000000 && localtime_r(&now, tm_buf) != NULL;
}

static int calendar_days_in_month(int year, int month)
{
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return days[month - 1];
}

/* Sunday-first Gregorian weekday index, matching the calendar header. */
static int calendar_weekday(int year, int month, int day)
{
    if (month < 3) {
        month += 12;
        --year;
    }
    const int century = year / 100;
    const int year_in_century = year % 100;
    const int zeller = (day + (13 * (month + 1)) / 5 + year_in_century +
                        year_in_century / 4 + century / 4 + 5 * century) % 7;
    return (zeller + 6) % 7; /* Zeller: 0=Saturday; convert to 0=Sunday. */
}

static void calendar_render_month(void)
{
    if (!calendar_month_label || !calendar_grid) return;

    if (calendar_view_year < 1 || calendar_view_month < 1) {
        lv_label_set_text(calendar_month_label, "等待校时");
        for (int i = 0; i < 42; ++i) {
            lv_label_set_text(calendar_day_labels[i], "");
            lv_obj_clear_state(calendar_day_labels[i], LV_STATE_USER_1);
        }
        return;
    }

    char title[24];
    snprintf(title, sizeof(title), "%04d年%02d月", calendar_view_year,
             calendar_view_month);
    lv_label_set_text(calendar_month_label, title);

    struct tm today;
    const bool time_valid = calendar_get_local_time(&today);
    const int first_weekday = calendar_weekday(calendar_view_year,
                                                calendar_view_month, 1);
    const int days = calendar_days_in_month(calendar_view_year,
                                             calendar_view_month);
    for (int i = 0; i < 42; ++i) {
        const int day = i - first_weekday + 1;
        if (day < 1 || day > days) {
            lv_label_set_text(calendar_day_labels[i], "");
            lv_obj_clear_state(calendar_day_labels[i], LV_STATE_USER_1);
            continue;
        }

        char day_text[3];
        snprintf(day_text, sizeof(day_text), "%d", day);
        lv_label_set_text(calendar_day_labels[i], day_text);
        const bool is_today = time_valid &&
                              calendar_view_year == today.tm_year + 1900 &&
                              calendar_view_month == today.tm_mon + 1 &&
                              day == today.tm_mday;
        if (is_today) lv_obj_add_state(calendar_day_labels[i], LV_STATE_USER_1);
        else lv_obj_clear_state(calendar_day_labels[i], LV_STATE_USER_1);
    }
}

static void calendar_build_grid(void)
{
    if (!calendar_screen || calendar_grid) return;

    calendar_grid = lv_obj_create(calendar_screen);
    lv_obj_set_size(calendar_grid, 456, 330);
    lv_obj_set_pos(calendar_grid, 12, 124);
    lv_obj_set_style_bg_opa(calendar_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(calendar_grid, 0, 0);
    lv_obj_set_style_pad_all(calendar_grid, 0, 0);
    lv_obj_clear_flag(calendar_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(calendar_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(calendar_grid, calendar_gesture_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(calendar_grid, calendar_gesture_cb, LV_EVENT_RELEASED, NULL);

    static const char *weekdays_zh[] = {"日", "一", "二", "三", "四", "五", "六"};
    for (int col = 0; col < 7; ++col) {
        lv_obj_t *weekday = lv_label_create(calendar_grid);
        lv_label_set_text(weekday, weekdays_zh[col]);
        lv_obj_set_size(weekday, 58, 30);
        lv_obj_set_pos(weekday, 3 + col * 64, 0);
        lv_obj_set_style_text_font(weekday, &lv_font_chinese_20_builtin, 0);
        lv_obj_set_style_text_align(weekday, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(weekday, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_clear_flag(weekday, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }

    for (int i = 0; i < 42; ++i) {
        lv_obj_t *day = lv_label_create(calendar_grid);
        calendar_day_labels[i] = day;
        lv_label_set_text(day, "");
        lv_obj_set_size(day, 58, 40);
        lv_obj_set_pos(day, 3 + (i % 7) * 64, 38 + (i / 7) * 48);
        lv_obj_set_style_text_font(day, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(day, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(day, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_bg_opa(day, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(day, lv_color_hex(UI_COLOR_ACCENT), LV_STATE_USER_1);
        lv_obj_set_style_bg_opa(day, LV_OPA_COVER, LV_STATE_USER_1);
        lv_obj_set_style_text_color(day, lv_color_hex(UI_COLOR_ON_ACCENT), LV_STATE_USER_1);
        lv_obj_set_style_radius(day, 18, LV_STATE_USER_1);
        lv_obj_set_style_border_width(day, 1, LV_STATE_USER_1);
        lv_obj_set_style_border_color(day, lv_color_hex(UI_COLOR_BORDER), LV_STATE_USER_1);
        lv_obj_clear_flag(day, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void calendar_app_btn_cb(lv_event_t *e)
{
    (void)e;
    struct tm now;
    if (calendar_get_local_time(&now)) {
        calendar_view_year = now.tm_year + 1900;
        calendar_view_month = now.tm_mon + 1;
    } else {
        calendar_view_year = 0;
        calendar_view_month = 0;
    }
    calendar_build_grid();
    calendar_render_month();
    lv_scr_load(calendar_screen);
}

static void calendar_back_cb(lv_event_t *e)
{
    (void)e;
    if (calendar_grid) {
        lv_obj_del(calendar_grid);
        calendar_grid = NULL;
        for (int i = 0; i < 42; ++i) calendar_day_labels[i] = NULL;
    }
    lv_scr_load(main_screen_ref);
}

static void calendar_gesture_cb(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        calendar_swipe_start_x = point.x;
        calendar_swipe_start_y = point.y;
        return;
    }
    if (code != LV_EVENT_RELEASED || calendar_view_year < 1 || calendar_view_month < 1) {
        return;
    }

    const int dx = point.x - calendar_swipe_start_x;
    const int dy = point.y - calendar_swipe_start_y;
    if (abs(dy) < 20 || abs(dy) <= abs(dx)) return;

    /* Match the 2048 screen's proven press/release swipe handling. */
    if (dy < 0) {
        if (++calendar_view_month > 12) {
            calendar_view_month = 1;
            ++calendar_view_year;
        }
    } else {
        if (--calendar_view_month < 1) {
            calendar_view_month = 12;
            --calendar_view_year;
        }
    }
    calendar_render_month();
}

static void create_calendar_screen(void)
{
    calendar_screen = lv_obj_create(NULL);
    ui_theme_apply_background(calendar_screen);
    lv_obj_clear_flag(calendar_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(calendar_screen);
    lv_label_set_text(title, "日历");
    lv_obj_set_style_text_font(title, &lv_font_chinese_20_builtin, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    calendar_month_label = lv_label_create(calendar_screen);
    lv_label_set_text(calendar_month_label, "");
    lv_obj_set_style_text_font(calendar_month_label, &lv_font_chinese_20_builtin, 0);
    lv_obj_set_style_text_color(calendar_month_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(calendar_month_label, LV_ALIGN_TOP_MID, 0, 63);
    lv_obj_add_flag(calendar_month_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_back_button(calendar_screen, LV_ALIGN_TOP_RIGHT, -8, calendar_back_cb);
}

static void photo_app_btn_cb(lv_event_t *e)
{
    (void)e;
    scan_photo_files();
    photo_render_gallery();
    lv_scr_load(photo_screen);
}

static void photo_open_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    const char *path = get_photo_path(index);
    if (!path) return;

    if (photo_load_timer) {
        lv_timer_del(photo_load_timer);
        photo_load_timer = NULL;
    }
    lv_img_set_src(photo_full_img, NULL);
    lv_label_set_text(photo_loading_label,
                      chinese_font_20 ? "正在读取照片..." : "Loading photo...");
    lv_obj_clear_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
    lv_scr_load(photo_view_screen);
    lv_refr_now(NULL);

    if (!start_photo_load(path)) {
        lv_label_set_text(photo_loading_label,
                          chinese_font_20 ? "照片仍在读取，请稍后重试" :
                                           "Another photo is still loading");
        return;
    }
    photo_load_timer = lv_timer_create(photo_load_timer_cb, 25, NULL);
}

static void photo_load_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    const int status = get_photo_load_status();
    if (status == 2) {
        const lv_img_dsc_t *image = get_loaded_photo_image();
        if (image) {
            lv_img_set_src(photo_full_img, image);
            lv_obj_add_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(photo_full_img);
        }
    } else if (status < 0) {
        lv_label_set_text(photo_loading_label,
                          chinese_font_20 ? "照片读取失败" : "Photo load failed");
    } else {
        return;
    }

    if (photo_load_timer) {
        lv_timer_del(photo_load_timer);
        photo_load_timer = NULL;
    }
}

static void photo_close_cb(lv_event_t *e)
{
    (void)e;
    if (photo_load_timer) {
        lv_timer_del(photo_load_timer);
        photo_load_timer = NULL;
    }
    lv_scr_load(photo_screen);
}

static void photo_back_cb(lv_event_t *e)
{
    (void)e;
    lv_scr_load(main_screen_ref);
}

static void photo_render_gallery(void)
{
    if (!photo_gallery) return;
    lv_obj_clean(photo_gallery);
    int count = get_photo_count();
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(photo_gallery);
        lv_label_set_text(empty, chinese_font_20 ? "没有照片" : "No photos");
        lv_obj_set_style_text_font(empty, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_center(empty);
        return;
    }
    for (int i = 0; i < count; ++i) {
        lv_obj_t *button = lv_btn_create(photo_gallery);
        lv_obj_set_size(button, SCREEN_WIDTH - 32, 48);
        lv_obj_set_pos(button, 16, 8 + i * 56);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_pad_hor(button, 14, 0);
        lv_obj_add_event_cb(button, photo_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *filename = lv_label_create(button);
        lv_label_set_text(filename, get_photo_name(i));
        lv_obj_set_style_text_font(filename, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_set_width(filename, lv_pct(100));
        lv_obj_set_height(filename,
                          lv_font_get_line_height(ui_chinese_font_20(&lv_font_montserrat_20)));
        lv_label_set_long_mode(filename, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(filename, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(filename, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void create_photo_screen(void)
{
    photo_screen = lv_obj_create(NULL);
    ui_theme_apply_background(photo_screen);
    create_back_button(photo_screen, LV_ALIGN_TOP_RIGHT, -8, photo_back_cb);
    lv_obj_t *title = lv_label_create(photo_screen);
    lv_label_set_text(title, chinese_font_20 ? "照片" : "Photos");
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    photo_gallery = lv_obj_create(photo_screen);
    lv_obj_set_size(photo_gallery, SCREEN_WIDTH, 410);
    lv_obj_set_pos(photo_gallery, 0, 58);
    lv_obj_set_style_bg_opa(photo_gallery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(photo_gallery, 0, 0);
    lv_obj_set_style_pad_all(photo_gallery, 0, 0);
    lv_obj_add_flag(photo_gallery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(photo_gallery, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(photo_gallery, LV_SCROLLBAR_MODE_AUTO);
}

static void create_photo_view_screen(void)
{
    photo_view_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(photo_view_screen, lv_color_hex(UI_COLOR_MEDIA_BG), 0);
    photo_full_img = lv_img_create(photo_view_screen);
    lv_img_set_antialias(photo_full_img, true);
    lv_obj_align(photo_full_img, LV_ALIGN_CENTER, 0, 0);
    photo_loading_label = lv_label_create(photo_view_screen);
    lv_label_set_text(photo_loading_label,
                      chinese_font_20 ? "正在读取照片..." : "Loading photo...");
    lv_obj_set_style_text_font(photo_loading_label,
                               ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(photo_loading_label, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
    lv_obj_center(photo_loading_label);
    lv_obj_add_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
    create_back_button(photo_view_screen, LV_ALIGN_TOP_RIGHT, -8, photo_close_cb);
}

static void video_app_btn_cb(lv_event_t *e)
{
    (void)e;
    scan_video_files();
    video_render_gallery();
    lv_scr_load(video_screen);
}

static void video_set_playback_mode(bool enabled)
{
    lv_disp_t *display = lv_disp_get_default();
    if (display && display->refr_timer) {
        if (enabled && !video_fast_refresh) {
            video_saved_refr_period_ms = display->refr_timer->period;
            lv_timer_set_period(display->refr_timer, 5);
            video_fast_refresh = true;
        } else if (!enabled && video_fast_refresh) {
            lv_timer_set_period(display->refr_timer, video_saved_refr_period_ms);
            video_fast_refresh = false;
        }
    }
    set_video_vsync_sync(enabled);
}

static void video_frame_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (video_audio_is_active()) {
        uint32_t position = 0;
        uint32_t duration = 0;
        uint32_t cycle = 0;
        uint32_t underruns = 0;
        video_audio_get_clock(&position, &duration, &cycle, &underruns);
        if (video_present_for_audio(position, duration, cycle,
                                    get_video_source_fps_milli()))
            lv_obj_invalidate(video_frame_img);
        return;
    }

    const uint32_t now = lv_tick_get();
    if ((int32_t)(now - video_next_frame_ms) < 0) return;
    if (!read_next_video_frame()) return;

    lv_obj_invalidate(video_frame_img);
    video_next_frame_ms += video_frame_period_ms;
    /* Keep the source cadence without bursts after a slow render/read. */
    if ((int32_t)(now - video_next_frame_ms) >= 0)
        video_next_frame_ms = now + video_frame_period_ms;
}

static void video_open_cb(lv_event_t *e)
{
    int index = (int)(intptr_t)lv_event_get_user_data(e);
    if (video_frame_timer) {
        lv_timer_del(video_frame_timer);
        video_frame_timer = NULL;
    }

    /* Present an empty viewer before the synchronous SD header/first-frame
     * read. This prevents the gallery from appearing frozen during startup. */
    video_set_playback_mode(false);
    lv_img_set_src(video_frame_img, NULL);
    lv_scr_load(video_view_screen);
    lv_refr_now(NULL);

    if (!open_video_file(index)) return;

    // Submit video frames at RGB VSYNC boundaries so the continuously
    // scanning framebuffer never shows an intermediate frame.
    video_set_playback_mode(true);

    const lv_img_dsc_t *frame = get_video_frame_image();
    video_frame_period_ms = get_video_frame_delay_ms();
    if (video_frame_period_ms == 0)
        video_frame_period_ms = 50;
    if (!frame) {
        close_video_file();
        video_audio_release();
        video_set_playback_mode(false);
        return;
    }

    const char *path = get_video_path(index);
    const bool audio_ready = path &&
        video_audio_prepare(path, get_video_frame_count(), get_video_source_fps_milli());

    lv_img_set_src(video_frame_img, frame);
    uint16_t zoom_x = (uint16_t)((SCREEN_WIDTH * 256L) / frame->header.w);
    uint16_t zoom_y = (uint16_t)(((SCREEN_HEIGHT - 80) * 256L) / frame->header.h);
    lv_img_set_zoom(video_frame_img, LV_MIN(zoom_x, zoom_y));
    lv_obj_center(video_frame_img);
    lv_obj_invalidate(video_frame_img);
    /* Frame 0 is on screen before the soundtrack starts. */
    lv_refr_now(NULL);
    if (audio_ready)
        (void)video_audio_start();
    video_next_frame_ms = lv_tick_get() + video_frame_period_ms;
    /* Poll readiness often; SD prefetch never blocks this callback. */
    video_frame_timer = lv_timer_create(video_frame_timer_cb, 5, NULL);
}

static void video_close_cb(lv_event_t *e)
{
    (void)e;
    if (video_frame_timer) {
        lv_timer_del(video_frame_timer);
        video_frame_timer = NULL;
    }
    close_video_file();
    video_audio_release();
    video_set_playback_mode(false);
    lv_scr_load(video_screen);
}

static void video_back_cb(lv_event_t *e)
{
    (void)e;
    close_video_file();
    video_audio_release();
    video_set_playback_mode(false);
    lv_scr_load(main_screen_ref);
}

static void video_render_gallery(void)
{
    if (!video_gallery) return;
    lv_obj_clean(video_gallery);
    int count = get_video_count();
    if (count == 0) {
        lv_obj_t *empty = lv_label_create(video_gallery);
        lv_label_set_text(empty, chinese_font_20 ? "没有视频" : "No videos");
        lv_obj_set_style_text_font(empty, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_center(empty);
        return;
    }

    for (int i = 0; i < count; ++i) {
        lv_obj_t *button = lv_btn_create(video_gallery);
        lv_obj_set_size(button, SCREEN_WIDTH - 32, 48);
        lv_obj_set_pos(button, 16, 8 + i * 56);
        lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_radius(button, 8, 0);
        lv_obj_set_style_pad_hor(button, 14, 0);
        lv_obj_add_event_cb(button, video_open_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_t *filename = lv_label_create(button);
        lv_label_set_text(filename, get_video_name(i));
        lv_obj_set_style_text_font(filename, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_set_width(filename, lv_pct(100));
        lv_obj_set_height(filename,
                          lv_font_get_line_height(ui_chinese_font_20(&lv_font_montserrat_20)));
        lv_label_set_long_mode(filename, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(filename, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(filename, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

static void create_video_screen(void)
{
    video_screen = lv_obj_create(NULL);
    ui_theme_apply_background(video_screen);
    create_back_button(video_screen, LV_ALIGN_TOP_RIGHT, -8, video_back_cb);
    lv_obj_t *title = lv_label_create(video_screen);
    lv_label_set_text(title, chinese_font_20 ? "视频" : "Videos");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    video_gallery = lv_obj_create(video_screen);
    lv_obj_set_size(video_gallery, SCREEN_WIDTH, 410);
    lv_obj_set_pos(video_gallery, 0, 58);
    lv_obj_set_style_bg_opa(video_gallery, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(video_gallery, 0, 0);
    lv_obj_set_style_pad_all(video_gallery, 0, 0);
    lv_obj_add_flag(video_gallery, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(video_gallery, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(video_gallery, LV_SCROLLBAR_MODE_AUTO);
}

static void create_video_view_screen(void)
{
    video_view_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(video_view_screen, lv_color_hex(UI_COLOR_MEDIA_BG), 0);
    video_frame_img = lv_img_create(video_view_screen);
    video_draw_set_image(video_frame_img);
    lv_img_set_antialias(video_frame_img, true);
    lv_obj_align(video_frame_img, LV_ALIGN_CENTER, 0, 0);
    create_back_button(video_view_screen, LV_ALIGN_TOP_RIGHT, -8, video_close_cb);
}

/* ==================== PAGE 1: ICON GRID ==================== */

static void gyro_btn_cb(lv_event_t *e) { lv_scr_load(gyro_screen); }
static void game_btn_cb(lv_event_t *e) { lv_scr_load(game_screen); }
static void settings_btn_cb(lv_event_t *e) { lv_scr_load(settings_screen); }
static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    if (ai_chat_keyboard) lv_obj_add_flag(ai_chat_keyboard, LV_OBJ_FLAG_HIDDEN);
    if (ai_chat_ime) {
        lv_obj_t *ime_panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (ime_panel) lv_obj_add_flag(ime_panel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_scr_load(main_screen_ref);
}
static void game_restart_cb(lv_event_t *e) { game_restart(); }

static void ai_assistant_app_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!ai_assistant_screen) {
        ai_assistant_screen = lv_obj_create(NULL);
        lv_obj_set_size(ai_assistant_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
        ui_theme_apply_background(ai_assistant_screen);
        lv_obj_clear_flag(ai_assistant_screen, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *header = lv_obj_create(ai_assistant_screen);
        lv_obj_set_size(header, SCREEN_WIDTH, 58);
        lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_border_width(header, 0, 0);
        lv_obj_set_style_radius(header, 0, 0);

        lv_obj_t *title = lv_label_create(ai_assistant_screen);
        lv_label_set_text(title, "AI 助手");
        lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);
        create_back_button(ai_assistant_screen, LV_ALIGN_TOP_RIGHT, -8, back_btn_cb);

        ai_chat_status = lv_label_create(ai_assistant_screen);
        lv_label_set_text(ai_chat_status, "输入问题，AI 将以文字和语音回复");
        lv_obj_set_width(ai_chat_status, 440);
        lv_obj_set_style_text_font(ai_chat_status, ui_chinese_font_16(&lv_font_montserrat_14), 0);
        lv_obj_set_style_text_color(ai_chat_status, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(ai_chat_status, LV_ALIGN_TOP_MID, 0, 72);

        lv_obj_t *reply_panel = lv_obj_create(ai_assistant_screen);
        lv_obj_set_size(reply_panel, 448, 232);
        lv_obj_set_pos(reply_panel, 16, 108);
        lv_obj_set_style_bg_color(reply_panel, lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_border_color(reply_panel, lv_color_hex(UI_COLOR_BORDER), 0);
        lv_obj_set_style_border_width(reply_panel, 1, 0);
        lv_obj_set_style_radius(reply_panel, 10, 0);
        lv_obj_set_style_pad_all(reply_panel, 12, 0);
        lv_obj_set_scroll_dir(reply_panel, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(reply_panel, LV_SCROLLBAR_MODE_AUTO);

        ai_chat_reply = lv_label_create(reply_panel);
        lv_label_set_text(ai_chat_reply, "你好！有什么可以帮你");
        lv_obj_set_width(ai_chat_reply, 420);
        lv_label_set_long_mode(ai_chat_reply, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(ai_chat_reply, ui_chinese_font_16(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(ai_chat_reply, lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(ai_chat_reply, LV_ALIGN_TOP_LEFT, 0, 0);

        ai_chat_input = lv_textarea_create(ai_assistant_screen);
        lv_obj_set_size(ai_chat_input, 448, 55);
        lv_obj_set_pos(ai_chat_input, 16, 352);
        lv_obj_add_flag(ai_chat_input, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CLICK_FOCUSABLE);
        lv_textarea_set_one_line(ai_chat_input, true);
        lv_textarea_set_max_length(ai_chat_input, 500);
        lv_textarea_set_text(ai_chat_input, "");
        lv_textarea_set_placeholder_text(ai_chat_input, "输入问题");
        lv_obj_set_style_bg_color(ai_chat_input, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
        lv_obj_set_style_text_color(ai_chat_input, lv_color_hex(UI_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_text_font(ai_chat_input, ui_chinese_font_20(&lv_font_montserrat_16), LV_PART_MAIN);
        lv_obj_set_style_border_color(ai_chat_input, lv_color_hex(UI_COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_radius(ai_chat_input, 8, LV_PART_MAIN);

        ai_chat_send_button = lv_btn_create(ai_assistant_screen);
        lv_obj_set_size(ai_chat_send_button, 448, 48);
        lv_obj_set_pos(ai_chat_send_button, 16, 418);
        lv_obj_set_style_bg_color(ai_chat_send_button, lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(ai_chat_send_button, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_STATE_PRESSED);
        lv_obj_set_style_radius(ai_chat_send_button, 8, 0);
        lv_obj_add_event_cb(ai_chat_send_button, ai_chat_send_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *send_label = lv_label_create(ai_chat_send_button);
        lv_label_set_text(send_label, "发送并播放");
        lv_obj_set_style_text_font(send_label, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(send_label, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
        lv_obj_center(send_label);

        ai_chat_keyboard = lv_keyboard_create(ai_assistant_screen);
        lv_keyboard_set_textarea(ai_chat_keyboard, ai_chat_input);
        lv_obj_set_size(ai_chat_keyboard, SCREEN_WIDTH, AI_KEYBOARD_HEIGHT);
        /* Reserve a fixed candidate row above the keyboard. */
        lv_obj_set_align(ai_chat_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(ai_chat_keyboard, 0, AI_CANDIDATE_HEIGHT);
        lv_obj_add_flag(ai_chat_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ai_chat_keyboard, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(ai_chat_keyboard, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ai_chat_keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
        lv_obj_set_style_bg_opa(ai_chat_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
        lv_obj_set_style_text_color(ai_chat_keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
        ai_chat_ime = lv_ime_pinyin_create(ai_assistant_screen);
        lv_ime_pinyin_set_keyboard(ai_chat_ime, ai_chat_keyboard);
        lv_pinyin_dict_t *pinyin_dict = get_sd_pinyin_dict();
        lv_ime_pinyin_set_dict(ai_chat_ime,
                               pinyin_dict ? pinyin_dict : ai_chat_ime_dict);
        lv_ime_pinyin_set_mode(ai_chat_ime, LV_IME_PINYIN_MODE_K26);
        /* The IME object is an event/processing helper.  Its default
         * bottom-half size would otherwise sit above the textarea and consume
         * all touches before they reach the input field. */
        lv_obj_clear_flag(ai_chat_ime,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                          LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_set_size(ai_chat_ime, 1, 1);
        lv_obj_t *ime_panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (ime_panel) {
            /* lv_ime_pinyin creates this panel on lv_scr_act().  The AI
             * screen is built before it becomes active, so explicitly move
             * the panel to the screen that owns the keyboard. */
            lv_obj_set_parent(ime_panel, ai_assistant_screen);
            lv_obj_set_size(ime_panel, SCREEN_WIDTH, AI_CANDIDATE_HEIGHT);
            lv_obj_set_pos(ime_panel, 0, 0);
            lv_obj_set_style_text_font(ime_panel,
                                       ui_chinese_font_20(&lv_font_montserrat_16),
                                       LV_PART_ITEMS);
            lv_obj_set_style_text_color(ime_panel, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
            lv_obj_set_style_bg_color(ime_panel, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(ime_panel, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_move_foreground(ime_panel);
        }
        lv_obj_add_event_cb(ai_chat_input, ai_chat_keyboard_cb, LV_EVENT_CLICKED,
                            ai_chat_keyboard);
        lv_obj_add_event_cb(ai_chat_input, ai_chat_keyboard_cb, LV_EVENT_PRESSED,
                            ai_chat_keyboard);
        lv_obj_add_event_cb(ai_chat_input, ai_chat_keyboard_cb, LV_EVENT_FOCUSED,
                            ai_chat_keyboard);
        lv_obj_add_event_cb(ai_chat_keyboard, ai_chat_keyboard_cb, LV_EVENT_CANCEL,
                             ai_chat_keyboard);
        lv_obj_add_event_cb(ai_chat_keyboard, ai_chat_keyboard_cb, LV_EVENT_READY,
                             ai_chat_keyboard);
        lv_obj_add_event_cb(ai_chat_keyboard, ai_chat_keyboard_cb,
                            LV_EVENT_LONG_PRESSED_REPEAT, ai_chat_keyboard);

        ai_chat_poll_timer = lv_timer_create(ai_chat_poll_cb, 150, NULL);
    }
    lv_scr_load(ai_assistant_screen);
}

static void ai_chat_send_cb(lv_event_t *e)
{
    (void)e;
    if (!ai_chat_input || ai_chat_is_busy()) return;
    const char *message = lv_textarea_get_text(ai_chat_input);
    if (!message || !message[0]) {
        lv_label_set_text(ai_chat_status, "请先输入问题");
        return;
    }
    snprintf(ai_chat_last_reply, sizeof(ai_chat_last_reply), "我：%s\n\nAI：", message);
    lv_label_set_text(ai_chat_reply, ai_chat_last_reply);
    lv_obj_scroll_to_y(lv_obj_get_parent(ai_chat_reply), LV_COORD_MAX, LV_ANIM_OFF);
    if (!ai_chat_submit(message)) {
        lv_label_set_text(ai_chat_status, "AI 服务暂时无法启动");
        return;
    }
    lv_obj_add_state(ai_chat_send_button, LV_STATE_DISABLED);
    lv_label_set_text(ai_chat_status, "正在连接 AI 并生成语音...");
    lv_textarea_set_text(ai_chat_input, "");
}

static void ai_chat_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!ai_chat_status || !ai_chat_reply) return;

    const int result = ai_chat_get_result(ai_chat_last_reply,
                                          sizeof(ai_chat_last_reply),
                                          ai_chat_last_error,
                                          sizeof(ai_chat_last_error));
    if (result == 1) {
        lv_label_set_text_fmt(ai_chat_reply, "AI：%s", ai_chat_last_reply);
        lv_obj_scroll_to_y(lv_obj_get_parent(ai_chat_reply), LV_COORD_MAX, LV_ANIM_OFF);
        lv_label_set_text(ai_chat_status, ai_audio_is_playing() ?
                          "正在设备扬声器播放..." : "回答完成，可继续提问");
    } else if (result < 0) {
        lv_label_set_text(ai_chat_status, ai_chat_last_error);
    } else if (!ai_websocket_is_connected()) {
        lv_label_set_text(ai_chat_status, "等待服务器连接...");
    }

    if (ai_chat_is_busy()) {
        lv_obj_add_state(ai_chat_send_button, LV_STATE_DISABLED);
        return;
    }
    lv_obj_clear_state(ai_chat_send_button, LV_STATE_DISABLED);
}

static void ai_chat_show_keyboard(void)
{
    if (!ai_chat_keyboard || !ai_chat_input) return;

    show_text_keyboard(ai_chat_keyboard, ai_chat_input);
    /* The shared helper uses a bottom anchored layout for regular forms.
     * Chat keeps its composer at the bottom, so place its keyboard at the top
     * and leave the candidate row directly below it. */
    lv_obj_set_align(ai_chat_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ai_chat_keyboard, 0, AI_CANDIDATE_HEIGHT);
    if (ai_chat_ime) {
        lv_obj_t *ime_panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (ime_panel) {
            lv_obj_set_size(ime_panel, SCREEN_WIDTH, AI_CANDIDATE_HEIGHT);
            lv_obj_set_pos(ime_panel, 0, 0);
            lv_obj_move_foreground(ime_panel);
        }
    }
    lv_obj_invalidate(ai_chat_keyboard);
}

static uint32_t ai_chat_utf8_next(const char **text)
{
    const uint8_t *bytes = (const uint8_t *)*text;
    uint32_t codepoint;
    if (bytes[0] < 0x80) {
        codepoint = bytes[0];
        *text += 1;
    } else if ((bytes[0] & 0xE0) == 0xC0) {
        codepoint = ((uint32_t)(bytes[0] & 0x1F) << 6) | (bytes[1] & 0x3F);
        *text += 2;
    } else if ((bytes[0] & 0xF0) == 0xE0) {
        codepoint = ((uint32_t)(bytes[0] & 0x0F) << 12) |
                    ((uint32_t)(bytes[1] & 0x3F) << 6) | (bytes[2] & 0x3F);
        *text += 3;
    } else {
        codepoint = ((uint32_t)(bytes[0] & 0x07) << 18) |
                    ((uint32_t)(bytes[1] & 0x3F) << 12) |
                    ((uint32_t)(bytes[2] & 0x3F) << 6) | (bytes[3] & 0x3F);
        *text += 4;
    }
    return codepoint;
}

static bool ai_chat_font_has_glyph(uint32_t codepoint)
{
    if (codepoint < 0x80) return true;
    for (uint16_t i = 0; i < ai_chat_glyph_cache_count; i++) {
        if (ai_chat_glyph_cache_codepoints[i] == codepoint) {
            return ai_chat_glyph_cache_results[i] != 0;
        }
    }

    lv_font_glyph_dsc_t glyph = {};
    bool found = chinese_font_20 &&
                 lv_font_get_glyph_dsc(chinese_font_20, &glyph, codepoint, 0);
    if (ai_chat_glyph_cache_count <
        sizeof(ai_chat_glyph_cache_codepoints) / sizeof(ai_chat_glyph_cache_codepoints[0])) {
        uint16_t index = ai_chat_glyph_cache_count++;
        ai_chat_glyph_cache_codepoints[index] = codepoint;
        ai_chat_glyph_cache_results[index] = found ? 1 : 0;
    }
    return found;
}

static bool ai_chat_phrase_has_font(const char *phrase)
{
    const char *cursor = phrase;
    while (*cursor) {
        uint32_t codepoint = ai_chat_utf8_next(&cursor);
        if (!ai_chat_font_has_glyph(codepoint)) return false;
    }
    return true;
}

static void ai_chat_keyboard_cb(lv_event_t *e)
{
    lv_obj_t *keyboard = (lv_obj_t *)lv_event_get_user_data(e);
    if (!keyboard) return;
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED ||
        code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        ai_chat_show_keyboard();
    } else if (code == LV_EVENT_LONG_PRESSED_REPEAT) {
        uint16_t btn_id = lv_btnmatrix_get_selected_btn(keyboard);
        const char *btn_text = lv_btnmatrix_get_btn_text(keyboard, btn_id);
        const bool is_enter = btn_text &&
                              (strcmp(btn_text, LV_SYMBOL_NEW_LINE) == 0 ||
                               strcmp(btn_text, LV_SYMBOL_OK) == 0 ||
                               strcmp(btn_text, "Enter") == 0);
        if (is_enter) {
            hide_text_keyboard(keyboard);
            if (ai_chat_ime) {
                lv_obj_t *ime_panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
                if (ime_panel) lv_obj_add_flag(ime_panel, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_state(ai_chat_input, LV_STATE_FOCUSED);
        }
    } else if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        hide_text_keyboard(keyboard);
        if (ai_chat_ime) {
            lv_obj_t *ime_panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
            if (ime_panel) lv_obj_add_flag(ime_panel, LV_OBJ_FLAG_HIDDEN);
        }
        if (code == LV_EVENT_READY) ai_chat_send_cb(e);
    }
}

static lv_obj_t * make_icon_btn(lv_obj_t *parent, const char *label, const void *img, uint16_t zoom, uint32_t color, int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    /* The image supplies its own rounded border; keep the button as a
     * transparent touch target so a second rectangular frame is not drawn. */
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *img_obj = lv_img_create(btn);
    lv_img_set_src(img_obj, img);
    lv_img_set_zoom(img_obj, zoom);
    lv_obj_align(img_obj, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, label);
    lv_obj_set_style_text_color(txt, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(txt, chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_14, 0);
    /* Put the caption immediately below the image instead of at the very
     * bottom of the touch target. */
    lv_obj_align(txt, LV_ALIGN_TOP_MID, 0, 60);

    return btn;
}

static lv_obj_t * make_theme_icon_btn(lv_obj_t *parent, const char *label, const void *img, uint16_t zoom, uint32_t color, int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    /* The image supplies its own rounded border; keep the button as a
     * transparent touch target so a second rectangular frame is not drawn. */
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_10, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    (void)color;
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);

    lv_obj_t *img_obj = lv_img_create(btn);
    lv_img_set_src(img_obj, img);
    lv_img_set_zoom(img_obj, zoom);
    lv_obj_align(img_obj, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *txt = lv_label_create(btn);
    lv_label_set_text(txt, label);
    lv_obj_set_style_text_color(txt, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(txt, chinese_font_20 ? chinese_font_20 : &lv_font_montserrat_14, 0);
    /* Put the caption immediately below the image instead of at the very
     * bottom of the touch target. */
    lv_obj_align(txt, LV_ALIGN_TOP_MID, 0, 60);

    return btn;
}

static void browser_app_btn_cb(lv_event_t *e)
{
    (void)e;
    browser_ui_open(main_screen_ref, ui_chinese_font_20(&lv_font_montserrat_16),
                    chinese_font_20 != NULL, create_back_button);
}

static void setup_icon_page(lv_obj_t *page)
{
    /* The weather app temporarily reuses this page; restore its grass gradient. */
    ui_theme_apply_background(page);
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "应用");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);

    // Four-column grid keeps all applications on one page while retaining a
    // stable touch target for each icon and caption.
    // The buttons are transparent touch targets; the visible borders belong
    // to the image assets.
    const int btn_w = 112, btn_h = 100, gap_x = 8, gap_y = 2;
    const int start_x = (SCREEN_WIDTH - (btn_w * 4 + gap_x * 3)) / 2;
    const int start_y = 52;

    struct { const char *label; const void *img; uint16_t zoom; uint32_t color; lv_event_cb_t cb; } icons[] = {
        {"陀螺仪", &gyro_img,          140, 0x4444FF, gyro_btn_cb},
        {"2048",   &numpad_img,        140, 0xFF8800, game_btn_cb},
        {"平衡球", &game_img,          125, 0xF07A3E, balance_game_btn_cb},
        {"音乐",   &sd_icon_music,     140, 0xF04455, music_app_btn_cb},
        {"天气",   &sd_icon_weather,   140, 0x00AEEF, weather_app_btn_cb},
        {"时钟",   &sd_icon_clock,     140, 0xFFAA00, clock_btn_cb},
        {"照片",   &sd_icon_photos,    140, 0xFFAA00, photo_app_btn_cb},
        {"浏览器", &sd_icon_browser,   140, 0x397F8A, browser_app_btn_cb},
        {"视频",   &sd_icon_video,     140, 0x8C48D8, video_app_btn_cb},
        {"设置",   &sd_icon_settings,  140, 0x12AEB2, settings_btn_cb},
        {"日历",   &sd_icon_calendar,  140, 0x35A85A, calendar_app_btn_cb},
        {"AI助手", &sd_icon_ai_assistant, 140, 0x4A78D0, ai_assistant_app_btn_cb},
    };
    int num_icons = sizeof(icons) / sizeof(icons[0]);

    for (int i = 0; i < num_icons; i++) {
        int col = i % 4, row = i / 4;
        lv_obj_t *btn = make_icon_btn(page,
            icons[i].label, icons[i].img, icons[i].zoom, icons[i].color,
            start_x + col * (btn_w + gap_x), start_y + row * (btn_h + gap_y),
            btn_w, btn_h);
        if (icons[i].cb) lv_obj_add_event_cb(btn, icons[i].cb, LV_EVENT_CLICKED, NULL);
    }

    detail_info = NULL;
}

static void weather_app_btn_cb(lv_event_t *e)
{
    (void)e;
    /* Reuse the app-grid page so the forecast UI does not add another full
     * screen's worth of LVGL objects to this no-PSRAM target. */
    lv_obj_clean(pages[1]);
    detail_info = NULL;
    weather_page_city_label = NULL;
    weather_page_icon = NULL;
    weather_page_condition = NULL;
    weather_page_temp = NULL;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) weather_forecast_labels[i] = NULL;
    setup_weather_page(pages[1]);
}

static void weather_back_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_clean(pages[1]);
    weather_page_city_label = NULL;
    weather_page_icon = NULL;
    weather_page_condition = NULL;
    weather_page_temp = NULL;
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) weather_forecast_labels[i] = NULL;
    setup_icon_page(pages[1]);
}

/* Static sky decoration uses regular LVGL shapes, without image buffers or timers. */
static void weather_sky_shape(lv_obj_t *page, int x, int y, int width, int height,
                              uint32_t color, lv_opa_t opacity, lv_coord_t radius)
{
    lv_obj_t *shape = lv_obj_create(page);
    lv_obj_remove_style_all(shape);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_set_style_bg_color(shape, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(shape, opacity, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE |
                            LV_OBJ_FLAG_GESTURE_BUBBLE);
}

static void weather_sky_cloud(lv_obj_t *page, int x, int y, int width)
{
    weather_sky_shape(page, x + width / 12, y + width / 6,
                     width * 10 / 12, width / 4,
                     UI_COLOR_CLOUD, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    weather_sky_shape(page, x + width / 6, y + width / 12,
                     width / 3, width / 3,
                     UI_COLOR_CLOUD, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    weather_sky_shape(page, x + width * 5 / 12, y,
                     width * 5 / 12, width * 5 / 12,
                     UI_COLOR_CLOUD, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    weather_sky_shape(page, x + width * 8 / 12, y + width / 8,
                     width / 3, width / 3,
                     UI_COLOR_CLOUD, LV_OPA_COVER, LV_RADIUS_CIRCLE);
}

static void weather_style_card(lv_obj_t *card)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SKY_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_SKY_BORDER), 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_text_color(card, lv_color_hex(UI_COLOR_SKY_TEXT), 0);
}

static void setup_weather_page(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_hex(UI_COLOR_SKY_TOP), 0);
    lv_obj_set_style_bg_grad_color(page, lv_color_hex(UI_COLOR_SKY_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(page, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_stop(page, 0, 0);
    lv_obj_set_style_bg_grad_stop(page, 255, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(page, lv_color_hex(UI_COLOR_SKY_TEXT), 0);

    /* Create decoration first so all controls and forecast text sit above it. */
    weather_sky_shape(page, 358, 5, 76, 76, UI_COLOR_SUN, LV_OPA_30, LV_RADIUS_CIRCLE);
    weather_sky_shape(page, 367, 14, 58, 58, UI_COLOR_SUN, LV_OPA_COVER, LV_RADIUS_CIRCLE);
    weather_sky_cloud(page, 168, 17, 108);
    weather_sky_cloud(page, 308, 172, 112);
    weather_sky_cloud(page, 312, 429, 122);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, chinese_font_20 ? "天气" : "Weather");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_SKY_TEXT), 0);
    lv_obj_set_pos(title, 18, 20);

    lv_obj_t *city_btn = lv_btn_create(page);
    lv_obj_set_size(city_btn, 166, 132);
    lv_obj_set_pos(city_btn, 14, 72);
    weather_style_card(city_btn);
    lv_obj_set_style_bg_color(city_btn, lv_color_hex(UI_COLOR_SKY_BOTTOM), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(city_btn, 8, 0);
    lv_obj_add_event_cb(city_btn, weather_city_btn_cb, LV_EVENT_CLICKED, NULL);

    weather_page_city_label = lv_label_create(city_btn);
    lv_label_set_text(weather_page_city_label, ui_city_name());
    lv_obj_set_style_text_font(weather_page_city_label, ui_chinese_font_32(&lv_font_montserrat_32), 0);
    lv_obj_set_style_text_color(weather_page_city_label, lv_color_hex(UI_COLOR_SKY_TEXT), 0);
    lv_obj_set_width(weather_page_city_label, 148);
    lv_label_set_long_mode(weather_page_city_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(weather_page_city_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(weather_page_city_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *city_hint = lv_label_create(city_btn);
    lv_label_set_text(city_hint, chinese_font_20 ? "切换城市" : "Change city");
    lv_obj_set_style_text_font(city_hint, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(city_hint, lv_color_hex(UI_COLOR_SKY_MUTED), 0);
    lv_obj_align(city_hint, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_obj_t *current = lv_obj_create(page);
    lv_obj_set_size(current, 272, 132);
    lv_obj_set_pos(current, 194, 72);
    weather_style_card(current);
    lv_obj_set_style_pad_all(current, 8, 0);
    lv_obj_clear_flag(current, LV_OBJ_FLAG_SCROLLABLE);

    weather_page_icon = lv_img_create(current);
    lv_img_set_src(weather_page_icon, get_weather_icon(weather));
    lv_img_set_zoom(weather_page_icon, 160);
    lv_obj_align(weather_page_icon, LV_ALIGN_LEFT_MID, 2, 0);

    weather_page_condition = lv_label_create(current);
    lv_label_set_text(weather_page_condition, weather ? weather : "--");
    lv_obj_set_style_text_font(weather_page_condition, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(weather_page_condition, lv_color_hex(UI_COLOR_SKY_MUTED), 0);
    lv_obj_set_width(weather_page_condition, 164);
    lv_label_set_long_mode(weather_page_condition, LV_LABEL_LONG_DOT);
    lv_obj_align(weather_page_condition, LV_ALIGN_TOP_LEFT, 86, 20);

    weather_page_temp = lv_label_create(current);
    char current_temp[16];
    snprintf(current_temp, sizeof(current_temp), "%s°C", temp ? temp : "--");
    lv_label_set_text(weather_page_temp, current_temp);
    lv_obj_set_style_text_font(weather_page_temp, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(weather_page_temp, lv_color_hex(UI_COLOR_SKY_TEXT), 0);
    lv_obj_align(weather_page_temp, LV_ALIGN_TOP_LEFT, 86, 62);

    lv_obj_t *forecast_title = lv_label_create(page);
    lv_label_set_text(forecast_title, chinese_font_20 ? "未来三天预报" : "3-day forecast");
    lv_obj_set_style_text_font(forecast_title, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(forecast_title, lv_color_hex(UI_COLOR_SKY_TEXT), 0);
    lv_obj_set_pos(forecast_title, 18, 216);
    for (int i = 0; i < WEATHER_FORECAST_DAYS; ++i) {
        lv_obj_t *row = lv_obj_create(page);
        lv_obj_set_size(row, SCREEN_WIDTH - 28, 64);
        lv_obj_set_pos(row, 14, 244 + i * 70);
        weather_style_card(row);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        weather_forecast_labels[i] = lv_label_create(row);
        lv_obj_set_width(weather_forecast_labels[i], SCREEN_WIDTH - 52);
        lv_obj_set_style_text_font(weather_forecast_labels[i], ui_chinese_font_16(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(weather_forecast_labels[i], lv_color_hex(UI_COLOR_SKY_TEXT), 0);
        lv_obj_align(weather_forecast_labels[i], LV_ALIGN_LEFT_MID, 4, 0);
    }

    create_back_button(page, LV_ALIGN_TOP_RIGHT, -8, weather_back_cb);
    refresh_weather_page();
}

/* ==================== GYRO FULL SCREEN ==================== */

static void create_gyro_screen(void)
{
    gyro_screen = lv_obj_create(NULL);
    lv_obj_set_size(gyro_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_clear_flag(gyro_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_background(gyro_screen);

    /* 标题栏 */
    lv_obj_t *header = lv_obj_create(gyro_screen);
    lv_obj_set_size(header, SCREEN_WIDTH, 54);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_width(header, 1, 0);
    lv_obj_set_style_pad_all(header, 0, 0);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "ATTITUDE  /  MPU6050");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 66, -7);

    lv_obj_t *subtitle = lv_label_create(header);
    lv_label_set_text(subtitle, "2-AXIS MOTION MONITOR");
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(subtitle, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(subtitle, LV_ALIGN_LEFT_MID, 67, 13);

    lv_obj_t *status_dot = lv_obj_create(header);
    lv_obj_set_size(status_dot, 9, 9);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(UI_COLOR_SUCCESS), 0);
    lv_obj_set_style_border_width(status_dot, 0, 0);
    lv_obj_align(status_dot, LV_ALIGN_RIGHT_MID, -75, -8);
    lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = lv_label_create(header);
    lv_label_set_text(status, "LIVE");
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(UI_COLOR_SUCCESS), 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -32, -8);

    /* 返回按钮 */
    create_back_button(header, LV_ALIGN_TOP_LEFT, 0, back_btn_cb);

    /* Two light instrument cards; the yaw axis is intentionally omitted. */
    lv_obj_t *cards[] = {lv_obj_create(gyro_screen), lv_obj_create(gyro_screen)};
    lv_obj_t **arcs[] = {&gyro_arc_roll, &gyro_arc_pitch};
    lv_obj_t **value_labels[] = {&gyro_roll_label, &gyro_pitch_label};
    const int card_x[] = {16, 252};
    const uint32_t card_colors[] = {UI_COLOR_ERROR, UI_COLOR_LINK};
    const char *card_names[] = {"ROLL", "PITCH"};

    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(cards[i], 212, 263);
        lv_obj_set_pos(cards[i], card_x[i], 68);
        lv_obj_set_style_bg_color(cards[i], lv_color_hex(UI_COLOR_SURFACE), 0);
        lv_obj_set_style_bg_opa(cards[i], LV_OPA_90, 0);
        lv_obj_set_style_border_width(cards[i], 1, 0);
        lv_obj_set_style_border_color(cards[i], lv_color_hex(card_colors[i]), 0);
        lv_obj_set_style_border_opa(cards[i], LV_OPA_60, 0);
        lv_obj_set_style_radius(cards[i], 16, 0);
        lv_obj_set_style_pad_all(cards[i], 0, 0);
        lv_obj_clear_flag(cards[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *name = lv_label_create(cards[i]);
        lv_label_set_text(name, card_names[i]);
        lv_obj_set_style_text_font(name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(card_colors[i]), 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 16, 12);

        *arcs[i] = lv_arc_create(cards[i]);
        lv_obj_set_size(*arcs[i], 172, 172);
        lv_obj_align(*arcs[i], LV_ALIGN_TOP_MID, 0, 38);
        lv_arc_set_range(*arcs[i], -90, 90);
        lv_arc_set_value(*arcs[i], 0);
        lv_arc_set_bg_angles(*arcs[i], 135, 405);
        lv_arc_set_angles(*arcs[i], 135, 135);
        lv_obj_set_style_arc_color(*arcs[i], lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
        lv_obj_set_style_arc_color(*arcs[i], lv_color_hex(card_colors[i]), LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(*arcs[i], 7, LV_PART_MAIN);
        lv_obj_set_style_arc_width(*arcs[i], 9, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(*arcs[i], true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(*arcs[i], true, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(*arcs[i], LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(*arcs[i], 0, 0);
        lv_obj_clear_flag(*arcs[i], LV_OBJ_FLAG_CLICKABLE);

        *value_labels[i] = lv_label_create(cards[i]);
        lv_label_set_text(*value_labels[i], "0.0°");
        lv_obj_set_style_text_font(*value_labels[i], &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(*value_labels[i], lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_align(*value_labels[i], LV_ALIGN_CENTER, 0, 36);

        lv_obj_t *scale = lv_label_create(cards[i]);
        lv_label_set_text(scale, "-90       0       +90");
        lv_obj_set_style_text_font(scale, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(scale, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
        lv_obj_align(scale, LV_ALIGN_BOTTOM_MID, 0, -12);
    }

    /* 温度 */
    gyro_temp_label = lv_label_create(gyro_screen);
    lv_label_set_text(gyro_temp_label, "--.-°C");
    lv_obj_set_style_text_font(gyro_temp_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gyro_temp_label, lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_align(gyro_temp_label, LV_ALIGN_TOP_LEFT, 32, 380);

    /* 数值显示 */
    lv_obj_t *info = lv_label_create(gyro_screen);
    lv_label_set_text(info, "ROLL / PITCH\nFILTER 0.96   |   100 Hz");
    lv_obj_set_style_text_font(info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_style_text_line_space(info, 5, 0);
    lv_obj_align(info, LV_ALIGN_TOP_RIGHT, -28, 378);

    lv_obj_t *telemetry = lv_obj_create(gyro_screen);
    lv_obj_set_size(telemetry, 448, 104);
    lv_obj_set_pos(telemetry, 16, 344);
    lv_obj_set_style_bg_color(telemetry, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_border_width(telemetry, 1, 0);
    lv_obj_set_style_border_color(telemetry, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(telemetry, 14, 0);
    lv_obj_set_style_pad_all(telemetry, 0, 0);
    lv_obj_clear_flag(telemetry, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(telemetry);

    lv_obj_t *temp_caption = lv_label_create(telemetry);
    lv_label_set_text(temp_caption, "SENSOR TEMP");
    lv_obj_set_style_text_font(temp_caption, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(temp_caption, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(temp_caption, LV_ALIGN_TOP_LEFT, 18, 13);
}

/* ==================== PAGE 2: EXTRAS ==================== */

static void setup_extras_page(lv_obj_t * page)
{
    ui_theme_apply_background(page);
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, chinese_font_32 ? "SD 卡" : "SD Card");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_24), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

    sd_status_label = lv_label_create(page);
    lv_label_set_text(sd_status_label, chinese_font_20 ? "状态:正在检查 SD 卡"
                                                      : "Status: Checking SD card");
    lv_obj_set_style_text_font(sd_status_label, ui_chinese_font_16(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(sd_status_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_line_space(sd_status_label, 12, 0);
    lv_obj_set_style_text_align(sd_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sd_status_label, SCREEN_WIDTH - 48);
    lv_obj_align(sd_status_label, LV_ALIGN_CENTER, 0, 5);
}

/* ==================== NAVIGATION ==================== */

void animate_to_page(int page) {
    if (page < 0 || page >= NUM_PAGES || page == current_page) return;

    lv_obj_add_flag(pages[current_page], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pages[page], LV_OBJ_FLAG_HIDDEN);

    current_page = page;
    update_page_dots(page);
}

static void update_page_dots(int active) {
    for (int i = 0; i < NUM_PAGES; i++) {
        lv_obj_set_style_bg_opa(page_dots[i], (i == active) ? LV_OPA_80 : LV_OPA_30, 0);
        lv_obj_set_style_bg_color(page_dots[i], lv_color_hex(active == 0
            ? ((i == active) ? 0xFFFFFF : 0x888888)
            : ((i == active) ? UI_COLOR_ACCENT_DARK : UI_COLOR_TEXT_MUTED)), 0);
    }
    if (fps_label) lv_obj_set_style_text_color(fps_label,
        lv_color_hex(active == 0 ? 0x00FF88 : UI_COLOR_TEXT), 0);
}

/* FPS 更新 */
static void fps_update(lv_timer_t * timer) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu FPS", (unsigned long)gui_fps);
    lv_label_set_text(fps_label, buf);
}

static void create_page_indicators(lv_obj_t * parent) {
    lv_obj_t * dot_cont = lv_obj_create(parent);
    lv_obj_set_size(dot_cont, NUM_PAGES * 24, 20);
    lv_obj_align(dot_cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(dot_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dot_cont, 0, 0);
    lv_obj_set_style_pad_all(dot_cont, 0, 0);
    lv_obj_clear_flag(dot_cont, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < NUM_PAGES; i++) {
        page_dots[i] = lv_obj_create(dot_cont);
        lv_obj_set_size(page_dots[i], 8, 8);
        lv_obj_set_pos(page_dots[i], i * 24 + 16, 6);
        lv_obj_set_style_radius(page_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(page_dots[i], 0, 0);
        lv_obj_set_style_bg_color(page_dots[i], lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_flag(page_dots[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
    update_page_dots(0);
}

/* ==================== GYRO DATA UPDATE ==================== */

/* `stored` is the LVGL static-text buffer: compare a candidate, then copy. */
static void gyro_set_label_if_changed(lv_obj_t *label, char *stored, size_t stored_size,
                                     const char *fmt, float value)
{
    char candidate[32];

    if (!label || !stored || stored_size == 0 || stored_size > sizeof(candidate)) return;

    snprintf(candidate, stored_size, fmt, value);
    if (strcmp(stored, candidate) == 0) return;

    memcpy(stored, candidate, strlen(candidate) + 1);
    lv_label_set_text_static(label, stored);
}

/* Re-entry and the first sample bypass the 1 s gate. Tick diff is wrap-safe. */
static bool gyro_temp_update_due(bool visible, bool *was_visible, bool *has_update, uint32_t *last_tick)
{
    const bool due = visible &&
                     (!*was_visible || !*has_update || lv_tick_elaps(*last_tick) >= 1000U);
    *was_visible = visible;
    return due;
}

static void gyro_ui_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!gyro_pending_valid) return;

    /* LVGL is single-threaded here: only LVGLTask may touch its objects. */
    const float r = gyro_pending_roll;
    const float p = gyro_pending_pitch;
    const float temp = gyro_pending_temp;
    const bool gyro_visible = gyro_screen && lv_obj_is_visible(gyro_screen);

    if (gyro_visible) {
        if (gyro_arc_roll) lv_arc_set_value(gyro_arc_roll, (int16_t)r);
        gyro_set_label_if_changed(gyro_roll_label, gyro_roll_text, sizeof(gyro_roll_text), "%.1f°", r);
        if (gyro_arc_pitch) lv_arc_set_value(gyro_arc_pitch, (int16_t)p);
        gyro_set_label_if_changed(gyro_pitch_label, gyro_pitch_text, sizeof(gyro_pitch_text), "%.1f°", p);
    }
    if (gyro_temp_update_due(gyro_visible, &gyro_ui_was_visible,
                             &gyro_temp_has_update, &gyro_temp_update_tick)) {
        gyro_set_label_if_changed(gyro_temp_label, gyro_temp_text, sizeof(gyro_temp_text), "%.1f°C", temp);
        gyro_temp_has_update = true;
        gyro_temp_update_tick = lv_tick_get();
    }

    const bool home_visible = home_gyro_temp_label && lv_obj_is_visible(home_gyro_temp_label);
    if (gyro_temp_update_due(home_visible, &home_gyro_temp_was_visible,
                             &home_gyro_temp_has_update, &home_gyro_temp_update_tick)) {
        gyro_set_label_if_changed(home_gyro_temp_label, home_gyro_temp_text,
                                 sizeof(home_gyro_temp_text), "%.1f°C", temp);
        home_gyro_temp_has_update = true;
        home_gyro_temp_update_tick = lv_tick_get();
    }
}

void update_gyro_data(float r, float p, float yaw_val, float temp)
{
    /* Called by SensorTask on core 0. Publish numbers only; defer every LVGL
     * call to gyro_ui_update_timer_cb running on LVGLTask/core 1. */
    gyro_pending_roll = r;
    gyro_pending_pitch = p;
    gyro_pending_temp = temp;
    gyro_pending_valid = true;
    (void)yaw_val;
}

void update_sd_status(bool mounted, const char *type, uint64_t size_bytes,
                      uint64_t used_bytes, uint32_t file_count)
{
    if (!sd_status_label) return;

    char text[192];
    if (!mounted) {
        if (chinese_font_20) {
            snprintf(text, sizeof(text), "状态:未挂载\n\n请插入 SD 卡并重启");
        } else {
            snprintf(text, sizeof(text), "Status: Not mounted\n\nInsert card and restart");
        }
        lv_obj_set_style_text_color(sd_status_label, lv_color_hex(UI_COLOR_ERROR), 0);
    } else {
        const unsigned long long total_mb = size_bytes / (1024ULL * 1024ULL);
        const unsigned long long used_mb = used_bytes / (1024ULL * 1024ULL);
        if (chinese_font_20) {
            snprintf(text, sizeof(text),
                     "状态:就绪\n类型:%s\n容量:%llu MB\n已用:%llu MB\n根目录文件:%lu",
                     type ? type : "Unknown", total_mb, used_mb,
                     (unsigned long)file_count);
        } else {
            snprintf(text, sizeof(text),
                     "Status: Ready\nType: %s\nCapacity: %llu MB\nUsed: %llu MB\nRoot files: %lu",
                     type ? type : "Unknown", total_mb, used_mb,
                     (unsigned long)file_count);
        }
        lv_obj_set_style_text_color(sd_status_label, lv_color_hex(UI_COLOR_TEXT), 0);
    }
    lv_label_set_text(sd_status_label, text);
}

/* ==================== VINYL MUSIC UI ==================== */

static lv_obj_t *music_make_circle(lv_obj_t *parent, int diameter,
                                   uint32_t fill, uint32_t border,
                                   int border_width)
{
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, diameter, diameter);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(circle, fill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(circle, border_width, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return circle;
}

static void music_add_record_groove(int diameter, uint32_t color)
{
    lv_obj_t *groove = music_make_circle(music_screen, diameter, 0, color, 1);
    lv_obj_set_pos(groove, (SCREEN_WIDTH - diameter) / 2,
                   203 - diameter / 2);
}

static lv_obj_t *music_make_control(const char *text, int x, int width,
                                    lv_event_cb_t callback)
{
    lv_obj_t *button = lv_btn_create(music_screen);
    lv_obj_set_size(button, width, 42);
    lv_obj_set_pos(button, x, 432);
    lv_obj_set_style_radius(button, 12, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x24252B), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3A3B43), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x55565E), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(button);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF4F4F4), 0);
    lv_obj_center(label);
    return button;
}

static void music_update_track_label(void)
{
    if (!music_track_label) return;
    const char *name = get_music_track_name((uint8_t)music_track_index);
    const char *text = name && name[0] ? name : "无音乐";
    if (strcmp(lv_label_get_text(music_track_label), text) != 0) {
        lv_label_set_text(music_track_label, text);
    }
}

static void music_update_tonearm(void)
{
    if (!music_tonearm || !music_tonearm_shadow || !music_tonearm_head) return;
    int progress = music_needle_progress;
    if (progress < 0) progress = 0;
    if (progress > 1000) progress = 1000;

    const lv_coord_t elbow_x = (lv_coord_t)(380 - (23 * progress) / 1000);
    const lv_coord_t elbow_y = (lv_coord_t)(115 + (18 * progress) / 1000);
    const lv_coord_t tip_x = (lv_coord_t)(360 - (66 * progress) / 1000);
    const lv_coord_t tip_y = (lv_coord_t)(130 + (110 * progress) / 1000);

    music_tonearm_points[0] = (lv_point_t){365, 89};
    music_tonearm_points[1] = (lv_point_t){elbow_x, elbow_y};
    music_tonearm_points[2] = (lv_point_t){tip_x, tip_y};
    memcpy(music_tonearm_shadow_points, music_tonearm_points,
           sizeof(music_tonearm_points));
    lv_line_set_points(music_tonearm_shadow, music_tonearm_shadow_points, 3);
    lv_line_set_points(music_tonearm, music_tonearm_points, 3);
    lv_obj_set_pos(music_tonearm_head, tip_x - 10, tip_y - 7);
}

static void music_animation_tick_cb(lv_timer_t *timer)
{
    const uint8_t backend_track = get_music_track_index();
    if (backend_track < get_music_track_count() &&
        (int)backend_track != music_track_index) {
        music_track_index = backend_track;
        music_update_track_label();
    }

    if (music_playing) {
        music_rotation_angle = (music_rotation_angle + 60) % 3600;
        if (music_needle_progress < 1000) {
            music_needle_progress += 36;
            if (music_needle_progress > 1000) music_needle_progress = 1000;
        }
    } else if (music_needle_progress > 0) {
        music_needle_progress -= 36;
        if (music_needle_progress < 0) music_needle_progress = 0;
    }

    music_update_tonearm();
    if (music_lyrics_label) {
        const char *lyric = get_music_lyric(get_music_elapsed_ms());
        if (strcmp(lv_label_get_text(music_lyrics_label), lyric) != 0) {
            lv_label_set_text(music_lyrics_label, lyric);
        }
    }
    if (!music_playing && music_needle_progress == 0) {
        lv_timer_del(timer);
        if (music_animation_timer == timer) music_animation_timer = NULL;
    }
}

static void music_start_animation(void)
{
    if (!music_animation_timer) {
        music_animation_timer = lv_timer_create(music_animation_tick_cb, 30, NULL);
    }
}

static void music_stop(void)
{
    music_playing = false;
    music_audio_stop();
    if (music_animation_timer) {
        lv_timer_del(music_animation_timer);
        music_animation_timer = NULL;
    }
    music_needle_progress = 0;
    if (music_play_label) lv_label_set_text(music_play_label, ">");
    music_update_tonearm();
}

static void music_play_pause_cb(lv_event_t *e)
{
    (void)e;
    music_playing = !music_playing;
    if (music_playing) music_audio_play();
    else music_audio_pause();
    if (music_play_label) {
        lv_label_set_text(music_play_label, music_playing ? "||" : ">");
    }
    if (music_playing || music_needle_progress > 0) music_start_animation();
}

static const char *music_play_mode_text(uint8_t mode)
{
    switch (mode) {
    case 0: return "单曲循环";
    case 2: return "顺序播放";
    default: return "循环歌单";
    }
}

static void music_update_mode_label(void)
{
    if (music_mode_label) {
        lv_label_set_text(music_mode_label,
                          music_play_mode_text(get_music_play_mode()));
    }
}

static void music_mode_cb(lv_event_t *e)
{
    (void)e;
    const uint8_t next_mode = (get_music_play_mode() + 1) % 3;
    set_music_play_mode(next_mode);
    music_update_mode_label();
}

static void music_previous_cb(lv_event_t *e)
{
    (void)e;
    const uint8_t count = get_music_track_count();
    if (count == 0) return;
    music_track_index = music_track_index == 0 ? count - 1 : music_track_index - 1;
    music_select_track((uint8_t)music_track_index);
    music_audio_play();
    music_playing = true;
    if (music_play_label) lv_label_set_text(music_play_label, "||");
    music_update_track_label();
    music_start_animation();
}

static void music_next_cb(lv_event_t *e)
{
    (void)e;
    const uint8_t count = get_music_track_count();
    if (count == 0) return;
    music_track_index = (music_track_index + 1) % count;
    music_select_track((uint8_t)music_track_index);
    music_audio_play();
    music_playing = true;
    if (music_play_label) lv_label_set_text(music_play_label, "||");
    music_update_track_label();
    music_start_animation();
}

static void music_back_cb(lv_event_t *e)
{
    (void)e;
    /* Music is a background service. Leaving the app must not stop the
     * current track, otherwise opening Settings to change volume loses the
     * playback session. The play/pause control remains the explicit stop. */
    lv_scr_load(main_screen_ref);
}

static void music_app_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!music_playing || !music_audio_is_playing()) {
        music_playing = true;
        music_audio_play();
    }
    if (music_play_label) lv_label_set_text(music_play_label, "||");
    music_update_track_label();
    lv_scr_load(music_screen);
    music_start_animation();
}

static void create_music_screen(void)
{
    music_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(music_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(music_screen, lv_color_hex(0x777574), 0);
    lv_obj_set_style_bg_opa(music_screen, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(music_screen);
    lv_label_set_text(title, "音乐");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
    create_legacy_back_button(music_screen, LV_ALIGN_TOP_RIGHT, -8, music_back_cb);

    lv_obj_t *deck = lv_obj_create(music_screen);
    lv_obj_set_size(deck, 300, 300);
    lv_obj_set_pos(deck, 90, 53);
    lv_obj_set_style_radius(deck, 38, 0);
    lv_obj_set_style_bg_color(deck, lv_color_hex(0xF2F1EF), 0);
    lv_obj_set_style_bg_opa(deck, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(deck, 2, 0);
    lv_obj_set_style_border_color(deck, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(deck, 14, 0);
    lv_obj_set_style_shadow_color(deck, lv_color_hex(0x393837), 0);
    lv_obj_set_style_shadow_opa(deck, LV_OPA_30, 0);
    lv_obj_set_style_pad_all(deck, 0, 0);
    lv_obj_clear_flag(deck, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *platter = music_make_circle(music_screen, 266, 0xD3D2D0,
                                          0xBCBBB9, 2);
    lv_obj_set_pos(platter, 107, 70);
    lv_obj_t *vinyl = music_make_circle(music_screen, 238, 0x101116,
                                         0x282930, 4);
    lv_obj_set_pos(vinyl, 121, 84);
    music_add_record_groove(222, 0x25262D);
    music_add_record_groove(202, 0x1D1E25);
    music_add_record_groove(182, 0x272830);
    music_add_record_groove(162, 0x1B1C22);
    music_add_record_groove(142, 0x26272E);

    music_record_label = music_make_circle(music_screen, 96, 0xF34463,
                                            0xC73553, 2);
    lv_obj_set_pos(music_record_label, 192, 155);
    lv_obj_set_style_transform_angle(music_record_label, 0, 0);
    music_record_title = lv_label_create(music_screen);
    lv_label_set_text(music_record_title, "MUSIC");
    lv_obj_set_style_text_font(music_record_title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(music_record_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(music_record_title, 96);
    lv_obj_set_style_text_align(music_record_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(music_record_title, 192, 168);
    music_track_label = lv_label_create(music_screen);
    lv_obj_set_style_text_font(music_track_label,
                               ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(music_track_label, lv_color_hex(0xFFE6EA), 0);
    lv_obj_set_width(music_track_label, 96);
    lv_obj_set_style_text_align(music_track_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(music_track_label, 192, 198);
    lv_obj_move_foreground(music_record_label);
    lv_obj_move_foreground(music_record_title);
    lv_obj_move_foreground(music_track_label);
    music_update_track_label();

    music_tonearm_shadow = lv_line_create(music_screen);
    lv_obj_set_style_line_color(music_tonearm_shadow, lv_color_hex(0x777777), 0);
    lv_obj_set_style_line_opa(music_tonearm_shadow, LV_OPA_50, 0);
    lv_obj_set_style_line_width(music_tonearm_shadow, 12, 0);
    lv_obj_set_style_line_rounded(music_tonearm_shadow, true, 0);
    lv_obj_clear_flag(music_tonearm_shadow, LV_OBJ_FLAG_CLICKABLE);
    music_tonearm = lv_line_create(music_screen);
    lv_obj_set_style_line_color(music_tonearm, lv_color_hex(0xF7F7F5), 0);
    lv_obj_set_style_line_width(music_tonearm, 7, 0);
    lv_obj_set_style_line_rounded(music_tonearm, true, 0);
    lv_obj_clear_flag(music_tonearm, LV_OBJ_FLAG_CLICKABLE);

    music_tonearm_head = lv_obj_create(music_screen);
    lv_obj_set_size(music_tonearm_head, 22, 15);
    lv_obj_set_style_radius(music_tonearm_head, 4, 0);
    lv_obj_set_style_bg_color(music_tonearm_head, lv_color_hex(0xF2F2F0), 0);
    lv_obj_set_style_border_color(music_tonearm_head, lv_color_hex(0xB9B9B7), 0);
    lv_obj_set_style_border_width(music_tonearm_head, 1, 0);
    lv_obj_clear_flag(music_tonearm_head, LV_OBJ_FLAG_SCROLLABLE |
                                          LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *stylus_tip = music_make_circle(music_tonearm_head, 4, 0x353535,
                                              0x353535, 0);
    lv_obj_align(stylus_tip, LV_ALIGN_BOTTOM_LEFT, 3, 0);
    music_update_tonearm();

    music_lyrics_label = lv_label_create(music_screen);
    lv_obj_set_size(music_lyrics_label, 460, 52);
    /* Use the 20px Chinese font with wrapping instead of ellipsis. Long
     * lyric lines must remain readable rather than being replaced by dots. */
    lv_label_set_long_mode(music_lyrics_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(music_lyrics_label, "请将 qingtian.lrc 放入 SD 卡 /music 文件夹");
    lv_obj_set_style_text_font(music_lyrics_label,
                               ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(music_lyrics_label, lv_color_hex(0xF1F1F1), 0);
    lv_obj_set_style_text_align(music_lyrics_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(music_lyrics_label, LV_ALIGN_TOP_MID, 0, 365);

    music_make_control("PREV", 119, 82, music_previous_cb);
    lv_obj_t *play_btn = music_make_control("||", 209, 62,
                                             music_play_pause_cb);
    music_play_label = lv_obj_get_child(play_btn, 0);
    music_make_control("NEXT", 279, 82, music_next_cb);
    lv_obj_t *mode_btn = music_make_control("循环歌单", 368, 104,
                                             music_mode_cb);
    music_mode_label = lv_obj_get_child(mode_btn, 0);
    lv_obj_set_style_text_font(music_mode_label,
                               ui_chinese_font_20(&lv_font_montserrat_16), 0);
    music_update_mode_label();
}

/* ==================== MAIN INIT (FIXED) ==================== */

void custom_init(lv_ui *ui)
{
    /* FIX: 使用 ui->screen 而非 lv_scr_act()，
     * 确保控件创建在 GuiGuider 生成的屏幕上 */
    lv_obj_t * screen = ui->screen;
    lv_scr_load(screen);
    // 屏幕本身也不允许滚动
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM | LV_OBJ_FLAG_SCROLL_CHAIN);

    /* 三页直接放 screen 上，切换 = 显/隐 */
    for (int i = 0; i < NUM_PAGES; i++) {
        pages[i] = create_page(screen, i);
    }

    /* 手绘图标 */
    create_gyro_icon();
    create_home_temp_icon();
    create_gear_icon();
    create_wifi_icon();
    create_volume_icon();
    create_numpad_icon();
    create_game_icon();

    /* 保存主屏幕引用（陀螺仪界面返回用） */
    main_screen_ref = screen;

    setup_home_page(pages[0]);
    setup_icon_page(pages[1]);
    setup_extras_page(pages[2]);
    create_page_indicators(screen);

    /* 创建独立屏幕 */
    create_gyro_screen();
    create_game_screen();
    create_balance_game_screen();
    create_keyboard_screen();
    create_settings_screen();
    create_wifi_list_screen();
    create_wifi_password_screen();
    create_brightness_screen();
    create_volume_screen();
    create_wallpaper_screen();
    create_clock_screen();
    create_calendar_screen();
    create_music_screen();
    create_photo_screen();
    create_photo_view_screen();
    create_video_screen();
    create_video_view_screen();

    /* FPS 显示（右下角） */
    fps_label = lv_label_create(screen);
    lv_label_set_text(fps_label, "0 FPS");
    lv_obj_set_pos(fps_label, SCREEN_WIDTH - 60, SCREEN_HEIGHT - 25);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF88), 0);

    /* FIX: 用 millis() 驱动时钟，不用 time()/localtime() */
    lv_timer_create(time_update, 1000, NULL);
    lv_timer_create(gyro_ui_update_timer_cb, 100, NULL);
    lv_timer_create(balance_game_update_timer_cb, 30, NULL);
    lv_timer_create(fps_update, 1000, NULL);
}

/* ==================== NTP TIME UPDATE ==================== */

static const char * get_weekday_name(int wday)
{
    static const char *names[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    return (wday >= 0 && wday <= 6) ? names[wday] : "";
}

void set_ntp_synced(void) {
    ntp_synced = true;
}

static void time_update(lv_timer_t * timer)
{
    char buf[64];
    time_t now = time(NULL);

    if (now > 1000000000) {
        // NTP 已同步，使用真实时间
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);

        update_digital_time(tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

        snprintf(buf, sizeof(buf), "%s   %d-%02d-%02d",
                 get_weekday_name(tm_buf.tm_wday), tm_buf.tm_year + 1900,
                 tm_buf.tm_mon + 1, tm_buf.tm_mday);
        if (date_label) lv_label_set_text(date_label, buf);
        clock_reminder_check(&tm_buf);
    } else {
        // NTP 未同步，显示等待
        update_digital_time(-1, -1, -1);
        if (date_label) lv_label_set_text(date_label, "---   ---- -- --");
    }
}

static void clock_reminder_check(const struct tm *tm_buf)
{
    if (!tm_buf || !clock_reminder_label) return;

    int alarm_hour = 0, alarm_minute = 0;
    bool enabled = false;
    get_clock_reminder(&alarm_hour, &alarm_minute, &enabled);
    if (!enabled || tm_buf->tm_hour != alarm_hour || tm_buf->tm_min != alarm_minute) return;

    const int day_key = tm_buf->tm_year * 400 + tm_buf->tm_yday;
    if (day_key == clock_last_reminder_day && alarm_minute == clock_last_reminder_minute) return;

    clock_last_reminder_day = day_key;
    clock_last_reminder_minute = alarm_minute;
    play_alarm_wav();
    lv_obj_clear_flag(clock_reminder_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(clock_reminder_label);

    lv_timer_t *hide_timer = lv_timer_create(clock_reminder_timeout, 8000, NULL);
    lv_timer_set_repeat_count(hide_timer, 1);
}
