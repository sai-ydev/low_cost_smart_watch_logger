#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Registers
#define MAX30102_REG_INT_STATUS1    0x00
#define MAX30102_REG_FIFO_WR_PTR   0x04
#define MAX30102_REG_FIFO_RD_PTR   0x06
#define MAX30102_REG_FIFO_DATA     0x07
#define MAX30102_REG_FIFO_CONFIG   0x08
#define MAX30102_REG_MODE_CONFIG   0x09
#define MAX30102_REG_SPO2_CONFIG   0x0A
#define MAX30102_REG_LED1_PA       0x0C  // Red LED
#define MAX30102_REG_LED2_PA       0x0D  // IR LED
#define MAX30102_REG_PART_ID       0xFF  // Should return 0x15

// FIFO sample — 18-bit red and IR values
typedef struct {
    uint32_t red;
    uint32_t ir;
} max30102_sample_t;

esp_err_t max30102_init(void);
esp_err_t max30102_read_fifo(max30102_sample_t *sample);
uint8_t   max30102_get_fifo_count(void);

// Add to existing max30102.h
#define HR_BUFFER_SIZE  400

esp_err_t max30102_read_buffer(uint32_t *red_buf, uint32_t *ir_buf, int32_t size);


bool max30102_finger_detected(uint32_t *ir_buf, int32_t size);