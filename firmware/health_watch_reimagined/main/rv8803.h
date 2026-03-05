#pragma once

#include "esp_err.h"
#include <stdint.h>

#define RV8803_REG_SECONDS  0x00
#define RV8803_REG_MINUTES  0x01
#define RV8803_REG_HOURS    0x02
#define RV8803_REG_WEEKDAY  0x03
#define RV8803_REG_DATE     0x04
#define RV8803_REG_MONTH    0x05
#define RV8803_REG_YEAR     0x06

typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t date;
    uint8_t month;
    uint16_t year;      // Full year e.g. 2025
} rv8803_time_t;

esp_err_t rv8803_init(void);
esp_err_t rv8803_set_time(const rv8803_time_t *time);
esp_err_t rv8803_get_time(rv8803_time_t *time);