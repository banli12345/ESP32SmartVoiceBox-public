#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef WORD_BOOK_SD_PATH
#define WORD_BOOK_SD_PATH "/sdcard/words/cet4.bwv"
#endif
#define WORD_BOOK_EN_CAP 48
#define WORD_BOOK_ZH_CAP 128
typedef struct { char english[WORD_BOOK_EN_CAP]; char chinese[WORD_BOOK_ZH_CAP]; } word_book_entry_t;
typedef struct { FILE *file; uint32_t count, data_offset, data_size; } word_book_t;
/* Initialize the struct to zero before first open. Strict binary bounds and UTF-8 validation; no full dictionary/index allocation. */
bool word_book_open(word_book_t *book, const char *path);
bool word_book_read(word_book_t *book, uint32_t index, word_book_entry_t *entry);
void word_book_close(word_book_t *book);
#ifdef __cplusplus
}
#endif
