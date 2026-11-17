#ifndef WAV_PCM_H
#define WAV_PCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Companion clips are fixed by the converter. No firmware resampling. */
#define WAV_PCM_SAMPLE_RATE 44100u
#define WAV_PCM_CHANNELS 2u
#define WAV_PCM_BITS 16u
#define WAV_PCM_BLOCK_ALIGN 4u
#define WAV_PCM_BYTE_RATE 176400u

/* Picture may trail the soundtrack by about one render, then seek. */
#define VIDEO_SYNC_LATE_MS 150u
#define VIDEO_SYNC_LEAD_MS 100u

typedef enum wav_pcm_status {
    WAV_PCM_OK = 0,
    WAV_PCM_ERR_ARGUMENT = 1,
    WAV_PCM_ERR_TRUNCATED = 2,
    WAV_PCM_ERR_RIFF = 3,
    WAV_PCM_ERR_FORMAT = 4,
    WAV_PCM_ERR_NO_DATA = 5,
    WAV_PCM_ERR_OVERFLOW = 6,
    WAV_PCM_ERR_RANGE = 7
} wav_pcm_status_t;

typedef struct wav_pcm_info {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint32_t byte_rate;
    uint32_t data_offset;
    uint32_t data_bytes;
    uint32_t sample_frames;
} wav_pcm_info_t;

/* read returns len on success. seek uses an absolute offset and returns 0. */
typedef struct wav_pcm_reader {
    int (*read)(void *ctx, void *dst, uint32_t len);
    int (*seek)(void *ctx, uint32_t offset);
    void *ctx;
    uint32_t size;
    uint32_t pos;
} wav_pcm_reader_t;

typedef enum video_sync_action {
    VIDEO_SYNC_HOLD = 0,
    VIDEO_SYNC_PUBLISH = 1,
    VIDEO_SYNC_SEEK = 2
} video_sync_action_t;

typedef struct video_sync_input {
    uint32_t position_samples;
    uint32_t duration_samples;
    uint32_t audio_cycle;
    uint32_t fps_milli;
    uint32_t video_frames;
    int ready_valid;
    uint32_t ready_frame;
    uint32_t ready_cycle;
} video_sync_input_t;

typedef struct video_sync_decision {
    video_sync_action_t action;
    uint32_t next_frame;
    int32_t drift_ms;
} video_sync_decision_t;

wav_pcm_status_t wav_pcm_parse(wav_pcm_reader_t *reader, wav_pcm_info_t *info);
const char *wav_pcm_status_name(wav_pcm_status_t status);

/* Round the video duration to the nearest sample, ties upwards, like the converter. */
int wav_pcm_video_sample_frames(uint32_t video_frames, uint32_t fps_milli,
                                uint32_t *out_samples);
int wav_pcm_frame_index(uint32_t position_samples, uint32_t fps_milli,
                        uint32_t video_frames, uint32_t *out_frame);
int wav_pcm_frame_sample(uint32_t frame_index, uint32_t fps_milli,
                         uint32_t *out_sample);
int32_t wav_pcm_drift_ms(uint32_t audio_samples, uint32_t frame_samples);

/* /videos/name.rvf -> /videos/name.wav. Rejects other suffixes. */
int wav_pcm_companion_path(const char *rvf_path, char *out, size_t out_len);

video_sync_decision_t video_sync_decide(const video_sync_input_t *input);
/* In-flight decode is still worth finishing when it is not past the late bound. */
int video_sync_frame_useful(uint32_t position_samples, uint32_t frame_index,
                            uint32_t fps_milli, uint32_t video_frames);

#ifdef __cplusplus
}
#endif

#endif
