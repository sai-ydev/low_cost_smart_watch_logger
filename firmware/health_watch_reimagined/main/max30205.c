#include "max30205.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "MAX30205";

esp_err_t max30205_init(void)
{
    // Default config register state is fine for continuous mode.
    // Write 0x00 to config to ensure it's awake (not shutdown).
    uint8_t write_buf[2] = { MAX30205_REG_CONF, 0x00 };

    esp_err_t ret = i2c_master_transmit(max30205_dev, write_buf, sizeof(write_buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initialised");
    return ESP_OK;
}

esp_err_t max30205_read_temperature(float *temperature)
{
    uint8_t reg = MAX30205_REG_TEMP;
    uint8_t data[2] = {0};

    // Write the register pointer, then read 2 bytes
    esp_err_t ret = i2c_master_transmit_receive(max30205_dev,
                                                 &reg, 1,
                                                 data, 2,
                                                 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Combine MSB and LSB into signed 16-bit value
    int16_t raw = (int16_t)((data[0] << 8) | data[1]);

    // Convert to Celsius
    *temperature = raw * 0.00390625f;

    return ESP_OK;
}