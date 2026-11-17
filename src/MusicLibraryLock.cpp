#include "MusicLibraryLock.h"
#include <atomic>
static std::atomic_flag library_writer = ATOMIC_FLAG_INIT;
bool music_library_try_lock(void) { return !library_writer.test_and_set(std::memory_order_acquire); }
void music_library_unlock(void) { library_writer.clear(std::memory_order_release); }
