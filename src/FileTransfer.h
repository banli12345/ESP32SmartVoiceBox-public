#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool file_transfer_begin(const char *kind,const char *name,uint32_t size,char *token,size_t token_size,char *error,size_t error_size);
bool file_transfer_append(const char *token,uint32_t offset,const void *bytes,size_t length,char *error,size_t error_size);
bool file_transfer_finish(const char *token,char *path,size_t path_size,char *error,size_t error_size);
void file_transfer_cancel(const char *token);
void file_transfer_poll(void);
void file_transfer_reset(void);
/* Media edits exclude upload sessions and commits. Caller must release on success. */
bool file_transfer_begin_media_edit(void);
void file_transfer_end_media_edit(void);
void file_transfer_set_media_recovery(bool pending);
#ifdef __cplusplus
}
#endif
