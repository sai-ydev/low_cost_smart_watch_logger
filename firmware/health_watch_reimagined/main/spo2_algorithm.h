#pragma once

#include <stdint.h>

#define BUFFER_SIZE         400   // 100 samples @ 100Hz = 1 second window
#define MA4_SIZE            4     // Moving average filter size
#define HAMMING_SIZE        5     // FIR filter size
#define SPO2_VALID_MIN      80    // Reject SpO2 below 80%
#define HR_VALID_MIN        20    // Reject HR below 20 bpm
#define HR_VALID_MAX        250   // Reject HR above 250 bpm
#define SAMPLE_RATE_HZ  100


void maxim_heart_rate_and_oxygen_saturation(
    uint32_t *ir_buffer,       // IR samples
    int32_t   ir_buffer_length,
    uint32_t *red_buffer,      // Red samples
    int32_t  *spo2,            // Output SpO2 (%)
    int8_t   *spo2_valid,      // 1 = valid result
    int32_t  *heart_rate,      // Output HR (bpm)
    int8_t   *hr_valid         // 1 = valid result
);

// Internal helpers — exposed for testing
void maxim_find_peaks(
    int32_t *locs,
    int32_t *n_peaks,
    int32_t *input,
    int32_t size,
    int32_t min_height,
    int32_t min_distance,
    int32_t max_num
);

void maxim_peaks_above_min_height(
    int32_t *locs,
    int32_t *n_peaks,
    int32_t *input,
    int32_t size,
    int32_t min_height
);

void maxim_remove_close_peaks(
    int32_t *locs,
    int32_t *n_peaks,
    int32_t *input,
    int32_t min_distance
);

void maxim_sort_ascend(int32_t *sort_buffer, int32_t size);
void maxim_sort_indices_descend(int32_t *input, int32_t *sort_indices, int32_t size);