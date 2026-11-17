#ifndef QUICK_BLUETOOTH_H
#define QUICK_BLUETOOTH_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Driver work runs on AppServices. A stop timeout retains resources safely.
bool quick_bluetooth_driver_set(bool enabled, bool *connected, char *detail, size_t detail_len);
bool quick_bluetooth_is_enabled(void);
bool quick_bluetooth_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
