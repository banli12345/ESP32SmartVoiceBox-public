#include "browser_local_url.h"
#include "browser_document.h"
#include <ctype.h>
#include <string.h>

static bool equal_host(const char *host, size_t length, const char *expected)
{
    if (!expected || !expected[0] || !strcmp(expected, "0.0.0.0") || strlen(expected) != length) return false;
    for (size_t i = 0; i < length; ++i) {
        if (tolower((unsigned char)host[i]) != tolower((unsigned char)expected[i])) return false;
    }
    return true;
}

bool browser_is_local_music_url(const char *input, const char *ap_ip, const char *sta_ip)
{
    if (!input) return false;
    while (isspace((unsigned char)*input)) ++input;
    size_t length = strlen(input);
    while (length && isspace((unsigned char)input[length - 1])) --length;
    if (!length || length >= BROWSER_URL_CAP) return false;
    const char *end = input + length;
    const char *host = input;
    if (length >= 7 && tolower((unsigned char)input[0]) == 'h' &&
        tolower((unsigned char)input[1]) == 't' && tolower((unsigned char)input[2]) == 't' &&
        tolower((unsigned char)input[3]) == 'p' && !memcmp(input + 4, "://", 3)) host += 7;
    else if (strstr(input, "://")) return false;
    const char *cursor = host;
    while (cursor < end && *cursor != ':' && *cursor != '/' && *cursor != '?' && *cursor != '#') {
        if (isspace((unsigned char)*cursor) || *cursor == '@' || *cursor == '\\' || *cursor == '%') return false;
        ++cursor;
    }
    const size_t host_length = (size_t)(cursor - host);
    if (!equal_host(host, host_length, "localhost") && !equal_host(host, host_length, "127.0.0.1") &&
        !equal_host(host, host_length, ap_ip) && !equal_host(host, host_length, sta_ip)) return false;
    if (cursor < end && *cursor == ':') {
        if (end - cursor < 3 || cursor[1] != '8' || cursor[2] != '0') return false;
        cursor += 3;
    }
    if (cursor < end && *cursor == '/') ++cursor;
    return cursor == end || *cursor == '?' || *cursor == '#';
}
