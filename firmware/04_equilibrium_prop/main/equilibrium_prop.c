/**
 * 04_equilibrium_prop.c - Learning Without Backpropagation
 * 
 * THE BACKWARD PASS IS THE FORWARD PASS, PERTURBED
 * 
 * Traditional neural network learning:
 *   1. Forward pass: compute output
 *   2. Backward pass: compute gradients (separate algorithm!)
 *   3. Update weights: w -= learning_rate * gradient
 * 
 * Equilibrium Propagation (Scellier & Bengio, 2017):
 *   1. Free phase: let system evolve to equilibrium
 *   2. Nudged phase: clamp output toward target, evolve again
 *   3. Update weights: w += lr * (correlation_nudged - correlation_free)
 * 
 * The gradient emerges from the DIFFERENCE between two forward passes
 * with different boundary conditions. No separate backward algorithm.
 * 
 * This is the culmination of demos 01-03:
 * - 01: PCNT counts pulses (addition)
 * - 02: PARLIO enables parallel computation
 * - 03: Spectral oscillators have phase dynamics
 * - 04: Equilibrium propagation learns from dynamics
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// ============================================================
// Configuration (simplified from demo 03)
// ============================================================

#define NUM_BANDS           4
#define NEURONS_PER_BAND    4
#define TOTAL_NEURONS       (NUM_BANDS * NEURONS_PER_BAND)
#define INPUT_DIM           4

#define BAND_DELTA          0
#define BAND_GAMMA          3

// Equilibrium propagation parameters
#define FREE_PHASE_STEPS    30
#define NUDGE_PHASE_STEPS   30
#define NUDGE_STRENGTH      0.5f
#define LEARNING_RATE       0.005f

static const float BAND_DECAY[NUM_BANDS] = { 0.98f, 0.90f, 0.70f, 0.30f };
static const float BAND_FREQ[NUM_BANDS] = { 0.1f, 0.3f, 1.0f, 3.0f };

// ============================================================
// Q15 Fixed-Point (same as demo 03)
// ============================================================

typedef struct {
    int16_t real;
    int16_t imag;
} complex_q15_t;

#define Q15_ONE     32767
#define Q15_HALF    16384
#define TRIG_TABLE_SIZE 256

static int16_t sin_table[TRIG_TABLE_SIZE];
static int16_t cos_table[TRIG_TABLE_SIZE];

static void init_trig_tables(void) {
    for (int i = 0; i < TRIG_TABLE_SIZE; i++) {
        float angle = (2.0f * M_PI * i) / TRIG_TABLE_SIZE;
        sin_table[i] = (int16_t)(sinf(angle) * Q15_ONE);
        cos_table[i] = (int16_t)(cosf(angle) * Q15_ONE);
    }
}

static inline int16_t q15_sin(uint8_t idx) { return sin_table[idx]; }
static inline int16_t q15_cos(uint8_t idx) { return cos_table[idx]; }
static inline int16_t q15_mul(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 15); }

static uint8_t get_phase_idx(complex_q15_t* z) {
    int16_t r = z->real, i = z->imag;
    int quadrant = 0;
    if (r < 0) { r = -r; quadrant |= 2; }
    if (i < 0) { i = -i; quadrant |= 1; }
    int angle = (r > i) ? (i * 32) / (r + 1) : 64 - (r * 32) / (i + 1);
    switch (quadrant) {
        case 0: return angle;
        case 2: return 128 - angle;
        case 3: return 128 + angle;
        case 1: return 256 - angle;
    }
    return 0;
}

static int16_t get_magnitude(complex_q15_t* z) {
    int32_t r = (z->real < 0) ? -z->real : z->real;
    int32_t i = (z->imag < 0) ? -z->imag : z->imag;
    return (r > i) ? (int16_t)(r + ((i * 13) >> 5)) : (int16_t)(i + ((r * 13) >> 5));
}

// ============================================================
// Network State
// ============================================================

typedef struct {
    complex_q15_t oscillator[NUM_BANDS][NEURONS_PER_BAND];
    int16_t phase_velocity[NUM_BANDS][NEURONS_PER_BAND];
    uint32_t input_pos_mask[NUM_BANDS][NEURONS_PER_BAND];
    uint32_t input_neg_mask[NUM_BANDS][NEURONS_PER_BAND];
    float coupling[NUM_BANDS][NUM_BANDS];  // LEARNABLE
} network_t;

typedef struct {
    float band_correlation[NUM_BANDS][NUM_BANDS];
    int16_t output_phase;
} snapshot_t;

static network_t net;
static snapshot_t snap_free, snap_nudged;

static uint32_t prng_state = 42;
static uint32_t prng(void) {
    prng_state = prng_state * 1103515245 + 12345;
    return (prng_state >> 16) & 0x7fff;
}

// ============================================================
// Network Initialization
// ============================================================

static void init_network(void) {
    prng_state = 42;
    
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t phase = prng() & 0xFF;
            net.oscillator[b][n].real = q15_cos(phase);
            net.oscillator[b][n].imag = q15_sin(phase);
            net.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
            
            // Structured input weights
            net.input_pos_mask[b][n] = 0;
            net.input_neg_mask[b][n] = 0;
            if (b == BAND_DELTA) {
                net.input_pos_mask[b][n] = 0x0C;  // Respond to inputs 2,3
                net.input_neg_mask[b][n] = 0x03;
            } else if (b == BAND_GAMMA) {
                net.input_pos_mask[b][n] = 0x03;  // Respond to inputs 0,1
                net.input_neg_mask[b][n] = 0x0C;
            } else {
                for (int i = 0; i < INPUT_DIM; i++) {
                    int r = prng() % 3;
                    if (r == 0) net.input_pos_mask[b][n] |= (1 << i);
                    else if (r == 1) net.input_neg_mask[b][n] |= (1 << i);
                }
            }
        }
    }
    
    // Uniform coupling
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            net.coupling[i][j] = (i == j) ? 0.0f : 0.2f;
        }
    }
}

static void reset_oscillators(void) {
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t phase = (uint8_t)((b * 64 + n * 16) & 0xFF);
            net.oscillator[b][n].real = q15_cos(phase);
            net.oscillator[b][n].imag = q15_sin(phase);
            net.phase_velocity[b][n] = (int16_t)(BAND_FREQ[b] * 1000);
        }
    }
}

// ============================================================
// Evolution Step (with optional nudge)
// ============================================================

static void evolve_step(const uint8_t* input, int16_t* nudge_target, float nudge_str) {
    // 1. Inject input
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            int energy = 0;
            for (int i = 0; i < INPUT_DIM; i++) {
                if (net.input_pos_mask[b][n] & (1 << i)) energy += input[i];
                if (net.input_neg_mask[b][n] & (1 << i)) energy -= input[i];
            }
            if (get_magnitude(&net.oscillator[b][n]) < Q15_HALF) {
                net.oscillator[b][n].real += energy * 50;
                net.oscillator[b][n].imag += energy * 25;
            }
        }
    }
    
    // 2. Rotate + decay
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            uint8_t angle = (uint8_t)((net.phase_velocity[b][n] >> 8) & 0xFF);
            int16_t c = q15_cos(angle), s = q15_sin(angle);
            int16_t nr = q15_mul(net.oscillator[b][n].real, c) - q15_mul(net.oscillator[b][n].imag, s);
            int16_t ni = q15_mul(net.oscillator[b][n].real, s) + q15_mul(net.oscillator[b][n].imag, c);
            int16_t decay = (int16_t)(BAND_DECAY[b] * Q15_ONE);
            net.oscillator[b][n].real = q15_mul(nr, decay);
            net.oscillator[b][n].imag = q15_mul(ni, decay);
        }
    }
    
    // 3. Kuramoto coupling
    int32_t vel_delta[NUM_BANDS][NEURONS_PER_BAND] = {0};
    for (int src = 0; src < NUM_BANDS; src++) {
        for (int dst = 0; dst < NUM_BANDS; dst++) {
            if (src == dst || net.coupling[src][dst] < 0.01f) continue;
            int32_t diff_sum = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                int diff = (int)get_phase_idx(&net.oscillator[src][n]) - 
                           (int)get_phase_idx(&net.oscillator[dst][n]);
                while (diff > 127) diff -= 256;
                while (diff < -128) diff += 256;
                diff_sum += diff;
            }
            int16_t pull = (int16_t)(net.coupling[src][dst] * (diff_sum / NEURONS_PER_BAND) * 10);
            for (int n = 0; n < NEURONS_PER_BAND; n++) vel_delta[dst][n] += pull;
        }
    }
    for (int b = 0; b < NUM_BANDS; b++) {
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            net.phase_velocity[b][n] += vel_delta[b][n] / 10;
            if (net.phase_velocity[b][n] > 10000) net.phase_velocity[b][n] = 10000;
            if (net.phase_velocity[b][n] < -10000) net.phase_velocity[b][n] = -10000;
        }
    }
    
    // 4. NUDGE (if target provided)
    if (nudge_target && nudge_str > 0) {
        uint8_t gamma_ph = get_phase_idx(&net.oscillator[BAND_GAMMA][0]);
        uint8_t delta_ph = get_phase_idx(&net.oscillator[BAND_DELTA][0]);
        int16_t current = (int16_t)gamma_ph - (int16_t)delta_ph;
        int16_t error = *nudge_target - current;
        while (error > 127) error -= 256;
        while (error < -128) error += 256;
        int16_t nudge = (int16_t)(error * nudge_str);
        for (int n = 0; n < NEURONS_PER_BAND; n++) {
            net.phase_velocity[BAND_GAMMA][n] += nudge;
        }
    }
}

// ============================================================
// Snapshot (for contrastive learning)
// ============================================================

static void take_snapshot(snapshot_t* snap) {
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) { snap->band_correlation[i][j] = 1.0f; continue; }
            float corr = 0;
            for (int n = 0; n < NEURONS_PER_BAND; n++) {
                int diff = (int)get_phase_idx(&net.oscillator[i][n]) - 
                           (int)get_phase_idx(&net.oscillator[j][n]);
                corr += cosf((float)diff * 2.0f * M_PI / 256.0f);
            }
            snap->band_correlation[i][j] = corr / NEURONS_PER_BAND;
        }
    }
    snap->output_phase = (int16_t)get_phase_idx(&net.oscillator[BAND_GAMMA][0]) - 
                         (int16_t)get_phase_idx(&net.oscillator[BAND_DELTA][0]);
}

// ============================================================
// Learning Step
// ============================================================

static float learn_step(const uint8_t* input, int16_t target) {
    // FREE PHASE
    reset_oscillators();
    for (int t = 0; t < FREE_PHASE_STEPS; t++) evolve_step(input, NULL, 0);
    take_snapshot(&snap_free);
    
    // NUDGED PHASE
    for (int t = 0; t < NUDGE_PHASE_STEPS; t++) evolve_step(input, &target, NUDGE_STRENGTH);
    take_snapshot(&snap_nudged);
    
    // WEIGHT UPDATE
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            if (i == j) continue;
            float delta = snap_nudged.band_correlation[i][j] - snap_free.band_correlation[i][j];
            net.coupling[i][j] += LEARNING_RATE * delta;
            if (net.coupling[i][j] < 0.01f) net.coupling[i][j] = 0.01f;
            if (net.coupling[i][j] > 1.0f) net.coupling[i][j] = 1.0f;
        }
    }
    
    // Return loss
    int16_t err = target - snap_free.output_phase;
    while (err > 127) err -= 256;
    while (err < -128) err += 256;
    return (float)(err * err) / (256.0f * 256.0f);
}

static int16_t forward_pass(const uint8_t* input) {
    reset_oscillators();
    for (int t = 0; t < FREE_PHASE_STEPS; t++) evolve_step(input, NULL, 0);
    return (int16_t)get_phase_idx(&net.oscillator[BAND_GAMMA][0]) - 
           (int16_t)get_phase_idx(&net.oscillator[BAND_DELTA][0]);
}

// ============================================================
// Training
// ============================================================

static void train_and_evaluate(void) {
    printf("\n");
    printf("======================================================================\n");
    printf("  EQUILIBRIUM PROPAGATION TRAINING\n");
    printf("======================================================================\n");
    printf("\n");
    
    // Training data
    uint8_t patterns[2][INPUT_DIM] = {
        {0, 0, 15, 15},   // Pattern 0: energy in dims 2,3 → Delta
        {15, 15, 0, 0},   // Pattern 1: energy in dims 0,1 → Gamma
    };
    int16_t targets[2] = {0, 128};  // Opposite phases
    
    printf("  Training data:\n");
    printf("    Pattern 0: [0,0,15,15] → target phase 0\n");
    printf("    Pattern 1: [15,15,0,0] → target phase 128\n");
    printf("\n");
    
    // Train
    int epochs = 150;
    printf("  Epoch | Loss    | Output 0 | Output 1 | Separation\n");
    printf("  ------+---------+----------+----------+-----------\n");
    
    for (int e = 0; e < epochs; e++) {
        float loss = 0;
        for (int p = 0; p < 2; p++) {
            loss += learn_step(patterns[p], targets[p]);
        }
        
        if (e % 25 == 0 || e == epochs - 1) {
            int16_t out0 = forward_pass(patterns[0]);
            int16_t out1 = forward_pass(patterns[1]);
            int sep = out1 - out0;
            while (sep > 127) sep -= 256;
            while (sep < -128) sep += 256;
            printf("  %5d | %.5f |   %4d   |   %4d   |    %4d\n",
                   e, loss / 2, out0, out1, sep);
        }
    }
    
    // Final evaluation
    printf("\n  Final Results:\n");
    int16_t out0 = forward_pass(patterns[0]);
    int16_t out1 = forward_pass(patterns[1]);
    int16_t err0 = targets[0] - out0; if (err0 < 0) err0 = -err0;
    int16_t err1 = targets[1] - out1; 
    while (err1 > 127) err1 -= 256;
    while (err1 < -128) err1 += 256;
    if (err1 < 0) err1 = -err1;
    
    int sep = out1 - out0;
    while (sep > 127) sep -= 256;
    while (sep < -128) sep += 256;
    
    printf("    Pattern 0: target=%d, output=%d, error=%d\n", targets[0], out0, err0);
    printf("    Pattern 1: target=%d, output=%d, error=%d\n", targets[1], out1, err1);
    printf("    Separation: %d (target: 128)\n", sep);
    printf("    Separation achieved: %.1f%%\n", 100.0f * (float)(sep > 0 ? sep : -sep) / 128.0f);
    
    printf("\n  Final coupling matrix:\n    ");
    for (int i = 0; i < NUM_BANDS; i++) {
        for (int j = 0; j < NUM_BANDS; j++) {
            printf("%.2f ", net.coupling[i][j]);
        }
        printf("\n    ");
    }
}

// ============================================================
// Benchmark
// ============================================================

static void run_benchmark(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  BENCHMARK\n");
    printf("----------------------------------------------------------------------\n");
    
    uint8_t input[INPUT_DIM] = {8, 8, 8, 8};
    int16_t target = 64;
    
    // Learning
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < 20; i++) learn_step(input, target);
    int64_t end = esp_timer_get_time();
    float learn_us = (float)(end - start) / 20;
    
    // Inference
    start = esp_timer_get_time();
    for (int i = 0; i < 100; i++) forward_pass(input);
    end = esp_timer_get_time();
    float infer_us = (float)(end - start) / 100;
    
    printf("\n  Learning step: %.1f us (%.0f Hz)\n", learn_us, 1000000.0f / learn_us);
    printf("  Inference only: %.1f us (%.0f Hz)\n", infer_us, 1000000.0f / infer_us);
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("======================================================================\n");
    printf("  EQUILIBRIUM PROPAGATION: Learning Without Backpropagation\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  The backward pass IS the forward pass, perturbed.\n");
    printf("\n");
    printf("  Algorithm:\n");
    printf("    1. FREE PHASE: Let system evolve to equilibrium\n");
    printf("    2. NUDGED PHASE: Clamp output toward target, evolve again\n");
    printf("    3. UPDATE: w += lr * (correlation_nudged - correlation_free)\n");
    printf("\n");
    printf("  No separate gradient computation. Learning emerges from dynamics.\n");
    printf("\n");
    
    init_trig_tables();
    init_network();
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    run_benchmark();
    train_and_evaluate();
    
    printf("\n");
    printf("======================================================================\n");
    printf("  COMPLETE\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  You have now seen:\n");
    printf("    01: Pulse counting = addition\n");
    printf("    02: Parallel I/O = parallel computation\n");
    printf("    03: Spectral oscillators = phase dynamics\n");
    printf("    04: Equilibrium propagation = learning from dynamics\n");
    printf("\n");
    printf("  The strange loop learns.\n");
    printf("\n");
    printf("======================================================================\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
