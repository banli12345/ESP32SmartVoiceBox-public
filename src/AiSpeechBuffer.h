#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Single-producer single-consumer byte ring. Capacity is usable in full.
   Indices are monotonic; the caller supplies the only producer and consumer. */
typedef struct AiSpeechRing AiSpeechRing;

AiSpeechRing *ai_speech_ring_create(size_t capacity);
void ai_speech_ring_destroy(AiSpeechRing *ring);
size_t ai_speech_ring_write(AiSpeechRing *ring, const uint8_t *src, size_t length);
size_t ai_speech_ring_read(AiSpeechRing *ring, uint8_t *dst, size_t length);
size_t ai_speech_ring_fill(const AiSpeechRing *ring);
size_t ai_speech_ring_peak(const AiSpeechRing *ring);
size_t ai_speech_ring_capacity(const AiSpeechRing *ring);

#ifdef __cplusplus
}
#endif
