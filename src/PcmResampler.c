#include "PcmResampler.h"
#include <string.h>

enum { OUTPUT_RATE = 44100 };
static bool flush(pcm_resampler_t *r)
{
    if (!r->used) return true;
    if (!r->sink(r->user, r->buffer, r->used)) {r->failed = true; return false;}
    r->used = 0; return true;
}
static bool emit(pcm_resampler_t *r, int16_t left, int16_t right)
{
    if (r->output_frames == UINT64_MAX || r->next_position > UINT64_MAX - r->rate) {r->failed = true; return false;}
    r->buffer[r->used * 2] = left; r->buffer[r->used * 2 + 1] = right;
    ++r->used; ++r->output_frames; r->next_position += r->rate;
    return r->used < r->buffer_frames || flush(r);
}
bool pcm_resampler_init(pcm_resampler_t *r, uint32_t rate, uint8_t channels,
    int16_t *buffer, size_t buffer_frames, pcm_resampler_sink_t sink, void *user)
{
    if (!r) return false;
    memset(r, 0, sizeof(*r)); r->failed = true;
    if (rate < 8000 || rate > 48000 || (channels != 1 && channels != 2) ||
        !buffer || !buffer_frames || buffer_frames > SIZE_MAX / (2 * sizeof(int16_t)) || !sink) return false;
    r->rate = rate; r->channels = channels; r->buffer = buffer; r->buffer_frames = buffer_frames;
    r->sink = sink; r->user = user; r->failed = false; return true;
}
bool pcm_resampler_push(pcm_resampler_t *r, const int16_t *pcm, size_t frames)
{
    if (!r || r->failed || r->finished) return false;
    if ((!pcm && frames) || frames > SIZE_MAX / r->channels || r->input_frames > UINT64_MAX / OUTPUT_RATE ||
        frames > UINT64_MAX / OUTPUT_RATE - r->input_frames) {r->failed = true; return false;}
    for (size_t i = 0; i < frames; ++i) {
        const int16_t sample[2] = {pcm[i * r->channels], pcm[i * r->channels + r->channels - 1]};
        if (r->rate == OUTPUT_RATE) {
            ++r->input_frames;
            r->previous[0] = sample[0]; r->previous[1] = sample[1];
            if (!emit(r, sample[0], sample[1])) return false;
            continue;
        }
        if (!r->input_frames) {
            r->previous[0] = sample[0]; r->previous[1] = sample[1];
            ++r->input_frames;
            if (!emit(r, sample[0], sample[1])) return false;
            continue;
        }
        /* Positions are exact rational input-sample coordinates: k*rate/44100.
         * Keep the preceding sample across pushes for chunk-independent output. */
        const uint64_t right = r->input_frames * OUTPUT_RATE;
        const uint64_t left = right - OUTPUT_RATE;
        while (r->next_position <= right) {
            const uint32_t fraction = (uint32_t)(r->next_position - left);
            int16_t output[2];
            for (unsigned c = 0; c < 2; ++c) {
                const int32_t difference = (int32_t)sample[c] - r->previous[c];
                /* 65535*44100+22050 fits uint32_t. Unsigned magnitude avoids
                 * expensive software 64-bit division on ESP32, with exactly
                 * the same symmetric rounding as signed rational division. */
                const uint32_t magnitude = (uint32_t)(difference < 0 ? -difference : difference);
                const int32_t delta = (int32_t)((magnitude * fraction + OUTPUT_RATE / 2) / OUTPUT_RATE);
                const int32_t interpolated = r->previous[c] + (difference < 0 ? -delta : delta);
                output[c] = (int16_t)interpolated;
            }
            if (!emit(r, output[0], output[1])) return false;
        }
        r->previous[0] = sample[0]; r->previous[1] = sample[1]; ++r->input_frames;
    }
    return true;
}
bool pcm_resampler_finish(pcm_resampler_t *r)
{
    if (!r || r->failed) return false;
    if (r->finished) return true;
    if (!r->input_frames) {r->failed = true; return false;}
    /* N samples cover N/rate seconds. Hold only the last sample's remaining
     * interval, producing exactly ceil(N*44100/rate) output frames. */
    const uint64_t end = r->input_frames * OUTPUT_RATE;
    while (r->next_position < end) if (!emit(r, r->previous[0], r->previous[1])) return false;
    if (!flush(r)) return false;
    r->finished = true; return true;
}
