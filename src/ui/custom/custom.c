#include "UiTransition.h"
/*
 * Custom UI — fixed for ESP32-S3 pure LVGL
 * GUI Guider integration: swipe pages + weather + gyro
 */

#include "IdfRuntime.h"
#include "esp_attr.h"
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
#include "QuickPanel.h"
#include "setwifi.h"
#include "EdgeBack.h"
#include "AppLauncher.h"
#include "GameHub.h"
#include "WordMatchUI.h"
#include "AppIcons.h"
#include "FlipClock.h"
#include "Sound.h"
#include "MusicPlaylist.h"
#include "SystemUpdateUI.h"
#include "MediaActions.h"
#include "OnlineMusic.h"
#include "board_backlight.h"
#include "HotspotIcon.h"
#include "ScreenIdleUI.h"
#include "AiVoiceUI.h"
#include "HomeInfo.h"
#include "WeatherIcons.h"
#include "WeatherPageUI.h"
#include "CalendarUI.h"
#include "AlarmUI.h"

// C 兼容声明
extern char WEATHER_CITY[32];
/* WEATHER_CITY_DISPLAY is declared by setwifi.h. */
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
void ai_chat_stop_speech(void);
bool ai_chat_speech_status(char *text, size_t capacity);
bool ai_chat_is_busy(void);
bool ai_audio_is_playing(void);
bool ai_websocket_is_connected(void);
const char *ai_service_status(void);
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
int scan_photo_files(void);
int get_photo_count(void);
const char *get_photo_path(int index);
const char *get_photo_name(int index);
bool start_photo_load(const char *path);
int get_photo_load_status(void);
const lv_img_dsc_t *get_loaded_photo_image(void);
const char *get_loaded_photo_path(void);
const char *get_photo_load_error(void);
void release_loaded_photo(void);
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
#define AI_CANDIDATE_HEIGHT 36
#define AI_KEYBOARD_HEIGHT 192
#define NUM_PAGES       3
#define SWIPE_THRESHOLD 50
#define WEATHER_FORECAST_DAYS 3

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void time_update(lv_timer_t * timer);
static void gyro_ui_update_timer_cb(lv_timer_t *timer);
void animate_to_page(int page);
static void update_page_dots(int page);
// 屏幕/按钮回调
static void gyro_btn_cb(lv_event_t *e);
static void settings_btn_cb(lv_event_t *e);
static void back_btn_cb(lv_event_t *e);
static void game_restart_cb(lv_event_t *e);
static void game_btn_cb(lv_event_t *e);
static void games_app_btn_cb(lv_event_t *e);
static void game_back_cb(lv_event_t *e);
static void balance_screen_event(lv_event_t *e);
static void game_restart(void);
static void balance_game_btn_cb(lv_event_t *e);
static void balance_game_reset(bool reset_score);
static void create_balance_game_screen(void);
static void balance_game_update_timer_cb(lv_timer_t *timer);
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
static void calendar_app_btn_cb(lv_event_t *e);
static void ai_assistant_app_btn_cb(lv_event_t *e);
static void browser_app_btn_cb(lv_event_t *e);
static void ai_chat_send_cb(lv_event_t *e);
static void ai_chat_stop_cb(lv_event_t *e);
static void ai_chat_poll_cb(lv_timer_t *timer);
static void ai_chat_show_keyboard(void);
static void ai_chat_hide_keyboard(void);
static void ai_chat_layout(bool keyboard_visible);
static void ai_chat_keyboard_cb(lv_event_t *e);
static void text_input_event_cb(lv_event_t *e);
static void text_keyboard_event_cb(lv_event_t *e);
static void show_text_keyboard(lv_obj_t *keyboard, lv_obj_t *textarea);
static void hide_text_keyboard(lv_obj_t *keyboard);
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
static flip_clock_t *home_flip_clock;
static bool weather_page_active;
static lv_obj_t * weather_icon_img;
static lv_obj_t * weather_text;
static lv_obj_t * weather_temp_label;
static lv_obj_t * wallpaper_img;
static lv_obj_t * wallpaper_screen;
static lv_obj_t *wallpaper_preview_objs[3];
static lv_obj_t *wallpaper_preview_labels[3];
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

static void update_digital_time(int hour, int minute, int second)
{
    if (home_flip_clock) flip_clock_set_time(home_flip_clock, hour, minute, second, true);
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
static lv_obj_t *ai_chat_stop_button;
static lv_obj_t *ai_chat_keyboard;
static lv_obj_t *ai_chat_ime;
static lv_timer_t *ai_chat_poll_timer;
static EXT_RAM_BSS_ATTR char ai_chat_last_reply[8193];
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

static const ai_chat_phrase_t ai_chat_phrases[] __attribute__((unused)) = {
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
static lv_obj_t *kb_ime, *kb_candidates, *kb_hint, *kb_return_screen;
static bool kb_submitted;
static lv_obj_t *city_btn_label;

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
static lv_obj_t *wifi_enabled_switch;
static bool wifi_ui_last_enabled;
#define WIFI_UI_NETWORK_LIMIT 20
typedef struct {
    char ssid[33];
    bool open;
} wifi_ui_network_t;
static wifi_ui_network_t wifi_ui_networks[WIFI_UI_NETWORK_LIMIT];
static uint32_t wifi_ui_last_revision;
static bool wifi_ui_last_scan_busy;
static bool wifi_ui_last_connect_busy;
static bool wifi_ui_last_connected;
static char wifi_ui_last_connected_ssid[33];
static char wifi_ui_last_target[33];
static bool wifi_password_submit_pending;
static lv_obj_t *wifi_refresh_label;
static void wifi_update_connection_info(void);

static lv_obj_t *brightness_screen;
static lv_obj_t *brightness_label;
static lv_obj_t *brightness_slider;
static lv_obj_t *volume_screen;
static lv_obj_t *volume_label;
static lv_obj_t *volume_slider;

#define WIFI_ICON_SIZE 56
// Canvas pixels are read by the software renderer, never by an ISR or DMA.
// Keep these persistent images in PSRAM, leaving internal RAM for WiFi/tasks.
static EXT_RAM_BSS_ATTR LV_ATTRIBUTE_MEM_ALIGN uint8_t wifi_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(WIFI_ICON_SIZE, WIFI_ICON_SIZE)];
static lv_img_dsc_t wifi_img;
#define VOLUME_ICON_SIZE 56
static EXT_RAM_BSS_ATTR LV_ATTRIBUTE_MEM_ALIGN uint8_t volume_icon_buf[
    LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(VOLUME_ICON_SIZE, VOLUME_ICON_SIZE)];
static lv_img_dsc_t volume_img;

static lv_obj_t *create_back_button(lv_obj_t *parent, lv_align_t align,
                                    lv_coord_t x_offset, lv_event_cb_t callback)
{
    (void)align; (void)x_offset;
    lv_obj_t *screen = lv_obj_get_screen(parent);
    /* The desktop's weather subpage is dispatched by desktop_back_cb. */
    if (screen != main_screen_ref) edge_back_bind(screen, callback, NULL);
    return NULL;
}

static lv_obj_t *create_legacy_back_button(lv_obj_t *parent, lv_align_t align,
                                    lv_coord_t x_offset, lv_event_cb_t callback)
{
    return create_back_button(parent, align, x_offset, callback);
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
static EXT_RAM_BSS_ATTR LV_ATTRIBUTE_MEM_ALIGN uint8_t gyro_icon_buf[
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
static lv_timer_t *balance_game_timer;

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
    static char buf[32];
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
    int compact[4], n = 0, merged[4] = {0}, pos = 0;
    for (int i = 0; i < 4; ++i) if (line[i]) compact[n++] = line[i];
    for (int i = 0; i < n; ++i) {
        int value = compact[i];
        if (i + 1 < n && compact[i + 1] == value) {value *= 2; game_score += value; ++i;}
        merged[pos++] = value;
    }
    bool moved = memcmp(line, merged, sizeof(merged)) != 0;
    memcpy(line, merged, sizeof(merged));
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
    if (balance_game_timer) lv_timer_pause(balance_game_timer);
    ui_screen_back(games_ui_get_screen() ? games_ui_get_screen() : main_screen_ref);
}

static void balance_game_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!balance_screen) create_balance_game_screen();
    balance_game_active = true;
    balance_game_reset(true);
    ui_screen_open(balance_screen);
}

static void balance_game_update_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!balance_game_active || lv_scr_act() != balance_screen || !balance_ball || !balance_hole) return;

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

static void balance_screen_event(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_DELETE) {
        if (balance_game_timer) {lv_timer_del(balance_game_timer); balance_game_timer = NULL;}
        balance_game_active = false;
        balance_screen = balance_table = balance_ball = balance_hole = NULL;
        balance_score_label = balance_angle_label = NULL;
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED) {
        balance_game_active = false;
        if (balance_game_timer) lv_timer_pause(balance_game_timer);
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        balance_game_active = true;
        if (balance_game_timer) {lv_timer_reset(balance_game_timer); lv_timer_resume(balance_game_timer);}
    }
}

static void create_balance_game_screen(void)
{
    balance_screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(balance_screen, balance_screen_event, LV_EVENT_ALL, NULL);
    balance_game_timer = lv_timer_create(balance_game_update_timer_cb, 30, NULL);
    lv_timer_pause(balance_game_timer);
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
    if (!indev) return;
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

static void game_deleted(lv_event_t *e)
{
    if (lv_event_get_target(e) != game_screen) return;
    game_screen = game_score_label = NULL;
    memset(game_tiles, 0, sizeof(game_tiles));
    memset(game_tile_boxes, 0, sizeof(game_tile_boxes));
}

static void create_game_screen(void)
{
    game_screen = lv_obj_create(NULL);
    lv_obj_add_event_cb(game_screen, game_deleted, LV_EVENT_DELETE, NULL);
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
    create_legacy_back_button(header, LV_ALIGN_RIGHT_MID, -8, game_back_cb);

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
static EXT_RAM_BSS_ATTR LV_ATTRIBUTE_MEM_ALIGN lv_color_t gear_buf[GEAR_SIZE * GEAR_SIZE];
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


static void wifi_open_password(const char *ssid)
{
    if (!ssid || !ssid[0]) return;
    snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s", ssid);
    wifi_password_submit_pending = false;
    if (wifi_password_ssid_label) {
        char network_text[64];
        snprintf(network_text, sizeof(network_text), "SSID: %s", wifi_selected_ssid);
        lv_label_set_text(wifi_password_ssid_label, network_text);
    }
    lv_textarea_set_text(wifi_pwd_textarea, "");
    ui_screen_open(wifi_pwd_screen);
}

static bool wifi_request_selected(bool saved, const char *password)
{
    const bool accepted = saved ? do_wifi_connect_saved(wifi_selected_ssid) :
                                  do_wifi_connect(wifi_selected_ssid, password ? password : "");
    if (!accepted) {
        lv_obj_t *message = lv_msgbox_create(NULL,
            chinese_font_20 ? "WiFi" : "WiFi",
            chinese_font_20 ? "连接请求未提交，请稍后重试" :
                              "Connection request not accepted. Please try again.",
            NULL, true);
        lv_obj_set_style_text_font(message, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        lv_obj_center(message);
        return false;
    }
    wifi_password_submit_pending = true;
    if (wifi_pwd_keyboard) hide_text_keyboard(wifi_pwd_keyboard);
    if (lv_scr_act() == wifi_pwd_screen) ui_screen_back(wifi_list_screen);
    wifi_update_connection_info();
    return true;
}

static void wifi_list_long_cb(lv_event_t *event)
{
    const wifi_ui_network_t *network = lv_event_get_user_data(event);
    if (network) wifi_open_password(network->ssid);
}

static void wifi_list_btn_cb(lv_event_t *event)
{
    const wifi_ui_network_t *network = lv_event_get_user_data(event);
    if (!network || !network->ssid[0]) return;
    if (get_wifi_connected() && !wifi_connection_is_busy() &&
        strcmp(network->ssid, get_wifi_connected_ssid()) == 0) return;
    snprintf(wifi_selected_ssid, sizeof(wifi_selected_ssid), "%s", network->ssid);
    if (wifi_network_is_saved(network->ssid)) {
        wifi_request_selected(true, NULL);
    } else if (network->open) {
        wifi_request_selected(false, "");
    } else {
        wifi_open_password(network->ssid);
    }
}

static void wifi_update_connection_info(void)
{
    if (!wifi_connection_status_label || !wifi_connection_ssid_label) return;
    quick_radio_state_t radio = {0};
    quick_radio_get(QUICK_RADIO_WIFI, &radio);
    if (wifi_enabled_switch && !radio.busy) {
        if (radio.enabled) lv_obj_add_state(wifi_enabled_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(wifi_enabled_switch, LV_STATE_CHECKED);
    }
    const bool connected = get_wifi_connected();
    const bool connecting = wifi_connection_is_busy();
    const bool failed = wifi_last_connection_failed();
    const char *state = radio.busy ? (chinese_font_20 ? "状态: 切换中" : "Status: Changing") :
        !radio.enabled ? (chinese_font_20 ? "状态: 已关闭" : "Status: Off") :
        connecting ? (chinese_font_20 ? "状态: 正在连接" : "Status: Connecting") :
        connected ? (chinese_font_20 ? "状态: 已连接" : "Status: Connected") :
        failed ? (chinese_font_20 ? "状态: 连接失败" : "Status: Connection failed") :
                 (chinese_font_20 ? "状态: 未连接" : "Status: Not connected");
    lv_label_set_text(wifi_connection_status_label, state);
    char network_text[80];
    const char *ssid = connecting ? wifi_connection_target() : get_wifi_connected_ssid();
    if ((connecting || connected) && ssid && ssid[0]) {
        snprintf(network_text, sizeof(network_text), "%s%s",
                 connecting ? (chinese_font_20 ? "连接: " : "Joining: ") : "WiFi: ", ssid);
    } else if (failed && radio.enabled) {
        snprintf(network_text, sizeof(network_text), "%s",
                 chinese_font_20 ? "长按网络可重新输入密码" : "Hold a network to change its password");
    } else {
        snprintf(network_text, sizeof(network_text), "WiFi: --");
    }
    lv_label_set_text(wifi_connection_ssid_label, network_text);
    lv_obj_set_style_text_color(wifi_connection_status_label,
        lv_color_hex(connecting || radio.busy ? UI_COLOR_ACCENT_DARK :
                     connected ? UI_COLOR_SUCCESS : UI_COLOR_ERROR), 0);
    if (wifi_refresh_label) lv_label_set_text(wifi_refresh_label,
        wifi_scan_is_busy() ? (chinese_font_20 ? "扫描中..." : "Scanning...") :
                              (chinese_font_20 ? "刷新" : "Refresh"));
}

static void wifi_render_scan_results(void)
{
    if (!wifi_list) return;
    wifi_update_connection_info();
    lv_obj_clean(wifi_list);
    quick_radio_state_t radio = {0};
    quick_radio_get(QUICK_RADIO_WIFI, &radio);
    if (!radio.enabled) {
        lv_obj_t *hint = lv_list_add_text(wifi_list,
            chinese_font_20 ? "开启 WiFi 后搜索网络" : "Turn on WiFi to find networks");
        lv_obj_set_style_text_font(hint, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        return;
    }
    const int count = get_wifi_scan_count();
    if (count <= 0) {
        lv_obj_t *empty = lv_list_add_text(wifi_list, wifi_scan_is_busy() ?
            (chinese_font_20 ? "正在搜索网络..." : "Searching for networks...") :
            (chinese_font_20 ? "没有发现 WiFi" : "No WiFi networks found"));
        lv_obj_set_style_text_font(empty, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        return;
    }
    char connected[33], target[33];
    snprintf(connected, sizeof(connected), "%s", get_wifi_connected_ssid());
    snprintf(target, sizeof(target), "%s", wifi_connection_target());
    const bool connecting = wifi_connection_is_busy();
    const bool online = get_wifi_connected();
    for (int i = 0; i < count && i < WIFI_UI_NETWORK_LIMIT; ++i) {
        wifi_ui_network_t *network = &wifi_ui_networks[i];
        snprintf(network->ssid, sizeof(network->ssid), "%s", get_wifi_ssid(i));
        network->open = wifi_scan_network_is_open(i);
        const bool selected = connecting && !strcmp(network->ssid, target);
        const bool current = online && !strcmp(network->ssid, connected);
        const bool saved = wifi_network_is_saved(network->ssid);
        lv_obj_t *button = lv_list_add_btn(wifi_list, NULL, network->ssid);
        lv_obj_set_height(button, 54);
        lv_obj_set_style_text_font(button, ui_chinese_font_20(&lv_font_montserrat_20), 0);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *name = lv_obj_get_child(button, 0);
        lv_obj_set_width(name, 250);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_t *badge = lv_label_create(button);
        lv_obj_set_width(badge, 72);
        lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(badge, ui_chinese_font_16(&lv_font_montserrat_16), 0);
        lv_label_set_text(badge, selected ? (chinese_font_20 ? "连接中" : "Joining") :
            current ? (chinese_font_20 ? "已连接" : "Online") :
            saved ? (chinese_font_20 ? "已保存" : "Saved") :
            network->open ? (chinese_font_20 ? "开放" : "Open") : "");
        lv_obj_set_style_text_color(badge,
            lv_color_hex(current ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT_DARK), 0);
        lv_obj_add_event_cb(button, wifi_list_btn_cb, LV_EVENT_SHORT_CLICKED, network);
        lv_obj_add_event_cb(button, wifi_list_long_cb, LV_EVENT_LONG_PRESSED, network);
    }
}

static void wifi_refresh_cb(lv_event_t *event)
{
    (void)event;
    quick_radio_state_t radio = {0};
    quick_radio_get(QUICK_RADIO_WIFI, &radio);
    if (radio.enabled && !radio.busy) do_wifi_scan();
    wifi_render_scan_results();
}

static void wifi_btn_cb(lv_event_t *event)
{
    (void)event;
    quick_radio_state_t radio = {0};
    quick_radio_get(QUICK_RADIO_WIFI, &radio);
    wifi_ui_last_enabled = radio.enabled;
    if (radio.enabled && !radio.busy) do_wifi_scan();
    wifi_render_scan_results();
    ui_screen_open(wifi_list_screen);
}

static void wifi_pwd_confirm_cb(lv_event_t *event)
{
    (void)event;
    if (wifi_password_submit_pending || lv_scr_act() != wifi_pwd_screen) return;
    wifi_request_selected(false, lv_textarea_get_text(wifi_pwd_textarea));
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

static void wireless_settings_cb(lv_event_t *event)
{
    quick_panel_open_radio_settings((quick_radio_t)(intptr_t)lv_event_get_user_data(event));
}

/* Compact settings rows use the same surface and spacing as device info. */
static void settings_card_draw_sun(lv_event_t *event)
{
    lv_obj_t *badge = lv_event_get_target(event);
    lv_draw_ctx_t *draw = lv_event_get_draw_ctx(event);
    lv_area_t area;
    lv_obj_get_coords(badge, &area);
    const lv_point_t center = {(lv_coord_t)(area.x1 + 18), (lv_coord_t)(area.y1 + 18)};
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.color = lv_color_hex(UI_COLOR_ACCENT_DARK);
    arc.width = 2;
    lv_draw_arc(draw, &arc, &center, 6, 0, 360);

    static const int8_t rays[8][4] = {
        {0,-12,0,-10}, {7,-7,9,-9}, {10,0,12,0}, {7,7,9,9},
        {0,10,0,12}, {-7,7,-9,9}, {-12,0,-10,0}, {-9,-9,-7,-7}
    };
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(UI_COLOR_ACCENT_DARK);
    line.width = 2;
    line.round_start = line.round_end = 1;
    for (unsigned i = 0; i < 8; ++i) {
        const lv_point_t from = {(lv_coord_t)(center.x + rays[i][0]), (lv_coord_t)(center.y + rays[i][1])};
        const lv_point_t to = {(lv_coord_t)(center.x + rays[i][2]), (lv_coord_t)(center.y + rays[i][3])};
        lv_draw_line(draw, &line, &from, &to);
    }
}

static lv_obj_t *settings_card_create(lv_obj_t *parent, const char *text,
    const char *symbol, lv_event_cb_t callback, void *user_data,
    bool warning, lv_obj_t **title_out)
{
    lv_obj_t *card = lv_btn_create(parent);
    lv_obj_set_size(card, lv_pct(100), 64);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_COLOR_DISABLED), LV_STATE_DISABLED);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_add_event_cb(card, callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t *badge = lv_obj_create(card);
    lv_obj_set_size(badge, 36, 36);
    lv_obj_align(badge, LV_ALIGN_LEFT_MID, 18, 0);
    lv_obj_set_style_pad_all(badge, 0, 0);
    lv_obj_set_style_radius(badge, 10, 0);
    lv_obj_set_style_border_width(badge, 0, 0);
    lv_obj_set_style_bg_color(badge, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    if (callback == wireless_settings_cb && (intptr_t)user_data == QUICK_RADIO_HOTSPOT) {
        lv_obj_t *icon = hotspot_icon_create(badge, 32);
        lv_obj_set_style_text_color(icon, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
        lv_obj_center(icon);
    } else if (symbol) {
        lv_obj_t *icon = lv_label_create(badge);
        lv_label_set_text(icon, symbol);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(warning ? UI_COLOR_WARNING : UI_COLOR_ACCENT_DARK), 0);
        lv_obj_center(icon);
    } else {
        lv_obj_add_event_cb(badge, settings_card_draw_sun, LV_EVENT_DRAW_MAIN, NULL);
    }

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 292);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 72, 0);
    if (title_out) *title_out = label;

    lv_obj_t *arrow = lv_label_create(card);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -18, 0);
    return card;
}

static void system_update_btn_cb(lv_event_t *e) {(void)e;system_update_ui_open(settings_screen);}
static void screen_idle_btn_cb(lv_event_t *e) {(void)e;screen_idle_ui_open(settings_screen, chinese_font_20);}
static void ai_voice_btn_cb(lv_event_t *e) {(void)e;ai_voice_ui_open(settings_screen, chinese_font_20);}

static void create_settings_screen(void)
{
    settings_screen = lv_obj_create(NULL);
    lv_obj_set_size(settings_screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(settings_screen, 0, 0);
    lv_obj_set_style_border_width(settings_screen, 0, 0);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);
    ui_theme_apply_background(settings_screen);

    lv_obj_t *header = lv_obj_create(settings_screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, SCREEN_WIDTH, 56);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    create_back_button(header, LV_ALIGN_RIGHT_MID, -8, back_btn_cb);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, chinese_font_20 ? "设置" : "Settings");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_center(title);

    /* Only the content scrolls; the header and return button stay reachable. */
    lv_obj_t *content = lv_obj_create(settings_screen);
    lv_obj_set_pos(content, 0, 56);
    lv_obj_set_size(content, SCREEN_WIDTH, SCREEN_HEIGHT - 56);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_left(content, 30, 0);
    lv_obj_set_style_pad_right(content, 30, 0);
    lv_obj_set_style_pad_top(content, 12, 0);
    lv_obj_set_style_pad_bottom(content, 20, 0);
    lv_obj_set_style_pad_row(content, 12, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(content, lv_color_hex(UI_COLOR_BORDER), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(content, 4, LV_PART_SCROLLBAR);

    settings_card_create(content, "WiFi", LV_SYMBOL_WIFI, wifi_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "亮度" : "Brightness",
        NULL, brightness_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "息屏时间" : "Screen timeout",
        LV_SYMBOL_EYE_CLOSE, screen_idle_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "音量" : "Volume",
        LV_SYMBOL_VOLUME_MAX, volume_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "语音设置" : "Voice settings",
        LV_SYMBOL_AUDIO, ai_voice_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "蓝牙" : "Bluetooth",
        LV_SYMBOL_BLUETOOTH, wireless_settings_cb, (void *)(intptr_t)QUICK_RADIO_BLUETOOTH, false, NULL);
    settings_card_create(content, chinese_font_20 ? "热点" : "Hotspot",
        NULL, wireless_settings_cb, (void *)(intptr_t)QUICK_RADIO_HOTSPOT, false, NULL);
    settings_card_create(content, chinese_font_20 ? "设备信息" : "Device information",
        "i", device_info_btn_cb, NULL, false, NULL);
    settings_card_create(content, chinese_font_20 ? "系统更新" : "System update",
        LV_SYMBOL_DOWNLOAD, system_update_btn_cb, NULL, false, NULL);
    restart_button = settings_card_create(content, chinese_font_20 ? "重启设备" : "Restart device",
        LV_SYMBOL_POWER, restart_btn_cb, NULL, true, NULL);
    display_restart_button = settings_card_create(content, chinese_font_20 ? "重启屏幕" : "Restart screen",
        LV_SYMBOL_REFRESH, display_restart_btn_cb, NULL, false, &display_restart_title);
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
    snprintf(value, sizeof(value), "%s %s", info.system_name, info.firmware_version);
    device_info_set_value(1, value);
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
        "芯片型号", "系统信息", "CPU性能", "Flash固件", "内部堆", "PSRAM", "屏幕刷新"
    };
    const char *names_en[DEVICE_INFO_ROW_COUNT] = {
        "Chip model", "System", "CPU", "Flash image", "Internal heap", "PSRAM", "LCD refresh"
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
    ui_screen_open(device_info_screen);
}

static void brightness_btn_cb(lv_event_t *e)
{
    (void)e;
    lv_slider_set_value(brightness_slider, get_backlight_brightness(), LV_ANIM_OFF);
    char value[12];
    snprintf(value, sizeof(value), "%d%%", get_backlight_brightness());
    lv_label_set_text(brightness_label, value);
    ui_screen_open(brightness_screen);
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
    lv_slider_set_value(volume_slider, get_audio_volume(), LV_ANIM_OFF);
    char value[12];
    snprintf(value, sizeof(value), "%u%%", (unsigned)get_audio_volume());
    lv_label_set_text(volume_label, value);
    ui_screen_open(volume_screen);
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

static void wifi_enabled_event(lv_event_t *event)
{
    quick_radio_request(QUICK_RADIO_WIFI,
        lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
}

static void wifi_settings_state_timer(lv_timer_t *timer)
{
    (void)timer;
    if (lv_scr_act() != wifi_list_screen) return;
    quick_radio_state_t radio = {0};
    quick_radio_get(QUICK_RADIO_WIFI, &radio);
    if (radio.busy) { wifi_update_connection_info(); return; }
    const bool enabled_changed = radio.enabled != wifi_ui_last_enabled;
    if (enabled_changed && radio.enabled) do_wifi_scan();
    const uint32_t revision = wifi_scan_revision();
    const bool scanning = wifi_scan_is_busy();
    const bool connecting = wifi_connection_is_busy();
    const bool connected = get_wifi_connected();
    char ssid[33], target[33];
    snprintf(ssid, sizeof(ssid), "%s", get_wifi_connected_ssid());
    snprintf(target, sizeof(target), "%s", wifi_connection_target());
    if (enabled_changed || revision != wifi_ui_last_revision ||
        scanning != wifi_ui_last_scan_busy || connecting != wifi_ui_last_connect_busy ||
        connected != wifi_ui_last_connected || strcmp(ssid, wifi_ui_last_connected_ssid) ||
        strcmp(target, wifi_ui_last_target)) {
        wifi_render_scan_results();
        wifi_ui_last_revision = revision;
        wifi_ui_last_scan_busy = scanning;
        wifi_ui_last_connect_busy = connecting;
        wifi_ui_last_connected = connected;
        snprintf(wifi_ui_last_connected_ssid, sizeof(wifi_ui_last_connected_ssid), "%s", ssid);
        snprintf(wifi_ui_last_target, sizeof(wifi_ui_last_target), "%s", target);
    }
    wifi_ui_last_enabled = radio.enabled;
    wifi_update_connection_info();
}

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
    lv_obj_set_width(wifi_connection_ssid_label, 318);
    lv_label_set_long_mode(wifi_connection_ssid_label, LV_LABEL_LONG_DOT);
    wifi_enabled_switch = lv_switch_create(connection_panel);
    lv_obj_set_size(wifi_enabled_switch, 66, 32);
    lv_obj_align(wifi_enabled_switch, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_bg_color(wifi_enabled_switch, lv_color_hex(UI_COLOR_ACCENT),
        LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifi_enabled_switch, wifi_enabled_event, LV_EVENT_VALUE_CHANGED, NULL);
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
    lv_obj_set_size(wifi_list, SCREEN_WIDTH - 20, 280);
    lv_obj_set_pos(wifi_list, 10, 138);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(wifi_list, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_border_width(wifi_list, 1, 0);
    lv_obj_set_style_border_color(wifi_list, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(wifi_list, 8, 0);

    lv_obj_t *usage = lv_label_create(wifi_list_screen);
    lv_label_set_text(usage, chinese_font_20 ? "点按连接，长按改密码" :
                                           "Tap to join\nHold to change password");
    lv_obj_set_width(usage, 300);
    lv_obj_set_style_text_font(usage, ui_chinese_font_16(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(usage, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_align(usage, LV_ALIGN_BOTTOM_LEFT, 14, -18);

    lv_obj_t *refresh_btn = lv_btn_create(wifi_list_screen);
    lv_obj_set_size(refresh_btn, 116, 42);
    lv_obj_align(refresh_btn, LV_ALIGN_BOTTOM_RIGHT, -14, -12);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(UI_COLOR_PRESSED), LV_STATE_PRESSED);
    lv_obj_set_style_radius(refresh_btn, 8, 0);
    lv_obj_add_event_cb(refresh_btn, wifi_refresh_cb, LV_EVENT_CLICKED, NULL);
    wifi_refresh_label = lv_label_create(refresh_btn);
    lv_label_set_text(wifi_refresh_label, chinese_font_20 ? "刷新" : "Refresh");
    lv_obj_set_style_text_font(wifi_refresh_label, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(wifi_refresh_label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_center(wifi_refresh_label);
    lv_timer_create(wifi_settings_state_timer, 500, NULL);
}

/* ==================== WIFI PASSWORD SCREEN ==================== */

// ✓ 键确认连接
static void wifi_pwd_ready_cb(lv_event_t *event)
{
    wifi_pwd_confirm_cb(event);
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
        ui_screen_back(wifi_list_screen);
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
    lv_label_set_text(hint, chinese_font_20 ? "侧边内滑返回，确认连接" : "Swipe from the edge to go back");
    lv_obj_set_style_text_font(hint, ui_chinese_font_20(&lv_font_montserrat_14), 0);
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

/* New observations use the exact provider code; old NVS records use text fallback. */
static const lv_img_dsc_t * get_weather_icon(const char *condition)
{
    int code = get_weather_code();
    return weather_icon_for_code(code < 0 ? weather_icon_legacy_code(condition) : code);
}

static void style_weather_icon(lv_obj_t *image)
{
    lv_img_set_zoom(image, 256);
    lv_obj_set_style_bg_color(image, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_set_style_bg_opa(image, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(image, 16, 0);
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

static void refresh_weather_status(void)
{
    weather_page_ui_refresh_status();
}

static void refresh_weather_page(void)
{
    weather_page_ui_refresh();
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

static void city_keyboard_show(bool visible)
{
    if (visible) {
        show_text_keyboard(kb_keyboard, kb_textarea);
        if (kb_ime) lv_ime_pinyin_refresh(kb_ime);
        if (kb_candidates) lv_obj_move_foreground(kb_candidates);
    } else {
        if (kb_ime) lv_ime_pinyin_clear(kb_ime);
        hide_text_keyboard(kb_keyboard);
        if (kb_candidates) lv_obj_add_flag(kb_candidates, LV_OBJ_FLAG_HIDDEN);
    }
}

static void kb_confirm(void)
{
    if (kb_submitted) return;
    const char *txt = lv_textarea_get_text(kb_textarea);
    if (!txt || !*txt) {
        lv_label_set_text(kb_hint, "请输入城市或区县名称");
        return;
    }
    if (!updateWeatherCity(txt)) {
        lv_label_set_text(kb_hint, getWeatherCityError());
        return;
    }
    /* LVGL can send READY to both keyboard and textarea for the same key. */
    kb_submitted = true;
    city_keyboard_show(false);
    if (city_btn_label) lv_label_set_text(city_btn_label, ui_city_name());
    if (weather && temp) set_weather(weather, temp);
    ui_screen_back(kb_return_screen ? kb_return_screen : main_screen_ref);
}

static void city_keyboard_event_cb(lv_event_t *event)
{
    /* The keyboard forwards READY to the textarea. Handle that one event,
     * including validation failures, rather than submitting both copies. */
    if (lv_event_get_code(event) == LV_EVENT_READY && lv_event_get_target(event) == kb_textarea) kb_confirm();
    else if (lv_event_get_code(event) == LV_EVENT_CANCEL) city_keyboard_show(false);
}

static void city_text_event_cb(lv_event_t *event)
{
    (void)event;
    city_keyboard_show(true);
}

static void city_btn_cb(lv_event_t *event)
{
    (void)event;
    kb_return_screen = lv_scr_act();
    kb_submitted = false;
    if (kb_ime) lv_ime_pinyin_clear(kb_ime);
    lv_textarea_set_text(kb_textarea, "");
    lv_label_set_text(kb_hint, "输入中文市县；重名可加省市，如北京朝阳");
    ui_screen_open(kb_screen);
    city_keyboard_show(true);
}

static void weather_city_btn_cb(lv_event_t *event) { city_btn_cb(event); }

static void create_keyboard_screen(void)
{
    kb_screen = lv_obj_create(NULL);
    ui_theme_apply_background(kb_screen);
    lv_obj_set_style_pad_all(kb_screen, 0, 0);
    lv_obj_clear_flag(kb_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = lv_label_create(kb_screen);
    lv_label_set_text(title, "输入城市");
    lv_obj_set_style_text_font(title, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(title, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    kb_hint = lv_label_create(kb_screen);
    lv_obj_set_style_text_font(kb_hint, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(kb_hint, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_set_pos(kb_hint, 20, 60);lv_obj_set_width(kb_hint, 440);
    lv_label_set_long_mode(kb_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(kb_hint, "输入中文市县；重名可加省市，如北京朝阳");

    kb_textarea = lv_textarea_create(kb_screen);
    lv_obj_set_style_bg_color(kb_textarea, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_text_color(kb_textarea, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(kb_textarea, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_border_color(kb_textarea, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_pos(kb_textarea, 16, 194);lv_obj_set_size(kb_textarea, 448, 46);
    lv_textarea_set_placeholder_text(kb_textarea, "城市或区县，例如南宁");
    lv_textarea_set_max_length(kb_textarea, 30);
    lv_textarea_set_one_line(kb_textarea, true);
    lv_obj_add_event_cb(kb_textarea, city_text_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(kb_textarea, city_text_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(kb_textarea, city_keyboard_event_cb, LV_EVENT_READY, NULL);

    kb_keyboard = lv_keyboard_create(kb_screen);
    lv_obj_set_size(kb_keyboard, SCREEN_WIDTH, AI_KEYBOARD_HEIGHT);
    lv_obj_set_align(kb_keyboard, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(kb_keyboard, 0, SCREEN_HEIGHT - AI_KEYBOARD_HEIGHT);
    lv_keyboard_set_textarea(kb_keyboard, kb_textarea);
    lv_obj_set_style_bg_color(kb_keyboard, lv_color_hex(UI_COLOR_PANEL), LV_PART_MAIN);
    lv_obj_set_style_bg_color(kb_keyboard, lv_color_hex(UI_COLOR_SURFACE), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_keyboard, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
    kb_ime = lv_ime_pinyin_create(kb_screen);
    lv_ime_pinyin_set_keyboard(kb_ime, kb_keyboard);
    lv_pinyin_dict_t *dict = get_sd_pinyin_dict();
    lv_ime_pinyin_set_dict(kb_ime, dict ? dict : ai_chat_ime_dict);
    lv_ime_pinyin_set_mode(kb_ime, LV_IME_PINYIN_MODE_K26);
    lv_obj_clear_flag(kb_ime, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(kb_ime, 1, 1);
    kb_candidates = lv_ime_pinyin_get_cand_panel(kb_ime);
    if (kb_candidates) {
        lv_obj_set_parent(kb_candidates, kb_screen);
        lv_obj_set_size(kb_candidates, SCREEN_WIDTH, AI_CANDIDATE_HEIGHT);
        lv_obj_set_align(kb_candidates, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(kb_candidates, 0, SCREEN_HEIGHT - AI_KEYBOARD_HEIGHT - AI_CANDIDATE_HEIGHT);
        lv_obj_set_style_text_font(kb_candidates, ui_chinese_font_20(&lv_font_montserrat_16), LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb_candidates, lv_color_hex(UI_COLOR_TEXT), LV_PART_ITEMS);
        lv_obj_set_style_bg_color(kb_candidates, lv_color_hex(UI_COLOR_SURFACE), LV_PART_MAIN);
    }
    lv_obj_add_event_cb(kb_keyboard, city_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_keyboard, city_keyboard_event_cb, LV_EVENT_CANCEL, NULL);
    city_keyboard_show(false);
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
    lv_obj_set_style_bg_color(wallpaper_btn, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(wallpaper_btn, LV_OPA_80, 0);
    lv_obj_set_style_border_width(wallpaper_btn, 1, 0);
    lv_obj_set_style_border_color(wallpaper_btn, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_radius(wallpaper_btn, 17, 0);
    lv_obj_set_style_shadow_width(wallpaper_btn, 0, 0);
    lv_obj_add_event_cb(wallpaper_btn, wallpaper_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *wallpaper_btn_label = lv_label_create(wallpaper_btn);
    lv_label_set_text(wallpaper_btn_label, chinese_font_20 ? "壁纸" : "Wallpaper");
    lv_obj_set_style_text_font(wallpaper_btn_label, ui_chinese_font_16(&lv_font_montserrat_14), 0);
    lv_obj_set_style_text_color(wallpaper_btn_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_center(wallpaper_btn_label);

    /* Transparent layout container: content is drawn directly over wallpaper. */
    lv_obj_t *clock_card = lv_obj_create(page);
    lv_obj_set_size(clock_card, 440, 360);
    lv_obj_align(clock_card, LV_ALIGN_TOP_MID, 0, 100);
    lv_obj_set_style_bg_opa(clock_card, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(clock_card, 0, 0);
    lv_obj_set_style_radius(clock_card, 0, 0);
    lv_obj_set_style_shadow_width(clock_card, 0, 0);
    lv_obj_set_style_pad_all(clock_card, 0, 0);
    lv_obj_clear_flag(clock_card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    /* Weather temperature and device-chip temperature on one pale card. */
    lv_obj_t *info_card = lv_obj_create(clock_card);
    lv_obj_set_pos(info_card, 8, 6);
    lv_obj_set_size(info_card, 408, 80);
    lv_obj_set_style_radius(info_card, 18, 0);
    lv_obj_set_style_bg_color(info_card, lv_color_hex(UI_COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(info_card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(info_card, 1, 0);
    lv_obj_set_style_border_color(info_card, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_border_opa(info_card, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(info_card, 0, 0);
    lv_obj_set_style_pad_all(info_card, 0, 0);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    weather_icon_img = lv_img_create(info_card);
    lv_img_set_src(weather_icon_img, get_weather_icon(weather));
    style_weather_icon(weather_icon_img);
    lv_obj_set_pos(weather_icon_img, 4, 8);
    lv_obj_clear_flag(weather_icon_img, LV_OBJ_FLAG_CLICKABLE);

    city_btn_label = NULL;
    weather_text = lv_label_create(info_card);
    lv_label_set_text(weather_text, "--");
    lv_obj_set_pos(weather_text, 78, 12);
    lv_obj_set_width(weather_text, 150);
    lv_label_set_long_mode(weather_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(weather_text, ui_chinese_font_20(&lv_font_montserrat_20), 0);
    lv_obj_set_style_text_color(weather_text, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_clear_flag(weather_text, LV_OBJ_FLAG_CLICKABLE);

    weather_temp_label = lv_label_create(info_card);
    lv_label_set_text(weather_temp_label, "--°C");
    lv_obj_set_pos(weather_temp_label, 78, 40);
    lv_obj_set_width(weather_temp_label, 150);
    lv_obj_set_style_text_font(weather_temp_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(weather_temp_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_clear_flag(weather_temp_label, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *info_divider = lv_obj_create(info_card);
    lv_obj_set_pos(info_divider, 244, 18);
    lv_obj_set_size(info_divider, 1, 44);
    lv_obj_set_style_bg_color(info_divider, lv_color_hex(UI_COLOR_BORDER), 0);
    lv_obj_set_style_bg_opa(info_divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(info_divider, 0, 0);
    lv_obj_set_style_radius(info_divider, 0, 0);
    lv_obj_set_style_shadow_width(info_divider, 0, 0);
    lv_obj_set_style_pad_all(info_divider, 0, 0);
    lv_obj_clear_flag(info_divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *device_temp_title = lv_label_create(info_card);
    lv_label_set_text(device_temp_title, chinese_font_20 ? "设备温度" : "Device temp");
    lv_obj_set_pos(device_temp_title, 264, 14);
    lv_obj_set_width(device_temp_title, 132);
    lv_obj_set_style_text_font(device_temp_title, ui_chinese_font_20(&lv_font_montserrat_16), 0);
    lv_obj_set_style_text_color(device_temp_title, lv_color_hex(UI_COLOR_TEXT_MUTED), 0);
    lv_obj_clear_flag(device_temp_title, LV_OBJ_FLAG_CLICKABLE);

    home_gyro_temp_label = lv_label_create(info_card);
    lv_label_set_text(home_gyro_temp_label, "--.-°C");
    lv_obj_set_pos(home_gyro_temp_label, 264, 43);
    lv_obj_set_width(home_gyro_temp_label, 132);
    lv_obj_set_style_text_align(home_gyro_temp_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_font(home_gyro_temp_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(home_gyro_temp_label, lv_color_hex(UI_COLOR_ACCENT_DARK), 0);
    lv_obj_clear_flag(home_gyro_temp_label, LV_OBJ_FLAG_CLICKABLE);

    /* Lightweight split-flap digits, with seconds on the same baseline. */
    time_label = NULL;
    home_flip_clock = flip_clock_create(clock_card);
    if (home_flip_clock) {
        lv_obj_set_pos(flip_clock_object(home_flip_clock), 8, 92);
        flip_clock_set_time(home_flip_clock, -1, -1, -1, false);
        lv_obj_clear_flag(flip_clock_object(home_flip_clock), LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }

    home_info_create(page, clock_card, ui_chinese_font_20(&lv_font_montserrat_20));
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
    ui_screen_open(wallpaper_screen);
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
    ui_screen_back(main_screen_ref);
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
    alarm_ui_open(main_screen_ref, chinese_font_20);
}

static void calendar_app_btn_cb(lv_event_t *e)
{
    (void)e;
    calendar_ui_open(main_screen_ref, ui_chinese_font_20(&lv_font_montserrat_20),
                     chinese_font_16 ? chinese_font_16 : ui_chinese_font_20(&lv_font_montserrat_16));
}

static void photo_app_btn_cb(lv_event_t *e)
{
    (void)e;
    scan_photo_files();
    photo_render_gallery();
    ui_screen_open(photo_screen);
}

static void photo_detach_and_release(void)
{
    if (photo_full_img) {
        const void *src = lv_img_get_src(photo_full_img);
        /* File sources are copied by LVGL and freed by set_src(NULL). */
        if (src) lv_img_cache_invalidate_src(src);
        lv_img_set_src(photo_full_img, NULL);
        lv_obj_add_flag(photo_full_img, LV_OBJ_FLAG_HIDDEN);
    }
    release_loaded_photo();
}

static void photo_show_load_error(void)
{
    const char *error = get_photo_load_error();
    if (error && error[0]) {
        lv_label_set_text(photo_loading_label, error);
    } else {
        lv_label_set_text(photo_loading_label,
                          chinese_font_20 ? "照片读取失败" : "Photo load failed");
    }
    lv_obj_clear_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
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
    photo_detach_and_release();
    lv_label_set_text(photo_loading_label,
                      chinese_font_20 ? "正在读取照片..." : "Loading photo...");
    lv_obj_clear_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
    ui_screen_open(photo_view_screen);
    lv_refr_now(NULL);

    if (!start_photo_load(path)) {
        photo_show_load_error();
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
        const char *path = get_loaded_photo_path();
        if (image) {
            lv_img_set_src(photo_full_img, image);
            lv_obj_clear_flag(photo_full_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(photo_full_img);
        } else if (path) {
            lv_img_set_src(photo_full_img, path);
            lv_obj_clear_flag(photo_full_img, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(photo_loading_label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(photo_full_img);
        } else {
            photo_show_load_error();
        }
    } else if (status < 0) {
        photo_show_load_error();
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
    photo_detach_and_release();
    ui_screen_back(photo_screen);
}

static void photo_back_cb(lv_event_t *e)
{
    (void)e;
    if (media_actions_back(photo_screen)) return;
    ui_screen_back(main_screen_ref);
}

static void photo_media_changed(void) { scan_photo_files(); photo_render_gallery(); }

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
        media_actions_row(photo_gallery, i, MEDIA_PHOTO, get_photo_path(i), get_photo_name(i),
                          photo_open_cb, photo_media_changed);
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
    lv_obj_add_flag(photo_full_img, LV_OBJ_FLAG_HIDDEN);
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
    ui_screen_open(video_screen);
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
    ui_screen_open(video_view_screen);
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
    ui_screen_back(video_screen);
}

static void video_back_cb(lv_event_t *e)
{
    (void)e;
    if (media_actions_back(video_screen)) return;
    close_video_file();
    video_audio_release();
    video_set_playback_mode(false);
    ui_screen_back(main_screen_ref);
}

static void video_media_changed(void) { scan_video_files(); video_render_gallery(); }

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
        media_actions_row(video_gallery, i, MEDIA_VIDEO, get_video_path(i), get_video_name(i),
                          video_open_cb, video_media_changed);
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

static void gyro_btn_cb(lv_event_t *e) { ui_screen_open(gyro_screen); }
static void game_btn_cb(lv_event_t *e) {(void)e; if (!game_screen) create_game_screen(); ui_screen_open(game_screen);}
static void game_back_cb(lv_event_t *e) {(void)e; ui_screen_back(games_ui_get_screen() ? games_ui_get_screen() : main_screen_ref);}
static void games_app_btn_cb(lv_event_t *e)
{
    (void)e;
    games_ui_open(main_screen_ref, ui_chinese_font_20(&lv_font_montserrat_20), chinese_font_20 != NULL,
                  game_btn_cb, balance_game_btn_cb);
}
static void settings_btn_cb(lv_event_t *e) { ui_screen_open(settings_screen); }
void custom_open_settings(void) { settings_btn_cb(NULL); }
void custom_open_wifi_settings(void) { wifi_btn_cb(NULL); }
static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    const bool keyboard_was_visible = ai_chat_keyboard && lv_scr_act() == ai_assistant_screen &&
        !lv_obj_has_flag(ai_chat_keyboard, LV_OBJ_FLAG_HIDDEN);
    ai_chat_hide_keyboard();
    if (!keyboard_was_visible) ui_screen_back(main_screen_ref);
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
        lv_obj_set_size(ai_chat_input, 352, 52);
        lv_obj_set_pos(ai_chat_input, 16, SCREEN_HEIGHT - 68);
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
        lv_obj_set_size(ai_chat_send_button, 88, 52);
        lv_obj_set_pos(ai_chat_send_button, 376, SCREEN_HEIGHT - 68);
        lv_obj_set_style_bg_color(ai_chat_send_button, lv_color_hex(UI_COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(ai_chat_send_button, lv_color_hex(UI_COLOR_ACCENT_DARK), LV_STATE_PRESSED);
        lv_obj_set_style_radius(ai_chat_send_button, 8, 0);
        lv_obj_add_event_cb(ai_chat_send_button, ai_chat_send_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *send_label = lv_label_create(ai_chat_send_button);
        lv_label_set_text(send_label, "发送");
        lv_obj_set_style_text_font(send_label, ui_chinese_font_20(&lv_font_montserrat_16), 0);
        lv_obj_set_style_text_color(send_label, lv_color_hex(UI_COLOR_ON_ACCENT), 0);
        lv_obj_center(send_label);

        ai_chat_keyboard = lv_keyboard_create(ai_assistant_screen);
        lv_keyboard_set_textarea(ai_chat_keyboard, ai_chat_input);
        lv_obj_set_size(ai_chat_keyboard, SCREEN_WIDTH, AI_KEYBOARD_HEIGHT);
        /* Reserve a fixed candidate row above the keyboard. */
        lv_obj_set_align(ai_chat_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(ai_chat_keyboard, 0, SCREEN_HEIGHT - AI_KEYBOARD_HEIGHT);
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
            lv_obj_set_align(ime_panel, LV_ALIGN_TOP_LEFT);
            lv_obj_set_pos(ime_panel, 0, SCREEN_HEIGHT - AI_KEYBOARD_HEIGHT - AI_CANDIDATE_HEIGHT);
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

        ai_chat_stop_button = lv_btn_create(ai_assistant_screen);
        lv_obj_set_pos(ai_chat_stop_button, 344, 12);
        lv_obj_set_size(ai_chat_stop_button, 120, 34);
        lv_obj_add_flag(ai_chat_stop_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(ai_chat_stop_button, ai_chat_stop_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *stop_label = lv_label_create(ai_chat_stop_button);
        lv_obj_set_style_text_font(stop_label, ui_chinese_font_16(&lv_font_montserrat_16), 0);
        lv_label_set_text(stop_label, "停止播报"); lv_obj_center(stop_label);
        ai_chat_layout(false);
        ai_chat_poll_timer = lv_timer_create(ai_chat_poll_cb, 150, NULL);
    }
    ui_screen_open(ai_assistant_screen);
}

static void ai_chat_stop_cb(lv_event_t *e)
{
    (void)e; ai_chat_stop_speech();
    lv_label_set_text(ai_chat_status, "正在停止播报...");
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
    lv_label_set_text(ai_chat_status, "正在请求 DeepSeek...");
    lv_textarea_set_text(ai_chat_input, "");
}

static void ai_chat_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!ai_chat_status || !ai_chat_reply) return;
    char speech_status[96];
    const bool speech_active = ai_chat_speech_status(speech_status, sizeof(speech_status));

    const int result = ai_chat_get_result(ai_chat_last_reply,
                                          sizeof(ai_chat_last_reply),
                                          ai_chat_last_error,
                                          sizeof(ai_chat_last_error));
    if (result == 1) {
        const char *displayed = lv_label_get_text(ai_chat_reply);
        if (strncmp(displayed, "AI：", strlen("AI：")) || strcmp(displayed + strlen("AI："), ai_chat_last_reply)) {
            lv_label_set_text_fmt(ai_chat_reply, "AI：%s", ai_chat_last_reply);
            lv_obj_scroll_to_y(lv_obj_get_parent(ai_chat_reply), 0, LV_ANIM_OFF);
        }
        lv_label_set_text(ai_chat_status, speech_active ? speech_status : ai_audio_is_playing() ? "正在设备扬声器播放..." :
            ai_chat_is_busy() ? "正在生成语音..." :
            ai_chat_last_error[0] ? ai_chat_last_error : "回答完成，可继续提问");
    } else if (result < 0) {
        lv_label_set_text(ai_chat_status, ai_chat_last_error);
    } else {
        lv_label_set_text(ai_chat_status, ai_chat_is_busy() ? "正在请求 DeepSeek..." : ai_service_status());
    }

    if (ai_chat_stop_button) {
        if (speech_active || ai_audio_is_playing()) lv_obj_clear_flag(ai_chat_stop_button, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ai_chat_stop_button, LV_OBJ_FLAG_HIDDEN);
    }
    if (ai_chat_is_busy()) {
        lv_obj_add_state(ai_chat_send_button, LV_STATE_DISABLED);
        return;
    }
    lv_obj_clear_state(ai_chat_send_button, LV_STATE_DISABLED);
}

/* Keep the composer above the bottom keyboard and reserve a stable IME row.
 * Closing it restores the full reply viewport without moving other forms. */
static void ai_chat_layout(bool keyboard_visible)
{
    if (!ai_chat_input || !ai_chat_send_button) return;
    const lv_coord_t keyboard_y = SCREEN_HEIGHT - AI_KEYBOARD_HEIGHT;
    const lv_coord_t candidate_y = keyboard_y - AI_CANDIDATE_HEIGHT;
    const lv_coord_t input_y = keyboard_visible ? candidate_y - 60 : SCREEN_HEIGHT - 68;
    lv_obj_set_height(ai_chat_input, 52);
    lv_obj_set_pos(ai_chat_input, 16, input_y);
    lv_obj_set_pos(ai_chat_send_button, 376, input_y);
    if (ai_chat_reply) lv_obj_set_height(lv_obj_get_parent(ai_chat_reply), input_y - 12 - 108);
    if (ai_chat_keyboard) {
        lv_obj_set_align(ai_chat_keyboard, LV_ALIGN_TOP_LEFT);
        lv_obj_set_pos(ai_chat_keyboard, 0, keyboard_y);
    }
    if (ai_chat_ime) {
        lv_obj_t *panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (panel) {
            lv_obj_set_align(panel, LV_ALIGN_TOP_LEFT);
            lv_obj_set_size(panel, SCREEN_WIDTH, AI_CANDIDATE_HEIGHT);
            lv_obj_set_pos(panel, 0, candidate_y);
        }
    }
}

static void ai_chat_hide_keyboard(void)
{
    if (ai_chat_ime) lv_ime_pinyin_clear(ai_chat_ime);
    hide_text_keyboard(ai_chat_keyboard);
    if (ai_chat_ime) {
        lv_obj_t *panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (panel) lv_obj_add_flag(panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (ai_chat_input) lv_obj_clear_state(ai_chat_input, LV_STATE_FOCUSED);
    ai_chat_layout(false);
}

static void ai_chat_show_keyboard(void)
{
    if (!ai_chat_keyboard || !ai_chat_input) return;
    ai_chat_layout(true);
    show_text_keyboard(ai_chat_keyboard, ai_chat_input);
    if (ai_chat_ime) {
        lv_ime_pinyin_refresh(ai_chat_ime);
        lv_obj_t *panel = lv_ime_pinyin_get_cand_panel(ai_chat_ime);
        if (panel) lv_obj_move_foreground(panel);
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
            ai_chat_hide_keyboard();
        }
    } else if (code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        ai_chat_hide_keyboard();
        if (code == LV_EVENT_READY) ai_chat_send_cb(e);
    }
}





static void browser_app_btn_cb(lv_event_t *e)
{
    (void)e;
    browser_ui_open(main_screen_ref, ui_chinese_font_20(&lv_font_montserrat_16),
                    chinese_font_20 != NULL, create_back_button);
}

static void words_app_btn_cb(lv_event_t *e)
{
    (void)e;
    word_match_ui_open(main_screen_ref, ui_chinese_font_20(&lv_font_montserrat_20),
                       chinese_font_20 != NULL);
}

static void setup_icon_page(lv_obj_t *page)
{
    ui_theme_apply_background(page);
    const app_launcher_item_t icons[] = {
        {chinese_font_20 ? "陀螺仪" : "Gyro", &gyro_img, gyro_btn_cb, APP_ID_GYRO},
        {chinese_font_20 ? "游戏" : "Games", &breeze_icon_game, games_app_btn_cb, APP_ID_GAMES},
        {chinese_font_20 ? "音乐" : "Music", &sd_icon_music, music_app_btn_cb, APP_ID_MUSIC},
        {chinese_font_20 ? "天气" : "Weather", &sd_icon_weather, weather_app_btn_cb, APP_ID_WEATHER},
        {chinese_font_20 ? "时钟" : "Clock", &sd_icon_clock, clock_btn_cb, APP_ID_CLOCK},
        {chinese_font_20 ? "照片" : "Photos", &sd_icon_photos, photo_app_btn_cb, APP_ID_PHOTOS},
        {chinese_font_20 ? "浏览器" : "Browser", &sd_icon_browser, browser_app_btn_cb, APP_ID_BROWSER},
        {chinese_font_20 ? "视频" : "Video", &sd_icon_video, video_app_btn_cb, APP_ID_VIDEO},
        {chinese_font_20 ? "设置" : "Settings", &sd_icon_settings, settings_btn_cb, APP_ID_SETTINGS},
        {chinese_font_20 ? "日历" : "Calendar", &sd_icon_calendar, calendar_app_btn_cb, APP_ID_CALENDAR},
        {chinese_font_20 ? "AI助手" : "AI", &sd_icon_ai_assistant, ai_assistant_app_btn_cb, APP_ID_AI},
        {chinese_font_20 ? "背单词" : "Words", &breeze_icon_english, words_app_btn_cb, APP_ID_WORDS},
    };
    app_launcher_create(page, ui_chinese_font_20(&lv_font_montserrat_16),
                        chinese_font_20 != NULL, icons, sizeof(icons) / sizeof(icons[0]));

    detail_info = NULL;
}

static void weather_app_btn_cb(lv_event_t *e)
{
    (void)e;
    /* Reuse the app-grid page so the forecast UI does not add another full
     * screen's worth of LVGL objects to this no-PSRAM target. */
    weather_page_ui_clear();
    lv_obj_clean(pages[1]);
    detail_info = NULL;
    weather_page_active = true;
    setup_weather_page(pages[1]);
}

static void weather_back_cb(lv_event_t *e)
{
    (void)e;
    weather_page_ui_clear();
    lv_obj_clean(pages[1]);
    weather_page_active = false;
    setup_icon_page(pages[1]);
}

static void setup_weather_page(lv_obj_t *page)
{
    weather_page_ui_mount(page, weather_city_btn_cb);
    create_back_button(page, LV_ALIGN_TOP_RIGHT, -8, weather_back_cb);
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

    app_launcher_cancel_drag();
    lv_obj_add_flag(pages[current_page], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(pages[page], LV_OBJ_FLAG_HIDDEN);

    const bool backwards = page < current_page;
    current_page = page;
    update_page_dots(page);
    ui_transition_reveal(pages[page], backwards);
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
    lv_obj_set_style_pad_all(button, 0, 0);
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
    char name[256];
    music_audio_copy_title(name, sizeof(name));
    const char *text = name[0] ? name : "无音乐";
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
    music_update_track_label();
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
        const char *lyric = music_audio_is_online() ? "网络音乐" : get_music_lyric(get_music_elapsed_ms());
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
    if (music_audio_is_online() && online_music_handle_command("上一首")) return;
    const uint8_t count = get_music_track_count();
    if (count == 0) return;
    const int previous = music_next_local_track(get_music_track_index(), -1);
    if (previous < 0) return;
    music_track_index = previous;
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
    if (music_audio_is_online() && online_music_handle_command("下一首")) return;
    const uint8_t count = get_music_track_count();
    if (count == 0) return;
    const int next = music_next_local_track(get_music_track_index(), 1);
    if (next < 0) return;
    music_track_index = next;
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
    ui_screen_back(main_screen_ref);
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
    ui_screen_open(music_screen);
    music_start_animation();
}

static void music_status_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!music_screen || lv_scr_act() != music_screen) return;
    const bool playing = music_audio_is_playing();
    if (playing != music_playing) {
        music_playing = playing;
        if (music_play_label) lv_label_set_text(music_play_label, playing ? "||" : ">");
        if (playing || music_needle_progress > 0) music_start_animation();
    }
    music_update_track_label();
}

static void music_playlist_cb(lv_event_t *event)
{
    (void)event;
    music_playlist_open(music_screen);
}

static void create_music_screen(void)
{
    music_screen = lv_obj_create(NULL);
    lv_timer_create(music_status_poll_cb, 250, NULL);
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

    lv_obj_t *library_btn = music_make_control("所有歌单", 8, 104, music_playlist_cb);
    lv_obj_set_style_text_font(lv_obj_get_child(library_btn, 0),
                              ui_chinese_font_20(&lv_font_montserrat_16), 0);
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

static void desktop_back_cb(lv_event_t *event)
{
    if (weather_page_active && current_page == 1) weather_back_cb(event);
    else if (current_page != 0) animate_to_page(0);
}
static void wifi_password_back_cb(lv_event_t *event)
{
    (void)event;
    if (wifi_pwd_keyboard && !lv_obj_has_flag(wifi_pwd_keyboard, LV_OBJ_FLAG_HIDDEN))
        hide_text_keyboard(wifi_pwd_keyboard);
    else ui_screen_back(wifi_list_screen);
}
static void city_keyboard_back_cb(lv_event_t *event)
{
    (void)event;
    if (kb_keyboard && !lv_obj_has_flag(kb_keyboard, LV_OBJ_FLAG_HIDDEN)) city_keyboard_show(false);
    else ui_screen_back(kb_return_screen ? kb_return_screen : main_screen_ref);
}

bool custom_navigation_can_back(void)
{
    return lv_scr_act() != main_screen_ref || current_page != 0;
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
    create_gear_icon();
    create_wifi_icon();
    create_volume_icon();

    /* 保存主屏幕引用（陀螺仪界面返回用） */
    main_screen_ref = screen;
    ui_transition_set_desktop(screen);

    setup_home_page(pages[0]);
    weather_page_active = false;
    setup_icon_page(pages[1]);
    setup_extras_page(pages[2]);
    create_page_indicators(screen);

    /* 创建独立屏幕 */
    create_gyro_screen();
    create_keyboard_screen();
    create_settings_screen();
    create_wifi_list_screen();
    create_wifi_password_screen();
    create_brightness_screen();
    create_volume_screen();
    create_wallpaper_screen();
    alarm_ui_init(chinese_font_20);
    create_music_screen();
    create_photo_screen();
    create_photo_view_screen();
    create_video_screen();
    create_video_view_screen();

    edge_back_bind(main_screen_ref, desktop_back_cb, NULL);
    edge_back_bind(wifi_pwd_screen, wifi_password_back_cb, NULL);
    edge_back_bind(kb_screen, city_keyboard_back_cb, NULL);

    /* FPS 显示（右下角） */
    fps_label = lv_label_create(screen);
    lv_label_set_text(fps_label, "0 FPS");
    lv_obj_set_pos(fps_label, SCREEN_WIDTH - 60, SCREEN_HEIGHT - 25);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x00FF88), 0);

    /* FIX: 用 millis() 驱动时钟，不用 time()/localtime() */
    lv_timer_create(time_update, 1000, NULL);
    lv_timer_create(gyro_ui_update_timer_cb, 100, NULL);
    lv_timer_create(fps_update, 1000, NULL);
}

/* ==================== NTP TIME UPDATE ==================== */

void set_ntp_synced(void) {
    ntp_synced = true;
}

static void time_update(lv_timer_t * timer)
{
    if (weather_page_active && current_page == 1 && lv_scr_act() == main_screen_ref) refresh_weather_status();
    time_t now = time(NULL);

    if (now > 1000000000) {
        // NTP 已同步，使用真实时间
        struct tm tm_buf;
        localtime_r(&now, &tm_buf);

        update_digital_time(tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

        home_info_update(&tm_buf);
    } else {
        // NTP 未同步，显示等待
        update_digital_time(-1, -1, -1);
        home_info_update(NULL);
    }
}
