#ifndef SDCAR_H
#define SDCAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl.h"
#include "lv_ime_pinyin.h"

#ifdef __cplusplus
extern "C" {
#endif

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

bool sdcar_init(void);
bool sdcar_is_ready(void);
void sdcar_init_fonts(void);
void sdcar_publish_status(void);
void sdcar_load_wallpaper_cache(void);
void sdcar_load_wallpaper_choice(void);

lv_pinyin_dict_t *get_sd_pinyin_dict(void);

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
void close_video_file(void);
bool open_video_file(int index);
const char *get_video_path(int index);
const lv_img_dsc_t *get_video_frame_image(void);
uint32_t get_video_frame_delay_ms(void);
uint32_t get_video_frame_count(void);
// Consumes a prefetched frame without waiting; keeps the old image if not ready.
// Falls back to synchronous reads if the prefetch task could not be created.
bool read_next_video_frame(void);
// Audio is the master clock. Publishes one due frame or seeks past obsolete ones.
bool video_present_for_audio(uint32_t position_samples, uint32_t duration_samples,
                             uint32_t cycle, uint32_t fps_milli);
void video_sync_cancel(void);
// Monotonic publication/read counters are retained across open/close cycles.
uint32_t get_video_frame_sequence(void);
uint32_t get_video_source_fps_milli(void);
void get_video_read_metrics(uint32_t *frames, uint32_t *total_ms, uint32_t *max_ms);
void get_video_sync_diag(uint32_t *displayed_frame, int32_t *drift_ms);

const lv_img_dsc_t *get_wallpaper_image(void);
const lv_img_dsc_t *get_wallpaper_choice_image(int choice);
const char *get_wallpaper_choice_path(int choice);
const char *get_wallpaper_path(void);
void set_wallpaper_choice(int choice);

void sdcar_scan_music_tracks(void);
uint8_t sdcar_get_music_track_count(void);
const char *sdcar_get_music_track_name(uint8_t index);
const char *sdcar_get_music_track_path(uint8_t index);
const char *get_music_lyric(uint32_t position_ms);

bool sdcar_open_music_wav(uint8_t track_index);
bool sdcar_music_file_is_open(void);
size_t sdcar_read_music(uint8_t *buffer, size_t capacity);
void sdcar_close_music(void);
bool sdcar_open_alarm_wav(void);
bool sdcar_alarm_file_is_open(void);
size_t sdcar_read_alarm(uint8_t *buffer, size_t capacity);
void sdcar_close_alarm(void);

#ifdef __cplusplus
}
#endif

#endif
