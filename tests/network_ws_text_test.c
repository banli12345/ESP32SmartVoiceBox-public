#include "NetworkWs.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *name)
{
    if (condition) {
        printf("ok %s\n", name);
        return;
    }
    printf("FAIL %s\n", name);
    ++failures;
}

static network_ws_text_t make_message(char *buffer, size_t capacity)
{
    network_ws_text_t message;
    memset(&message, 0, sizeof(message));
    message.buffer = buffer;
    message.capacity = capacity;
    return message;
}

static void test_single_frame(void)
{
    char buffer[32];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    const char *text = "hello";
    const bool ready = network_ws_text_feed(&message, 0x1, true, text, 5, 5, 0);
    expect(ready, "single frame ready");
    expect(strcmp(buffer, "hello") == 0, "single frame text");
    expect(!message.active, "single frame inactive");
}

static void test_buffer_split(void)
{
    char buffer[32];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    expect(!network_ws_text_feed(&message, 0x1, true, "hel", 3, 5, 0), "split first stays open");
    expect(message.active, "split first active");
    expect(network_ws_text_feed(&message, 0x1, true, "lo", 2, 5, 3), "split second ready");
    expect(strcmp(buffer, "hello") == 0, "split text");
}

static void test_continuation(void)
{
    char buffer[32];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    expect(!network_ws_text_feed(&message, 0x1, false, "abc", 3, 3, 0), "fin0 not ready");
    expect(message.active, "fin0 still active");
    expect(network_ws_text_feed(&message, 0x0, true, "def", 3, 3, 0), "continuation ready");
    expect(strcmp(buffer, "abcdef") == 0, "continuation text");
    expect(!message.active, "continuation inactive");
}

static void test_binary_and_ping_keep_partial(void)
{
    char buffer[32];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    expect(!network_ws_text_feed(&message, 0x1, false, "xy", 2, 2, 0), "partial text");
    expect(!network_ws_text_feed(&message, 0x2, true, "ZZ", 2, 2, 0), "binary ignored");
    expect(!network_ws_text_feed(&message, 0x9, true, NULL, 0, 0, 0), "ping ignored");
    expect(message.active, "partial still active");
    expect(strcmp(buffer, "xy") == 0, "partial text kept");
    expect(network_ws_text_feed(&message, 0x0, true, "z", 1, 1, 0), "continuation after control");
    expect(strcmp(buffer, "xyz") == 0, "text after control");
}

static void test_overflow_resets(void)
{
    char buffer[5];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    expect(!network_ws_text_feed(&message, 0x1, true, "abcdef", 6, 6, 0), "overflow rejected");
    expect(!message.active, "overflow reset active");
    expect(buffer[0] == '\0', "overflow cleared");
}

static void test_bad_offset_resets(void)
{
    char buffer[32];
    network_ws_text_t message = make_message(buffer, sizeof(buffer));
    expect(!network_ws_text_feed(&message, 0x1, true, "ab", 2, 4, 0), "bad offset setup");
    expect(!network_ws_text_feed(&message, 0x1, true, "cd", 2, 4, 9), "bad offset rejected");
    expect(!message.active, "bad offset reset");
    expect(buffer[0] == '\0', "bad offset cleared");
}

int main(void)
{
    test_single_frame();
    test_buffer_split();
    test_continuation();
    test_binary_and_ping_keep_partial();
    test_overflow_resets();
    test_bad_offset_resets();
    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
