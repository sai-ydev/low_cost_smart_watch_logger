#include "lsm6ds3.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LSM6DS3";

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(lsm6ds3_dev, buf, 2, 100);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(lsm6ds3_dev, &reg, 1, data, len, 100);
}

esp_err_t lsm6ds3_init(void)
{
    esp_err_t ret;
    uint8_t who_am_i = 0;

    // ── 0. Ensure embedded functions page is DISABLED first ───────────────────
    // If a previous run left FUNC_CFG_ACCESS enabled, WHO_AM_I (0x0F) will
    // read from the embedded page instead — giving 0x10 instead of 0x6A
    ret = write_reg(LSM6DS3_FUNC_CFG_ACCESS, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FUNC_CFG_ACCESS disable failed: %s", esp_err_to_name(ret));
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // ── 1. Software reset ─────────────────────────────────────────────────────
    // SW_RESET bit (bit 0) of CTRL3_C — clears all registers to default
    ret = write_reg(LSM6DS3_CTRL3_C, 0x01);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "SW reset failed"); return ret; }
    vTaskDelay(pdMS_TO_TICKS(20));  // Wait for reset to complete

    // ── 2. Verify chip identity ───────────────────────────────────────────────
    ret = read_regs(LSM6DS3_WHO_AM_I, &who_am_i, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WHO_AM_I read failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (who_am_i != 0x6A) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I: 0x%02X (expected 0x6A)", who_am_i);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "WHO_AM_I OK: 0x%02X", who_am_i);

    // ── 3. CTRL3_C — BDU + auto-increment (re-apply after reset) ─────────────
    ret = write_reg(LSM6DS3_CTRL3_C, 0x44);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "CTRL3_C failed"); return ret; }

    // ── 4. CTRL1_XL — accelerometer 26Hz, ±2g ────────────────────────────────
    ret = write_reg(LSM6DS3_CTRL1_XL, 0x20);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "CTRL1_XL failed"); return ret; }

    // ── 5. CTRL2_G — gyroscope 104Hz, 250dps ─────────────────────────────────
    ret = write_reg(LSM6DS3_CTRL2_G, 0x40);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "CTRL2_G failed"); return ret; }

    // ── 6. Enable embedded functions ──────────────────────────────────────────
    ret = write_reg(LSM6DS3_FUNC_CFG_ACCESS, 0x80);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "FUNC_CFG_ACCESS failed"); return ret; }

    // ── 7. TAP_CFG — enable pedometer ─────────────────────────────────────────
    ret = write_reg(LSM6DS3_TAP_CFG, 0xC1);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "TAP_CFG failed"); return ret; }

    // ── 8. Disable embedded functions page (return to normal register map) ────
    ret = write_reg(LSM6DS3_FUNC_CFG_ACCESS, 0x00);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "FUNC_CFG_ACCESS re-disable failed"); return ret; }

    ESP_LOGI(TAG, "Initialised — Accel: 26Hz ±2g | Gyro: 104Hz 250dps | Pedometer: ON");
    return ESP_OK;
}

esp_err_t lsm6ds3_read(lsm6ds3_data_t *data)
{
    esp_err_t ret;
    uint8_t raw[6];

    // ── Read gyroscope ────────────────────────────────────────────────────────
    ret = read_regs(LSM6DS3_OUTX_L_G, raw, 6);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Gyro read failed"); return ret; }

    int16_t gx = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t gy = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t gz = (int16_t)((raw[5] << 8) | raw[4]);

    data->gyro_x = gx * GYRO_SENSITIVITY;
    data->gyro_y = gy * GYRO_SENSITIVITY;
    data->gyro_z = gz * GYRO_SENSITIVITY;

    // ── Read accelerometer ────────────────────────────────────────────────────
    ret = read_regs(LSM6DS3_OUTX_L_XL, raw, 6);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Accel read failed"); return ret; }

    int16_t ax = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ay = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t az = (int16_t)((raw[5] << 8) | raw[4]);

    data->accel_x = ax * ACCEL_SENSITIVITY;
    data->accel_y = ay * ACCEL_SENSITIVITY;
    data->accel_z = az * ACCEL_SENSITIVITY;

    return ESP_OK;
}

esp_err_t lsm6ds3_read_steps(uint16_t *steps)
{
    uint8_t raw[2] = {0};

    esp_err_t ret = read_regs(LSM6DS3_STEP_COUNTER_L, raw, 2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Step counter read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    *steps = (uint16_t)((raw[1] << 8) | raw[0]);
    return ESP_OK;
}

esp_err_t lsm6ds3_reset_steps(void)
{
    esp_err_t ret;

    // Set PEDO_RST_STEP bit in CTRL10_C (0x19)
    ret = write_reg(0x19, 0x02);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Step reset failed"); return ret; }

    // Small busy-wait instead of vTaskDelay — safe before scheduler starts
    for (volatile int i = 0; i < 100000; i++);

    ret = write_reg(0x19, 0x00);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Step reset clear failed"); return ret; }

    ESP_LOGI(TAG, "Step counter reset");
    return ESP_OK;
}