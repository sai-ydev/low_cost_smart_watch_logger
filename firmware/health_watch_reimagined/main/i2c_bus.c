#include "i2c_bus.h"
#include "esp_log.h"

static const char *TAG = "I2C_BUS";

// Define the handles
i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t max30102_dev;
i2c_master_dev_handle_t max30205_dev;
i2c_master_dev_handle_t lsm6ds3_dev;
i2c_master_dev_handle_t rv8803_dev;

esp_err_t i2c_bus_init(void)
{
    esp_err_t ret;

    // ── 1. Configure and create the bus ──────────────────────────────────────
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port        = I2C_BUS_PORT,
        .sda_io_num      = I2C_SDA_GPIO,
        .scl_io_num      = I2C_SCL_GPIO,
        .clk_source      = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false, // You have external pull-ups
    };

    ret = i2c_new_master_bus(&bus_cfg, &i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C bus created on SDA=%d SCL=%d", I2C_SDA_GPIO, I2C_SCL_GPIO);

    // ── 2. Register each device on the bus ───────────────────────────────────
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz    = I2C_BUS_SPEED,
    };

    // MAX30102
    dev_cfg.device_address = MAX30102_ADDR;
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &max30102_dev);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "MAX30102 add failed"); return ret; }

    // MAX30205
    dev_cfg.device_address = MAX30205_ADDR;
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &max30205_dev);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "MAX30205 add failed"); return ret; }

    // LSM6DS3TR-C
    dev_cfg.device_address = LSM6DS3_ADDR;
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &lsm6ds3_dev);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "LSM6DS3 add failed"); return ret; }

    // RV8803
    dev_cfg.device_address = RV8803_ADDR;
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &rv8803_dev);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "RV8803 add failed"); return ret; }

    ESP_LOGI(TAG, "All I2C devices registered");
    return ESP_OK;
}