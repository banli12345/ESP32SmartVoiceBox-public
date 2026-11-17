#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AI_VOICE_STELLA = 0, AI_VOICE_TIDE = 1 } ai_voice_t;
typedef struct {
    ai_voice_t current;
    ai_voice_t requested;
    bool busy;
    bool failed;
} ai_voice_state_t;

void ai_voice_init(void);
/* Immutable literal, safe to snapshot once at the start of a TTS request. */
const char *ai_voice_get_id(void);
void ai_voice_get_state(ai_voice_state_t *state);
/* GUI queues a change; AppServices commits it before changing the active voice. */
bool ai_voice_request(ai_voice_t voice);
void ai_voice_poll(void);

#ifdef __cplusplus
}
#endif
