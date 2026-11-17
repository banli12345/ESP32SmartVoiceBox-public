#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI/AI entry points only publish work; HTTP and SD downloads run on a worker. */
bool online_music_handle_command(const char *text);
/* save_to_library is retained for callers; playback always uses temporary cache.
 * Permanent saving is requested only by online_music_download_current(). */
bool online_music_handle_web_command(const char *text, bool save_to_library);
/* Explicit save of the song the audio task is playing or paused on. Never starts playback. */
bool online_music_can_download_current(void);
bool online_music_download_current(void);
typedef struct {char id[24], title[128], artist[160];} online_music_song_t;
unsigned online_music_copy_songs(online_music_song_t *songs, unsigned capacity);
bool online_music_is_preparing(void);
int online_music_saved_track(char *path, size_t capacity);
void online_music_service_poll(void);
void online_music_cancel_pending(void);
uint32_t online_music_get_result(char *reply, size_t reply_size);
/* MusicAudioTask reports actual decoder/output errors here. */
void online_music_playback_error(uint32_t job, const char *message);
void online_music_note_pause(bool paused);
/* Local browser upload participates in the same cancellation generation. */
uint32_t online_music_begin_upload(void);
bool online_music_job_current(uint32_t job);
bool online_music_finish_upload(uint32_t job, const char *path, const char *title);
void online_music_abort_upload(uint32_t job, const char *message);
#ifdef __cplusplus
}
#endif
