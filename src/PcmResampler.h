#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*pcm_resampler_sink_t)(void *, const int16_t *, size_t frames);
typedef struct {
    uint64_t input_frames, output_frames, next_position;
    uint32_t rate;
    uint8_t channels;
    int16_t previous[2], *buffer;
    size_t buffer_frames, used;
    pcm_resampler_sink_t sink;
    void *user;
    bool failed, finished;
} pcm_resampler_t;
bool pcm_resampler_init(pcm_resampler_t *, uint32_t input_rate, uint8_t channels,
    int16_t *buffer, size_t buffer_frames, pcm_resampler_sink_t sink, void *user);
bool pcm_resampler_push(pcm_resampler_t *, const int16_t *pcm, size_t frames);
bool pcm_resampler_finish(pcm_resampler_t *);
#ifdef __cplusplus
}
#endif
