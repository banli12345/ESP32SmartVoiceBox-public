#ifndef SMARTVOICEBOX_BROWSER_BODY_H
#define SMARTVOICEBOX_BROWSER_BODY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { BROWSER_BODY_FIXED, BROWSER_BODY_CLOSE, BROWSER_BODY_CHUNKED } browser_body_mode_t;
typedef enum { BROWSER_BODY_READING, BROWSER_BODY_DONE, BROWSER_BODY_STOPPED, BROWSER_BODY_INVALID } browser_body_result_t;
typedef size_t (*browser_body_write_fn)(void *context, const uint8_t *bytes, size_t length);
typedef struct {
    browser_body_mode_t mode;
    browser_body_result_t result;
    uint64_t remaining;
    unsigned stage;
    size_t line_length;
    size_t trailer_bytes;
    char line[128];
} browser_body_t;

/* Fixed-length, EOF-delimited or chunked HTTP body; no allocation. A short
 * consumer write stops decoding (for cancellation or the caller's size cap).
 * feed returns wire bytes consumed; protocol framing is never sent to write.
 */
void browser_body_init(browser_body_t *body, browser_body_mode_t mode, uint64_t length);
size_t browser_body_feed(browser_body_t *body, const uint8_t *bytes, size_t length,
                         browser_body_write_fn write, void *context);
void browser_body_eof(browser_body_t *body);
#ifdef __cplusplus
}
#endif
#endif
