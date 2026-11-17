#include "video_wav_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode) & S_IFMT) == S_IFREG)
#endif
#else
#include <unistd.h>
#endif

void video_wav_reader_close(video_wav_reader_t *reader)
{
    if (!reader) return;
    if (reader->fd >= 0) close(reader->fd);
    reader->fd = -1;
    reader->position = 0;
    reader->size = 0;
}

int video_wav_reader_open(video_wav_reader_t *reader, const char *path)
{
    struct stat info;
    int flags = O_RDONLY;
    int saved_errno;
    if (!reader || !path) { errno = EINVAL; return -1; }
    video_wav_reader_close(reader);
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
    reader->fd = open(path, flags);
    if (reader->fd < 0) return -1;
    if (fstat(reader->fd, &info) != 0) {
        saved_errno = errno;
        video_wav_reader_close(reader);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        (uint64_t)info.st_size > UINT32_MAX) {
        video_wav_reader_close(reader);
        errno = EINVAL;
        return -1;
    }
    reader->size = (uint32_t)info.st_size;
    return 0;
}

int video_wav_reader_read(void *ctx, void *dst, uint32_t len)
{
    video_wav_reader_t *reader = (video_wav_reader_t *)ctx;
    int got;
    if (!reader || reader->fd < 0 || !dst || len > INT_MAX) {
        errno = EINVAL;
        return -1;
    }
    if (len > reader->size - reader->position)
        len = reader->size - reader->position;
    if (!len) return 0;
    got = (int)read(reader->fd, dst, len);
    if (got > 0) reader->position += (uint32_t)got;
    return got;
}

int video_wav_reader_seek(void *ctx, uint32_t offset)
{
    video_wav_reader_t *reader = (video_wav_reader_t *)ctx;
    off_t target = (off_t)offset;
    if (!reader || reader->fd < 0 || offset > reader->size ||
        target < 0 || (uint64_t)target != offset) {
        errno = EINVAL;
        return -1;
    }
    if (reader->position == offset) return 0;
    if (lseek(reader->fd, target, SEEK_SET) != target) return -1;
    reader->position = offset;
    return 0;
}
