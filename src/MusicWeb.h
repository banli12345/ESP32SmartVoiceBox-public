#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Called only by AppServices; server exists only while the hotspot is on. */
void music_web_service_poll(void);
bool music_web_is_running(void);
#ifdef __cplusplus
}
#endif
