#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {MEDIA_PHOTO=0,MEDIA_VIDEO=1} media_kind_t;
typedef struct {int state;char path[96],message[192];} media_file_status_t;
bool media_files_recover_boot(void);
bool media_files_request(media_kind_t kind,const char *path,const char *new_name,bool remove);
void media_files_get_status(media_file_status_t *out);
#ifdef __cplusplus
}
#endif
