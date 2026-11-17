#ifndef TOUCH_H
#define TOUCH_H

#include <stdbool.h>
#include <stdint.h>

#ifndef TOUCH_SDA
#define TOUCH_SDA 19
#endif
#ifndef TOUCH_SCL
#define TOUCH_SCL 45
#endif
#ifndef TOUCH_ADDR
#define TOUCH_ADDR 0x5D
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern volatile bool i2c_busy;
extern int touch_last_x;
extern int touch_last_y;

typedef enum {
    TOUCH_SAMPLE_ERROR = -1,
    TOUCH_SAMPLE_NONE = 0,
    TOUCH_SAMPLE_RELEASED = 1,
    TOUCH_SAMPLE_PRESSED = 2
} touch_sample_status_t;

void touch_init(void);
bool touch_has_signal(void);
bool touch_touched(void);
bool touch_released(void);
bool touch_read_point(int *x, int *y);
touch_sample_status_t touch_read_sample(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif
