#pragma once
#include <cstdlib>
enum { MALLOC_CAP_SPIRAM = 1, MALLOC_CAP_8BIT = 2 };
inline void *heap_caps_malloc(size_t size, unsigned) { return std::malloc(size); }
inline void *heap_caps_calloc(size_t count, size_t size, unsigned) { return std::calloc(count, size); }
inline void heap_caps_free(void *ptr) { std::free(ptr); }
