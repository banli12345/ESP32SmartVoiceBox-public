#include "MusicSave.h"
#include "Mp3Source.h"
#include "PcmResampler.h"
#include "Storage.h"
#include "IdfRuntime.h"
#include "esp_heap_caps.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {
constexpr const char *WAV_PART = "/.online/save.wav.part";
constexpr const char *LRC_PART = "/.online/save.lrc.part";
constexpr uint64_t RESERVE = 1024u * 1024u, MAX_PCM = 256u * 1024u * 1024u;
void put16(uint8_t *p, uint16_t n) {p[0] = n; p[1] = n >> 8;}
void put32(uint8_t *p, uint32_t n) {put16(p, n); put16(p + 2, n >> 16);}
uint32_t get32(const uint8_t *p) {return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;}
void header(uint8_t *p, uint32_t bytes)
{
    memset(p, 0, 44); memcpy(p, "RIFF", 4); put32(p + 4, bytes + 36);
    memcpy(p + 8, "WAVEfmt ", 8); put32(p + 16, 16); put16(p + 20, 1); put16(p + 22, 2);
    put32(p + 24, 44100); put32(p + 28, 176400); put16(p + 32, 4); put16(p + 34, 16);
    memcpy(p + 36, "data", 4); put32(p + 40, bytes);
}
bool existing_wav(const char *path, uint64_t *bytes)
{
    StorageStream file; uint8_t h[44];
    if (!file.open(path, "rb") || file.read(h, sizeof(h)) != sizeof(h)) return false;
    const uint32_t data = get32(h + 40);
    if (!data || data % 4 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVEfmt ", 8) ||
        get32(h + 16) != 16 || h[20] != 1 || h[21] || h[22] != 2 || h[23] ||
        get32(h + 24) != 44100 || get32(h + 28) != 176400 || h[32] != 4 || h[33] ||
        h[34] != 16 || h[35] || memcmp(h + 36, "data", 4) ||
        get32(h + 4) != (uint64_t)data + 36 || file.byte_count() != (uint64_t)data + 44) return false;
    *bytes = data; return true;
}
size_t clean_name(const char *text, char *out, size_t limit)
{
    size_t used = 0;
    const auto *p = reinterpret_cast<const unsigned char *>(text ? text : "");
    while (*p && used < limit) {
        size_t n = *p < 128 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 : *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
        bool valid = n != 0;
        for (size_t i = 1; valid && i < n; ++i) if (!p[i] || (p[i] & 0xc0) != 0x80) valid = false;
        if (valid && ((n == 3 && ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] >= 0xa0))) ||
            (n == 4 && ((*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90))))) valid = false;
        if (!valid) {out[used++] = '_'; ++p; continue;}
        if (used + n > limit) break;
        if (n == 1 && (*p < 32 || *p == 127 || strchr("/\\:*?\"<>|.", *p))) out[used++] = '_';
        else {memcpy(out + used, p, n); used += n;}
        p += n;
    }
    while (used && out[used - 1] == ' ') --used;
    out[used] = 0; return used;
}
bool paths(const char *title, const char *artist, const char *id, music_save_result_t *result)
{
    if (!id || !*id || strlen(id) > 20) return false;
    for (const char *p = id; *p; ++p) if (*p < '0' || *p > '9') return false;
    char name[53], singer[25];
    clean_name(artist, singer, 24);
    const size_t budget = 52 - (singer[0] ? strlen(singer) + 3 : 0);
    if (!clean_name(title, name, budget)) strlcpy(name, "song", sizeof(name));
    const int n = snprintf(result->wav_path, sizeof(result->wav_path), "/music/%s%s%s.wav", name, singer[0] ? " - " : "", singer);
    if (n < 0 || n >= (int)sizeof(result->wav_path)) return false;
    strlcpy(result->lrc_path, result->wav_path, sizeof(result->lrc_path));
    memcpy(result->lrc_path + strlen(result->lrc_path) - 3, "lrc", 3); return true;
}
bool timed_lyrics(const char *lyrics)
{
    if (!lyrics || strlen(lyrics) > 65536) return false;
    for (const char *p = lyrics; *p; ++p) {
        if (*p != '[') continue;
        const char *q = p + 1; unsigned digits = 0;
        while (*q >= '0' && *q <= '9' && digits < 4) {++digits; ++q;}
        if (digits && *q == ':' && q[1] >= '0' && q[1] <= '5' && q[2] >= '0' && q[2] <= '9' &&
            (q[3] == '.' || q[3] == ':' || q[3] == ']')) return true;
    }
    return false;
}
struct Sink {
    StorageStream *file;
    uint64_t bytes, limit, noted;
    uint32_t job;
    music_save_current_t current;
    music_save_progress_t progress;
    bool failed;
};
bool pcm_sink(void *context, const int16_t *samples, size_t frames)
{
    auto *sink = static_cast<Sink *>(context);
    const uint64_t bytes = (uint64_t)frames * 4;
    if ((sink->current && !sink->current(sink->job)) || bytes > sink->limit - sink->bytes ||
        sink->file->write(samples, bytes) != bytes) {sink->failed = true; return false;}
    sink->bytes += bytes;
    if (sink->progress && sink->bytes - sink->noted >= 1024u * 1024u) {
        sink->noted = sink->bytes; sink->progress(sink->job, sink->bytes);
    }
    return true;
}
}

extern "C" bool music_save_mp3(const char *mp3_path, const char *title, const char *artist,
    const char *song_id, const char *lyrics, uint32_t job, music_save_current_t current,
    music_save_progress_t progress, music_save_result_t *result, char *error, size_t capacity)
{
    if (error && capacity) *error = 0;
    auto fail = [&](const char *text) {if (error && capacity) strlcpy(error, text, capacity);};
    auto active = [&]() {return !current || current(job);};
    if (!result) {fail("保存参数无效"); return false;}
    *result = {};
    if (!mp3_path || !paths(title, artist, song_id, result) || !active() ||
        !storage_mkdir("/music") || !storage_mkdir("/.online")) {fail("无法创建保存目录或任务已取消"); return false;}
    storage_remove(WAV_PART); storage_remove(LRC_PART);
    bool ok = true;
    if (storage_exists(result->wav_path)) {
        result->reused = existing_wav(result->wav_path, &result->pcm_bytes);
        if (!result->reused) {fail("同名文件已存在，已保留原文件"); return false;}
    } else {
        const uint64_t available = storage_free_bytes();
        if (available <= RESERVE + 44) {fail("SD 卡空间不足"); return false;}
        mp3_source_t *decoder = mp3_source_open(mp3_path);
        auto *buffer = static_cast<int16_t *>(heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        StorageStream file; uint8_t h[44]; header(h, 0);
        ok = decoder && buffer && file.open(WAV_PART, "wb+");
        if (ok) {file.use_buffer(32768); ok = file.write(h, 44) == 44;}
        Sink sink = {&file, 0, std::min<uint64_t>(MAX_PCM, available - RESERVE - 44), 0, job, current, progress, false};
        pcm_resampler_t resampler = {};
        bool started = false; uint32_t rate = 0; uint8_t channels = 0;
        int status = -1; unsigned frames = 0; const uint32_t began = app_millis();
        while (ok && active() && app_millis() - began < 900000) {
            mp3_source_frame_t frame = {};
            status = mp3_source_read_frame(decoder, &frame);
            if (status <= 0) break;
            if (!started) {
                rate = frame.sample_rate; channels = frame.channels;
                started = pcm_resampler_init(&resampler, rate, channels, buffer, 1024, pcm_sink, &sink);
                if (!started) {ok = false; break;}
            }
            if (rate != frame.sample_rate || channels != frame.channels || !pcm_resampler_push(&resampler, frame.pcm, frame.frames)) {ok = false; break;}
            if (!(++frames % 8)) app_delay_ms(1);
        }
        ok = ok && active() && status == 0 && started && pcm_resampler_finish(&resampler) && sink.bytes > 0 && !sink.failed;
        if (ok) {header(h, sink.bytes); ok = file.seek(0) && file.write(h, 44) == 44 && file.flush();}
        file.close(); mp3_source_close(decoder); free(buffer);
        result->pcm_bytes = sink.bytes;
        if (!ok) fail(!active() ? "WAV 保存已取消" : "WAV 转换失败：音频格式、SD 写入或空间不足");
    }
    bool have_staged_lyrics = false;
    if (ok && active() && timed_lyrics(lyrics) && !storage_exists(result->lrc_path)) {
        StorageStream file;
        const size_t length = strlen(lyrics);
        have_staged_lyrics = file.open(LRC_PART, "wb") && file.write(lyrics, length) == length && file.flush();
        if (!have_staged_lyrics) fail("WAV 已完成，歌词写入失败");
    }
    ok = ok && active();
    if (ok && !result->reused) {
        ok = !storage_exists(result->wav_path) && storage_rename(WAV_PART, result->wav_path);
        if (!ok) fail("WAV 提交失败，原有文件已保留");
    }
    if (ok && have_staged_lyrics && active() && !storage_exists(result->lrc_path)) {
        if (!storage_rename(LRC_PART, result->lrc_path)) fail("WAV 已保存，歌词提交失败");
    }
    result->lyrics_saved = ok && storage_is_file(result->lrc_path);
    storage_remove(WAV_PART); storage_remove(LRC_PART);
    printf("[MUSIC SAVE] ok=%d reused=%d pcm_bytes=%llu lyrics=%d\n", ok, result->reused,
        (unsigned long long)result->pcm_bytes, result->lyrics_saved);
    return ok;
}
