#ifndef WEB_AI_H
#define WEB_AI_H
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void web_ai_init(void);bool ai_chat_submit(const char*);bool ai_chat_is_busy(void);bool ai_audio_is_playing(void);bool ai_websocket_is_connected(void);int ai_chat_get_result(char*,size_t,char*,size_t);
#ifdef __cplusplus
}
#endif
#endif
