#pragma once
#include "AiConfig.h"
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool ai_direct_speak(const ai_api_config_t *config, const char *text,
                     char *error, size_t error_capacity);
void ai_speech_cancel(void);
typedef enum { AI_SPEECH_IDLE, AI_SPEECH_CACHING, AI_SPEECH_PLAYING } ai_speech_phase_t;
typedef struct {
    ai_speech_phase_t phase;
    uint32_t cached_bytes, expected_bytes;
    /* Cache task only: entry through flush. Playback keeps the frozen values. IDLE is zero. */
    uint32_t cache_elapsed_ms;
    uint32_t cache_bytes_per_second;
} ai_speech_status_t;
void ai_speech_get_status(ai_speech_status_t *status);
/* Boot cleanup of a cache left by a power loss; refuses an active request. */
bool ai_speech_clear_cache(void);
#ifdef __cplusplus
}
#endif
