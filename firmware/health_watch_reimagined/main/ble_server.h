#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// ── UUIDs ─────────────────────────────────────────────────────────────────────
#define HEALTH_SERVICE_UUID         0x1234
#define CHAR_HEART_RATE_UUID        0x1235
#define CHAR_SPO2_UUID              0x1236
#define CHAR_TEMPERATURE_UUID       0x1237
#define CHAR_STEPS_UUID             0x1238
#define CHAR_IMU_UUID               0x1239

#define TIME_SERVICE_UUID           0x1240
#define CHAR_DATETIME_UUID          0x1241

// ── Data structures sent over BLE ─────────────────────────────────────────────

// IMU packet — 6 floats = 24 bytes
typedef struct __attribute__((packed)) {
    float accel_x, accel_y, accel_z;
    float gyro_x,  gyro_y,  gyro_z;
} ble_imu_t;

// DateTime packet for RTC sync
typedef struct __attribute__((packed)) {
    uint16_t year;
    uint8_t  month;
    uint8_t  date;
    uint8_t  hours;
    uint8_t  minutes;
    uint8_t  seconds;
} ble_datetime_t;

// ── API ───────────────────────────────────────────────────────────────────────
esp_err_t ble_server_init(void);

// Call these from sensor_task to update characteristic values
void ble_update_heart_rate(int32_t bpm);
void ble_update_spo2(int32_t spo2);
void ble_update_temperature(float temp);
void ble_update_steps(uint16_t steps);
void ble_update_imu(float ax, float ay, float az,
                    float gx, float gy, float gz);

// Returns true if a client is connected
bool ble_is_connected(void);