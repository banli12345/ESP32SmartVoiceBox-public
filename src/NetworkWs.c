#include "NetworkWs.h"

#include <string.h>

void network_ws_text_reset(network_ws_text_t *message)
{
    if (!message) return;
    message->length = 0;
    message->frame_filled = 0;
    message->frame_len = 0;
    message->active = false;
    if (message->buffer && message->capacity) message->buffer[0] = '\0';
}

bool network_ws_text_feed(network_ws_text_t *message, unsigned opcode, bool fin,
                          const void *data, int data_len, int payload_len,
                          int payload_offset)
{
    if (!message || !message->buffer || message->capacity < 2) return false;
    if (data_len < 0 || payload_len < 0 || payload_offset < 0) return false;
    if (data_len > 0 && !data) return false;

    const unsigned op = opcode & 0x0fu;
    if (payload_offset == 0) {
        if (op == 0x1u) {
            message->length = 0;
            message->frame_filled = 0;
            message->frame_len = payload_len;
            message->active = true;
        } else if (op == 0x0u && message->active) {
            message->frame_filled = 0;
            message->frame_len = payload_len;
        } else {
            /* Ping, close and binary frames must not discard a text message
             * that is still waiting for a continuation frame. */
            return false;
        }
    } else if (!message->active || payload_offset != (int)message->frame_filled ||
               payload_len != message->frame_len) {
        network_ws_text_reset(message);
        return false;
    }

    if ((size_t)data_len > message->capacity - 1u - message->length) {
        network_ws_text_reset(message);
        return false;
    }
    if (data_len > 0) {
        memcpy(message->buffer + message->length, data, (size_t)data_len);
        message->length += (size_t)data_len;
        message->frame_filled += (size_t)data_len;
    }
    message->buffer[message->length] = '\0';

    if ((int)message->frame_filled >= message->frame_len && fin) {
        const bool ready = message->length > 0;
        message->active = false;
        return ready;
    }
    return false;
}
