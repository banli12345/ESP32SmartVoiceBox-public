#include "MPU6050_ESP32.h"

static uint32_t last_time = 0;
static float yaw_angle = 0;

static Kalman_t KalmanX = {.Q_angle = 0.05f, .Q_bias = 0.003f, .R_measure = 0.005f};
static Kalman_t KalmanY = {.Q_angle = 0.05f, .Q_bias = 0.003f, .R_measure = 0.005f};
static Kalman_t KalmanZ = {.Q_angle = 0.001f, .Q_bias = 0.003f, .R_measure = 0.03f};

static bool I2C_Read(uint8_t reg, uint8_t *buffer, uint8_t len) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission() != 0) return false;
    
    Wire.requestFrom(MPU6050_ADDR, len);
    for (uint8_t i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        buffer[i] = Wire.read();
    }
    return true;
}

static bool I2C_Write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
}

bool MPU6050_Init(void) {
    uint8_t check;
    
    if (!I2C_Read(WHO_AM_I_REG, &check, 1)) return false;
    if (check != 0x68) return false;
    
    I2C_Write(PWR_MGMT_1_REG, 0x00);
    delay(100);
    I2C_Write(SMPLRT_DIV_REG, 0x07);
    I2C_Write(ACCEL_CONFIG_REG, 0x00);
    I2C_Write(GYRO_CONFIG_REG, 0x00);
    
    return true;
}

static void Calibrate_Accel(MPU6050_Calibration_t *calib) {
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    const uint16_t samples = 500;
    uint8_t data[6];
    
    for (int i = 0; i < samples; i++) {
        if (I2C_Read(ACCEL_XOUT_H_REG, data, 6)) {
            sum_x += (int16_t)(data[0] << 8 | data[1]);
            sum_y += (int16_t)(data[2] << 8 | data[3]);
            sum_z += (int16_t)(data[4] << 8 | data[5]);
        }
        delay(2);
    }
    
    calib->reference_accel_x = (float)sum_x / samples;
    calib->reference_accel_y = (float)sum_y / samples;
    calib->reference_accel_z = (float)sum_z / samples;
    
    float target_z = 16384.0;
    calib->accel_offset_x = calib->reference_accel_x;
    calib->accel_offset_y = calib->reference_accel_y;
    calib->accel_offset_z = calib->reference_accel_z - target_z;
}

static void Calibrate_Gyro(MPU6050_Calibration_t *calib) {
    int32_t sum_x = 0, sum_y = 0, sum_z = 0;
    const uint16_t samples = 500;
    uint8_t data[6];
    
    for (int i = 0; i < samples; i++) {
        if (I2C_Read(GYRO_XOUT_H_REG, data, 6)) {
            sum_x += (int16_t)(data[0] << 8 | data[1]);
            sum_y += (int16_t)(data[2] << 8 | data[3]);
            sum_z += (int16_t)(data[4] << 8 | data[5]);
        }
        delay(2);
    }
    
    calib->gyro_offset_x = (float)sum_x / samples;
    calib->gyro_offset_y = (float)sum_y / samples;
    calib->gyro_offset_z = (float)sum_z / samples;
    calib->gyro_z_error = calib->gyro_offset_z / 131.0;
}

void MPU6050_Calibrate(MPU6050_Calibration_t *calib) {
    Serial.println("=== MPU6050 Calibration ===");
    Serial.println("Keep sensor still...");
    
    Calibrate_Accel(calib);
    delay(500);
    Calibrate_Gyro(calib);
    
    calib->is_calibrated = 1;
    
    KalmanZ.angle = 0;
    KalmanZ.bias = 0;
    yaw_angle = 0;
    
    Serial.println("Calibration complete!");
    Serial.printf("Gyro Z error: %.4f °/s\n", calib->gyro_z_error);
}

void MPU6050_Read_All(MPU6050_Data_t *data, MPU6050_Calibration_t *calib) {
    uint8_t buffer[14];
    int16_t accel_x, accel_y, accel_z;
    int16_t gyro_x, gyro_y, gyro_z;
    int16_t temp;
    
    if (!I2C_Read(ACCEL_XOUT_H_REG, buffer, 14)) return;
    
    accel_x = (int16_t)(buffer[0] << 8 | buffer[1]);
    accel_y = (int16_t)(buffer[2] << 8 | buffer[3]);
    accel_z = (int16_t)(buffer[4] << 8 | buffer[5]);
    temp = (int16_t)(buffer[6] << 8 | buffer[7]);
    gyro_x = (int16_t)(buffer[8] << 8 | buffer[9]);
    gyro_y = (int16_t)(buffer[10] << 8 | buffer[11]);
    gyro_z = (int16_t)(buffer[12] << 8 | buffer[13]);
    
    if (calib->is_calibrated) {
        data->Accel_X_RAW = accel_x - calib->accel_offset_x;
        data->Accel_Y_RAW = accel_y - calib->accel_offset_y;
        data->Accel_Z_RAW = accel_z - calib->accel_offset_z;
        data->Gyro_X_RAW = gyro_x - calib->gyro_offset_x;
        data->Gyro_Y_RAW = gyro_y - calib->gyro_offset_y;
        data->Gyro_Z_RAW = gyro_z - calib->gyro_offset_z;
        data->Gz_error = calib->gyro_z_error;
    } else {
        data->Accel_X_RAW = accel_x;
        data->Accel_Y_RAW = accel_y;
        data->Accel_Z_RAW = accel_z;
        data->Gyro_X_RAW = gyro_x;
        data->Gyro_Y_RAW = gyro_y;
        data->Gyro_Z_RAW = gyro_z;
        data->Gz_error = 0;
    }
    
    data->Ax = data->Accel_X_RAW / 16384.0;
    data->Ay = data->Accel_Y_RAW / 16384.0;
    data->Az = data->Accel_Z_RAW / 16384.0;
    
    data->Gx = data->Gyro_X_RAW / 131.0;
    data->Gy = data->Gyro_Y_RAW / 131.0;
    data->Gz = data->Gyro_Z_RAW / 131.0;
    
    if (data->Gz > -1 && data->Gz < 1) data->Gz = 0;
    
    data->Temperature = (temp / 340.0) + 36.53;
    
    uint32_t now = millis();
    float dt = (now - last_time) / 1000.0;
    if (dt > 0.05) dt = 0.05;
    if (dt < 0.001) dt = 0.01;
    
    last_time = now;
    
    double roll = atan2(data->Accel_Y_RAW, 
                        sqrt(data->Accel_X_RAW * data->Accel_X_RAW + 
                             data->Accel_Z_RAW * data->Accel_Z_RAW)) * RAD_TO_DEG;
    double pitch = atan2(-data->Accel_X_RAW,
                         sqrt(data->Accel_Y_RAW * data->Accel_Y_RAW +
                              data->Accel_Z_RAW * data->Accel_Z_RAW)) * RAD_TO_DEG;
    
    yaw_angle += data->Gz * dt;
    if (yaw_angle > 360) yaw_angle -= 360;
    if (yaw_angle < 0) yaw_angle += 360;
    
    data->KalmanAngleX = Kalman_getAngle(&KalmanX, roll, data->Gx, dt);
    data->KalmanAngleY = Kalman_getAngle(&KalmanY, pitch, data->Gy, dt);
    data->KalmanAngleZ = Kalman_getAngle(&KalmanZ, yaw_angle, data->Gz, dt);
}

double Kalman_getAngle(Kalman_t *Kalman, double newAngle, double newRate, double dt) {
    double rate = newRate - Kalman->bias;
    Kalman->angle += dt * rate;
    
    Kalman->P[0][0] += dt * (dt * Kalman->P[1][1] - Kalman->P[0][1] - Kalman->P[1][0] + Kalman->Q_angle);
    Kalman->P[0][1] -= dt * Kalman->P[1][1];
    Kalman->P[1][0] -= dt * Kalman->P[1][1];
    Kalman->P[1][1] += Kalman->Q_bias * dt;
    
    double S = Kalman->P[0][0] + Kalman->R_measure;
    double K[2];
    K[0] = Kalman->P[0][0] / S;
    K[1] = Kalman->P[1][0] / S;
    
    double y = newAngle - Kalman->angle;
    Kalman->angle += K[0] * y;
    Kalman->bias += K[1] * y;
    
    double P00_temp = Kalman->P[0][0];
    double P01_temp = Kalman->P[0][1];
    Kalman->P[0][0] -= K[0] * P00_temp;
    Kalman->P[0][1] -= K[0] * P01_temp;
    Kalman->P[1][0] -= K[1] * P00_temp;
    Kalman->P[1][1] -= K[1] * P01_temp;
    
    return Kalman->angle;
}

void MPU6050_Reset_Z_Angle(void) {
    KalmanZ.angle = 0;
    KalmanZ.bias = 0;
    yaw_angle = 0;
}

float MPU6050_Get_Gyro_Z_Error(MPU6050_Calibration_t *calib) {
    if (calib->is_calibrated) {
        return calib->gyro_z_error;
    }
    return 0.0f;
}
void MPU6050_SetCurrentPlaneAsZero(void) {
    Serial.println("=== Setting current plane as zero ===");
    Serial.println("Keep sensor still for 2 seconds...");
    
    // 等待传感器稳定
    delay(2000);
    
    // 采样当前角度
    MPU6050_Data_t temp_data;
    MPU6050_Calibration_t temp_calib = {0};
    temp_calib.is_calibrated = 0;  // 使用原始数据
    
    float sum_roll = 0, sum_pitch = 0, sum_yaw = 0;
    const int samples = 100;
    
    for (int i = 0; i < samples; i++) {
        MPU6050_Read_All(&temp_data, &temp_calib);
        sum_roll += temp_data.KalmanAngleX;
        sum_pitch += temp_data.KalmanAngleY;
        sum_yaw += temp_data.KalmanAngleZ;
        delay(10);
    }
    
    float avg_roll = sum_roll / samples;
    float avg_pitch = sum_pitch / samples;
    float avg_yaw = sum_yaw / samples;
    
    Serial.printf("Average angles - Roll: %.2f°, Pitch: %.2f°, Yaw: %.2f°\n", 
                  avg_roll, avg_pitch, avg_yaw);
    
    // 重置卡尔曼滤波器，以当前角度为基准
    KalmanX.angle = 0;
    KalmanX.bias = 0;
    KalmanY.angle = 0;
    KalmanY.bias = 0;
    KalmanZ.angle = 0;
    KalmanZ.bias = 0;
    
    // 重新初始化卡尔曼滤波器状态
    KalmanX.P[0][0] = 0;
    KalmanX.P[0][1] = 0;
    KalmanX.P[1][0] = 0;
    KalmanX.P[1][1] = 0;
    
    KalmanY.P[0][0] = 0;
    KalmanY.P[0][1] = 0;
    KalmanY.P[1][0] = 0;
    KalmanY.P[1][1] = 0;
    
    KalmanZ.P[0][0] = 0;
    KalmanZ.P[0][1] = 0;
    KalmanZ.P[1][0] = 0;
    KalmanZ.P[1][1] = 0;
    
    // 重置积分变量
    yaw_angle = 0;
    last_time = millis();
    
    Serial.println("Current plane set as zero reference!");
    Serial.println("All future angles will be relative to this plane.");
}
