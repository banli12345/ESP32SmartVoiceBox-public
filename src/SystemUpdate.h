#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {int state;unsigned progress;bool available,busy;char current_version[32],candidate_version[32],message[192];} system_update_status_t;
void system_update_poll(bool boot_healthy);
void system_update_get_status(system_update_status_t *out);
bool system_update_check(void);
bool system_update_install(void);
bool system_update_reserve_upload(void);
void system_update_release_upload(void);
bool system_update_is_busy(void);
#ifdef __cplusplus
}
#endif
