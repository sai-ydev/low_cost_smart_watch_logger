#include "spo2_algorithm.h"
#include <string.h>
#include <stdint.h>
#include "esp_log.h"

// ── SpO2 lookup table (Maxim empirical calibration) ───────────────────────────
// Indexed by R value * 10 (0.0 to 3.0 in 0.1 steps)
static const uint8_t uch_spo2_table[184] = {
    95, 95, 95, 96, 96, 96, 97, 97, 97, 97,
    97, 98, 98, 98, 98, 98, 99, 99, 99, 99,
    99, 99, 99, 99, 100,100,100,100,100,100,
    100,100,100,100,100,100,100,100,100,100,
    99, 99, 99, 99, 99, 99, 99, 99, 98, 98,
    98, 98, 98, 98, 97, 97, 97, 97, 97, 96,
    96, 96, 96, 95, 95, 95, 95, 94, 94, 94,
    94, 93, 93, 93, 93, 92, 92, 92, 92, 91,
    91, 91, 90, 90, 90, 89, 89, 89, 88, 88,
    88, 87, 87, 87, 86, 86, 86, 85, 85, 85,
    84, 84, 84, 83, 83, 83, 82, 82, 82, 81,
    81, 80, 80, 80, 79, 79, 78, 78, 78, 77,
    77, 76, 76, 76, 75, 75, 74, 74, 73, 73,
    73, 72, 72, 71, 71, 70, 70, 69, 69, 68,
    68, 67, 67, 66, 66, 65, 65, 64, 64, 63,
    63, 62, 62, 61, 61, 60, 60, 59, 59, 58,
    58, 57, 57, 56, 56, 55, 55, 54, 54, 53,
    53, 52, 52, 51, 51, 50, 50, 49, 49, 48,
    48, 47, 47, 46
};

// ── Sorting helpers ───────────────────────────────────────────────────────────

void maxim_sort_ascend(int32_t *sort_buffer, int32_t size)
{
    int32_t i, j, temp;
    for (i = 1; i < size; i++) {
        temp = sort_buffer[i];
        for (j = i; j > 0 && temp < sort_buffer[j-1]; j--) {
            sort_buffer[j] = sort_buffer[j-1];
        }
        sort_buffer[j] = temp;
    }
}

void maxim_sort_indices_descend(int32_t *input, int32_t *sort_indices, int32_t size)
{
    int32_t i, j, temp;
    for (i = 0; i < size; i++) sort_indices[i] = i;
    for (i = 1; i < size; i++) {
        temp = sort_indices[i];
        for (j = i; j > 0 && input[temp] > input[sort_indices[j-1]]; j--) {
            sort_indices[j] = sort_indices[j-1];
        }
        sort_indices[j] = temp;
    }
}

// ── Peak detection ────────────────────────────────────────────────────────────

void maxim_peaks_above_min_height(int32_t *locs, int32_t *n_peaks,
                                   int32_t *input, int32_t size,
                                   int32_t min_height)
{
    int32_t i = 1;
    *n_peaks = 0;

    while (i < size - 1) {
        if (input[i] > min_height &&
            input[i] > input[i-1] &&
            input[i] > input[i+1]) {
            locs[(*n_peaks)++] = i;
        }
        i++;
    }
}



void maxim_find_peaks(int32_t *locs, int32_t *n_peaks, int32_t *input,
                      int32_t size, int32_t min_height, int32_t min_distance,
                      int32_t max_num)
{
    maxim_peaks_above_min_height(locs, n_peaks, input, size, min_height);
    
    if (*n_peaks > max_num) *n_peaks = max_num;
}

// Add this function before maxim_heart_rate_and_oxygen_saturation()
static void detrend(int32_t *signal, int32_t len)
{
    // Fit and subtract a linear trend (first sample to last sample)
    int32_t start = signal[0];
    int32_t end   = signal[len - 1];
    for (int i = 0; i < len; i++) {
        int32_t trend = start + (int32_t)((float)(end - start) * i / (len - 1));
        signal[i] -= trend;
    }
}

// ── Main algorithm ────────────────────────────────────────────────────────────

void maxim_heart_rate_and_oxygen_saturation(
    uint32_t *ir_buffer,
    int32_t   ir_buffer_length,
    uint32_t *red_buffer,
    int32_t  *spo2,
    int8_t   *spo2_valid,
    int32_t  *heart_rate,
    int8_t   *hr_valid)
{
    // Guard against buffer overflow
    if (ir_buffer_length > BUFFER_SIZE) {
        ESP_LOGE("SPO2", "Buffer length %ld exceeds BUFFER_SIZE %d",
                 ir_buffer_length, BUFFER_SIZE);
        *hr_valid   = 0;
        *spo2_valid = 0;
        *heart_rate = -1;
        *spo2       = -1;
        return;
    }
    uint32_t ir_mean = 0;
    int32_t  i, k;
    static int32_t  an_x[BUFFER_SIZE];      // DC-removed IR
    static int32_t  an_y[BUFFER_SIZE];      // DC-removed Red
    int32_t  n_peaks;
    int32_t  peak_locs[15];
    static int32_t  an_dx[BUFFER_SIZE];
    int32_t  n_last_peak_interval;

    // ── 1. Calculate IR mean (DC component) ───────────────────────────────────
    for (i = 0; i < ir_buffer_length; i++) ir_mean += ir_buffer[i];
    ir_mean /= ir_buffer_length;

    // ── 2. Remove DC, apply 4-point moving average ────────────────────────────
    for (i = 0; i < ir_buffer_length; i++) {
        an_x[i] = ir_buffer[i]  - ir_mean;
        an_y[i] = red_buffer[i] - ir_mean;  // Use same DC offset for both
    }

    // Add detrending:
    detrend(an_x, ir_buffer_length);
    detrend(an_y, ir_buffer_length);

    // 4-point MA filter on IR
    for (i = 0; i < ir_buffer_length - MA4_SIZE; i++) {
        an_dx[i] = (an_x[i] + an_x[i+1] + an_x[i+2] + an_x[i+3]);
    }

    // ── 3. Find peaks in filtered IR signal ───────────────────────────────────
// More sensitive threshold + larger min distance between peaks
// At 100Hz, a 40bpm HR = peaks every 150 samples
// At 100Hz, a 200bpm HR = peaks every 30 samples
// Min distance of 20 covers up to ~300bpm safely
maxim_find_peaks(peak_locs, &n_peaks, an_dx,
                 ir_buffer_length - MA4_SIZE,
                 ir_mean / 1000,   // 2% threshold
                 20,             // min 20 samples between peaks
                 15);

    // ── 4. Calculate heart rate from peak intervals ───────────────────────────
    *hr_valid = 0;
    *heart_rate = -1;

    if (n_peaks >= 2) {
        n_last_peak_interval = peak_locs[n_peaks - 1] - peak_locs[0];
        // 100Hz sample rate: HR = (n_peaks-1) / interval_in_seconds * 60
        *heart_rate = (int32_t)(((n_peaks - 1) * SAMPLE_RATE_HZ * 60) / n_last_peak_interval);

        if (*heart_rate >= HR_VALID_MIN && *heart_rate <= HR_VALID_MAX) {
            *hr_valid = 1;
        }
    }

    // ── 5. Calculate SpO2 from R ratio ────────────────────────────────────────
    *spo2_valid = 0;
    *spo2 = -1;

    int32_t an_ir_valley_locs[15];
    int32_t n_ir_valleys = 0;

    // Find valleys (invert signal to find peaks = valleys in original)
    static int32_t an_x_inv[BUFFER_SIZE];
    for (i = 0; i < ir_buffer_length; i++) an_x_inv[i] = -an_x[i];

    // More sensitive threshold + larger min distance between peaks
    // At 100Hz, a 40bpm HR = peaks every 150 samples
    // At 100Hz, a 200bpm HR = peaks every 30 samples
    // Min distance of 20 covers up to ~300bpm safely
    maxim_find_peaks(peak_locs, &n_peaks, an_dx,
                    ir_buffer_length - MA4_SIZE,
                    ir_mean / 1000,   // 2% threshold
                    20,             // min 20 samples between peaks
                    15);

    if (n_ir_valleys >= 2) {
        // Calculate AC and DC components for R ratio
        int32_t ir_ac = 0, red_ac = 0;
        int32_t ir_dc_max = 0, red_dc_max = 0;
        int32_t n = 0;

        for (k = 0; k < n_ir_valleys - 1; k++) {
            int32_t left  = an_ir_valley_locs[k];
            int32_t right = an_ir_valley_locs[k+1];

            // AC = peak-to-peak in window
            int32_t ir_max_val  = an_x[left];
            int32_t red_max_val = an_y[left];

            for (i = left; i < right; i++) {
                if (an_x[i]  > ir_max_val)  ir_max_val  = an_x[i];
                if (an_y[i]  > red_max_val) red_max_val = an_y[i];
            }

            ir_ac  += (ir_max_val  - an_x[left]);
            red_ac += (red_max_val - an_y[left]);

            // DC = raw mean in window
            int32_t ir_dc_sum = 0, red_dc_sum = 0;
            for (i = left; i < right; i++) {
                ir_dc_sum  += ir_buffer[i];
                red_dc_sum += red_buffer[i];
            }
            int32_t wlen = right - left;
            ir_dc_max  += ir_dc_sum  / wlen;
            red_dc_max += red_dc_sum / wlen;
            n++;
        }

        if (n > 0 && ir_ac > 0 && ir_dc_max > 0) {
            // R = (AC_red/DC_red) / (AC_ir/DC_ir) scaled by 100
            int32_t r = ((red_ac * ir_dc_max) * 100) / ((ir_ac * red_dc_max) + 1);

            // Clamp R to table bounds (0–183)
            if (r < 0)   r = 0;
            if (r > 183) r = 183;

            *spo2 = uch_spo2_table[r];

            if (*spo2 >= SPO2_VALID_MIN) {
                *spo2_valid = 1;
            }
        }
    }
}

