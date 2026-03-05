#pragma once

#include "esp_err.h"
#include <stdint.h>

// Registers
#define LSM6DS3_WHO_AM_I        0x0F
#define LSM6DS3_FUNC_CFG_ACCESS 0x01
#define LSM6DS3_CTRL1_XL        0x10
#define LSM6DS3_CTRL2_G         0x11
#define LSM6DS3_CTRL3_C         0x12
#define LSM6DS3_TAP_CFG         0x58
#define LSM6DS3_OUTX_L_G        0x22
#define LSM6DS3_OUTX_L_XL       0x28
#define LSM6DS3_STEP_COUNTER_L  0x4B
#define LSM6DS3_STEP_COUNTER_H  0x4C

// Sensitivity
#define ACCEL_SENSITIVITY   0.000061f   // g/LSB   (±2g)
#define GYRO_SENSITIVITY    0.00875f    // dps/LSB (250dps)

typedef struct {
    float accel_x, accel_y, accel_z;   // g
    float gyro_x,  gyro_y,  gyro_z;    // dps
} lsm6ds3_data_t;

esp_err_t lsm6ds3_init(void);
esp_err_t lsm6ds3_read(lsm6ds3_data_t *data);
esp_err_t lsm6ds3_read_steps(uint16_t *steps);
esp_err_t lsm6ds3_reset_steps(void);