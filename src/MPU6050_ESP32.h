#ifndef MPU6050_ESP32_H
#define MPU6050_ESP32_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#define RAD_TO_DEG 57.295779513082320876798154814105

// MPU6050 寄存器地址
#define WHO_AM_I_REG 0x75
#define PWR_MGMT_1_REG 0x6B
#define SMPLRT_DIV_REG 0x19
#define ACCEL_CONFIG_REG 0x1C
#define ACCEL_XOUT_H_REG 0x3B
#define TEMP_OUT_H_REG 0x41
#define GYRO_CONFIG_REG 0x1B
#define GYRO_XOUT_H_REG 0x43

// MPU6050 I2C 地址
#define MPU6050_ADDR 0x68

// ========== 卡尔曼滤波器结构体 ==========
typedef struct {
    double Q_angle;
    double Q_bias;
    double R_measure;
    double angle;
    double bias;
    double P[2][2];
} Kalman_t;

// ========== 校准参数结构体 ==========
typedef struct {
    float accel_offset_x;
    float accel_offset_y;
    float accel_offset_z;
    float gyro_offset_x;
    float gyro_offset_y;
    float gyro_offset_z;
    float gyro_z_error;
    float reference_accel_x;
    float reference_accel_y;
    float reference_accel_z;
    uint8_t is_calibrated;
} MPU6050_Calibration_t;

// ========== MPU6050 数据结构体 ==========
typedef struct {
    int16_t Accel_X_RAW;
    int16_t Accel_Y_RAW;
    int16_t Accel_Z_RAW;
    int16_t Gyro_X_RAW;
    int16_t Gyro_Y_RAW;
    int16_t Gyro_Z_RAW;
    
    double Ax, Ay, Az;
    double Gx, Gy, Gz;
    float Temperature;
    
    double KalmanAngleX;
    double KalmanAngleY;
    double KalmanAngleZ;
    
    float Gz_error;
} MPU6050_Data_t;

// ========== 函数声明 ==========
bool MPU6050_Init(void);
void MPU6050_Calibrate(MPU6050_Calibration_t *calib);
void MPU6050_Read_All(MPU6050_Data_t *data, MPU6050_Calibration_t *calib);
double Kalman_getAngle(Kalman_t *Kalman, double newAngle, double newRate, double dt);
void MPU6050_Reset_Z_Angle(void);
float MPU6050_Get_Gyro_Z_Error(MPU6050_Calibration_t *calib);
void MPU6050_SetCurrentPlaneAsZero(void);
#endif
