#include "browser_body.h"
#include <string.h>

enum { SIZE_LINE, SIZE_LF, DATA, DATA_CR, DATA_LF, TRAILER_LINE, TRAILER_LF };

static int hex_digit(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int chunk_size(browser_body_t *body)
{
    uint64_t value = 0;
    size_t i = 0;
    for (; i < body->line_length; ++i) {
        int digit = hex_digit((unsigned char)body->line[i]);
        if (digit < 0) break;
        if (value > (UINT64_MAX - (unsigned)digit) / 16) return 0;
        value = value * 16 + (unsigned)digit;
    }
    if (!i) return 0;
    while (i < body->line_length && (body->line[i] == ' ' || body->line[i] == '\t')) ++i;
    if (i < body->line_length && body->line[i] != ';') return 0;
    body->remaining = value;
    body->stage = value ? DATA : TRAILER_LINE;
    body->line_length = 0;
    return 1;
}

static int trailer_valid(const browser_body_t *body)
{
    size_t i;
    for (i = 0; i < body->line_length && body->line[i] != ':'; ++i) {
        unsigned char c = (unsigned char)body->line[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || strchr("!#$%&'*+-.^_`|~", c))) return 0;
    }
    return i != 0 && i < body->line_length;
}

void browser_body_init(browser_body_t *body, browser_body_mode_t mode, uint64_t length)
{
    if (!body) return;
    memset(body, 0, sizeof(*body));
    body->mode = mode;
    body->remaining = length;
    body->result = mode < BROWSER_BODY_FIXED || mode > BROWSER_BODY_CHUNKED ? BROWSER_BODY_INVALID :
                   (mode == BROWSER_BODY_FIXED && !length ? BROWSER_BODY_DONE : BROWSER_BODY_READING);
}

size_t browser_body_feed(browser_body_t *body, const uint8_t *bytes, size_t length,
                         browser_body_write_fn write, void *context)
{
    if (!body || body->result != BROWSER_BODY_READING) return 0;
    if ((!bytes && length) || !write) { body->result = BROWSER_BODY_INVALID; return 0; }
    size_t pos = 0;
    while (pos < length && body->result == BROWSER_BODY_READING) {
        if (body->mode != BROWSER_BODY_CHUNKED || body->stage == DATA) {
            size_t offer = length - pos;
            if (body->mode != BROWSER_BODY_CLOSE && (uint64_t)offer > body->remaining) offer = (size_t)body->remaining;
            const size_t used = write(context, bytes + pos, offer);
            if (used > offer) { body->result = BROWSER_BODY_INVALID; break; }
            pos += used;
            if (body->mode != BROWSER_BODY_CLOSE) body->remaining -= used;
            if (used != offer) { body->result = BROWSER_BODY_STOPPED; break; }
            if (body->mode == BROWSER_BODY_FIXED && !body->remaining) body->result = BROWSER_BODY_DONE;
            else if (body->mode == BROWSER_BODY_CHUNKED && !body->remaining) body->stage = DATA_CR;
            continue;
        }
        unsigned char c = bytes[pos++];
        if ((body->stage == TRAILER_LINE || body->stage == TRAILER_LF) && ++body->trailer_bytes > 2048) {
            body->result = BROWSER_BODY_INVALID; break;
        }
        switch (body->stage) {
        case SIZE_LINE:
        case TRAILER_LINE:
            if (c == '\r') body->stage = body->stage == SIZE_LINE ? SIZE_LF : TRAILER_LF;
            else if ((c < 32 && c != '\t') || c == 127 || body->line_length + 1 >= sizeof(body->line))
                body->result = BROWSER_BODY_INVALID;
            else body->line[body->line_length++] = (char)c;
            break;
        case SIZE_LF:
            if (c != '\n' || !chunk_size(body)) body->result = BROWSER_BODY_INVALID;
            break;
        case DATA_CR:
            if (c != '\r') body->result = BROWSER_BODY_INVALID;
            else body->stage = DATA_LF;
            break;
        case DATA_LF:
            if (c != '\n') body->result = BROWSER_BODY_INVALID;
            else body->stage = SIZE_LINE;
            break;
        case TRAILER_LF:
            if (c != '\n') body->result = BROWSER_BODY_INVALID;
            else if (!body->line_length) body->result = BROWSER_BODY_DONE;
            else if (!trailer_valid(body)) body->result = BROWSER_BODY_INVALID;
            else { body->line_length = 0; body->stage = TRAILER_LINE; }
            break;
        default:
            body->result = BROWSER_BODY_INVALID;
        }
    }
    return pos;
}

void browser_body_eof(browser_body_t *body)
{
    if (body && body->result == BROWSER_BODY_READING)
        body->result = body->mode == BROWSER_BODY_CLOSE ? BROWSER_BODY_DONE : BROWSER_BODY_INVALID;
}
