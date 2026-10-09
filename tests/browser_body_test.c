#include "browser_body.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct { uint8_t before[16], bytes[4096], after[16]; size_t length, limit; } output;
static size_t write_bytes(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    size_t size = length < output.limit - output.length ? length : output.limit - output.length;
    memcpy(output.bytes + output.length, bytes, size);
    output.length += size;
    return size;
}
static void reset(void)
{
    memset(&output, 0xa5, sizeof(output)); output.length = 0; output.limit = sizeof(output.bytes);
}
static void guards(void)
{
    for (unsigned i = 0; i < 16; ++i) { assert(output.before[i] == 0xa5); assert(output.after[i] == 0xa5); }
}
static browser_body_result_t decode(const char *wire, size_t length, unsigned split)
{
    browser_body_t body;
    reset(); browser_body_init(&body, BROWSER_BODY_CHUNKED, 0);
    for (size_t pos = 0; pos < length && body.result == BROWSER_BODY_READING;) {
        size_t count = length - pos < split ? length - pos : split;
        size_t used = browser_body_feed(&body, (const uint8_t *)wire + pos, count, write_bytes, NULL);
        assert(used <= count && (used != 0 || body.result != BROWSER_BODY_READING)); pos += used;
    }
    browser_body_eof(&body); guards(); return body.result;
}
static void valid(void)
{
    const char wire[] = "3;ext=\"a;b\"\r\n\xe4\xb8\xad\r\n4\r\nA\0B\n\r\n0\r\nX-Read: done\r\n\r\nignored";
    const char expected[] = "\xe4\xb8\xad" "A\0B\n";
    for (unsigned split = 1; split <= 31; ++split) {
        assert(decode(wire, sizeof(wire) - 1, split) == BROWSER_BODY_DONE);
        assert(output.length == sizeof(expected) - 1);
        assert(!memcmp(output.bytes, expected, sizeof(expected) - 1));
        assert(decode("0\r\n\r\n", 5, split) == BROWSER_BODY_DONE && output.length == 0);
    }
    browser_body_t body;
    reset(); browser_body_init(&body, BROWSER_BODY_FIXED, 3);
    assert(browser_body_feed(&body, (const uint8_t *)"abcignored", 10, write_bytes, NULL) == 3);
    assert(body.result == BROWSER_BODY_DONE && output.length == 3);
    assert(browser_body_feed(&body, (const uint8_t *)"more", 4, write_bytes, NULL) == 0);
    browser_body_init(&body, BROWSER_BODY_FIXED, 0); assert(body.result == BROWSER_BODY_DONE);
    reset(); browser_body_init(&body, BROWSER_BODY_CLOSE, 0);
    assert(browser_body_feed(&body, (const uint8_t *)"abc", 3, write_bytes, NULL) == 3);
    assert(body.result == BROWSER_BODY_READING); browser_body_eof(&body); assert(body.result == BROWSER_BODY_DONE);
    guards(); puts("Body framing, binary payload, extensions, trailers and all split sizes: passed");
}
static void invalid(void)
{
    const char *bad[] = {"", "\r\n", "x\r\n", "-1\r\n", "0x1\r\n", "1 garbage\r\n", "1\nx\r\n0\r\n\r\n",
                        "1\r\nxX\n0\r\n\r\n", "1\r\nx\rX0\r\n\r\n", "2\r\nx", "0\r\n",
                        "0\r\nInvalid trailer\r\n\r\n", "0\r\n Bad: header\r\n\r\n", "10000000000000000\r\n", "1;\x01\r\nx\r\n0\r\n\r\n"};
    for (unsigned split = 1; split <= 31; ++split)
        for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i)
            assert(decode(bad[i], strlen(bad[i]), split) == BROWSER_BODY_INVALID);
    char overlong[300]; memset(overlong, 'a', sizeof(overlong));
    assert(decode(overlong, sizeof(overlong), 13) == BROWSER_BODY_INVALID);
    char trailers[3072]; strcpy(trailers, "0\r\n"); size_t used = 3;
    while (used + 6 < sizeof(trailers)) { memcpy(trailers + used, "X: y\r\n", 6); used += 6; }
    assert(decode(trailers, used, 31) == BROWSER_BODY_INVALID);
    browser_body_t body;
    reset(); browser_body_init(&body, BROWSER_BODY_FIXED, 4);
    browser_body_feed(&body, (const uint8_t *)"abc", 3, write_bytes, NULL);
    browser_body_eof(&body); assert(body.result == BROWSER_BODY_INVALID);
    reset(); output.limit = 2; browser_body_init(&body, BROWSER_BODY_CHUNKED, 0);
    assert(browser_body_feed(&body, (const uint8_t *)"4\r\nabcd\r\n0\r\n\r\n", 14, write_bytes, NULL) == 5);
    assert(body.result == BROWSER_BODY_STOPPED && output.length == 2);
    assert(browser_body_feed(&body, (const uint8_t *)"later", 5, write_bytes, NULL) == 0);
    browser_body_eof(&body); assert(body.result == BROWSER_BODY_STOPPED); guards();
    puts("Malformed sizes/CRLF, overflow, bounded headers/trailers, premature EOF and short consumer: passed");
}
static void fuzz(void)
{
    struct { uint8_t before[16]; browser_body_t body; uint8_t after[16]; } guarded;
    uint32_t random = 0x76543210; uint8_t bytes[512];
    for (unsigned trial = 0; trial < 600; ++trial) {
        memset(&guarded, 0xa5, sizeof(guarded)); reset();
        browser_body_init(&guarded.body, (browser_body_mode_t)(trial % 3), trial % 211);
        for (unsigned i = 0; i < sizeof(bytes); ++i) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; bytes[i] = (uint8_t)random; }
        for (unsigned pos = 0; pos < sizeof(bytes) && guarded.body.result == BROWSER_BODY_READING;) {
            size_t count = trial % 31 + 1; if (count > sizeof(bytes) - pos) count = sizeof(bytes) - pos;
            size_t used = browser_body_feed(&guarded.body, bytes + pos, count, write_bytes, NULL);
            assert(used <= count && (used || guarded.body.result != BROWSER_BODY_READING)); pos += (unsigned)used;
        }
        browser_body_eof(&guarded.body);
        for (unsigned i = 0; i < 16; ++i) { assert(guarded.before[i] == 0xa5); assert(guarded.after[i] == 0xa5); }
        guards();
    }
    puts("600 malformed wire-input cases with guard bytes: passed");
}
int main(void) { valid(); invalid(); fuzz(); printf("HTTP body decoder tests passed; state=%zu bytes\n", sizeof(browser_body_t)); return 0; }
