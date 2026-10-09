#include "wav_pcm.h"

#include <string.h>

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int pull(wav_pcm_reader_t *reader, void *dst, uint32_t len)
{
    if (!reader->read || reader->pos > reader->size || len > reader->size - reader->pos) return -1;
    if (reader->read(reader->ctx, dst, len) != (int)len) return -1;
    reader->pos += len;
    return 0;
}

static int skip_to(wav_pcm_reader_t *reader, uint32_t offset)
{
    if (!reader->seek || offset > reader->size || offset < reader->pos) return -1;
    if (offset == reader->pos) return 0;
    if (reader->seek(reader->ctx, offset) != 0) return -1;
    reader->pos = offset;
    return 0;
}

const char *wav_pcm_status_name(wav_pcm_status_t status)
{
    switch (status) {
    case WAV_PCM_OK: return "ok";
    case WAV_PCM_ERR_ARGUMENT: return "argument";
    case WAV_PCM_ERR_TRUNCATED: return "truncated";
    case WAV_PCM_ERR_RIFF: return "riff";
    case WAV_PCM_ERR_FORMAT: return "format";
    case WAV_PCM_ERR_NO_DATA: return "no-data";
    case WAV_PCM_ERR_OVERFLOW: return "overflow";
    case WAV_PCM_ERR_RANGE: return "range";
    default: return "unknown";
    }
}

wav_pcm_status_t wav_pcm_parse(wav_pcm_reader_t *reader, wav_pcm_info_t *info)
{
    uint8_t hdr[12];
    uint32_t riff_size;
    uint32_t container_end;
    int saw_fmt = 0;
    int saw_data = 0;
    wav_pcm_info_t parsed;

    if (!reader || !info || !reader->read || !reader->seek || reader->size < 12)
        return WAV_PCM_ERR_ARGUMENT;
    reader->pos = 0;
    if (reader->seek(reader->ctx, 0) != 0) return WAV_PCM_ERR_TRUNCATED;

    memset(&parsed, 0, sizeof(parsed));
    if (pull(reader, hdr, 12) != 0) return WAV_PCM_ERR_TRUNCATED;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
        return WAV_PCM_ERR_RIFF;

    riff_size = rd_le32(hdr + 4);
    if (riff_size > UINT32_MAX - 8u) return WAV_PCM_ERR_OVERFLOW;
    container_end = 8u + riff_size;
    if (container_end > reader->size) return WAV_PCM_ERR_TRUNCATED;
    if (container_end < reader->pos) return WAV_PCM_ERR_TRUNCATED;

    while (container_end - reader->pos >= 8u) {
        uint8_t chunk[8];
        uint32_t chunk_size;
        uint32_t payload;
        uint32_t skip;
        int pad;

        if (pull(reader, chunk, 8) != 0) return WAV_PCM_ERR_TRUNCATED;
        chunk_size = rd_le32(chunk + 4);
        if (chunk_size > UINT32_MAX - reader->pos) return WAV_PCM_ERR_OVERFLOW;
        payload = reader->pos;
        if (payload + chunk_size > container_end) return WAV_PCM_ERR_TRUNCATED;
        pad = (chunk_size & 1u) ? 1 : 0;
        skip = payload + chunk_size;
        if (pad) {
            if (skip + 1u < skip) return WAV_PCM_ERR_OVERFLOW;
            if (skip + 1u <= container_end) skip += 1u;
            else return WAV_PCM_ERR_TRUNCATED;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (saw_fmt) return WAV_PCM_ERR_FORMAT;
            if (chunk_size < 16u || chunk_size > 1024u) return WAV_PCM_ERR_FORMAT;
            if (pull(reader, fmt, 16) != 0) return WAV_PCM_ERR_TRUNCATED;
            if (rd_le16(fmt) != 1u ||
                rd_le16(fmt + 2) != WAV_PCM_CHANNELS ||
                rd_le32(fmt + 4) != WAV_PCM_SAMPLE_RATE ||
                rd_le32(fmt + 8) != WAV_PCM_BYTE_RATE ||
                rd_le16(fmt + 12) != WAV_PCM_BLOCK_ALIGN ||
                rd_le16(fmt + 14) != WAV_PCM_BITS) {
                return WAV_PCM_ERR_FORMAT;
            }
            parsed.sample_rate = WAV_PCM_SAMPLE_RATE;
            parsed.channels = WAV_PCM_CHANNELS;
            parsed.bits_per_sample = WAV_PCM_BITS;
            parsed.block_align = WAV_PCM_BLOCK_ALIGN;
            parsed.byte_rate = WAV_PCM_BYTE_RATE;
            saw_fmt = 1;
            if (skip_to(reader, skip) != 0) return WAV_PCM_ERR_TRUNCATED;
        } else if (memcmp(chunk, "data", 4) == 0) {
            if (saw_data) return WAV_PCM_ERR_RANGE;
            if (chunk_size == 0) return WAV_PCM_ERR_NO_DATA;
            if ((chunk_size % WAV_PCM_BLOCK_ALIGN) != 0) return WAV_PCM_ERR_RANGE;
            parsed.data_offset = payload;
            parsed.data_bytes = chunk_size;
            parsed.sample_frames = chunk_size / WAV_PCM_BLOCK_ALIGN;
            saw_data = 1;
            if (skip_to(reader, skip) != 0) return WAV_PCM_ERR_TRUNCATED;
        } else {
            /* LIST and any other chunk are metadata, never samples. */
            if (skip_to(reader, skip) != 0) return WAV_PCM_ERR_TRUNCATED;
        }
    }

    if (reader->pos != container_end) return WAV_PCM_ERR_TRUNCATED;
    if (!saw_fmt) return WAV_PCM_ERR_FORMAT;
    if (!saw_data) return WAV_PCM_ERR_NO_DATA;
    if (parsed.data_offset + parsed.data_bytes > reader->size)
        return WAV_PCM_ERR_TRUNCATED;
    *info = parsed;
    return WAV_PCM_OK;
}

int wav_pcm_video_sample_frames(uint32_t video_frames, uint32_t fps_milli,
                                uint32_t *out_samples)
{
    uint64_t ticks;
    if (!out_samples || video_frames == 0 || fps_milli < 1000u || fps_milli > 30000u)
        return 0;
    ticks = (uint64_t)video_frames * (uint64_t)WAV_PCM_SAMPLE_RATE * 1000ull;
    ticks = (ticks + fps_milli / 2u) / fps_milli;
    if (ticks == 0 || ticks > 0xffffffffull) return 0;
    *out_samples = (uint32_t)ticks;
    return 1;
}

int wav_pcm_frame_index(uint32_t position_samples, uint32_t fps_milli,
                        uint32_t video_frames, uint32_t *out_frame)
{
    uint64_t frame;
    if (!out_frame || video_frames == 0 || fps_milli == 0) return 0;
    frame = (uint64_t)position_samples * (uint64_t)fps_milli /
            ((uint64_t)WAV_PCM_SAMPLE_RATE * 1000ull);
    if (frame >= video_frames) frame = (uint64_t)video_frames - 1ull;
    *out_frame = (uint32_t)frame;
    return 1;
}

int wav_pcm_frame_sample(uint32_t frame_index, uint32_t fps_milli,
                         uint32_t *out_sample)
{
    uint64_t sample;
    if (!out_sample || fps_milli == 0) return 0;
    sample = (uint64_t)frame_index * (uint64_t)WAV_PCM_SAMPLE_RATE * 1000ull /
             fps_milli;
    if (sample > 0xffffffffull) return 0;
    *out_sample = (uint32_t)sample;
    return 1;
}

int32_t wav_pcm_drift_ms(uint32_t audio_samples, uint32_t frame_samples)
{
    int64_t delta = (int64_t)audio_samples - (int64_t)frame_samples;
    return (int32_t)((delta * 1000) / (int64_t)WAV_PCM_SAMPLE_RATE);
}

int wav_pcm_companion_path(const char *rvf_path, char *out, size_t out_len)
{
    size_t len;
    char a, b, c;
    if (!rvf_path || !out || out_len < 6) return 0;
    len = strlen(rvf_path);
    if (len < 5 || len >= out_len) return 0;
    a = rvf_path[len - 3];
    b = rvf_path[len - 2];
    c = rvf_path[len - 1];
    if (rvf_path[len - 4] != '.' ||
        (a != 'r' && a != 'R') || (b != 'v' && b != 'V') ||
        (c != 'f' && c != 'F')) {
        return 0;
    }
    memcpy(out, rvf_path, len - 4);
    memcpy(out + len - 4, ".wav", 5);
    return 1;
}

static uint32_t lead_position(uint32_t position, uint32_t duration)
{
    uint32_t lead = (uint32_t)(((uint64_t)VIDEO_SYNC_LEAD_MS * WAV_PCM_SAMPLE_RATE) / 1000ull);
    if (duration == 0) return position;
    if (position >= duration) position %= duration;
    if (duration - position <= lead) return duration - 1u;
    return position + lead;
}

video_sync_decision_t video_sync_decide(const video_sync_input_t *input)
{
    video_sync_decision_t decision;
    uint32_t position;
    uint32_t frame_sample = 0;
    uint32_t lead_frame = 0;
    uint32_t now_frame = 0;

    decision.action = VIDEO_SYNC_SEEK;
    decision.next_frame = 0;
    decision.drift_ms = 0;
    if (!input || input->video_frames == 0 || input->fps_milli == 0 ||
        input->duration_samples == 0) {
        return decision;
    }

    position = input->position_samples;
    if (position >= input->duration_samples) position %= input->duration_samples;
    if (!wav_pcm_frame_index(lead_position(position, input->duration_samples),
                             input->fps_milli, input->video_frames, &lead_frame)) {
        return decision;
    }
    if (!wav_pcm_frame_index(position, input->fps_milli, input->video_frames, &now_frame))
        return decision;

    if (!input->ready_valid || input->ready_cycle != input->audio_cycle ||
        input->ready_frame >= input->video_frames) {
        decision.action = VIDEO_SYNC_SEEK;
        /* A new soundtrack cycle starts at the audible frame, not a lead frame. */
        decision.next_frame = (input->ready_valid && input->ready_cycle != input->audio_cycle)
                                  ? now_frame
                                  : lead_frame;
        return decision;
    }

    if (!wav_pcm_frame_sample(input->ready_frame, input->fps_milli, &frame_sample))
        return decision;
    decision.drift_ms = wav_pcm_drift_ms(position, frame_sample);
    if (decision.drift_ms < 0) {
        decision.action = VIDEO_SYNC_HOLD;
        decision.next_frame = input->ready_frame;
        return decision;
    }
    if ((uint32_t)decision.drift_ms <= VIDEO_SYNC_LATE_MS) {
        decision.action = VIDEO_SYNC_PUBLISH;
        decision.next_frame = lead_frame;
        if (decision.next_frame <= input->ready_frame) {
            if (input->ready_frame + 1u < input->video_frames)
                decision.next_frame = input->ready_frame + 1u;
            else
                decision.next_frame = input->ready_frame;
        }
        return decision;
    }

    decision.action = VIDEO_SYNC_SEEK;
    decision.next_frame = lead_frame;
    return decision;
}

int video_sync_frame_useful(uint32_t position_samples, uint32_t frame_index,
                            uint32_t fps_milli, uint32_t video_frames)
{
    uint32_t frame_sample = 0;
    int32_t drift;
    if (video_frames == 0 || frame_index >= video_frames) return 0;
    if (!wav_pcm_frame_sample(frame_index, fps_milli, &frame_sample)) return 0;
    drift = wav_pcm_drift_ms(position_samples, frame_sample);
    return drift <= (int32_t)VIDEO_SYNC_LATE_MS;
}
