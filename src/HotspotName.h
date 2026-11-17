#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hotspot_name_state {
    bool busy;
    int result;
    uint32_t revision;
    char name[33];
    char password[64];
    char message[128];
} hotspot_name_state_t;

bool hotspot_name_normalize(const char* text, char out[33]);
bool hotspot_password_valid(const char* password);
void hotspot_name_restore(const char* fallback, char out[33]);
void hotspot_password_restore(const char* fallback, char out[64]);
bool hotspot_name_request(const char* name);
bool hotspot_credentials_request(const char* name, const char* password);
void hotspot_name_poll(void);
void hotspot_name_get(hotspot_name_state_t* out);

#ifdef __cplusplus
}
#endif
