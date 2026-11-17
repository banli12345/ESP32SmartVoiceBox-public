#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* All calls require exclusive LVGL ownership (initialization or the GUI task).
 * Call after lv_init and display registration, before SD fonts.
 * Idle logo stays up until boot_splash_ready(); it does not time out.
 * Version argument is retained for compatibility; the footer displays 青禾. */
void boot_splash_start(const char *version);
void boot_splash_ready(void);
bool boot_splash_busy(void);
void boot_splash_cancel(void);

#ifdef __cplusplus
}
#endif

#endif
