#include "Mp3Source.h"
#include "Storage.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MINIMP3_NO_SIMD
#define MINIMP3_ONLY_MP3
#define MINIMP3_IMPLEMENTATION
/* Playback and WAV export own separate instances. Each instance has its own
 * PSRAM scratch, so simultaneous decoding cannot corrupt the other's state. */
static void *source_scratch(void *decoder);
#define MINIMP3_SCRATCH_POINTER(decoder) source_scratch(decoder)
#include "minimp3.h"

enum { INPUT_BYTES = 16384, MAX_JUNK_BYTES = 128 * 1024 };
struct mp3_source {
    FILE *file;
    mp3dec_t decoder;
    mp3dec_scratch_t scratch;
    uint8_t input[INPUT_BYTES];
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t used, junk;
    bool eof, decoded;
    int terminal; /* 2 while active; terminal values match read_frame. */
};

static void *source_scratch(void *decoder)
{
    mp3_source_t *source = (mp3_source_t *)((uint8_t *)decoder - offsetof(mp3_source_t, decoder));
    return &source->scratch;
}

void mp3_source_close(mp3_source_t *source)
{
    if (!source) return;
    if (source->file) fclose(source->file);
    heap_caps_free(source);
}

mp3_source_t *mp3_source_open(const char *logical_path)
{
    char path[320];
    if (!logical_path || !*logical_path || !storage_vfs_path(logical_path, path, sizeof(path))) return NULL;
    mp3_source_t *source = (mp3_source_t *)heap_caps_calloc(1, sizeof(*source), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!source) return NULL;
    source->file = fopen(path, "rb");
    if (!source->file) { mp3_source_close(source); return NULL; }
    source->terminal = 2;
    mp3dec_init(&source->decoder);
    uint8_t header[10];
    const size_t got = fread(header, 1, sizeof(header), source->file);
    if (!got || ferror(source->file)) { mp3_source_close(source); return NULL; }
    long begin = 0;
    if (got == sizeof(header) && !memcmp(header, "ID3", 3)) {
        if ((header[6] | header[7] | header[8] | header[9]) & 0x80) { mp3_source_close(source); return NULL; }
        const uint32_t bytes = (uint32_t)header[6] << 21 | (uint32_t)header[7] << 14 |
                               (uint32_t)header[8] << 7 | header[9];
        begin = 10 + (long)bytes + ((header[3] == 4 && (header[5] & 0x10)) ? 10 : 0);
        if (fseek(source->file, 0, SEEK_END) != 0 || ftell(source->file) <= begin) { mp3_source_close(source); return NULL; }
    }
    if (fseek(source->file, begin, SEEK_SET) != 0) { mp3_source_close(source); return NULL; }
    return source;
}

int mp3_source_read_frame(mp3_source_t *source, mp3_source_frame_t *frame)
{
    if (!frame) return -1;
    memset(frame, 0, sizeof(*frame));
    if (!source) return -1;
    if (source->terminal != 2) return source->terminal;
    for (;;) {
        if (!source->eof && source->used < INPUT_BYTES) {
            const size_t got = fread(source->input + source->used, 1, INPUT_BYTES - source->used, source->file);
            source->used += got;
            if (ferror(source->file)) return source->terminal = -1;
            if (!got || feof(source->file)) source->eof = true;
        }
        if (!source->used) return source->terminal = source->decoded ? 0 : -1;
        mp3dec_frame_info_t info = {0};
        const int samples = mp3dec_decode_frame(&source->decoder, source->input,
                                                (int)source->used, source->pcm, &info);
        size_t consumed = info.frame_bytes > 0 ? (size_t)info.frame_bytes : 0;
        if (consumed > source->used) return source->terminal = -1;
        if (!consumed) {
            if (source->eof) return source->terminal = source->decoded ? 0 : -1;
            if (source->used < INPUT_BYTES) continue;
            /* A full input window with no decoder progress is malformed. */
            return source->terminal = -1;
        }
        memmove(source->input, source->input + consumed, source->used - consumed);
        source->used -= consumed;
        if (samples > 0) {
            if (samples > 1152 || (info.channels != 1 && info.channels != 2) ||
                info.hz < 8000 || info.hz > 48000) return source->terminal = -1;
            source->decoded = true;
            source->junk = 0;
            frame->pcm = source->pcm;
            frame->frames = (uint16_t)samples;
            frame->channels = (uint8_t)info.channels;
            frame->sample_rate = (uint32_t)info.hz;
            return 1;
        }
        source->junk += consumed;
        if (source->junk > MAX_JUNK_BYTES) return source->terminal = -1;
    }
}
