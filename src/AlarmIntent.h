#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    ALARM_INTENT_NONE, ALARM_INTENT_SET, ALARM_INTENT_QUERY,
    ALARM_INTENT_DISABLE, ALARM_INTENT_STOP, ALARM_INTENT_INVALID
} alarm_intent_action_t;
typedef struct {
    alarm_intent_action_t action;
    uint8_t hour, minute;
    uint8_t weekdays; /* Mon=bit 0 ... Sun=bit 6; 0=once, 0x7f=daily. */
    uint8_t ring_minutes; /* 0 means retain the current ringing duration. */
    int8_t day_offset; /* -1=next occurrence, 0=today, 1=tomorrow, 2=day after. */
    uint32_t delay_minutes; /* >0 means one-shot relative reminder, <=10080. */
} alarm_intent_t;
/* Pure, bounded local parser. No networking, flash, clocks, or side effects. */
alarm_intent_t alarm_intent_parse(const char *text);
#ifdef __cplusplus
}
#endif
