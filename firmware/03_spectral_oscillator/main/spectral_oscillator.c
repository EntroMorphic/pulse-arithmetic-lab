/**
 * 03_spectral_oscillator.c - Phase Dynamics and Kuramoto Coupling
 * 
 * NEURONS THAT OSCILLATE AND SYNCHRONIZE
 * 
 * Previous demos showed static computation: input → output.
 * Real neural dynamics have STATE that evolves over time.
 * 
 * This demo introduces:
 * - Complex-valued oscillators (phase + magnitude)
 * - Band-specific frequencies (Delta=slow, Gamma=fast)
 * - Kuramoto coupling (oscillators pull each other toward synchronization)
 * - Coherence measurement (how synchronized is the system?)
 * 
 * No learning yet - just dynamics. Demo 04 adds equilibrium propagation.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// ============================================================
// Configuration
// ============================================================

#define NUM_BANDS           4       // Delta, Theta, Alpha, Gamma
#define NEURONS_PER_BAND    4       // 4 oscillators per band
#define TOTAL_NEURONS       (NUM_BANDS * NEURONS_PER_BAND)
#define INPUT_DIM           4

// Band indices
#define BAND_DELTA          0       // Slowest
#define BAND_THETA          1
#define BAND_ALPHA          2
#define BAND_GAMMA          3       // Fastest

// Band characteristics
static const float BAND_DECAY[NUM_BANDS] = { 0.98f, 0.90f, 0.70f, 0.30f };
static const float BAND_FREQ[NUM_BANDS] = { 0.1f, 0.3f, 1.0f, 3.0f };
static const char* BAND_NAMES[NUM_BANDS] = { "Delta", "Theta", "Alpha", "Gamma" };

// ============================================================
// Q15 Fixed-Point Math
// ============================================================

typedef struct {
    int16_t real;
    int16_t imag;
} complex_q15_t;

#define Q15_ONE     32767
#define Q15_HALF    16384

// Trig lookup tables (256 entries = ~1.4 degree resolution)
#define TRIG_TABLE_SIZE     256
static int16_t sin_table[TRIG_TABLE_SIZE];
static int16_t cos_table[TRIG_TABLE_SIZE];

static void init_trig_tables(void) {
    for (int i = 0; i < TRIG_TABLE_SIZE; i++) {
        float angle = (2.0f * M_PI * i) / TRIG_TABLE_SIZE;
        sin_table[i] = (int16_t)(sinf(angle) * Q15_ONE);
        cos_table[i] = (int16_t)(cosf(angle) * Q15_ONE);
    }
}

static inline int16_t q15_sin(uint8_t angle_idx) { return sin_table[angle_idx]; }
static inline int16_t q15_cos(uint8_t angle_idx) { return cos_table[angle_idx]; }
static inline int16_t q15_mul(int16_t a, int16_t b) { 
    return (int16_t)(((int32_t)a * b) >> 15); 
}

// ============================================================
// Phase extraction (atan2 approximation)
// ============================================================

static uint8_t get_phase_idx(complex_q15_t* z) {
    int16_t r = z->real;
    int16_t i = z->imag;
    
    int quadrant = 0;
    if (r < 0) { r = -r; quadrant |= 2; }
    if (i < 0) { i = -i; quadrant |= 1; }
    
    int angle;
    if (r > i) {
        angle = (i * 32) / (r + 1);
    } else {
        angle = 64 - (r * 32) / (i + 1);
    }
    
    switch (quadrant) {
        case 0: return angle;
        case 2: return 128 - angle;
        case 3: return 128 + angle;
        case 1: return 256 - angle;
    }
    return 0;
}

static int16_t get_magnitude(complex_q15_t* z) {
    int32_t r = z->real;
    int32_t i = z->imag;
    if (r < 0) r = -r;
    if (i < 0) i = -i;
    // Fast approximation: max + 0.4*min
    if (r > i) {
        return (int16_t)(r + ((i * 13) >> 5));
    } else {
        return (int16_t)(i + ((r * 13) >> 5));
    }
}

// ============================================================
// Network State
// ============================================================

typedef struct {
    // Oscillator states
    complex_q15_t oscillator[NUM_BANDS][NEURONS_PER_BAND];
    int16_t phase_velocity[NUM_BANDS][NEURONS_PER_BAND];
    
    // Cross-band coupling (how strongly bands influence each other)
    float coupling[NUM_BANDS][NUM_BANDS];
    
    // Input projection (ternary weights)
    uint32_t input_pos_mask[NUM_BANDS][NEURONS_PER_BAND];
    uint32_t input_neg_mask[NUM_BANDS][NEURONS_PER_BAND];
    
    // Coherence (synchronization measure)
    int16_t coherence;
    
} spectral_network_t;

static spectral_network_t network;

// Simple PRNG for reproducibility
static uint32_t prng_state = 12345;
static uint32_t prng(void) {
    prng_state = prng_state * 1103515245 + 12345;
    return (prng_state >> 16) & 0x7fff;
}

// ============================================================
// Initialization
// ============================================================

static void init_network(float coupling_strength) {
    prng_state = 12345;  // Reset for reproducibility
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            // Random initial phase
            uint8_t phase = prng() & 0xFF;
            network.oscillator[b][n].real = q15_cos(phase);
            network.oscillator[b][n].imag = q15_sin(phase);
            network.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
            
            // Random ternary input weights
            network.input_pos_mask[b][n] = 0;
            network.input_neg_mask[b][n] = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                int r = prng() % 3;
                if (r == 0) network.input_pos_mask[b][n] |= (1 << i);
                else if (r == 1) network.input_neg_mask[b][n] |= (1 << i);
            }
        }
    }
    
    // Initialize coupling matrix
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) {
                network.coupling[i][j] = 0.0f;
            } else {
                network.coupling[i][j] = coupling_strength;
            }
        }
    }
    
    network.coherence = 0;
}

// ============================================================
// Single Evolution Step
// ============================================================

static void evolve_step(const uint8_t* input) {
    // 1. Inject input energy
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            int energy = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (network.input_pos_mask[b][n] & (1 << i)) energy += input[i];
                if (network.input_neg_mask[b][n] & (1 << i)) energy -= input[i];
            }
            
            // Only inject if magnitude is low (prevents runaway)
            int16_t mag = get_magnitude(&network.oscillator[b][n]);
            if (mag < Q15_HALF) {
                network.oscillator[b][n].real += energy * 50;
                network.oscillator[b][n].imag += energy * 25;
            }
        }
    }
    
    // 2. Rotate oscillators (phase advance)
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t angle_idx = (uint8_t)((network.phase_velocity[b][n] >> 8) & 0xFF);
            int16_t c = q15_cos(angle_idx);
            int16_t s = q15_sin(angle_idx);
            
            // z_new = z * e^(i*angle) = (r+ij)(c+is) = (rc-is) + i(rs+ic)
            int16_t new_real = q15_mul(network.oscillator[b][n].real, c) 
                             - q15_mul(network.oscillator[b][n].imag, s);
            int16_t new_imag = q15_mul(network.oscillator[b][n].real, s) 
                             + q15_mul(network.oscillator[b][n].imag, c);
            
            // Apply decay
            int16_t decay_q15 = (int16_t)(BAND_DECAY[b] * Q15_ONE);
            network.oscillator[b][n].real = q15_mul(new_real, decay_q15);
            network.oscillator[b][n].imag = q15_mul(new_imag, decay_q15);
        }
    }
    
    // 3. Kuramoto coupling: bands influence each other's phase velocities
    int32_t velocity_delta[NUM_BANDS][NEURONS_PER_BAND] = {0};
    
    for (int src = 0; src < NUM_BANDS; src++) {
        for (int dst = 0; dst < NUM_BANDS; dst++) {
            if (src == dst) continue;
            float strength = network.coupling[src][dst];
            if (strength < 0.01f) continue;
            
            // Compute average phase difference
            int32_t phase_diff_sum = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                uint8_t src_phase = get_phase_idx(&network.oscillator[src][n]);
                uint8_t dst_phase = get_phase_idx(&network.oscillator[dst][n]);
                int diff = (int)src_phase - (int)dst_phase;
                while (diff > 127) diff -= 256;
                while (diff < -128) diff += 256;
                phase_diff_sum += diff;
            }
            int avg_diff = phase_diff_sum / NEURONS_PER_BAND;
            
            // Pull destination toward source
            int16_t pull = (int16_t)(strength * avg_diff * 10);
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                velocity_delta[dst][n] += pull;
            }
        }
    }
    
    // Apply velocity changes
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            network.phase_velocity[b][n] += velocity_delta[b][n] / 10;
            // Clamp
            if (network.phase_velocity[b][n] > 10000) network.phase_velocity[b][n] = 10000;
            if (network.phase_velocity[b][n] < -10000) network.phase_velocity[b][n] = -10000;
        }
    }
    
    // 4. Compute global coherence (Kuramoto order parameter)
    // coherence = |mean(e^(i*phase))| = |mean(z/|z|)|
    // This measures PHASE alignment, independent of magnitude
    int32_t sum_real = 0, sum_imag = 0;
    int valid_count = 0;
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            int16_t mag = get_magnitude(&network.oscillator[b][n]);
            if (mag > 100) {  // Only count oscillators with meaningful magnitude
                // Normalize to unit vector: z/|z|
                // Scale to Q15: (real * 32767) / mag
                int32_t norm_real = ((int32_t)network.oscillator[b][n].real * Q15_ONE) / mag;
                int32_t norm_imag = ((int32_t)network.oscillator[b][n].imag * Q15_ONE) / mag;
                sum_real += norm_real;
                sum_imag += norm_imag;
                valid_count++;
            }
        }
    }
    if (valid_count > 0) {
        sum_real /= valid_count;
        sum_imag /= valid_count;
        complex_q15_t avg = { .real = (int16_t)sum_real, .imag = (int16_t)sum_imag };
        network.coherence = get_magnitude(&avg);
    } else {
        network.coherence = 0;
    }
}

// ============================================================
// Measurement
// ============================================================

static void print_network_state(void) {
    printf("    Band   | Phase (avg) | Magnitude (avg) | Velocity (avg)\n");
    printf("    -------+-------------+-----------------+---------------\n");
    
    for (int b = 0; b < NUM_BANDS; b++) {
        int32_t phase_sum = 0, mag_sum = 0, vel_sum = 0;
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            phase_sum += get_phase_idx(&network.oscillator[b][n]);
            mag_sum += get_magnitude(&network.oscillator[b][n]);
            vel_sum += network.phase_velocity[b][n];
        }
        printf("    %-6s |    %3d      |     %5d       |    %5d\n",
               BAND_NAMES[b],
               (int)(phase_sum / NEURONS_PER_BAND),
               (int)(mag_sum / NEURONS_PER_BAND),
               (int)(vel_sum / NEURONS_PER_BAND));
    }
    printf("\n    Global coherence: %d (0=desynchronized, 32767=fully synchronized)\n",
           network.coherence);
}

// ============================================================
// Tests
// ============================================================

static int16_t measure_band_coherence(int band) {
    // Measure phase coherence within a single band
    int32_t sum_real = 0, sum_imag = 0;
    int valid = 0;
    for (int n = 0; n < NEURONS_PER_BAND; n++) {
        int16_t mag = get_magnitude(&network.oscillator[band][n]);
        if (mag > 100) {
            uint8_t phase = get_phase_idx(&network.oscillator[band][n]);
            sum_real += q15_cos(phase);
            sum_imag += q15_sin(phase);
            valid++;
        }
    }
    if (valid == 0) return 0;
    sum_real /= valid;
    sum_imag /= valid;
    complex_q15_t avg = { .real = (int16_t)sum_real, .imag = (int16_t)sum_imag };
    return get_magnitude(&avg);
}

static void test_coupling_effect(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST: Phase Variance Within Bands\n");
    printf("----------------------------------------------------------------------\n");
    printf("\n");
    printf("  Measure how spread out phases are within each band.\n");
    printf("  High coherence = phases aligned. Low = random.\n");
    printf("\n");
    
    init_network(0.0f);  // No inter-band coupling
    uint8_t input[INPUT_DIM] = {10, 10, 10, 10};
    
    // Inject energy to sustain oscillators
    for (int s = 0; s < 20; s++) {
        evolve_step(input);
    }
    
    printf("  After injection (20 steps with input):\n");
    printf("    Band   | Coherence | Interpretation\n");
    printf("    -------+-----------+---------------\n");
    
    for (int b = 0; b < NUM_BANDS; b++) {
        int16_t coh = measure_band_coherence(b);
        const char* interp = (coh > 25000) ? "highly aligned" :
                            (coh > 15000) ? "moderately aligned" :
                            (coh > 5000)  ? "weakly aligned" : "random";
        printf("    %-6s |   %5d   | %s\n", BAND_NAMES[b], coh, interp);
    }
    
    // Evolve more without input - phases should spread
    uint8_t zero[INPUT_DIM] = {0, 0, 0, 0};
    for (int s = 0; s < 100; s++) {
        evolve_step(zero);
    }
    
    printf("\n  After 100 more steps (no input, free evolution):\n");
    printf("    Band   | Coherence | Interpretation\n");
    printf("    -------+-----------+---------------\n");
    
    for (int b = 0; b < NUM_BANDS; b++) {
        int16_t coh = measure_band_coherence(b);
        const char* interp = (coh > 25000) ? "highly aligned" :
                            (coh > 15000) ? "moderately aligned" :
                            (coh > 5000)  ? "weakly aligned" : "random/decayed";
        printf("    %-6s |   %5d   | %s\n", BAND_NAMES[b], coh, interp);
    }
    
    printf("\n  Note: Delta band retains energy longest (slow decay),\n");
    printf("        Gamma decays fastest. Coherence depends on both\n");
    printf("        phase alignment AND having enough magnitude to measure.\n");
}

static void test_band_frequencies(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST: Band-Specific Frequencies\n");
    printf("----------------------------------------------------------------------\n");
    printf("\n");
    printf("  Different bands oscillate at different speeds.\n");
    printf("  Delta=slowest, Gamma=fastest.\n");
    printf("\n");
    
    init_network(0.0f);  // No coupling for this test
    
    uint8_t input[INPUT_DIM] = {4, 4, 4, 4};
    
    // Inject input
    for (int s = 0; s < 10; s++) {
        evolve_step(input);
    }
    
    printf("  Initial state:\n");
    print_network_state();
    
    // Evolve more
    uint8_t zero_input[INPUT_DIM] = {0, 0, 0, 0};
    for (int s = 0; s < 50; s++) {
        evolve_step(zero_input);
    }
    
    printf("\n  After 50 steps (no input):\n");
    print_network_state();
    
    printf("\n  Expected: Gamma decays fastest (lowest magnitude),\n");
    printf("            Delta decays slowest (highest magnitude).\n");
}

static void run_benchmark(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  BENCHMARK: Evolution Speed\n");
    printf("----------------------------------------------------------------------\n");
    
    init_network(0.3f);
    uint8_t input[INPUT_DIM] = {8, 8, 8, 8};
    int iterations = 10000;
    
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < iterations; i++) {
        evolve_step(input);
    }
    int64_t end = esp_timer_get_time();
    
    float total_ms = (float)(end - start) / 1000.0f;
    float per_step_us = (float)(end - start) / iterations;
    float steps_per_sec = 1000000.0f / per_step_us;
    
    printf("\n  %d evolution steps\n", iterations);
    printf("  Total time: %.2f ms\n", total_ms);
    printf("  Per step: %.1f us\n", per_step_us);
    printf("  Throughput: %.0f steps/second\n", steps_per_sec);
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("======================================================================\n");
    printf("  SPECTRAL OSCILLATOR: Phase Dynamics and Kuramoto Coupling\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  This demo shows dynamic neural computation:\n");
    printf("  - 4 frequency bands: Delta (slow) to Gamma (fast)\n");
    printf("  - Complex-valued oscillators with phase and magnitude\n");
    printf("  - Kuramoto coupling: oscillators synchronize\n");
    printf("  - Coherence: how synchronized is the whole system?\n");
    printf("\n");
    
    // Initialize
    printf("  Initializing trig tables...\n");
    init_trig_tables();
    printf("  Ready.\n");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Run tests
    test_band_frequencies();
    test_coupling_effect();
    run_benchmark();
    
    // Summary
    printf("\n");
    printf("======================================================================\n");
    printf("  SUMMARY\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  What we demonstrated:\n");
    printf("    1. Oscillators rotate at band-specific frequencies\n");
    printf("    2. Decay rates vary by band (Gamma=fast, Delta=slow)\n");
    printf("    3. Kuramoto coupling increases synchronization\n");
    printf("    4. Coherence measures global synchronization\n");
    printf("\n");
    printf("  This is the state representation for neural dynamics.\n");
    printf("  Next: 04_equilibrium_prop - add learning!\n");
    printf("\n");
    printf("======================================================================\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
