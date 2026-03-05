#include "rv8803.h"
#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "RV8803";

static uint8_t bcd_to_dec(uint8_t bcd) { return (bcd >> 4) * 10 + (bcd & 0x0F); }
static uint8_t dec_to_bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

esp_err_t rv8803_init(void)
{
    // Read seconds register to verify communication
    uint8_t reg = RV8803_REG_SECONDS;
    uint8_t data = 0;

    esp_err_t ret = i2c_master_transmit_receive(rv8803_dev, &reg, 1, &data, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initialised");
    return ESP_OK;
}

esp_err_t rv8803_set_time(const rv8803_time_t *time)
{
    // Write all time registers in one transaction starting at 0x00
    uint8_t buf[8];
    buf[0] = RV8803_REG_SECONDS;               // Register pointer
    buf[1] = dec_to_bcd(time->seconds);
    buf[2] = dec_to_bcd(time->minutes);
    buf[3] = dec_to_bcd(time->hours);
    buf[4] = 0x01;                              // Weekday — not critical, set to 1
    buf[5] = dec_to_bcd(time->date);
    buf[6] = dec_to_bcd(time->month);
    buf[7] = dec_to_bcd(time->year - 2000);    // RV8803 stores 00-99

    esp_err_t ret = i2c_master_transmit(rv8803_dev, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set time failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Time set: %04d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->date,
             time->hours, time->minutes, time->seconds);
    return ESP_OK;
}

esp_err_t rv8803_get_time(rv8803_time_t *time)
{
    uint8_t reg = RV8803_REG_SECONDS;
    uint8_t data[7] = {0};

    // Read 7 registers starting from seconds
    esp_err_t ret = i2c_master_transmit_receive(rv8803_dev, &reg, 1, data, 7, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get time failed: %s", esp_err_to_name(ret));
        return ret;
    }

    time->seconds = bcd_to_dec(data[0] & 0x7F);  // Mask bit 7 (unused)
    time->minutes = bcd_to_dec(data[1] & 0x7F);
    time->hours   = bcd_to_dec(data[2] & 0x3F);  // Mask bits 7:6 (unused)
    // data[3] = weekday, skip
    time->date    = bcd_to_dec(data[4] & 0x3F);
    time->month   = bcd_to_dec(data[5] & 0x1F);
    time->year    = bcd_to_dec(data[6]) + 2000;

    return ESP_OK;
}