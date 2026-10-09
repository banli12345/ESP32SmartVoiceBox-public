/*******************************************************************************
 * Touch Library — TouchLib (supports GT911 / FT5x06 / CSTxxx / ZTW622 etc.)
 * https://github.com/mmMicky/TouchLib.git
 *
 * No INT/RST pins required — pure I2C communication.
 ******************************************************************************/

#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>
#include <Wire.h>

// 必须在使用 TouchLib 前定义触摸芯片型号
#define TOUCH_MODULES_GT911

#include <TouchLib.h>

// ========== 触摸引脚配置 ==========
#ifndef TOUCH_SDA
#define TOUCH_SDA 19
#endif
#ifndef TOUCH_SCL
#define TOUCH_SCL 45
#endif
#ifndef TOUCH_ADDR
#define TOUCH_ADDR 0x5D  // GT911 I2C 地址
#endif

// ========== I2C 忙标志（与 MPU6050 共享 I2C 总线时避免冲突） ==========
extern volatile bool i2c_busy;

// ========== 触摸对象 ==========
TouchLib touch(Wire, TOUCH_SDA, TOUCH_SCL, TOUCH_ADDR);

// 触摸坐标变量
int touch_last_x = 0, touch_last_y = 0;

// ========== 触摸 API ==========
void touch_init()
{
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    Wire.setClock(100000);
    delay(50);
    touch.init();
    Serial.println("Touch initialized (TouchLib)");
}

bool touch_has_signal()
{
    return true;
}

bool touch_touched()
{
    // 限制触摸读取频率，避免和 MPU6050 抢 I2C 总线
    static unsigned long last_touch_read = 0;
    unsigned long now = millis();
    if (now - last_touch_read < 30) {
        return false;  // 不到 30ms 跳过
    }

    // 等 I2C 总线空闲
    if (i2c_busy) return false;

    i2c_busy = true;
    last_touch_read = now;

    bool touched = touch.read();
    if (touched) {
        TP_Point t = touch.getPoint(0);
        touch_last_x = t.x;
        touch_last_y = t.y;
    }

    i2c_busy = false;
    return touched;
}

bool touch_released()
{
    return true;
}

#endif // TOUCH_H
