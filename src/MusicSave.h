#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char wav_path[64], lrc_path[64];
    uint64_t pcm_bytes;
    bool reused, lyrics_saved;
} music_save_result_t;
typedef bool (*music_save_current_t)(uint32_t job);
typedef void (*music_save_progress_t)(uint32_t job, uint64_t pcm_bytes);
/* One export worker owns the staging files. Complete user files are never
 * replaced. Cancellation removes only these private, unfinished staging files. */
bool music_save_mp3(const char *mp3_path, const char *title, const char *artist,
                    const char *song_id, const char *lyrics, uint32_t job,
                    music_save_current_t current, music_save_progress_t progress,
                    music_save_result_t *result, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
