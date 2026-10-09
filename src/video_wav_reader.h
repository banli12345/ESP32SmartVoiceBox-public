#ifndef VIDEO_WAV_READER_H
#define VIDEO_WAV_READER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One owner at a time: the audio task hands this reader to the prefetch task
 * and joins that task before seeking or closing it. No stdio read buffer. */
typedef struct video_wav_reader {
    int fd;
    uint32_t position;
    uint32_t size;
} video_wav_reader_t;

#define VIDEO_WAV_READER_INIT { -1, 0, 0 }

/* path is the mounted VFS path, e.g. /videos/ikun20.wav. */
int video_wav_reader_open(video_wav_reader_t *reader, const char *path);
void video_wav_reader_close(video_wav_reader_t *reader);
int video_wav_reader_read(void *ctx, void *dst, uint32_t len);
int video_wav_reader_seek(void *ctx, uint32_t offset);

#ifdef __cplusplus
}
#endif

#endif
