#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct mp3_source mp3_source_t;
typedef struct {
    const int16_t *pcm;
    uint32_t sample_rate;
    uint16_t frames; /* Samples per channel. */
    uint8_t channels;
} mp3_source_frame_t;
/* Each instance has one task owner; separate instances may decode concurrently.
 * PCM is valid until the next read/close of that instance.
 * read_frame returns 1 for PCM, 0 for EOF, -1 for invalid/truncated input. */
mp3_source_t *mp3_source_open(const char *logical_path);
int mp3_source_read_frame(mp3_source_t *source, mp3_source_frame_t *frame);
void mp3_source_close(mp3_source_t *source);
#ifdef __cplusplus
}
#endif
