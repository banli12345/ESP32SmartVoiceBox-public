/* Host checks for the companion WAV parser and the audio-master scheduler. */
#include "wav_pcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void expect_true(int cond, const char *name)
{
    if (cond) return;
    fprintf(stderr, "FAIL %s\n", name);
    ++failures;
}

typedef struct mem_file {
    uint8_t data[512];
    uint32_t size;
    uint32_t pos;
} mem_file_t;

static void mem_put(mem_file_t *file, const void *bytes, uint32_t len)
{
    if (file->size + len > sizeof(file->data)) {
        fprintf(stderr, "fixture overflow\n");
        exit(1);
    }
    memcpy(file->data + file->size, bytes, len);
    file->size += len;
}

static void mem_u16(mem_file_t *file, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    mem_put(file, bytes, 2);
}

static void mem_u32(mem_file_t *file, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    mem_put(file, bytes, 4);
}

static void mem_fourcc(mem_file_t *file, const char *tag)
{
    mem_put(file, tag, 4);
}

static int mem_read(void *ctx, void *dst, uint32_t len)
{
    mem_file_t *file = ctx;
    if (file->pos + len > file->size) return -1;
    memcpy(dst, file->data + file->pos, len);
    file->pos += len;
    return (int)len;
}

static int mem_seek(void *ctx, uint32_t offset)
{
    mem_file_t *file = ctx;
    if (offset > file->size) return -1;
    file->pos = offset;
    return 0;
}

static void write_fmt(mem_file_t *file, uint32_t chunk_size, uint16_t format,
                      uint16_t channels, uint32_t rate, uint32_t byte_rate,
                      uint16_t align, uint16_t bits)
{
    mem_fourcc(file, "fmt ");
    mem_u32(file, chunk_size);
    mem_u16(file, format);
    mem_u16(file, channels);
    mem_u32(file, rate);
    mem_u32(file, byte_rate);
    mem_u16(file, align);
    mem_u16(file, bits);
    if (chunk_size > 16) {
        uint32_t extra = chunk_size - 16;
        while (extra--) mem_put(file, "\0", 1);
    }
}

static void finish_riff(mem_file_t *file)
{
    uint32_t size = file->size - 8;
    file->data[4] = (uint8_t)size;
    file->data[5] = (uint8_t)(size >> 8);
    file->data[6] = (uint8_t)(size >> 16);
    file->data[7] = (uint8_t)(size >> 24);
}

static void begin_riff(mem_file_t *file)
{
    memset(file, 0, sizeof(*file));
    mem_fourcc(file, "RIFF");
    mem_u32(file, 0);
    mem_fourcc(file, "WAVE");
}

static wav_pcm_status_t parse_file(mem_file_t *file, wav_pcm_info_t *info)
{
    wav_pcm_reader_t reader;
    memset(&reader, 0, sizeof(reader));
    reader.read = mem_read;
    reader.seek = mem_seek;
    reader.ctx = file;
    reader.size = file->size;
    return wav_pcm_parse(&reader, info);
}

static void valid_pcm(mem_file_t *file, uint32_t data_bytes)
{
    begin_riff(file);
    write_fmt(file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(file, "data");
    mem_u32(file, data_bytes);
    while (data_bytes--) mem_put(file, "\0", 1);
    finish_riff(file);
}

static void test_parser(void)
{
    mem_file_t file;
    wav_pcm_info_t info;
    char path[64];

    valid_pcm(&file, 8);
    expect_true(parse_file(&file, &info) == WAV_PCM_OK, "minimal pcm");
    expect_true(info.data_bytes == 8 && info.sample_frames == 2, "minimal sizes");
    expect_true(info.sample_rate == 44100 && info.channels == 2, "minimal format");

    file.data[4] += 2;
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_TRUNCATED,
                "declared RIFF exceeds physical file");
    valid_pcm(&file, 8);
    mem_put(&file, "x", 1);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_TRUNCATED,
                "partial trailing chunk header");

    begin_riff(&file);
    write_fmt(&file, 18, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_OK, "extended fmt");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "JUNK");
    mem_u32(&file, 1);
    mem_put(&file, "Z", 1);
    mem_put(&file, "\0", 1);
    mem_fourcc(&file, "LIST");
    mem_u32(&file, 4);
    mem_put(&file, "INFO", 4);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_OK, "list and odd junk");
    expect_true(info.data_offset > 44 && info.sample_frames == 1, "data skips list");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "pad!");
    mem_u32(&file, 1);
    mem_put(&file, "Q", 1);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_TRUNCATED, "odd chunk requires padding");

    valid_pcm(&file, 4);
    file.size -= 2;
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_TRUNCATED, "truncated payload");

    begin_riff(&file);
    mem_fourcc(&file, "fmt ");
    mem_u32(&file, 0xffffffffu);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_OVERFLOW, "overflow chunk");

    begin_riff(&file);
    mem_put(&file, "WAVE", 4);
    finish_riff(&file);
    file.data[0] = 'X';
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_RIFF, "bad riff");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 1, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "mono");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 16000, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "16k");

    begin_riff(&file);
    write_fmt(&file, 16, 3, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "float");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176399, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "byte rate");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 2, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "align");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 24);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "bits");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 6);
    mem_put(&file, "abcdef", 6);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_RANGE, "unaligned data");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "efgh", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_RANGE, "duplicate data");

    begin_riff(&file);
    mem_fourcc(&file, "data");
    mem_u32(&file, 4);
    mem_put(&file, "abcd", 4);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_FORMAT, "no fmt");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_NO_DATA, "no data");

    begin_riff(&file);
    write_fmt(&file, 16, 1, 2, 44100, 176400, 4, 16);
    mem_fourcc(&file, "data");
    mem_u32(&file, 0);
    finish_riff(&file);
    expect_true(parse_file(&file, &info) == WAV_PCM_ERR_NO_DATA, "empty data");

    expect_true(wav_pcm_companion_path("/videos/ikun20.rvf", path, sizeof(path)) &&
                    strcmp(path, "/videos/ikun20.wav") == 0,
                "companion lower");
    expect_true(wav_pcm_companion_path("/videos/Foo.RVF", path, sizeof(path)) &&
                    strcmp(path, "/videos/Foo.wav") == 0,
                "companion upper");
    expect_true(!wav_pcm_companion_path("/videos/foo.mp4", path, sizeof(path)),
                "companion reject");
}

static void test_timing(void)
{
    uint32_t samples = 0;
    uint32_t frame = 99;
    uint32_t at = 99;
    video_sync_input_t input;
    video_sync_decision_t decision;

    expect_true(wav_pcm_video_sample_frames(663, 20000, &samples) && samples == 1461915u,
                "ikun20 duration");
    expect_true(wav_pcm_video_sample_frames(1, 24000, &samples) && samples == 1838u,
                "24fps rounds half sample upwards");
    expect_true(wav_pcm_video_sample_frames(1, 1001, &samples) && samples == 44056u,
                "fractional fps matches converter rounding");
    expect_true(!wav_pcm_video_sample_frames(UINT32_MAX, 1000, &samples),
                "duration overflow");
    expect_true(!wav_pcm_video_sample_frames(0, 20000, &samples), "zero frames");
    expect_true(!wav_pcm_video_sample_frames(10, 999, &samples), "fps too low");

    expect_true(wav_pcm_frame_sample(0, 20000, &at) && at == 0, "frame 0 sample");
    expect_true(wav_pcm_frame_sample(1, 20000, &at) && at == 2205u, "frame 1 sample");
    expect_true(wav_pcm_frame_index(0, 20000, 663, &frame) && frame == 0, "index 0");
    expect_true(wav_pcm_frame_index(2205u, 20000, 663, &frame) && frame == 1, "index 1");
    expect_true(wav_pcm_frame_index(1461915u - 1u, 20000, 663, &frame) && frame == 662u,
                "last frame");

    memset(&input, 0, sizeof(input));
    input.duration_samples = 1461915u;
    input.fps_milli = 20000;
    input.video_frames = 663;
    input.ready_valid = 1;
    input.ready_frame = 1;
    input.ready_cycle = 0;
    input.position_samples = 0;
    decision = video_sync_decide(&input);
    expect_true(decision.action == VIDEO_SYNC_HOLD && decision.drift_ms < 0, "hold early");

    input.position_samples = 2205u + 4410u;
    decision = video_sync_decide(&input);
    expect_true(decision.action == VIDEO_SYNC_PUBLISH && decision.drift_ms > 0 &&
                    decision.drift_ms <= 150,
                "publish inside bound");

    input.position_samples = 2205u + 44100u / 5u;
    decision = video_sync_decide(&input);
    expect_true(decision.action == VIDEO_SYNC_SEEK && decision.drift_ms > 150, "seek late");

    input.position_samples = 0;
    input.audio_cycle = 1;
    decision = video_sync_decide(&input);
    expect_true(decision.action == VIDEO_SYNC_SEEK && decision.next_frame == 0,
                "loop seeks audible frame");
    expect_true(video_sync_frame_useful(0, 1, 20000, 663), "early frame still useful");
    expect_true(!video_sync_frame_useful(2205u + 44100u, 1, 20000, 663),
                "very late frame is not useful");
}

/* Mirrors the firmware worker: 60 ms seek/read, 80 ms render, 5 ms poll. */
static void test_scheduler(void)
{
    const uint32_t frames = 663;
    const uint32_t fps = 20000;
    const uint32_t duration = 1461915u;
    const int read_ms = 60;
    const int render_ms = 80;
    const int end_ms = 33150 + 2500;
    uint32_t displayed = 0;
    uint32_t displayed_cycle = 0;
    int state = 0;
    uint32_t back_index = 0;
    uint32_t back_cycle = 0;
    int loading = 0;
    uint32_t loading_frame = 0;
    uint32_t loading_cycle = 0;
    int load_left = 0;
    int load_use_target = 0;
    uint32_t load_generation = 0;
    int target_valid = 0;
    uint32_t target_frame = 0;
    uint32_t target_cycle = 0;
    uint32_t generation = 0;
    uint32_t next_index = 1;
    int render_left = 0;
    int publishes = 0;
    int max_publish_drift = 0;
    int max_shown_drift = 0;
    int saw_cycle1 = 0;
    int t;

    for (t = 0; t <= end_ms; t += 5) {
        if (loading) {
            load_left -= 5;
            if (load_left <= 0) {
                const int stale = load_use_target &&
                                  (load_generation != generation || !target_valid ||
                                   target_frame != loading_frame);
                loading = 0;
                if (stale) {
                    state = 0;
                } else {
                    if (load_use_target && loading_frame + 1u < frames)
                        next_index = loading_frame + 1u;
                    back_index = loading_frame;
                    back_cycle = loading_cycle;
                    state = 1;
                }
            }
        }
        if (!loading && state != 1) {
            const int use_target = target_valid;
            uint32_t target = use_target ? target_frame : next_index;
            if (!use_target && target >= frames) target = 0;
            loading = 1;
            loading_frame = target;
            loading_cycle = use_target ? target_cycle : displayed_cycle;
            load_generation = generation;
            load_use_target = use_target;
            load_left = read_ms;
        }

        if (render_left > 0) {
            render_left -= 5;
            continue;
        }

        {
            const uint64_t abs_samples = (uint64_t)t * 44100ull / 1000ull;
            const uint32_t cycle = (uint32_t)(abs_samples / duration);
            const uint32_t position = (uint32_t)(abs_samples % duration);
            uint32_t shown_sample = 0;
            video_sync_input_t input;
            video_sync_decision_t decision;
            int64_t shown;

            if (wav_pcm_frame_sample(displayed, fps, &shown_sample)) {
                shown = ((int64_t)cycle * duration + position) -
                        ((int64_t)displayed_cycle * duration + shown_sample);
                shown = shown * 1000 / 44100;
                if (shown > max_shown_drift) max_shown_drift = (int)shown;
                if (shown > 400) {
                    fprintf(stderr, "FAIL shown drift %d ms at t=%d frame=%u\n",
                            (int)shown, t, displayed);
                    ++failures;
                    return;
                }
            }

            memset(&input, 0, sizeof(input));
            input.position_samples = position;
            input.duration_samples = duration;
            input.audio_cycle = cycle;
            input.fps_milli = fps;
            input.video_frames = frames;
            input.ready_valid = state == 1;
            input.ready_frame = back_index;
            input.ready_cycle = back_cycle;
            decision = video_sync_decide(&input);

            if (decision.action == VIDEO_SYNC_HOLD) continue;
            if (decision.action == VIDEO_SYNC_PUBLISH) {
                if (!(state == 1 && back_index == input.ready_frame &&
                      back_cycle == input.ready_cycle)) {
                    continue;
                }
                if (decision.drift_ms < 0 || decision.drift_ms > 150) {
                    fprintf(stderr, "FAIL publish drift %d at t=%d frame=%u\n",
                            decision.drift_ms, t, back_index);
                    ++failures;
                    return;
                }
                if (decision.drift_ms > max_publish_drift)
                    max_publish_drift = decision.drift_ms;
                displayed = back_index;
                displayed_cycle = back_cycle;
                state = 0;
                ++publishes;
                render_left = render_ms;
                if (displayed_cycle >= 1) saw_cycle1 = 1;
            } else if (loading && loading_cycle == cycle &&
                       video_sync_frame_useful(position, loading_frame, fps, frames)) {
                continue;
            }
            if (decision.action == VIDEO_SYNC_PUBLISH || decision.action == VIDEO_SYNC_SEEK) {
                uint32_t frame = decision.next_frame;
                int other_ready;
                int showing;
                int same;
                if (frame >= frames) frame = frames - 1u;
                other_ready = state == 1 && (back_index != frame || back_cycle != cycle);
                showing = frame == displayed && cycle == displayed_cycle;
                if (showing && !other_ready) {
                    target_valid = 0;
                } else {
                    same = target_valid && target_frame == frame && target_cycle == cycle &&
                           (loading || (state == 1 && back_index == frame && back_cycle == cycle));
                    if (!same) {
                        target_frame = frame;
                        target_cycle = cycle;
                        target_valid = 1;
                        ++generation;
                        if (other_ready) state = 0;
                    }
                }
            }
        }
    }

    expect_true(publishes > 200, "published enough frames");
    expect_true(max_publish_drift <= 150, "publish drift bound");
    expect_true(saw_cycle1, "loop advanced the picture");
    printf("scheduler publishes=%d max_publish_drift=%d max_shown_drift=%d\n",
           publishes, max_publish_drift, max_shown_drift);
}

int main(void)
{
    test_parser();
    test_timing();
    test_scheduler();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("wav_pcm host test passed\n");
    return 0;
}
