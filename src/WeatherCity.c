#include "WeatherCity.h"

#include <stdint.h>
#include <string.h>

#include "weather_city_data.inc"

static int utf8_width(const unsigned char *text, size_t available, size_t *width)
{
    unsigned char lead;
    size_t need;
    size_t index;
    if (!text || !width || available == 0) return 0;
    lead = text[0];
    if (lead < 0x80) {
        *width = 1;
        return lead >= 32 && lead != 127;
    }
    if ((lead & 0xE0) == 0xC0 && lead >= 0xC2) need = 2;
    else if ((lead & 0xF0) == 0xE0) need = 3;
    else if ((lead & 0xF8) == 0xF0 && lead <= 0xF4) need = 4;
    else return 0;
    if (need > available) return 0;
    for (index = 1; index < need; ++index) {
        if ((text[index] & 0xC0) != 0x80) return 0;
    }
    if (need == 3 && ((lead == 0xE0 && text[1] < 0xA0) || (lead == 0xED && text[1] >= 0xA0))) return 0;
    if (need == 4 && ((lead == 0xF0 && text[1] < 0x90) || (lead == 0xF4 && text[1] >= 0x90))) return 0;
    *width = need;
    return 1;
}

static int rejected_ascii(unsigned char value)
{
    return value < 32 || value == 127 || value == '&' || value == '?' || value == '#' ||
           value == '%' || value == '\\' || value == '"' || value == '\'' || value == '<' ||
           value == '>' || value == ' ';
}

int weather_city_check_input(const char *input, char *output, size_t output_cap)
{
    const unsigned char *start;
    const unsigned char *end;
    const unsigned char *cursor;
    size_t length;
    if (!input || !output || output_cap < 2) return 3;
    start = (const unsigned char *)input;
    end = start + strlen(input);
    while (start < end && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r' ||
                           *start == '\v' || *start == '\f')) {
        ++start;
    }
    while (start + 3 <= end && start[0] == 0xE3 && start[1] == 0x80 && start[2] == 0x80) start += 3;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r' ||
                           end[-1] == '\v' || end[-1] == '\f')) {
        --end;
    }
    while (end >= start + 3 && end[-3] == 0xE3 && end[-2] == 0x80 && end[-1] == 0x80) end -= 3;
    length = (size_t)(end - start);
    if (length == 0 || length + 1 > output_cap || length > 95) return 3;
    cursor = start;
    while (cursor < end) {
        size_t width = 0;
        if (!utf8_width(cursor, (size_t)(end - cursor), &width)) return 3;
        if (width == 1 && rejected_ascii(*cursor)) return 3;
        cursor += width;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    return 0;
}

bool weather_city_is_legacy_token(const char *text)
{
    size_t length = 0;
    const unsigned char *cursor;
    if (!text || !text[0]) return false;
    for (cursor = (const unsigned char *)text; *cursor; ++cursor) {
        unsigned char value = *cursor;
        int ok = (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
                 (value >= '0' && value <= '9') || value == '_' || value == '-' || value == '.';
        if (!ok || length >= 31) return false;
        ++length;
    }
    return true;
}

/* Rebuild one alias from its block checkpoint. At most one block (16 keys)
 * is walked, into the caller’s 96-byte stack buffer. Returns 0 on a bad
 * offset, length, prefix, or city row. */
static int weather_city_key_at(size_t index, char *text, size_t text_cap, uint16_t *city_out)
{
    size_t block;
    size_t slot;
    size_t step;
    size_t cursor;
    size_t length = 0;
    uint16_t city = 0;
    if (!text || text_cap < 2 || !city_out || index >= weather_city_key_count) return 0;
    if (weather_city_key_block == 0 || weather_city_key_packed_size > 0xFFFFFFFFu) return 0;
    block = index / weather_city_key_block;
    slot = index % weather_city_key_block;
    if (block >= weather_city_key_checkpoint_count) return 0;
    cursor = weather_city_key_checkpoint[block];
    if (cursor > weather_city_key_packed_size) return 0;
    for (step = 0; step <= slot; ++step) {
        size_t prefix;
        size_t suffix;
        size_t record;
        if (cursor > weather_city_key_packed_size || weather_city_key_packed_size - cursor < 4) return 0;
        prefix = weather_city_key_packed[cursor];
        suffix = weather_city_key_packed[cursor + 1];
        record = 4u + suffix;
        if (prefix > length || prefix + suffix >= text_cap) return 0;
        if (weather_city_key_packed_size - cursor < record) return 0;
        memcpy(text + prefix, weather_city_key_packed + cursor + 2, suffix);
        length = prefix + suffix;
        text[length] = '\0';
        city = (uint16_t)weather_city_key_packed[cursor + 2 + suffix] |
               ((uint16_t)weather_city_key_packed[cursor + 3 + suffix] << 8);
        if (city >= weather_city_count) return 0;
        cursor += record;
    }
    *city_out = city;
    return 1;
}

int weather_city_lookup(const char *query, char *id, size_t id_cap, char *name, size_t name_cap)
{
    char probe[96];
    size_t lower = 0;
    size_t upper;
    size_t index;
    const char *matched = NULL;
    uint16_t best = 0;
    int best_rank = 256;
    if (!query || !query[0] || !id || id_cap < 13 || !name || name_cap == 0) return 3;
    upper = weather_city_key_count;
    while (lower < upper) {
        size_t mid = lower + (upper - lower) / 2;
        uint16_t ignored = 0;
        int compared;
        if (!weather_city_key_at(mid, probe, sizeof probe, &ignored)) return 3;
        compared = strcmp(query, probe);
        if (compared > 0) lower = mid + 1;
        else upper = mid;
    }
    for (index = lower; index < weather_city_key_count; ++index) {
        uint16_t city = 0;
        int rank;
        if (!weather_city_key_at(index, probe, sizeof probe, &city)) return 3;
        if (strcmp(query, probe) != 0) break;
        if (!matched) {
            matched = weather_city_id[city];
            best = city;
            best_rank = weather_city_rank[city];
            continue;
        }
        if (strcmp(matched, weather_city_id[city]) != 0) return 1;
        rank = weather_city_rank[city];
        if (rank < best_rank || (rank == best_rank && city < best)) {
            best = city;
            best_rank = rank;
        }
    }
    if (!matched) return 2;
    if (strlen(weather_city_name[best]) + 1 > name_cap) return 3;
    memcpy(id, matched, 13);
    memcpy(name, weather_city_name[best], strlen(weather_city_name[best]) + 1);
    return 0;
}
