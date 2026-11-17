#include "AiVoice.h"
#include "Storage.h"
#include "freertos/FreeRTOS.h"

namespace {
portMUX_TYPE voice_mux = portMUX_INITIALIZER_UNLOCKED;
ai_voice_state_t state = {AI_VOICE_STELLA, AI_VOICE_STELLA, false, false};
bool committing;
}

extern "C" void ai_voice_init(void)
{
    uint8_t stored = AI_VOICE_STELLA;
    if (!storage_nvs_get_u8("ai_voice", "voice", &stored) || stored > AI_VOICE_TIDE)
        stored = AI_VOICE_STELLA;
    portENTER_CRITICAL(&voice_mux);
    state = {static_cast<ai_voice_t>(stored), static_cast<ai_voice_t>(stored), false, false};
    committing = false;
    portEXIT_CRITICAL(&voice_mux);
}

extern "C" const char *ai_voice_get_id(void)
{
    portENTER_CRITICAL(&voice_mux);
    const ai_voice_t voice = state.current;
    portEXIT_CRITICAL(&voice_mux);
    return voice == AI_VOICE_TIDE ? "tide" : "stella";
}

extern "C" void ai_voice_get_state(ai_voice_state_t *result)
{
    if (!result) return;
    portENTER_CRITICAL(&voice_mux);
    *result = state;
    portEXIT_CRITICAL(&voice_mux);
}

extern "C" bool ai_voice_request(ai_voice_t voice)
{
    if (voice != AI_VOICE_STELLA && voice != AI_VOICE_TIDE) return false;
    portENTER_CRITICAL(&voice_mux);
    const bool accepted = !state.busy;
    if (accepted) {
        state.requested = voice;
        state.failed = false;
        state.busy = voice != state.current;
    }
    portEXIT_CRITICAL(&voice_mux);
    return accepted;
}

extern "C" void ai_voice_poll(void)
{
    portENTER_CRITICAL(&voice_mux);
    if (!state.busy || committing) {
        portEXIT_CRITICAL(&voice_mux);
        return;
    }
    const ai_voice_t requested = state.requested;
    committing = true;
    portEXIT_CRITICAL(&voice_mux);
    const bool saved = storage_nvs_set_u8("ai_voice", "voice", static_cast<uint8_t>(requested));
    portENTER_CRITICAL(&voice_mux);
    if (saved) state.current = requested;
    state.busy = false;
    state.failed = !saved;
    committing = false;
    portEXIT_CRITICAL(&voice_mux);
}
