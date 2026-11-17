#ifndef NETWORK_WS_H
#define NETWORK_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Accumulates one WebSocket text message across buffer splits and
 * continuation frames. The callback only copies bytes; it does not parse. */
typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t frame_filled;
    int frame_len;
    bool active;
} network_ws_text_t;

void network_ws_text_reset(network_ws_text_t *message);
bool network_ws_text_feed(network_ws_text_t *message, unsigned opcode, bool fin,
                          const void *data, int data_len, int payload_len,
                          int payload_offset);

#ifdef __cplusplus
}
#endif

#endif
