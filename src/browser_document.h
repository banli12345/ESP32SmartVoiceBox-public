#ifndef SMARTVOICEBOX_BROWSER_DOCUMENT_H
#define SMARTVOICEBOX_BROWSER_DOCUMENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BROWSER_TEXT_CAP 12288
#define BROWSER_TITLE_CAP 160
#define BROWSER_URL_CAP 384
#define BROWSER_LINK_CAP 8
#define BROWSER_LINK_TEXT_CAP 80
typedef struct { char text[BROWSER_LINK_TEXT_CAP]; char url[BROWSER_URL_CAP]; } browser_link_t;
typedef struct {
    char text[BROWSER_TEXT_CAP];
    char title[BROWSER_TITLE_CAP];
    browser_link_t links[BROWSER_LINK_CAP];
    size_t text_length;
    unsigned link_count;
    bool truncated;
    bool unsupported_charset;
} browser_document_t;
typedef struct {
    browser_document_t *document;
    char base_url[BROWSER_URL_CAP];
    char tag[768];
    char entity[24];
    size_t tag_length, entity_length, title_length;
    uint32_t utf8_codepoint, utf8_minimum;
    unsigned utf8_remaining, raw_match, comment_dashes;
    int anchor, raw_kind;
    char quote;
    bool in_tag, tag_overflow, in_comment, in_entity;
    bool in_head, in_title, plain_text, pending_space, title_space;
} browser_parser_t;
void browser_parser_init(browser_parser_t *, browser_document_t *, const char *base_url, bool plain_text);
void browser_parser_feed(browser_parser_t *, const char *bytes, size_t length);
void browser_parser_finish(browser_parser_t *);
bool browser_url_normalize(const char *input, char *out, size_t out_size);
bool browser_url_resolve(const char *base, const char *href, char *out, size_t out_size);
#ifdef __cplusplus
}
#endif
#endif
