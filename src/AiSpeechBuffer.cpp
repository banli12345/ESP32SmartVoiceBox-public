#include "AiSpeechBuffer.h"
#include "esp_heap_caps.h"
#include <atomic>
#include <cstring>
#include <new>

struct AiSpeechRing {
    uint8_t *data;
    size_t capacity;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    std::atomic<size_t> peak;
};

extern "C" AiSpeechRing *ai_speech_ring_create(size_t capacity)
{
    if (!capacity) return nullptr;
    void *memory = heap_caps_malloc(sizeof(AiSpeechRing), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    auto *ring = static_cast<AiSpeechRing *>(memory);
    if (!ring) return nullptr;
    uint8_t *data = static_cast<uint8_t *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) {
        heap_caps_free(ring);
        return nullptr;
    }
    ring->data = data;
    ring->capacity = capacity;
    new (&ring->head) std::atomic<size_t>(0);
    new (&ring->tail) std::atomic<size_t>(0);
    new (&ring->peak) std::atomic<size_t>(0);
    return ring;
}

extern "C" void ai_speech_ring_destroy(AiSpeechRing *ring)
{
    if (!ring) return;
    ring->head.~atomic();
    ring->tail.~atomic();
    ring->peak.~atomic();
    heap_caps_free(ring->data);
    heap_caps_free(ring);
}

extern "C" size_t ai_speech_ring_write(AiSpeechRing *ring, const uint8_t *src, size_t length)
{
    if (!ring || !src || !length) return 0;
    const size_t tail = ring->tail.load(std::memory_order_relaxed);
    const size_t head = ring->head.load(std::memory_order_acquire);
    const size_t space = ring->capacity - (tail - head);
    if (length > space) length = space;
    if (!length) return 0;
    const size_t index = tail % ring->capacity;
    const size_t first = length < ring->capacity - index ? length : ring->capacity - index;
    memcpy(ring->data + index, src, first);
    if (length > first) memcpy(ring->data, src + first, length - first);
    ring->tail.store(tail + length, std::memory_order_release);
    const size_t fill = tail + length - head;
    size_t peak = ring->peak.load(std::memory_order_relaxed);
    while (fill > peak && !ring->peak.compare_exchange_weak(peak, fill, std::memory_order_relaxed)) {}
    return length;
}

extern "C" size_t ai_speech_ring_read(AiSpeechRing *ring, uint8_t *dst, size_t length)
{
    if (!ring || !dst || !length) return 0;
    const size_t head = ring->head.load(std::memory_order_relaxed);
    const size_t tail = ring->tail.load(std::memory_order_acquire);
    size_t have = tail - head;
    if (length > have) length = have;
    if (!length) return 0;
    const size_t index = head % ring->capacity;
    const size_t first = length < ring->capacity - index ? length : ring->capacity - index;
    memcpy(dst, ring->data + index, first);
    if (length > first) memcpy(dst + first, ring->data, length - first);
    ring->head.store(head + length, std::memory_order_release);
    return length;
}

extern "C" size_t ai_speech_ring_fill(const AiSpeechRing *ring)
{
    if (!ring) return 0;
    const size_t head = ring->head.load(std::memory_order_acquire);
    const size_t tail = ring->tail.load(std::memory_order_acquire);
    return tail - head;
}

extern "C" size_t ai_speech_ring_peak(const AiSpeechRing *ring)
{
    return ring ? ring->peak.load(std::memory_order_relaxed) : 0;
}

extern "C" size_t ai_speech_ring_capacity(const AiSpeechRing *ring)
{
    return ring ? ring->capacity : 0;
}
