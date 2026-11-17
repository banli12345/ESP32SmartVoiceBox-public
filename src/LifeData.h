#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    char date[11];
    char lunar_month[24];
    char lunar_day[20];
    char zodiac[16];
    char solar_term[32];
    char festival[128];
} life_calendar_day_t;
typedef struct {
    char date[11];
    char city[32];
    char brief[48];
} life_dressing_t;
typedef struct {
    char date[11];
    char city[32];
    char dressing[48];
    char uv[48];
    char flu[48];
    char sport[48];
} life_indices_t;
/* Init after SD/NVS restoration, before GUI and service tasks start. */
void life_data_init(const char *seniverse_key, const char *city,
                    const char *endpoint, bool force_refresh);
void life_data_set_city(const char *city);
/* AppServices calls this; network and SD work stays in a background worker. */
void life_data_poll(void);
/* Compatibility hints: local conversion needs no prefetch. */
void life_data_request_month(int year, int month);
void life_data_request_day(int year, int month, int day);
/* Pure local date conversion; dressing reads a copied snapshot.
 * Neither getter performs HTTP/SD access or waits on a worker. */
bool life_data_get_day(int year, int month, int day, life_calendar_day_t *out);
bool life_data_get_dressing(life_dressing_t *out);
/* Missing individual indices are empty; available fields are never fabricated. */
bool life_data_get_indices(life_indices_t *out);
void life_data_calendar_status(char *out, size_t capacity);
uint32_t life_data_revision(void);
#ifdef __cplusplus
}
#endif
