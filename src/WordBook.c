#include "WordBook.h"
#include <string.h>

#define BOOK_HEADER_SIZE 24U
#define BOOK_MAX_COUNT 100000U
#define BOOK_MAX_BYTES (32U * 1024U * 1024U)

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool read_at(FILE *file, uint32_t offset, void *out, size_t bytes)
{
    return fseek(file, (long)offset, SEEK_SET) == 0 && fread(out, 1, bytes, file) == bytes;
}

void word_book_close(word_book_t *book)
{
    if (!book) return;
    if (book->file) fclose(book->file);
    memset(book, 0, sizeof(*book));
}

bool word_book_open(word_book_t *book, const char *path)
{
    if (!book) return false;
    word_book_close(book);
    if (!path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    unsigned char h[BOOK_HEADER_SIZE], first[4], last[4];
    if (fseek(file, 0, SEEK_END) != 0) goto invalid;
    long size = ftell(file);
    if (size < (long)BOOK_HEADER_SIZE || size > BOOK_MAX_BYTES || !read_at(file, 0, h, sizeof(h))) goto invalid;
    uint32_t count = le32(h + 4), data = le32(h + 12), bytes = le32(h + 16);
    if (memcmp(h, "BWV1", 4) || !count || count > BOOK_MAX_COUNT || le32(h + 8) != BOOK_HEADER_SIZE || le32(h + 20) != 0) goto invalid;
    if (data != BOOK_HEADER_SIZE + (count + 1U) * 4U || data > (uint32_t)size || bytes != (uint32_t)size - data) goto invalid;
    if (!read_at(file, BOOK_HEADER_SIZE, first, 4) || !read_at(file, BOOK_HEADER_SIZE + count * 4U, last, 4)) goto invalid;
    if (le32(first) != 0 || le32(last) != bytes) goto invalid;
    book->file = file; book->count = count; book->data_offset = data; book->data_size = bytes;
    return true;
invalid:
    fclose(file);
    return false;
}

/* Reject overlong sequences, surrogates, out-of-range Unicode and controls. */
static bool valid_utf8(const unsigned char *s, size_t bytes)
{
    for (size_t i = 0; i < bytes;) {
        uint32_t cp = s[i++]; unsigned more = 0; uint32_t minimum = 0;
        if (cp < 0x80) { if (cp < 0x20 || cp == 0x7F) return false; continue; }
        if (cp >= 0xC2 && cp <= 0xDF) { more = 1; minimum = 0x80; cp &= 0x1F; }
        else if (cp >= 0xE0 && cp <= 0xEF) { more = 2; minimum = 0x800; cp &= 0x0F; }
        else if (cp >= 0xF0 && cp <= 0xF4) { more = 3; minimum = 0x10000; cp &= 7; }
        else return false;
        if (bytes - i < more) return false;
        while (more--) { unsigned c = s[i++]; if ((c & 0xC0) != 0x80) return false; cp = (cp << 6) | (c & 0x3F); }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}

bool word_book_read(word_book_t *book, uint32_t index, word_book_entry_t *entry)
{
    if (!entry) return false;
    memset(entry, 0, sizeof(*entry));
    if (!book || !book->file || index >= book->count) return false;
    unsigned char offsets[8], record[WORD_BOOK_EN_CAP + WORD_BOOK_ZH_CAP];
    if (!read_at(book->file, BOOK_HEADER_SIZE + index * 4U, offsets, sizeof(offsets))) return false;
    uint32_t from = le32(offsets), to = le32(offsets + 4);
    if (from >= to || to > book->data_size || to - from < 4U || to - from > sizeof(record)) return false;
    size_t bytes = to - from;
    if (!read_at(book->file, book->data_offset + from, record, bytes) || record[bytes - 1] != 0) return false;
    unsigned char *separator = memchr(record, 0, bytes - 1);
    if (!separator) return false;
    size_t en = (size_t)(separator - record), zh = bytes - en - 2;
    if (!en || !zh || en >= WORD_BOOK_EN_CAP || zh >= WORD_BOOK_ZH_CAP || memchr(separator + 1, 0, zh)) return false;
    if (!valid_utf8(record, en) || !valid_utf8(separator + 1, zh)) return false;
    memcpy(entry->english, record, en); memcpy(entry->chinese, separator + 1, zh);
    return true;
}
