#ifndef SMARTVOICEBOX_WEB_BROWSER_H
#define SMARTVOICEBOX_WEB_BROWSER_H

#include "browser_document.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BROWSER_PAGE_BYTES 1024
typedef enum { BROWSER_IDLE, BROWSER_LOADING, BROWSER_READY, BROWSER_ERROR } browser_state_t;
typedef struct {
    browser_state_t state;
    bool busy;
    bool truncated;
    unsigned page_count;
    unsigned link_count;
    uint32_t revision;
    uint32_t received_bytes;
    char url[BROWSER_URL_CAP];
    char title[BROWSER_TITLE_CAP];
    char error[160];
} web_browser_view_t;

bool web_browser_start(const char *url);
void web_browser_cancel(void);
void web_browser_release(void);
void web_browser_get_view(web_browser_view_t *view);
bool web_browser_copy_page(unsigned page, char *text, size_t capacity);
bool web_browser_copy_link(unsigned index, browser_link_t *link);

#ifdef __cplusplus
}
#endif
#endif
