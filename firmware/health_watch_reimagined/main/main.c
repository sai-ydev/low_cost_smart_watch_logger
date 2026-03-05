#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"
#include "max30205.h"
#include "rv8803.h"
#include "lsm6ds3.h"
#include "max30102.h"
#include "spo2_algorithm.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "ble_server.h"

static const char *TAG = "MAIN";

static uint32_t ir_buffer[BUFFER_SIZE];
static uint32_t red_buffer[BUFFER_SIZE];

void sensor_task(void *pvParameters)
{
    float temperature = 0.0f;
    rv8803_time_t current_time = {0};
    lsm6ds3_data_t imu = {0};
    uint16_t steps = 0;
    int32_t heart_rate = 0, spo2 = 0;
    int8_t hr_valid = 0, spo2_valid = 0;

    lsm6ds3_reset_steps();

    while (1) {
        // ── Simulated HR and SpO2 ─────────────────────────────────────────────────────
        // ── Simulated HR and SpO2 ────────────────────────────────────────────────────
        static uint32_t sim_seed = 12345;

        // Simple LCG random number generator (no stdlib rand needed)
        sim_seed = sim_seed * 1664525 + 1013904223;
        int32_t sim_hr = 60 + (int32_t)((sim_seed >> 16) % 26);  // 60-85 bpm

        sim_seed = sim_seed * 1664525 + 1013904223;
        int32_t sim_spo2 = 95 + (int32_t)((sim_seed >> 16) % 5); // 95-99%

        heart_rate = sim_hr;
        spo2       = sim_spo2;
        hr_valid   = 1;
        spo2_valid = 1;

        rv8803_get_time(&current_time);
        vTaskDelay(pdMS_TO_TICKS(100));

        max30205_read_temperature(&temperature);
        vTaskDelay(pdMS_TO_TICKS(100));

        lsm6ds3_read(&imu);
        vTaskDelay(pdMS_TO_TICKS(100));

        lsm6ds3_read_steps(&steps);
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "──────────────────────────────────────");
        ESP_LOGI(TAG, "Time:  %04d-%02d-%02d %02d:%02d:%02d",
                 current_time.year, current_time.month, current_time.date,
                 current_time.hours, current_time.minutes, current_time.seconds);
        ESP_LOGI(TAG, "Temp:  %.4f °C", temperature);
        ESP_LOGI(TAG, "Accel  X: %+.4f  Y: %+.4f  Z: %+.4f  g",
                 imu.accel_x, imu.accel_y, imu.accel_z);
        ESP_LOGI(TAG, "Gyro   X: %+.4f  Y: %+.4f  Z: %+.4f  dps",
                 imu.gyro_x, imu.gyro_y, imu.gyro_z);
        ESP_LOGI(TAG, "Steps: %d", steps);

        if (hr_valid) {
            ESP_LOGI(TAG, "HR:    %ld bpm", heart_rate);
        } else {
            ESP_LOGW(TAG, "HR:    invalid — keep finger still on sensor");
        }

        if (spo2_valid) {
            ESP_LOGI(TAG, "SpO2:  %ld %%", spo2);
        } else {
            ESP_LOGW(TAG, "SpO2:  invalid — keep finger still on sensor");
        }
        ESP_LOGI(TAG, "──────────────────────────────────────");

        // In sensor_task(), after reading all sensors, add:
        if (ble_is_connected()) {
            ESP_LOGI("MAIN", "BLE connected — sending updates. HR valid=%d IMU sending", hr_valid);
            if (hr_valid)   ble_update_heart_rate(heart_rate);
            if (spo2_valid) ble_update_spo2(spo2);
            ble_update_temperature(temperature);
            ble_update_steps(steps);
            ble_update_imu(imu.accel_x, imu.accel_y, imu.accel_z,
                        imu.gyro_x,  imu.gyro_y,  imu.gyro_z);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(i2c_bus_init());
    ESP_ERROR_CHECK(max30205_init());
    ESP_ERROR_CHECK(rv8803_init());
    ESP_ERROR_CHECK(lsm6ds3_init());
    ESP_ERROR_CHECK(max30102_init());

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true
    };
    esp_task_wdt_reconfigure(&wdt_config);

    rv8803_time_t set_time = {
        .seconds = 0, .minutes = 30, .hours = 10,
        .date = 27,   .month = 2,   .year = 2026
    };
    ESP_ERROR_CHECK(rv8803_set_time(&set_time));

    // In app_main(), add before xTaskCreate:
    ESP_ERROR_CHECK(ble_server_init());

    xTaskCreate(sensor_task, "sensor_task", 16384, NULL, 5, NULL);

    // app_main returns here — this is correct and expected in ESP-IDF
    // The FreeRTOS scheduler continues running sensor_task
}