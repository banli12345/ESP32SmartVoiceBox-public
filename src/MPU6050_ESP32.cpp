#include "MPU6050_ESP32.h"

#include <stddef.h>
#include <stdio.h>
#include "IdfRuntime.h"
#include "board_i2c.h"
#include "board_pins.h"

#define MPU6050_WHO_AM_I 0x75
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_SIGNAL_PATH_RESET 0x68
#define MPU6050_SMPLRT_DIV 0x19
#define MPU6050_CONFIG 0x1A
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_ACCEL_CONFIG 0x1C
#define MPU6050_ACCEL_XOUT_H 0x3B
#define MPU6050_DEVICE_ID 0x68
#define MPU6050_ACCEL_SCALE 4096.0f
#define MPU6050_GYRO_SCALE 65.5f
#define MPU6050_GRAVITY 9.80665f
#define MPU6050_DPS_TO_RADS 0.017453293f

static i2c_master_dev_handle_t s_dev;
static bool s_ready;

static bool mpu_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return board_i2c_write(s_dev, buf, sizeof(buf)) == ESP_OK;
}

static bool mpu_read(uint8_t reg, uint8_t *data, size_t len)
{
    return board_i2c_write_read(s_dev, &reg, 1, data, len) == ESP_OK;
}

bool MPU6050_Init(void)
{
    if (!board_i2c_bus()) return false;
    if (board_i2c_add_device(BOARD_MPU6050_ADDR, &s_dev) != ESP_OK) return false;

    uint8_t who = 0;
    if (!mpu_read(MPU6050_WHO_AM_I, &who, 1) || who != MPU6050_DEVICE_ID) {
        printf("[MPU] whoami=0x%02x\n", who);
        return false;
    }

    if (!mpu_write(MPU6050_PWR_MGMT_1, 0x80)) return false;
    uint32_t waited = 0;
    while (waited < 100) {
        uint8_t pwr = 0x80;
        if (mpu_read(MPU6050_PWR_MGMT_1, &pwr, 1) && (pwr & 0x80) == 0) break;
        app_delay_ms(1);
        ++waited;
    }
    if (waited >= 100) {
        puts("[MPU] reset timeout");
        return false;
    }
    app_delay_ms(100);
    if (!mpu_write(MPU6050_SIGNAL_PATH_RESET, 0x07)) return false;
    app_delay_ms(100);
    if (!mpu_write(MPU6050_SMPLRT_DIV, 0x00)) return false;
    if (!mpu_write(MPU6050_CONFIG, 0x04)) return false;
    if (!mpu_write(MPU6050_GYRO_CONFIG, 0x08)) return false;
    if (!mpu_write(MPU6050_ACCEL_CONFIG, 0x10)) return false;
    if (!mpu_write(MPU6050_PWR_MGMT_1, 0x01)) return false;
    app_delay_ms(100);
    s_ready = true;
    return true;
}

bool MPU6050_Read(float *ax_ms2, float *ay_ms2, float *az_ms2,
                  float *gx_rads, float *gy_rads, float *gz_rads,
                  float *temp_c)
{
    if (!s_ready) return false;
    uint8_t buffer[14];
    if (!mpu_read(MPU6050_ACCEL_XOUT_H, buffer, sizeof(buffer))) return false;
    const int16_t raw_ax = (int16_t)((buffer[0] << 8) | buffer[1]);
    const int16_t raw_ay = (int16_t)((buffer[2] << 8) | buffer[3]);
    const int16_t raw_az = (int16_t)((buffer[4] << 8) | buffer[5]);
    const int16_t raw_temp = (int16_t)((buffer[6] << 8) | buffer[7]);
    const int16_t raw_gx = (int16_t)((buffer[8] << 8) | buffer[9]);
    const int16_t raw_gy = (int16_t)((buffer[10] << 8) | buffer[11]);
    const int16_t raw_gz = (int16_t)((buffer[12] << 8) | buffer[13]);
    if (ax_ms2) *ax_ms2 = (raw_ax / MPU6050_ACCEL_SCALE) * MPU6050_GRAVITY;
    if (ay_ms2) *ay_ms2 = (raw_ay / MPU6050_ACCEL_SCALE) * MPU6050_GRAVITY;
    if (az_ms2) *az_ms2 = (raw_az / MPU6050_ACCEL_SCALE) * MPU6050_GRAVITY;
    if (gx_rads) *gx_rads = (raw_gx / MPU6050_GYRO_SCALE) * MPU6050_DPS_TO_RADS;
    if (gy_rads) *gy_rads = (raw_gy / MPU6050_GYRO_SCALE) * MPU6050_DPS_TO_RADS;
    if (gz_rads) *gz_rads = (raw_gz / MPU6050_GYRO_SCALE) * MPU6050_DPS_TO_RADS;
    if (temp_c) *temp_c = (raw_temp / 340.0f) + 36.53f;
    return true;
}
