#ifndef SOUND_H
#define SOUND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void sound_init(void);
bool sound_is_ready(void);
void music_audio_play(void);
void music_audio_pause(void);
void music_audio_stop(void);
bool music_audio_is_playing(void);
/* Queue only publishes flags; caller wakes after releasing its own lock. */
bool music_audio_queue_online(const char *path, const char *title, uint32_t job, bool paused);
void music_audio_discard_online_queue(void);
void music_audio_wake(void);
bool music_audio_is_online(void);
bool music_audio_online_uses_path(const char *path);
/* Copies the path the audio task is actually playing or paused on. A queued replacement is not included. */
bool music_audio_copy_active_online_path(char *path, size_t size);
void music_audio_copy_title(char *text, size_t size);
uint8_t get_music_track_count(void);
uint8_t get_music_track_index(void);
uint8_t get_music_play_mode(void);
void set_music_play_mode(uint8_t);
const char *get_music_track_name(uint8_t);
void music_select_track(uint8_t);
bool music_select_library_track(uint8_t index, uint32_t generation);
int music_next_local_track(int index, int direction);
/* Nonblocking request, serviced by the audio owner. Status: 1 busy, 2 done, -1 failed. */
bool music_delete_track(uint8_t index, uint32_t generation);
int music_delete_status(void);
uint32_t get_music_elapsed_ms(void);
void play_alarm_wav(void);
void sound_alarm_start(uint16_t minutes);
void sound_alarm_stop(void);
bool sound_alarm_is_active(void);
uint8_t get_audio_volume(void);
void set_audio_volume(int);
void save_audio_volume(void);
void sound_pause_music_for_ai_audio(void);
bool sound_ai_output_begin(void);
bool sound_ai_output_begin_rate(uint32_t sample_rate);
bool sound_ai_output_write(int16_t *, size_t);
bool sound_ai_output_finish(bool);
bool sound_ai_audio_is_playing(void);

/* Companion WAV uses the music I2S channel. Missing or bad audio stays silent. */
bool video_audio_prepare(const char *rvf_path, uint32_t frame_count, uint32_t fps_milli);
bool video_audio_start(void);
void video_audio_halt(void);
void video_audio_release(void);
bool video_audio_is_active(void);
void video_audio_get_clock(uint32_t *position_samples, uint32_t *duration_samples,
                           uint32_t *cycle, uint32_t *underruns);
#ifdef __cplusplus
}
#endif
#endif
