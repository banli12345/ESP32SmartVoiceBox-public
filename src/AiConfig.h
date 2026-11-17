#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char api_base[192];
    char api_key[256];
    char model[64];
    bool speak;
} ai_api_config_t;
void ai_config_init(void);
bool ai_config_get(ai_api_config_t *config);
bool ai_config_is_ready(void);
const char *ai_config_status(void);
bool ai_config_validate(const ai_api_config_t *config);
/* Persist only. Web changes become active together after reboot. */
bool ai_config_store(const ai_api_config_t *config);
bool ai_config_read_saved(ai_api_config_t *config);
#ifdef __cplusplus
}
#endif
