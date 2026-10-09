#include "browser_document.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool starts_ci(const char *text, const char *prefix)
{
    while (*prefix) {
        if (tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)) return false;
    }
    return true;
}

bool browser_url_normalize(const char *input, char *out, size_t capacity)
{
    if (!input || !out || !capacity) return false;
    out[0] = '\0';
    while (isspace((unsigned char)*input)) ++input;
    size_t length = strlen(input);
    while (length && isspace((unsigned char)input[length - 1])) --length;
    const char *fragment = memchr(input, '#', length);
    if (fragment) length = (size_t)(fragment - input);
    if (!length || length >= BROWSER_URL_CAP) return false;
    const char *scheme = "https://";
    size_t skip = 0;
    if (starts_ci(input, "https://")) skip = 8;
    else if (starts_ci(input, "http://")) { scheme = "http://"; skip = 7; }
    else {
        const char *colon = memchr(input, ':', length);
        const char *slash = memchr(input, '/', length);
        const char *query = memchr(input, '?', length);
        if (colon && (!slash || colon < slash) && (!query || colon < query)) {
            // A bare host:port is allowed; arbitrary URI schemes are not.
            const char *end = slash && (!query || slash < query) ? slash : query;
            if (!end) end = input + length;
            if (colon + 1 == end) return false;
            for (const char *p = colon + 1; p < end; ++p) if (!isdigit((unsigned char)*p)) return false;
            if (!memchr(input, '.', (size_t)(colon - input)) && strncmp(input, "localhost:", 10) != 0) return false;
        }
    }
    const size_t scheme_length = strlen(scheme);
    size_t authority_end = skip;
    while (authority_end < length && input[authority_end] != '/' && input[authority_end] != '?') ++authority_end;
    if (authority_end == skip) return false;
    if (input[skip] == ':' || input[skip] == '[') {
        if (input[skip] != '[' || !memchr(input + skip, ']', authority_end - skip)) return false;
    }
    for (size_t i = skip; i < length; ++i) {
        const unsigned char c = (unsigned char)input[i];
        if (c <= 32 || c == 127 || c == '\\' || (i < authority_end && (c == '@' || c >= 128))) return false;
    }
    if (scheme_length >= capacity) return false;
    memcpy(out, scheme, scheme_length);
    size_t position = scheme_length;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = skip; i < length; ++i) {
        const unsigned char c = (unsigned char)input[i];
        const size_t count = c >= 128 ? 3 : 1;
        if (position + count >= capacity) { out[0] = '\0'; return false; }
        if (count == 3) { out[position++] = '%'; out[position++] = hex[c >> 4]; out[position++] = hex[c & 15]; }
        else out[position++] = (char)c;
    }
    out[position] = '\0';
    return true;
}

static void remove_last_segment(char *out, size_t *length)
{
    while (*length && out[*length - 1] != '/') --*length;
    if (*length) --*length;
    out[*length] = '\0';
}

static void remove_dot_segments(const char *path, char *out)
{
    size_t used = 0;
    while (*path) {
        if (strncmp(path, "../", 3) == 0) path += 3;
        else if (strncmp(path, "./", 2) == 0) path += 2;
        else if (strncmp(path, "/./", 3) == 0) path += 2;
        else if (strcmp(path, "/.") == 0) { path += 2; out[used++] = '/'; }
        else if (strncmp(path, "/../", 4) == 0) { path += 3; remove_last_segment(out, &used); }
        else if (strcmp(path, "/..") == 0) { path += 3; remove_last_segment(out, &used); out[used++] = '/'; }
        else if (strcmp(path, ".") == 0 || strcmp(path, "..") == 0) break;
        else {
            if (*path == '/') out[used++] = *path++;
            while (*path && *path != '/') out[used++] = *path++;
        }
    }
    out[used] = '\0';
}

bool browser_url_resolve(const char *base, const char *href, char *out, size_t capacity)
{
    if (!href || !out || !capacity) return false;
    out[0] = '\0';
    while (isspace((unsigned char)*href)) ++href;
    if (!*href || *href == '#') return false;
    if (starts_ci(href, "http://") || starts_ci(href, "https://")) return browser_url_normalize(href, out, capacity);
    char clean_base[BROWSER_URL_CAP], combined[BROWSER_URL_CAP];
    if (!browser_url_normalize(base, clean_base, sizeof(clean_base))) return false;
    const char *scheme_end = strstr(clean_base, "://");
    const size_t scheme_length = (size_t)(scheme_end - clean_base);
    if (href[0] == '/' && href[1] == '/') {
        if (snprintf(combined, sizeof(combined), "%.*s:%s", (int)scheme_length, clean_base, href) >= (int)sizeof(combined)) return false;
        return browser_url_normalize(combined, out, capacity);
    }
    const char *colon = strchr(href, ':');
    const char *first_slash = strchr(href, '/');
    const char *first_query = strchr(href, '?');
    if (colon && (!first_slash || colon < first_slash) && (!first_query || colon < first_query)) return false;
    const char *authority = scheme_end + 3;
    size_t origin_length = (size_t)(authority - clean_base);
    while (clean_base[origin_length] && clean_base[origin_length] != '/' && clean_base[origin_length] != '?') ++origin_length;
    size_t base_length = strcspn(clean_base, "?");
    size_t prefix_length = origin_length;
    if (*href == '?') prefix_length = base_length;
    else if (*href != '/') {
        prefix_length = base_length;
        while (prefix_length > origin_length && clean_base[prefix_length - 1] != '/') --prefix_length;
    }
    int written = snprintf(combined, sizeof(combined), "%.*s%s%s", (int)prefix_length, clean_base,
                           *href != '/' && *href != '?' && prefix_length == origin_length ? "/" : "", href);
    if (written < 0 || written >= (int)sizeof(combined)) return false;
    char normalized[BROWSER_URL_CAP];
    if (!browser_url_normalize(combined, normalized, sizeof(normalized))) return false;
    size_t path_start = origin_length;
    const char *query = strchr(normalized + path_start, '?');
    size_t path_length = query ? (size_t)(query - normalized) - path_start : strlen(normalized + path_start);
    char path[BROWSER_URL_CAP], clean_path[BROWSER_URL_CAP];
    memcpy(path, normalized + path_start, path_length);
    path[path_length] = '\0';
    remove_dot_segments(path, clean_path);
    written = snprintf(out, capacity, "%.*s%s%s", (int)origin_length, normalized, clean_path, query ? query : "");
    if (written < 0 || (size_t)written >= capacity) { out[0] = '\0'; return false; }
    return true;
}

static unsigned encode_utf8(uint32_t cp, char bytes[4])
{
    if (cp < 0x80) { bytes[0] = (char)cp; return 1; }
    if (cp < 0x800) { bytes[0] = (char)(0xc0 | (cp >> 6)); bytes[1] = (char)(0x80 | (cp & 63)); return 2; }
    if (cp < 0x10000) {
        bytes[0] = (char)(0xe0 | (cp >> 12)); bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
        bytes[2] = (char)(0x80 | (cp & 63)); return 3;
    }
    bytes[0] = (char)(0xf0 | (cp >> 18)); bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
    bytes[2] = (char)(0x80 | ((cp >> 6) & 63)); bytes[3] = (char)(0x80 | (cp & 63)); return 4;
}

static bool append_cp(char *text, size_t *length, size_t capacity, uint32_t cp)
{
    char bytes[4];
    const unsigned count = encode_utf8(cp, bytes);
    if (*length + count >= capacity) return false;
    memcpy(text + *length, bytes, count);
    *length += count;
    text[*length] = '\0';
    return true;
}

static uint32_t decode_entity(const char *entity)
{
    if (!strcmp(entity, "amp")) return '&';
    if (!strcmp(entity, "lt")) return '<';
    if (!strcmp(entity, "gt")) return '>';
    if (!strcmp(entity, "quot")) return '"';
    if (!strcmp(entity, "apos")) return '\'';
    if (!strcmp(entity, "nbsp")) return 160;
    if (*entity == '#') {
        char *end;
        const bool hex = entity[1] == 'x' || entity[1] == 'X';
        const char *digits = entity + (hex ? 2 : 1);
        if (!*digits) return 0;
        for (const char *p = digits; *p; ++p) if (hex ? !isxdigit((unsigned char)*p) : !isdigit((unsigned char)*p)) return 0;
        const unsigned long cp = strtoul(digits, &end, hex ? 16 : 10);
        if (*end || !cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return '?';
        return (uint32_t)cp;
    }
    return 0;
}

static void emit_cp(browser_parser_t *p, uint32_t cp)
{
    browser_document_t *d = p->document;
    if (p->in_head && !p->in_title) return;
    const bool whitespace = cp == 160 || cp == 32 || (cp >= 9 && cp <= 13);
    if (cp < 32 && !whitespace) return;
    if (cp == 127) return;
    if (p->anchor >= 0 && !p->in_title) {
        char *text = d->links[p->anchor].text;
        size_t length = strlen(text);
        if (!whitespace || (length && text[length - 1] != ' ')) append_cp(text, &length, BROWSER_LINK_TEXT_CAP, whitespace ? ' ' : cp);
    }
    if (p->in_title) {
        if (whitespace) { p->title_space = p->title_length != 0; return; }
        if (p->title_space) append_cp(d->title, &p->title_length, BROWSER_TITLE_CAP, ' ');
        p->title_space = false;
        append_cp(d->title, &p->title_length, BROWSER_TITLE_CAP, cp);
    } else {
        if (whitespace) { p->pending_space = d->text_length != 0; return; }
        if (p->pending_space && d->text_length && d->text[d->text_length - 1] != '\n') {
            if (!append_cp(d->text, &d->text_length, BROWSER_TEXT_CAP, ' ')) d->truncated = true;
        }
        p->pending_space = false;
        if (!append_cp(d->text, &d->text_length, BROWSER_TEXT_CAP, cp)) d->truncated = true;
    }
}

static void utf8_byte(browser_parser_t *p, unsigned char byte)
{
    if (p->utf8_remaining) {
        if ((byte & 0xc0) != 0x80) { p->utf8_remaining = 0; emit_cp(p, '?'); utf8_byte(p, byte); return; }
        p->utf8_codepoint = (p->utf8_codepoint << 6) | (byte & 63);
        if (--p->utf8_remaining == 0) {
            uint32_t cp = p->utf8_codepoint;
            if (cp < p->utf8_minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) cp = '?';
            emit_cp(p, cp);
        }
    } else if (byte < 128) emit_cp(p, byte);
    else if (byte >= 0xc2 && byte <= 0xdf) { p->utf8_codepoint = byte & 31; p->utf8_remaining = 1; p->utf8_minimum = 0x80; }
    else if (byte >= 0xe0 && byte <= 0xef) { p->utf8_codepoint = byte & 15; p->utf8_remaining = 2; p->utf8_minimum = 0x800; }
    else if (byte >= 0xf0 && byte <= 0xf4) { p->utf8_codepoint = byte & 7; p->utf8_remaining = 3; p->utf8_minimum = 0x10000; }
    else emit_cp(p, '?');
}

static void flush_entity(browser_parser_t *p)
{
    if (!p->in_entity) return;
    utf8_byte(p, '&');
    for (size_t i = 0; i < p->entity_length; ++i) utf8_byte(p, (unsigned char)p->entity[i]);
    p->entity_length = 0;
    p->in_entity = false;
}

static void text_byte(browser_parser_t *p, unsigned char byte)
{
    if (p->plain_text) { utf8_byte(p, byte); return; }
    if (p->in_entity) {
        if (byte == ';') {
            p->entity[p->entity_length] = '\0';
            uint32_t cp = decode_entity(p->entity);
            if (cp) { emit_cp(p, cp); p->in_entity = false; p->entity_length = 0; }
            else { flush_entity(p); utf8_byte(p, ';'); }
            return;
        }
        if ((isalnum(byte) || byte == '#') && p->entity_length + 1 < sizeof(p->entity)) {
            p->entity[p->entity_length++] = (char)byte;
            return;
        }
        flush_entity(p);
    }
    if (byte == '&') {
        if (p->utf8_remaining) { emit_cp(p, '?'); p->utf8_remaining = 0; }
        p->in_entity = true;
        p->entity_length = 0;
    } else utf8_byte(p, byte);
}

static void boundary(browser_parser_t *p)
{
    flush_entity(p);
    if (p->utf8_remaining) { emit_cp(p, '?'); p->utf8_remaining = 0; }
}

static void line_break(browser_parser_t *p)
{
    if (p->in_head || p->in_title) return;
    boundary(p);
    browser_document_t *d = p->document;
    if (d->text_length && d->text[d->text_length - 1] != '\n') {
        if (!append_cp(d->text, &d->text_length, BROWSER_TEXT_CAP, '\n')) d->truncated = true;
    }
    p->pending_space = false;
}

static bool attribute(const char *tag, const char *wanted, char *out, size_t capacity)
{
    out[0] = '\0';
    while (*tag && !isspace((unsigned char)*tag)) ++tag;
    while (*tag) {
        while (isspace((unsigned char)*tag) || *tag == '/') ++tag;
        const char *name = tag;
        while (*tag && *tag != '=' && !isspace((unsigned char)*tag) && *tag != '/') ++tag;
        const size_t name_length = (size_t)(tag - name);
        while (isspace((unsigned char)*tag)) ++tag;
        if (*tag != '=') { if (!name_length && *tag) ++tag; continue; }
        ++tag;
        while (isspace((unsigned char)*tag)) ++tag;
        char quote = *tag == '"' || *tag == '\'' ? *tag++ : 0;
        const char *value = tag;
        while (*tag && (quote ? *tag != quote : !isspace((unsigned char)*tag))) ++tag;
        const size_t length = (size_t)(tag - value);
        const bool match = name_length == strlen(wanted) && starts_ci(name, wanted);
        if (quote && *tag) ++tag;
        if (match) {
            if (length >= capacity) return false;
            memcpy(out, value, length); out[length] = '\0'; return true;
        }
    }
    return false;
}

static void decode_attribute(char *text)
{
    char decoded[BROWSER_URL_CAP];
    size_t used = 0;
    for (size_t i = 0; text[i] && used + 1 < sizeof(decoded); ++i) {
        if (text[i] == '&') {
            const char *end = strchr(text + i + 1, ';');
            if (end && end - (text + i + 1) < 24) {
                char entity[24];
                size_t length = (size_t)(end - text - i - 1);
                memcpy(entity, text + i + 1, length); entity[length] = '\0';
                uint32_t cp = decode_entity(entity);
                if (cp && append_cp(decoded, &used, sizeof(decoded), cp)) { i = (size_t)(end - text); continue; }
            }
        }
        decoded[used++] = text[i];
    }
    decoded[used] = '\0';
    memcpy(text, decoded, used + 1);
}

static bool valid_charset(const char *charset)
{
    while (isspace((unsigned char)*charset) || *charset == '\'' || *charset == '"') ++charset;
    const size_t length = strcspn(charset, ";\"' \t\r\n");
    const char *allowed[] = {"utf-8", "utf8", "us-ascii", "ascii"};
    for (unsigned i = 0; i < sizeof(allowed) / sizeof(*allowed); ++i)
        if (length == strlen(allowed[i]) && starts_ci(charset, allowed[i])) return true;
    return false;
}

static void process_tag(browser_parser_t *p)
{
    p->tag[p->tag_length] = '\0';
    const char *name = p->tag;
    while (isspace((unsigned char)*name)) ++name;
    const bool closing = *name == '/';
    if (closing) ++name;
    char tag_name[20]; size_t length = 0;
    while (isalnum((unsigned char)*name)) {
        if (length + 1 >= sizeof(tag_name)) return;
        tag_name[length++] = (char)tolower((unsigned char)*name++);
    }
    tag_name[length] = '\0';
    if (!strcmp(tag_name, "head")) {
        if (closing && p->in_title) { boundary(p); p->in_title = false; }
        p->in_head = !closing; return;
    }
    if (!strcmp(tag_name, "body")) { boundary(p); p->in_head = false; p->in_title = false; return; }
    if (!strcmp(tag_name, "title")) { boundary(p); p->in_title = !closing; return; }
    if (!closing && (!strcmp(tag_name, "script") || !strcmp(tag_name, "style"))) {
        boundary(p); p->raw_kind = !strcmp(tag_name, "script") ? 1 : 2; p->raw_match = 0; return;
    }
    if (!closing && !p->tag_overflow && !strcmp(tag_name, "meta")) {
        char charset[BROWSER_URL_CAP];
        if (attribute(p->tag, "charset", charset, sizeof(charset))) {
            if (!valid_charset(charset)) p->document->unsupported_charset = true;
        } else if (attribute(p->tag, "content", charset, sizeof(charset))) {
            for (const char *cursor = charset; *cursor; ++cursor) {
                if (!starts_ci(cursor, "charset")) continue;
                cursor += 7;
                while (isspace((unsigned char)*cursor)) ++cursor;
                if (*cursor == '=' && !valid_charset(cursor + 1)) p->document->unsupported_charset = true;
                break;
            }
        }
    }
    if (p->in_head || p->in_title) return;
    if (!strcmp(tag_name, "a")) {
        boundary(p); p->anchor = -1;
        if (!closing && !p->tag_overflow && p->document->link_count < BROWSER_LINK_CAP) {
            char href[BROWSER_URL_CAP], url[BROWSER_URL_CAP];
            if (attribute(p->tag, "href", href, sizeof(href))) {
                decode_attribute(href);
                if (browser_url_resolve(p->base_url, href, url, sizeof(url))) {
                    bool duplicate = false;
                    for (unsigned i = 0; i < p->document->link_count; ++i) if (!strcmp(url, p->document->links[i].url)) duplicate = true;
                    if (!duplicate) {
                        p->anchor = (int)p->document->link_count++;
                        strcpy(p->document->links[p->anchor].url, url);
                    }
                }
            }
        }
    }
    if (!strcmp(tag_name, "br") || !strcmp(tag_name, "p") || !strcmp(tag_name, "div") ||
        !strcmp(tag_name, "li") || !strcmp(tag_name, "tr") || !strcmp(tag_name, "section") ||
        !strcmp(tag_name, "article") || !strcmp(tag_name, "hr") || !strcmp(tag_name, "pre") ||
        (tag_name[0] == 'h' && tag_name[1] >= '1' && tag_name[1] <= '6' && !tag_name[2])) line_break(p);
}

void browser_parser_init(browser_parser_t *p, browser_document_t *d, const char *base_url, bool plain_text)
{
    memset(p, 0, sizeof(*p)); memset(d, 0, sizeof(*d));
    p->document = d; p->anchor = -1; p->plain_text = plain_text;
    snprintf(p->base_url, sizeof(p->base_url), "%s", base_url ? base_url : "");
}

void browser_parser_feed(browser_parser_t *p, const char *bytes, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)bytes[i];
        if (p->plain_text) { if (c == '\n') line_break(p); else text_byte(p, c); continue; }
        if (p->in_comment) {
            if (c == '>' && p->comment_dashes >= 2) { p->in_comment = false; p->comment_dashes = 0; }
            else p->comment_dashes = c == '-' ? (p->comment_dashes < 2 ? p->comment_dashes + 1 : 2) : 0;
            continue;
        }
        if (p->raw_kind) {
            const char *end_tag = p->raw_kind == 1 ? "</script" : "</style";
            const unsigned match_length = (unsigned)strlen(end_tag);
            if (p->raw_match == match_length) {
                if (c == '>' || c == '/' || isspace(c)) {
                    strcpy(p->tag, end_tag + 1); p->tag_length = match_length - 1;
                    p->raw_kind = 0; p->raw_match = 0; p->quote = 0; p->tag_overflow = false;
                    if (c == '>') process_tag(p); else p->in_tag = true;
                    continue;
                }
                p->raw_match = 0;
            }
            if (tolower(c) == end_tag[p->raw_match]) ++p->raw_match;
            else p->raw_match = c == '<' ? 1 : 0;
            continue;
        }
        if (p->in_tag) {
            if (!p->quote && c == '>') { process_tag(p); p->in_tag = false; continue; }
            if (!p->quote && c == '<') { p->tag_length = 0; p->tag_overflow = false; continue; }
            if (c == p->quote) p->quote = 0;
            else if (!p->quote && (c == '\'' || c == '"')) p->quote = (char)c;
            if (p->tag_length + 1 < sizeof(p->tag)) p->tag[p->tag_length++] = (char)c;
            else p->tag_overflow = true;
            if (p->tag_length == 3 && !memcmp(p->tag, "!--", 3)) {
                p->in_comment = true; p->in_tag = false; p->comment_dashes = 0;
            }
        } else if (c == '<') {
            boundary(p); p->in_tag = true; p->tag_length = 0; p->quote = 0; p->tag_overflow = false;
        } else text_byte(p, c);
    }
}

void browser_parser_finish(browser_parser_t *p)
{
    boundary(p);
    browser_document_t *d = p->document;
    while (d->text_length && isspace((unsigned char)d->text[d->text_length - 1])) d->text[--d->text_length] = '\0';
    for (unsigned i = 0; i < d->link_count; ++i) {
        char *text = d->links[i].text;
        size_t length = strlen(text);
        while (length && text[length - 1] == ' ') text[--length] = '\0';
        if (!length) snprintf(text, BROWSER_LINK_TEXT_CAP, "%s", d->links[i].url);
    }
}
