/**
 * 02_parallel_dot.c - PARLIO + PCNT = Parallel Dot Product
 * 
 * FOUR ADDITIONS HAPPEN SIMULTANEOUSLY
 * 
 * This demonstrates parallel computation using:
 * - PARLIO: Transmits 8 bits in parallel at 10 MHz
 * - PCNT: 4 units, each counting pulses on its GPIO
 * 
 * We compute 4 dot products at once:
 *   dot[i] = sum(weights[i][j] * inputs[j]) for j in 0..INPUT_DIM
 * 
 * With ternary weights {-1, 0, +1}, multiplication becomes routing:
 *   weight = +1: send pulses to positive channel
 *   weight = -1: send pulses to negative channel  
 *   weight =  0: send nothing
 * 
 * Hardware setup: Internal loopback (PARLIO output -> PCNT input)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

// ============================================================
// Configuration
// ============================================================

#define NUM_NEURONS     4       // 4 parallel dot products
#define INPUT_DIM       4       // 4-element input vector

// GPIO assignments (directly matching PARLIO data bits)
// Each neuron uses 2 bits: one for positive, one for negative
#define GPIO_N0_POS     4       // Neuron 0 positive channel
#define GPIO_N0_NEG     5       // Neuron 0 negative channel
#define GPIO_N1_POS     6       // Neuron 1 positive channel
#define GPIO_N1_NEG     7       // Neuron 1 negative channel
#define GPIO_N2_POS     8       // Neuron 2 positive channel
#define GPIO_N2_NEG     9       // Neuron 2 negative channel
#define GPIO_N3_POS     10      // Neuron 3 positive channel
#define GPIO_N3_NEG     11      // Neuron 3 negative channel

static const int gpio_pos[NUM_NEURONS] = {GPIO_N0_POS, GPIO_N1_POS, GPIO_N2_POS, GPIO_N3_POS};
static const int gpio_neg[NUM_NEURONS] = {GPIO_N0_NEG, GPIO_N1_NEG, GPIO_N2_NEG, GPIO_N3_NEG};

// PARLIO configuration
#define PARLIO_DATA_WIDTH   8       // 8 parallel bits
#define PARLIO_FREQ_HZ      10000000 // 10 MHz
#define MAX_PATTERN_BYTES   1024

// ============================================================
// Hardware handles
// ============================================================

static pcnt_unit_handle_t pcnt_units[NUM_NEURONS] = {NULL};
static pcnt_channel_handle_t pcnt_ch_pos[NUM_NEURONS] = {NULL};
static pcnt_channel_handle_t pcnt_ch_neg[NUM_NEURONS] = {NULL};
static parlio_tx_unit_handle_t parlio_tx = NULL;
static uint8_t *pattern_buffer = NULL;

// ============================================================
// Ternary weight storage
// Weights are {-1, 0, +1}, stored as bitmasks
// ============================================================

typedef struct {
    uint32_t pos_mask;  // Bit i set = weight[i] is +1
    uint32_t neg_mask;  // Bit i set = weight[i] is -1
} ternary_weights_t;

static ternary_weights_t weights[NUM_NEURONS];

// ============================================================
// Hardware initialization
// ============================================================

static void init_gpio(void) {
    // Configure all GPIOs for input/output (loopback)
    for (int n = 0; n < NUM_NEURONS; n++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << gpio_pos[n]) | (1ULL << gpio_neg[n]),
            .mode = GPIO_MODE_INPUT_OUTPUT,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }
}

static void init_pcnt(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        // Create PCNT unit
        pcnt_unit_config_t unit_cfg = {
            .low_limit = -32768,
            .high_limit = 32767,
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &pcnt_units[n]));
        
        // Channel for positive counts
        pcnt_chan_config_t ch_pos_cfg = {
            .edge_gpio_num = gpio_pos[n],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[n], &ch_pos_cfg, &pcnt_ch_pos[n]));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_ch_pos[n],
            PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_HOLD));
        
        // Channel for negative counts
        pcnt_chan_config_t ch_neg_cfg = {
            .edge_gpio_num = gpio_neg[n],
            .level_gpio_num = -1,
        };
        ESP_ERROR_CHECK(pcnt_new_channel(pcnt_units[n], &ch_neg_cfg, &pcnt_ch_neg[n]));
        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_ch_neg[n],
            PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // Negative channel subtracts!
            PCNT_CHANNEL_EDGE_ACTION_HOLD));
        
        // Enable and start
        ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_units[n]));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_units[n]));
        ESP_ERROR_CHECK(pcnt_unit_start(pcnt_units[n]));
    }
}

static void init_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_FREQ_HZ,
        .data_width = PARLIO_DATA_WIDTH,
        .trans_queue_depth = 4,
        .max_transfer_size = MAX_PATTERN_BYTES + 64,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },  // Enable loopback!
    };
    
    // Assign GPIOs to PARLIO data bits
    cfg.data_gpio_nums[0] = GPIO_N0_POS;
    cfg.data_gpio_nums[1] = GPIO_N0_NEG;
    cfg.data_gpio_nums[2] = GPIO_N1_POS;
    cfg.data_gpio_nums[3] = GPIO_N1_NEG;
    cfg.data_gpio_nums[4] = GPIO_N2_POS;
    cfg.data_gpio_nums[5] = GPIO_N2_NEG;
    cfg.data_gpio_nums[6] = GPIO_N3_POS;
    cfg.data_gpio_nums[7] = GPIO_N3_NEG;
    
    ESP_ERROR_CHECK(parlio_new_tx_unit(&cfg, &parlio_tx));
    ESP_ERROR_CHECK(parlio_tx_unit_enable(parlio_tx));
    
    // Allocate DMA buffer
    pattern_buffer = heap_caps_aligned_alloc(4, MAX_PATTERN_BYTES, 
                                              MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

// ============================================================
// Core computation
// ============================================================

static void clear_counts(void) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        pcnt_unit_clear_count(pcnt_units[n]);
    }
}

static void get_counts(int *results) {
    for (int n = 0; n < NUM_NEURONS; n++) {
        pcnt_unit_get_count(pcnt_units[n], &results[n]);
    }
}

/**
 * Generate pulse pattern for parallel dot product
 * 
 * For each input[i]:
 *   - If weight[n][i] = +1: generate input[i] pulses on neuron n's positive channel
 *   - If weight[n][i] = -1: generate input[i] pulses on neuron n's negative channel
 *   - If weight[n][i] =  0: generate no pulses
 * 
 * All 4 neurons process simultaneously!
 */
static int generate_pattern(const uint8_t *inputs) {
    int byte_idx = 0;
    
    // For each input element
    for (int i = 0; i < INPUT_DIM; i++) {
        uint8_t val = inputs[i];
        
        // Generate 'val' pulses
        for (int p = 0; p < val; p++) {
            uint8_t pulse_byte = 0;
            
            // Set bits for each neuron based on weight
            for (int n = 0; n < NUM_NEURONS; n++) {
                if (weights[n].pos_mask & (1 << i)) {
                    // Positive weight: pulse on positive channel
                    pulse_byte |= (1 << (n * 2));      // Even bits are positive
                }
                if (weights[n].neg_mask & (1 << i)) {
                    // Negative weight: pulse on negative channel
                    pulse_byte |= (1 << (n * 2 + 1));  // Odd bits are negative
                }
            }
            
            // Rising edge
            pattern_buffer[byte_idx++] = pulse_byte;
            // Falling edge (return to zero)
            pattern_buffer[byte_idx++] = 0x00;
        }
    }
    
    // Ensure even length for PARLIO
    if (byte_idx & 1) {
        pattern_buffer[byte_idx++] = 0x00;
    }
    
    return byte_idx;
}

static void transmit_pattern(int length) {
    parlio_transmit_config_t tx_cfg = {
        .idle_value = 0x00,
    };
    ESP_ERROR_CHECK(parlio_tx_unit_transmit(parlio_tx, pattern_buffer, length * 8, &tx_cfg));
    ESP_ERROR_CHECK(parlio_tx_unit_wait_all_done(parlio_tx, 1000));
}

/**
 * Compute parallel dot product
 */
static void parallel_dot(const uint8_t *inputs, int *results) {
    clear_counts();
    int pattern_len = generate_pattern(inputs);
    transmit_pattern(pattern_len);
    get_counts(results);
}

// ============================================================
// Reference implementation (for verification)
// ============================================================

static void reference_dot(const uint8_t *inputs, const ternary_weights_t *w, int *result) {
    *result = 0;
    for (int i = 0; i < INPUT_DIM; i++) {
        if (w->pos_mask & (1 << i)) {
            *result += inputs[i];
        }
        if (w->neg_mask & (1 << i)) {
            *result -= inputs[i];
        }
    }
}

// ============================================================
// Test cases
// ============================================================

static void init_test_weights(void) {
    // Neuron 0: all positive [+1, +1, +1, +1]
    weights[0].pos_mask = 0x0F;
    weights[0].neg_mask = 0x00;
    
    // Neuron 1: all negative [-1, -1, -1, -1]
    weights[1].pos_mask = 0x00;
    weights[1].neg_mask = 0x0F;
    
    // Neuron 2: alternating [+1, -1, +1, -1]
    weights[2].pos_mask = 0x05;  // bits 0, 2
    weights[2].neg_mask = 0x0A;  // bits 1, 3
    
    // Neuron 3: half and half [+1, +1, -1, -1]
    weights[3].pos_mask = 0x03;  // bits 0, 1
    weights[3].neg_mask = 0x0C;  // bits 2, 3
}

static bool run_verification_test(const char *name, const uint8_t *inputs) {
    printf("\n  %s\n", name);
    printf("    Input: [%d, %d, %d, %d]\n", inputs[0], inputs[1], inputs[2], inputs[3]);
    
    // Hardware computation
    int hw_results[NUM_NEURONS];
    parallel_dot(inputs, hw_results);
    
    // Reference computation
    int ref_results[NUM_NEURONS];
    for (int n = 0; n < NUM_NEURONS; n++) {
        reference_dot(inputs, &weights[n], &ref_results[n]);
    }
    
    // Compare
    bool all_pass = true;
    printf("    Neuron | Weight Pattern | Reference | Hardware | Match\n");
    printf("    -------+----------------+-----------+----------+------\n");
    
    const char *patterns[] = {
        "[+1,+1,+1,+1]",
        "[-1,-1,-1,-1]",
        "[+1,-1,+1,-1]",
        "[+1,+1,-1,-1]"
    };
    
    for (int n = 0; n < NUM_NEURONS; n++) {
        bool match = (hw_results[n] == ref_results[n]);
        if (!match) all_pass = false;
        printf("       %d   | %s |    %4d   |   %4d   |  %s\n",
               n, patterns[n], ref_results[n], hw_results[n],
               match ? "OK" : "FAIL");
    }
    
    printf("    Result: %s\n", all_pass ? "PASS" : "FAIL");
    return all_pass;
}

static void run_benchmark(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  BENCHMARK: Throughput Measurement\n");
    printf("----------------------------------------------------------------------\n");
    
    uint8_t inputs[INPUT_DIM] = {8, 8, 8, 8};
    int results[NUM_NEURONS];
    int iterations = 1000;
    
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < iterations; i++) {
        parallel_dot(inputs, results);
    }
    int64_t end = esp_timer_get_time();
    
    float total_ms = (float)(end - start) / 1000.0f;
    float per_dot_us = (float)(end - start) / iterations;
    float dots_per_sec = 1000000.0f / per_dot_us;
    
    printf("\n  %d iterations completed\n", iterations);
    printf("  Total time: %.2f ms\n", total_ms);
    printf("  Per dot product: %.1f us\n", per_dot_us);
    printf("  Throughput: %.0f dot products/second\n", dots_per_sec);
    printf("\n  Note: Each 'dot product' computes 4 neurons in PARALLEL.\n");
    printf("  Effective rate: %.0f neuron-updates/second\n", dots_per_sec * NUM_NEURONS);
}

// ============================================================
// Main
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("======================================================================\n");
    printf("  PARALLEL DOT PRODUCT: PARLIO + PCNT = 4 Neurons Simultaneously\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  This demo shows parallel computation:\n");
    printf("  - PARLIO transmits 8 bits in parallel\n");
    printf("  - Each bit pair drives one neuron's +/- channels\n");
    printf("  - 4 PCNT units accumulate simultaneously\n");
    printf("  - Ternary weights {-1, 0, +1} eliminate multiplication\n");
    printf("\n");
    
    // Initialize
    printf("  Initializing hardware...\n");
    init_gpio();
    init_pcnt();
    init_parlio();
    init_test_weights();
    printf("  Ready.\n");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // ========================================
    // Verification tests
    // ========================================
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  VERIFICATION: Compare Hardware vs Reference\n");
    printf("----------------------------------------------------------------------\n");
    
    int tests_passed = 0;
    int tests_total = 0;
    
    uint8_t test1[] = {1, 1, 1, 1};
    tests_total++; if (run_verification_test("Test 1: Unit input [1,1,1,1]", test1)) tests_passed++;
    
    uint8_t test2[] = {10, 10, 10, 10};
    tests_total++; if (run_verification_test("Test 2: Uniform input [10,10,10,10]", test2)) tests_passed++;
    
    uint8_t test3[] = {15, 0, 15, 0};
    tests_total++; if (run_verification_test("Test 3: Sparse input [15,0,15,0]", test3)) tests_passed++;
    
    uint8_t test4[] = {1, 2, 3, 4};
    tests_total++; if (run_verification_test("Test 4: Gradient input [1,2,3,4]", test4)) tests_passed++;
    
    uint8_t test5[] = {15, 15, 15, 15};
    tests_total++; if (run_verification_test("Test 5: Max input [15,15,15,15]", test5)) tests_passed++;
    
    // ========================================
    // Benchmark
    // ========================================
    run_benchmark();
    
    // ========================================
    // Summary
    // ========================================
    printf("\n");
    printf("======================================================================\n");
    printf("  SUMMARY\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  Verification: %d / %d tests passed\n", tests_passed, tests_total);
    printf("\n");
    
    if (tests_passed == tests_total) {
        printf("  ALL TESTS PASSED\n");
        printf("\n");
        printf("  What we demonstrated:\n");
        printf("    1. 4 dot products computed simultaneously (parallel)\n");
        printf("    2. Ternary weights: +1 adds, -1 subtracts, 0 skips\n");
        printf("    3. Hardware matches reference implementation exactly\n");
        printf("    4. PARLIO + PCNT = parallel accumulation\n");
        printf("\n");
        printf("  This is the foundation of neural network inference.\n");
        printf("  Next: 03_spectral_oscillator - add phase dynamics.\n");
    } else {
        printf("  SOME TESTS FAILED - Please report this issue.\n");
    }
    
    printf("\n");
    printf("======================================================================\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
