#include "max30102.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "MAX30102";

static esp_err_t write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(max30102_dev, buf, 2, 100);
}

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(max30102_dev, &reg, 1, data, len, 100);
}

esp_err_t max30102_init(void)
{
    esp_err_t ret;
    uint8_t part_id = 0;

    // ── 1. Verify part ID ─────────────────────────────────────────────────────
    ret = read_regs(MAX30102_REG_PART_ID, &part_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Part ID read failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (part_id != 0x15) {
        ESP_LOGE(TAG, "Unexpected Part ID: 0x%02X (expected 0x15)", part_id);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Part ID OK: 0x%02X", part_id);

    // ── 2. Reset the sensor ───────────────────────────────────────────────────
    ret = write_reg(MAX30102_REG_MODE_CONFIG, 0x40);  // RESET bit
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Reset failed"); return ret; }
    vTaskDelay(pdMS_TO_TICKS(20));

    // ── 3. FIFO config — 4 sample average, FIFO rollover enabled ─────────────
    // SMP_AVE = 010 (4 avg), FIFO_ROLLOVER_EN = 1 → 0x50
    ret = write_reg(MAX30102_REG_FIFO_CONFIG, 0x10);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "FIFO config failed"); return ret; }

    // ── 4. SpO2 config — ADC range 4096nA, 100Hz, 411us pulse width ──────────
    // ADC_RNG = 01, SR = 001, LED_PW = 11 → 0x27
    ret = write_reg(MAX30102_REG_SPO2_CONFIG, 0x27);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "SpO2 config failed"); return ret; }

    // ── 5. LED pulse amplitude — 6.4mA each ──────────────────────────────────
    ret = write_reg(MAX30102_REG_LED1_PA, 0x1F);  // Red
    if (ret != ESP_OK) { ESP_LOGE(TAG, "LED1 config failed"); return ret; }
    ret = write_reg(MAX30102_REG_LED2_PA, 0x1F);  // IR
    if (ret != ESP_OK) { ESP_LOGE(TAG, "LED2 config failed"); return ret; }

    // ── 6. Mode — SpO2 mode (Red + IR) ───────────────────────────────────────
    ret = write_reg(MAX30102_REG_MODE_CONFIG, 0x03);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Mode config failed"); return ret; }

    ESP_LOGI(TAG, "Initialised — SpO2 mode | 100Hz | 411us | 6.4mA LEDs");
    return ESP_OK;
}

uint8_t max30102_get_fifo_count(void)
{
    uint8_t wr_ptr = 0, rd_ptr = 0;
    read_regs(MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1);
    read_regs(MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1);

    // Number of unread samples in FIFO
    return (wr_ptr - rd_ptr) & 0x1F;
}

esp_err_t max30102_read_fifo(max30102_sample_t *sample)
{
    uint8_t raw[6] = {0};  // 3 bytes Red + 3 bytes IR

    esp_err_t ret = read_regs(MAX30102_REG_FIFO_DATA, raw, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FIFO read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Each sample is 18-bit, stored in 3 bytes — mask top 6 bits
    sample->red = ((uint32_t)(raw[0] << 16) | (uint32_t)(raw[1] << 8) | raw[2]) & 0x3FFFF;
    sample->ir  = ((uint32_t)(raw[3] << 16) | (uint32_t)(raw[4] << 8) | raw[5]) & 0x3FFFF;

    return ESP_OK;
}

esp_err_t max30102_read_buffer(uint32_t *red_buf, uint32_t *ir_buf, int32_t size)
{
    int32_t collected = 0;

    write_reg(MAX30102_REG_FIFO_WR_PTR, 0x00);
    write_reg(MAX30102_REG_FIFO_RD_PTR, 0x00);
    write_reg(0x05, 0x00);

    int64_t start_time = esp_timer_get_time(); // microseconds

    while (collected < size) {
        vTaskDelay(pdMS_TO_TICKS(50));

        uint8_t wr_ptr = 0, rd_ptr = 0;
        read_regs(MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1);
        read_regs(MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1);
        uint8_t available = (wr_ptr - rd_ptr) & 0x1F;

        int32_t to_read = available;
        if (collected + to_read > size) to_read = size - collected;

        for (int i = 0; i < to_read; i++) {
            max30102_sample_t sample;
            esp_err_t ret = max30102_read_fifo(&sample);
            if (ret != ESP_OK) return ret;
            red_buf[collected] = sample.red;
            ir_buf[collected]  = sample.ir;
            collected++;
        }
    }

    int64_t elapsed_us = esp_timer_get_time() - start_time;
    float actual_rate = (float)size / (elapsed_us / 1000000.0f);
    ESP_LOGI("MAX30102", "Collected %ld samples in %.2f sec — actual rate: %.1f Hz",
             size, elapsed_us / 1000000.0f, actual_rate);

    return ESP_OK;
}

bool max30102_finger_detected(uint32_t *ir_buf, int32_t size)
{
    // Calculate mean IR value
    uint32_t sum = 0;
    for (int i = 0; i < size; i++) sum += ir_buf[i];
    uint32_t mean = sum / size;

    // Finger present if IR mean is above threshold
    // No finger: ~500-2000, Finger present: ~50000+
    return (mean > 50000);
}