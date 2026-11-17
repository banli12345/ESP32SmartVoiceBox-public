#pragma once
#include "AiConfig.h"
#ifdef __cplusplus
extern "C" {
#endif
bool ai_direct_chat(const ai_api_config_t *config, const char *prompt,
                    char *reply, size_t reply_capacity, char *error, size_t error_capacity);
bool ai_direct_chat_context(const ai_api_config_t *config, const char *prompt,
                    const char *previous_question, const char *previous_answer,
                    char *reply, size_t reply_capacity, char *error, size_t error_capacity);
#ifdef __cplusplus
}
#endif
