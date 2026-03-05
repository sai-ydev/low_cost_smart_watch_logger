#pragma once

#include "esp_err.h"

#define MAX30205_REG_TEMP   0x00
#define MAX30205_REG_CONF   0x01

esp_err_t max30205_init(void);
esp_err_t max30205_read_temperature(float *temperature);