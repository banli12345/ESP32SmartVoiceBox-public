#include "Mp3Source.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int fail_mp3_alloc;
bool storage_vfs_path(const char *path, char *out, size_t capacity)
{
    return path && snprintf(out, capacity, "%s", path) >= 0 && strlen(path) < capacity;
}
static void decode(const char *path)
{
    mp3_source_t *source = mp3_source_open(path);
    assert(source);
    mp3_source_frame_t frame;
    unsigned frames = 0, nonzero = 0;
    unsigned long long samples = 0;
    int result;
    while ((result = mp3_source_read_frame(source, &frame)) == 1) {
        assert(frame.pcm && frame.frames > 0 && frame.frames <= 1152);
        assert(frame.channels == 1 || frame.channels == 2);
        assert(frame.sample_rate >= 8000 && frame.sample_rate <= 48000);
        for (unsigned i = 0; i < frame.frames * frame.channels; ++i) if (frame.pcm[i]) ++nonzero;
        samples += frame.frames;
        ++frames;
    }
    assert(result == 0 && frames > 100 && samples > 441000 && nonzero > 1000);
    assert(mp3_source_read_frame(source, &frame) == 0 && !frame.pcm);
    assert(mp3_source_read_frame(source, NULL) == -1);
    mp3_source_close(source);
    printf("MP3 decoded: %u frames, %llu samples/channel, %u nonzero samples\n", frames, samples, nonzero);
}
int main(int argc, char **argv)
{
    assert(argc == 5);
    decode(argv[1]);
    decode(argv[4]); /* Large ID3 tag precedes the same real MPEG stream. */
    assert(!mp3_source_open("this-file-does-not-exist.mp3"));
    assert(!mp3_source_open(NULL));
    assert(!mp3_source_open(""));
    assert(!mp3_source_open(argv[2]));
    mp3_source_t *source = mp3_source_open(argv[3]);
    assert(source);
    mp3_source_frame_t frame;
    assert(mp3_source_read_frame(source, &frame) == -1 && !frame.pcm);
    assert(mp3_source_read_frame(source, &frame) == -1);
    mp3_source_close(source);
    fail_mp3_alloc = 1;
    assert(!mp3_source_open(argv[1]));
    mp3_source_close(NULL);
    puts("MP3 source: real decode, ID3, EOF, missing/empty/junk and OOM passed");
    return 0;
}
