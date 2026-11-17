#ifndef SMARTVOICEBOX_DISPLAY_CONTROL_H
#define SMARTVOICEBOX_DISPLAY_CONTROL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DISPLAY_RESTART_IDLE,
    DISPLAY_RESTART_PENDING,
    DISPLAY_RESTART_RUNNING,
    DISPLAY_RESTART_SUCCEEDED,
    DISPLAY_RESTART_FAILED
} display_restart_status_t;

// Call from LVGLTask only. The request is serviced after lv_timer_handler.
// Restart the RGB controller on the existing panel; retain framebuffer pointers.
bool display_restart_request(void);
display_restart_status_t display_restart_get_status(void);

#ifdef __cplusplus
}
#endif

#endif
