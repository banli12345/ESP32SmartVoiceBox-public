#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Match only the local music homepage. Other sites remain text-browser pages. */
bool browser_is_local_music_url(const char *input, const char *ap_ip, const char *sta_ip);
#ifdef __cplusplus
}
#endif
