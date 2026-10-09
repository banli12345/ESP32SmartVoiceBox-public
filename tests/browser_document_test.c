#include "browser_document.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static browser_document_t document;
static browser_parser_t parser;
static void parse(const char *html, size_t length, unsigned chunk, bool plain)
{
    browser_parser_init(&parser, &document, "https://example.com/a/b/index.html?old=1", plain);
    for (size_t i = 0; i < length; i += chunk) browser_parser_feed(&parser, html + i, length - i < chunk ? length - i : chunk);
    browser_parser_finish(&parser);
}
static void valid_utf8(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        unsigned count; uint32_t cp, minimum;
        if (*p < 128) { ++p; continue; }
        if (*p >= 0xc2 && *p <= 0xdf) { count = 1; cp = *p & 31; minimum = 0x80; }
        else if (*p >= 0xe0 && *p <= 0xef) { count = 2; cp = *p & 15; minimum = 0x800; }
        else { assert(*p >= 0xf0 && *p <= 0xf4); count = 3; cp = *p & 7; minimum = 0x10000; }
        ++p;
        while (count--) { assert((*p & 0xc0) == 0x80); cp = (cp << 6) | (*p++ & 63); }
        assert(cp >= minimum && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff));
    }
}
static void urls(void)
{
    char out[BROWSER_URL_CAP];
    assert(browser_url_normalize(" example.com/news#top ", out, sizeof(out)));
    assert(!strcmp(out, "https://example.com/news"));
    assert(browser_url_normalize("HTTP://example.com:8080/a", out, sizeof(out)));
    assert(!strcmp(out, "http://example.com:8080/a"));
    assert(browser_url_normalize("http://127.0.0.1:9000/", out, sizeof(out)));
    assert(browser_url_normalize("https://example.com/中文", out, sizeof(out)));
    assert(!strcmp(out, "https://example.com/%E4%B8%AD%E6%96%87"));
    const char *bad[] = {"", "javascript:alert(1)", "data:text/html,test", "ftp://example.com", "mailto:a@b.com", "http:///a", "http://user:pass@example.com/", "http://example.com/\r\nx", "http://example.com/a b"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) assert(!browser_url_normalize(bad[i], out, sizeof(out)));
    struct { const char *href, *expected; } cases[] = {
        {"../news?x=1", "https://example.com/a/news?x=1"},
        {"../.././news", "https://example.com/news"},
        {"/root/../next", "https://example.com/next"},
        {"?q=2", "https://example.com/a/b/index.html?q=2"},
        {"//other.example/x", "https://other.example/x"},
        {"./", "https://example.com/a/b/"},
        {"../../../..", "https://example.com/"},
        {"a//b/./c", "https://example.com/a/b/a//b/c"},
        {"https://other.example/a#top", "https://other.example/a"}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        assert(browser_url_resolve("https://example.com/a/b/index.html?old=1", cases[i].href, out, sizeof(out)));
        if (strcmp(out, cases[i].expected)) { printf("URL mismatch %s: %s\n", cases[i].href, out); fflush(stdout); }
        assert(!strcmp(out, cases[i].expected));
    }
    assert(browser_url_resolve("https://example.com", "next", out, sizeof(out)));
    assert(!strcmp(out, "https://example.com/next"));
    assert(!browser_url_resolve("https://example.com", "#top", out, sizeof(out)));
    assert(!browser_url_resolve("https://example.com", "javascript:alert(1)", out, sizeof(out)));
    assert(!browser_url_normalize("example.com", out, 8));
    puts("URL normalization, relative resolution and unsafe schemes: passed");
}
static void html(void)
{
    const char *input = "<!doctype html><head><title>测试 &amp; 标题</title><style>p{color:red}</style></head><body><h1>你好</h1><p>A&nbsp;B &#x4E2D;&#25991; &lt;x&gt; &quot;Q&quot; &apos;S&apos;</p><!-- hidden > <p>bad</p> --><script>if (x < y) {alert('<a>bad');}</script><STYLE>bad</STYLE><a href='../news?x=1&amp;y=2'>新闻 <b>链接</b></a><a href='javascript:bad()'>不要执行</a><br>结尾</body>";
    for (unsigned chunk = 1; chunk <= 31; ++chunk) {
        parse(input, strlen(input), chunk, false);
        assert(!strcmp(document.title, "测试 & 标题"));
        assert(strstr(document.text, "你好\nA B 中文 <x> \"Q\" 'S'"));
        assert(!strstr(document.text, "hidden")); assert(!strstr(document.text, "alert")); assert(!strstr(document.text, "bad"));
        assert(document.link_count == 1);
        assert(!strcmp(document.links[0].text, "新闻 链接"));
        assert(!strcmp(document.links[0].url, "https://example.com/a/news?x=1&y=2"));
        valid_utf8(document.text); valid_utf8(document.title);
    }
    input = "<p>A</p><script>'unmatched quote <a> </script><p>B</p><a href='/x'></a><a href='/x'>duplicate</a>";
    parse(input, strlen(input), 1, false);
    assert(!strcmp(document.text, "A\nB\nduplicate"));
    assert(document.link_count == 1); assert(!strcmp(document.links[0].text, "https://example.com/x"));
    input = "<meta charset='gbk'><p>content</p>"; parse(input, strlen(input), 2, false);
    assert(document.unsupported_charset);
    input = "<meta http-equiv='Content-Type' content='text/html; charset=gbk'>";
    parse(input, strlen(input), 1, false); assert(document.unsupported_charset);
    input = "<meta charset='UTF-8bogus'>"; parse(input, strlen(input), 3, false); assert(document.unsupported_charset);
    input = "<meta charset='ascii'><p>text</p>"; parse(input, strlen(input), 1, false); assert(!document.unsupported_charset);
    input = "A <b>\nB &amp;\n中文"; parse(input, strlen(input), 1, true);
    assert(!strcmp(document.text, input));
    input = "<p>&#0;&#xD800;&#x110000; &unknown; end &amp";
    parse(input, strlen(input), 1, false);
    assert(!strcmp(document.text, "??? &unknown; end &amp"));
    const char invalid[] = {'A', (char)0xf0, '(', (char)0x8c, '(', (char)0xe4};
    parse(invalid, sizeof(invalid), 1, true);
    assert(!strcmp(document.text, "A?(?(?"));
    puts("HTML, Chinese, entities, links, skipped content and plain text: passed");
}
static void limits(void)
{
    char *long_tag = malloc(4096); assert(long_tag);
    strcpy(long_tag, "<p title='"); memset(long_tag + 10, 'x', 3000);
    strcpy(long_tag + 3010, "'>visible</p><p>end</p>");
    parse(long_tag, strlen(long_tag), 7, false);
    assert(!strcmp(document.text, "visible\nend")); free(long_tag);
    char *large = malloc(BROWSER_TEXT_CAP * 3); assert(large);
    size_t used = 0;
    while (used + 3 < BROWSER_TEXT_CAP * 3) { memcpy(large + used, "中", 3); used += 3; }
    parse(large, used, 13, true); free(large);
    assert(document.truncated); assert(document.text_length < BROWSER_TEXT_CAP);
    assert(document.text_length == strlen(document.text)); valid_utf8(document.text);
    char title[600]; memset(title, 'a', sizeof(title));
    browser_parser_init(&parser, &document, "https://example.com", false);
    browser_parser_feed(&parser, "<title>", 7); browser_parser_feed(&parser, title, sizeof(title));
    browser_parser_feed(&parser, "</title><p>body</p>", strlen("</title><p>body</p>")); browser_parser_finish(&parser);
    assert(strlen(document.title) == BROWSER_TITLE_CAP - 1); assert(!strcmp(document.text, "body"));
    puts("Overlong tags, bounded output and UTF-8 truncation: passed");
}
static void fuzz(void)
{
    struct { unsigned char before[32]; browser_document_t d; unsigned char after[32]; } guarded;
    uint32_t random = 0x12345678;
    char bytes[8192];
    for (unsigned trial = 0; trial < 600; ++trial) {
        memset(&guarded, 0xa5, sizeof(guarded));
        browser_parser_init(&parser, &guarded.d, "https://example.com/a/b", trial % 3 == 0);
        for (unsigned i = 0; i < sizeof(bytes); ++i) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; bytes[i] = (char)random; }
        unsigned chunk = trial % 41 + 1;
        for (size_t i = 0; i < sizeof(bytes); i += chunk) browser_parser_feed(&parser, bytes + i, sizeof(bytes) - i < chunk ? sizeof(bytes) - i : chunk);
        browser_parser_finish(&parser);
        for (unsigned i = 0; i < 32; ++i) { assert(guarded.before[i] == 0xa5); assert(guarded.after[i] == 0xa5); }
        assert(guarded.d.text_length < BROWSER_TEXT_CAP); assert(guarded.d.link_count <= BROWSER_LINK_CAP);
        assert(strlen(guarded.d.text) == guarded.d.text_length);
        valid_utf8(guarded.d.text); valid_utf8(guarded.d.title);
        for (unsigned i = 0; i < guarded.d.link_count; ++i) valid_utf8(guarded.d.links[i].text);
    }
    puts("600 malformed-input fuzz cases with guard bytes and UTF-8 checks: passed");
}
int main(void)
{
    urls(); html(); limits(); fuzz();
    printf("Browser parser tests passed; document=%zu bytes parser=%zu bytes\n", sizeof(document), sizeof(parser));
    return 0;
}
