#ifndef MPU6050_ESP32_H
#define MPU6050_ESP32_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool MPU6050_Init(void);
bool MPU6050_Read(float *ax_ms2, float *ay_ms2, float *az_ms2,
                  float *gx_rads, float *gy_rads, float *gz_rads,
                  float *temp_c);

#ifdef __cplusplus
}
#endif

#endif
