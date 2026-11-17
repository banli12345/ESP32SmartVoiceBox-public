#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Serializes final library files and catalog publication with deletion. */
bool music_library_try_lock(void);
void music_library_unlock(void);
#ifdef __cplusplus
}
#endif
