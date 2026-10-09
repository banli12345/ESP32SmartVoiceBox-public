#include "browser_body.h"
#include "browser_document.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static browser_document_t document;
static browser_parser_t parser;
static size_t payload_bytes, payload_limit;
static size_t consume(void *context, const uint8_t *bytes, size_t length)
{
    (void)context;
    size_t accepted = length;
    if (accepted > payload_limit - payload_bytes) accepted = payload_limit - payload_bytes;
    browser_parser_feed(&parser, (const char *)bytes, accepted);
    payload_bytes += accepted;
    return document.truncated ? 0 : accepted;
}

static void begin(size_t limit)
{
    payload_bytes = 0;
    payload_limit = limit;
    browser_parser_init(&parser, &document, "https://example.com/", false);
}

static void feed(browser_body_t *body, const char *bytes, size_t length, unsigned split)
{
    for (size_t pos = 0; pos < length && body->result == BROWSER_BODY_READING;) {
        size_t size = length - pos < split ? length - pos : split;
        size_t consumed = browser_body_feed(body, (const uint8_t *)bytes + pos, size, consume, NULL);
        assert(consumed <= size);
        assert(consumed != 0 || body->result != BROWSER_BODY_READING);
        pos += consumed;
    }
}

static size_t chunked(char *out, const char *payload, size_t length, unsigned chunk_size)
{
    size_t used = 0;
    for (size_t pos = 0; pos < length; pos += chunk_size) {
        size_t size = length - pos < chunk_size ? length - pos : chunk_size;
        used += sprintf(out + used, "%zx;test=\"split\"\r\n", size);
        memcpy(out + used, payload + pos, size); used += size;
        memcpy(out + used, "\r\n", 2); used += 2;
    }
    strcpy(out + used, "0\r\nX-Read: complete\r\n\r\n");
    return used + strlen(out + used);
}

static void split_html(void)
{
    const char *html = "<head><title>网页测试</title><script>hidden <body> <a href='/bad'>bad</a></script></head>"
                       "<body><p>中文 &amp; 正文</p><a href='/next'>下一页</a></body>";
    char wire[8192];
    for (unsigned payload_split = 1; payload_split <= 29; payload_split += 7) {
        size_t length = chunked(wire, html, strlen(html), payload_split);
        for (unsigned network_split = 1; network_split <= 31; ++network_split) {
            browser_body_t body;
            begin(256 * 1024);
            browser_body_init(&body, BROWSER_BODY_CHUNKED, 0);
            feed(&body, wire, length, network_split);
            assert(body.result == BROWSER_BODY_DONE);
            browser_parser_finish(&parser);
            assert(payload_bytes == strlen(html));
            assert(!strcmp(document.title, "网页测试"));
            assert(!strcmp(document.text, "中文 & 正文\n下一页"));
            assert(document.link_count == 1);
            assert(!strcmp(document.links[0].url, "https://example.com/next"));
        }
    }
    puts("HTTP chunk boundaries inside UTF-8, entities, scripts and HTML tags: passed");
}

static void bounds(void)
{
    browser_body_t body;
    const char *html = "<p>Hello</p>";
    begin(256 * 1024);
    browser_body_init(&body, BROWSER_BODY_FIXED, strlen(html));
    feed(&body, "<p>Hello</p>extra", strlen(html) + 5, 512);
    browser_parser_finish(&parser);
    assert(body.result == BROWSER_BODY_DONE);
    assert(payload_bytes == strlen(html));
    assert(!strcmp(document.text, "Hello"));

    begin(256 * 1024);
    browser_body_init(&body, BROWSER_BODY_CLOSE, 0);
    feed(&body, html, strlen(html), 3);
    assert(body.result == BROWSER_BODY_READING);
    browser_body_eof(&body);
    browser_parser_finish(&parser);
    assert(body.result == BROWSER_BODY_DONE && !strcmp(document.text, "Hello"));

    char *large = malloc(3 * BROWSER_TEXT_CAP); assert(large);
    memset(large, 'x', 3 * BROWSER_TEXT_CAP);
    begin(256 * 1024);
    browser_body_init(&body, BROWSER_BODY_FIXED, 3 * BROWSER_TEXT_CAP);
    feed(&body, large, 3 * BROWSER_TEXT_CAP, 512);
    browser_parser_finish(&parser);
    assert(body.result == BROWSER_BODY_STOPPED && document.truncated);
    assert(document.text_length == BROWSER_TEXT_CAP - 1);

    memset(large, ' ', 3 * BROWSER_TEXT_CAP);
    begin(1024);
    browser_body_init(&body, BROWSER_BODY_FIXED, 3 * BROWSER_TEXT_CAP);
    feed(&body, large, 3 * BROWSER_TEXT_CAP, 511);
    assert(body.result == BROWSER_BODY_STOPPED && payload_bytes == 1024);
    free(large);
    puts("Content-Length, close-delimited body, text cap and download cap: passed");
}

int main(void)
{
    split_html(); bounds();
    puts("HTTP body and HTML parser integration tests passed.");
    return 0;
}
