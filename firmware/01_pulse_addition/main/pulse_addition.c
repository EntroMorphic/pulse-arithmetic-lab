/**
 * 01_pulse_addition.c - PCNT Counts Pulses = Hardware Addition
 * 
 * THE SIMPLEST POSSIBLE DEMO
 * 
 * This demonstrates that the ESP32-C6's Pulse Counter (PCNT) peripheral
 * performs addition in hardware. We generate pulses on a GPIO pin,
 * loop them back to PCNT, and the counter increments.
 * 
 * No CPU computation. The silicon does the math.
 * 
 * Hardware setup: None required! We use internal GPIO loopback.
 * GPIO 4 output -> GPIO 4 input (same pin, configured for loopback)
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_timer.h"

// Configuration
#define PULSE_GPIO      4       // GPIO pin for pulse generation
#define PCNT_HIGH_LIMIT 32767   // Max count before overflow

// Handles
static pcnt_unit_handle_t pcnt_unit = NULL;
static pcnt_channel_handle_t pcnt_channel = NULL;

/**
 * Initialize PCNT to count rising edges on PULSE_GPIO
 */
static void init_pcnt(void) {
    // Configure PCNT unit
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = -PCNT_HIGH_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));
    
    // Configure channel to count rising edges
    pcnt_chan_config_t chan_config = {
        .edge_gpio_num = PULSE_GPIO,
        .level_gpio_num = -1,  // No level input
    };
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_channel));
    
    // Count +1 on rising edge, ignore falling edge
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
        pcnt_channel,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge = +1
        PCNT_CHANNEL_EDGE_ACTION_HOLD       // Falling edge = no change
    ));
    
    // Enable and start
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));
}

/**
 * Initialize GPIO for pulse generation (output mode)
 */
static void init_gpio(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PULSE_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT,  // Both input and output for loopback
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PULSE_GPIO, 0);  // Start low
}

/**
 * Generate N pulses on GPIO by toggling high/low
 * Each high->low->high cycle = 1 pulse (we count rising edges)
 */
static void generate_pulses(int count) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(PULSE_GPIO, 1);  // Rising edge - PCNT counts this
        gpio_set_level(PULSE_GPIO, 0);  // Falling edge - ignored
    }
}

/**
 * Get current PCNT value
 */
static int get_count(void) {
    int count = 0;
    pcnt_unit_get_count(pcnt_unit, &count);
    return count;
}

/**
 * Clear PCNT to zero
 */
static void clear_count(void) {
    pcnt_unit_clear_count(pcnt_unit);
}

/**
 * Run a test: generate N pulses, verify PCNT = N
 */
static bool run_test(int expected_count, const char* test_name) {
    clear_count();
    
    int64_t start = esp_timer_get_time();
    generate_pulses(expected_count);
    int64_t end = esp_timer_get_time();
    
    int actual = get_count();
    bool pass = (actual == expected_count);
    
    printf("\n  %s\n", test_name);
    printf("    Expected: %d\n", expected_count);
    printf("    Actual:   %d\n", actual);
    printf("    Time:     %lld us (%.1f ns/pulse)\n", 
           (end - start), 
           (float)(end - start) * 1000.0f / expected_count);
    printf("    Result:   %s\n", pass ? "PASS" : "FAIL");
    
    return pass;
}

/**
 * Demonstrate addition: A + B via sequential pulse generation
 */
static bool test_addition(int a, int b) {
    clear_count();
    
    printf("\n  Addition Test: %d + %d\n", a, b);
    
    // Generate A pulses
    generate_pulses(a);
    int after_a = get_count();
    printf("    After %d pulses: PCNT = %d\n", a, after_a);
    
    // Generate B more pulses (without clearing!)
    generate_pulses(b);
    int after_b = get_count();
    printf("    After %d more pulses: PCNT = %d\n", b, after_b);
    
    int expected = a + b;
    bool pass = (after_b == expected);
    printf("    Expected sum: %d\n", expected);
    printf("    Result: %s\n", pass ? "PASS" : "FAIL");
    
    return pass;
}

/**
 * Main entry point
 */
void app_main(void) {
    printf("\n\n");
    printf("======================================================================\n");
    printf("  PULSE ADDITION: PCNT Counts Pulses = Hardware Addition\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  This demo shows that PCNT performs addition in hardware.\n");
    printf("  We generate pulses on GPIO %d, and PCNT counts them.\n", PULSE_GPIO);
    printf("  No CPU computation - the silicon does the math.\n");
    printf("\n");
    
    // Initialize
    printf("  Initializing GPIO and PCNT...\n");
    init_gpio();
    init_pcnt();
    printf("  Ready.\n");
    
    // Small delay for stability
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // ========================================
    // Test 1: Basic counting
    // ========================================
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 1: Basic Pulse Counting\n");
    printf("----------------------------------------------------------------------\n");
    
    int tests_passed = 0;
    int tests_total = 0;
    
    tests_total++; if (run_test(10, "Count 10 pulses")) tests_passed++;
    tests_total++; if (run_test(100, "Count 100 pulses")) tests_passed++;
    tests_total++; if (run_test(1000, "Count 1000 pulses")) tests_passed++;
    tests_total++; if (run_test(10000, "Count 10000 pulses")) tests_passed++;
    
    // ========================================
    // Test 2: Addition
    // ========================================
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 2: Addition via Sequential Pulses\n");
    printf("----------------------------------------------------------------------\n");
    printf("\n");
    printf("  Key insight: Generating A pulses, then B more pulses,\n");
    printf("  results in PCNT = A + B. The hardware accumulates.\n");
    
    tests_total++; if (test_addition(5, 3)) tests_passed++;
    tests_total++; if (test_addition(100, 50)) tests_passed++;
    tests_total++; if (test_addition(1000, 2000)) tests_passed++;
    
    // ========================================
    // Test 3: Throughput benchmark
    // ========================================
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 3: Throughput Benchmark\n");
    printf("----------------------------------------------------------------------\n");
    
    clear_count();
    int benchmark_pulses = 100000;
    
    int64_t start = esp_timer_get_time();
    generate_pulses(benchmark_pulses);
    int64_t end = esp_timer_get_time();
    
    int final_count = get_count();
    float time_ms = (float)(end - start) / 1000.0f;
    float pulses_per_sec = (float)benchmark_pulses / time_ms * 1000.0f;
    
    printf("\n  Benchmark: %d pulses\n", benchmark_pulses);
    printf("    Time: %.2f ms\n", time_ms);
    printf("    Rate: %.0f pulses/second\n", pulses_per_sec);
    printf("    Final count: %d (expected %d)\n", final_count, benchmark_pulses);
    
    if (final_count == benchmark_pulses) {
        tests_passed++;
    }
    tests_total++;
    
    // ========================================
    // Summary
    // ========================================
    printf("\n");
    printf("======================================================================\n");
    printf("  SUMMARY\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  Tests passed: %d / %d\n", tests_passed, tests_total);
    printf("\n");
    
    if (tests_passed == tests_total) {
        printf("  ALL TESTS PASSED\n");
        printf("\n");
        printf("  What we demonstrated:\n");
        printf("    1. PCNT counts pulses accurately (up to 100,000 tested)\n");
        printf("    2. Accumulation = addition (A pulses + B pulses = A+B)\n");
        printf("    3. The counting happens in hardware, not software\n");
        printf("\n");
        printf("  This is the foundation of Pulse Arithmetic.\n");
        printf("  Next: 02_parallel_dot - multiple additions in parallel.\n");
    } else {
        printf("  SOME TESTS FAILED - Please report this issue.\n");
    }
    
    printf("\n");
    printf("======================================================================\n");
    printf("\n");
    
    // Idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
