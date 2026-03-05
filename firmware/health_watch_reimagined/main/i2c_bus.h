#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

// Bus config
#define I2C_BUS_PORT    I2C_NUM_0
#define I2C_SCL_GPIO    18
#define I2C_SDA_GPIO    19
#define I2C_BUS_SPEED   100000   // 100 kHz — safe for all 4 sensors

// Device I2C addresses
#define MAX30102_ADDR   0x57
#define MAX30205_ADDR   0x48
#define LSM6DS3_ADDR    0x6A
#define RV8803_ADDR     0x32

// Bus handle — extern so all sensor files can access it
extern i2c_master_bus_handle_t i2c_bus;

// Device handles — extern so sensor drivers can use them
extern i2c_master_dev_handle_t max30102_dev;
extern i2c_master_dev_handle_t max30205_dev;
extern i2c_master_dev_handle_t lsm6ds3_dev;
extern i2c_master_dev_handle_t rv8803_dev;

esp_err_t i2c_bus_init(void);