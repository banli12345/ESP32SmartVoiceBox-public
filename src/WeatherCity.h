#ifndef WEATHER_CITY_H
#define WEATHER_CITY_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trim, bound, and validate a location query.
 * Returns 0, or 3 when the text is empty, too long, not UTF-8,
 * contains controls, or contains URL/query injection characters.
 * Slash is kept so official admin paths can match. */
int weather_city_check_input(const char *input, char *output, size_t output_cap);

/* ASCII pinyin, English name, or provider id. Callers must store these
 * unchanged instead of rewriting them to a V3 id. */
bool weather_city_is_legacy_token(const char *text);

/* Official domestic table, kept in flash rodata.
 * 0 unique: id receives the 12-character V3 id and name the short label.
 * 1 the short name is shared by more than one id.
 * 2 no match. 3 the buffers cannot hold the result. */
int weather_city_lookup(const char *query, char *id, size_t id_cap,
                        char *name, size_t name_cap);

#ifdef __cplusplus
}
#endif

#endif
