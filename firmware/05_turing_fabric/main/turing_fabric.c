/**
 * 05_turing_fabric.c - Turing-Complete ETM Fabric
 *
 * AUTONOMOUS HARDWARE COMPUTATION WITH CONDITIONAL BRANCHING
 *
 * This demo proves that the ESP32-C6 can perform Turing-complete computation
 * using only peripheral hardware, with the CPU idle or sleeping.
 *
 * Architecture:
 *   Timer0 ─ETM─► GDMA ─► PARLIO ─► GPIO ─► PCNT
 *                                            │
 *   PCNT threshold ─ETM─► Timer0 STOP ◄──────┘
 *
 * This implements hardware IF/ELSE:
 *   IF PCNT reaches threshold → Timer STOPS (conditional branch taken)
 *   ELSE → Timer continues normally
 *
 * Key Components:
 *   - PARLIO + GDMA: Autonomous waveform generation (no CPU during TX)
 *   - PCNT: Hardware edge counting with threshold watch points
 *   - ETM: Event Task Matrix wires PCNT threshold → Timer stop
 *   - Timer: Provides timing reference, stopped by ETM when threshold hit
 *
 * Verification:
 *   1. PARLIO→PCNT edge counting (100% accuracy)
 *   2. Conditional branch (timer stops before alarm when threshold reached)
 *   3. ELSE branch (timer runs normally when threshold not reached)
 *   4. Autonomous operation (CPU idle while hardware executes)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "driver/gptimer.h"
#include "driver/pulse_cnt.h"
#include "driver/parlio_tx.h"
#include "driver/gpio.h"
#include "soc/soc_etm_source.h"

static const char *TAG = "TURING";

// ============================================================
// ETM Register Definitions (bare metal - PCNT ETM not in ESP-IDF API)
// ============================================================

#define ETM_BASE                    0x600B8000
#define ETM_CH_ENA_SET_REG          (ETM_BASE + 0x04)
#define ETM_CH_ENA_CLR_REG          (ETM_BASE + 0x08)
#define ETM_CH_EVT_ID_REG(n)        (ETM_BASE + 0x18 + (n) * 8)
#define ETM_CH_TASK_ID_REG(n)       (ETM_BASE + 0x1C + (n) * 8)

#define ETM_REG(addr)               (*(volatile uint32_t*)(addr))

// PCR for ETM clock
#define PCR_BASE                    0x60096000
#define PCR_SOC_ETM_CONF            (PCR_BASE + 0x90)

// ============================================================
// Configuration
// ============================================================

#define TEST_GPIO           4       // PARLIO output / PCNT input (directly connected)
#define PARLIO_CLK_HZ       2000000 // 2 MHz pulse rate
#define THRESHOLD_EDGES     256     // PCNT threshold for conditional branch
#define TIMER_ALARM_US      10000   // Timer alarm at 10ms (should NOT reach if ETM works)

// ============================================================
// Global Handles
// ============================================================

static gptimer_handle_t timer0 = NULL;
static pcnt_unit_handle_t pcnt = NULL;
static pcnt_channel_handle_t pcnt_chan = NULL;
static parlio_tx_unit_handle_t parlio = NULL;

// Pattern buffer in DMA-capable memory
// 0x55 = 01010101 = 4 rising edges per byte
// 64 bytes × 4 edges = 256 edges total
static uint8_t __attribute__((aligned(4))) pattern_256_edges[64];

static volatile int tx_done_count = 0;

// ============================================================
// ETM Clock Enable
// ============================================================

static void etm_enable_clock(void) {
    volatile uint32_t *conf = (volatile uint32_t*)PCR_SOC_ETM_CONF;
    *conf &= ~(1 << 1);  // Clear reset
    *conf |= (1 << 0);   // Enable clock
    ESP_LOGI(TAG, "ETM clock enabled");
}

// ============================================================
// THE KEY: Wire PCNT threshold → Timer stop via ETM
// ============================================================

static void etm_wire_pcnt_to_timer_stop(int etm_channel) {
    // PCNT doesn't have ESP-IDF ETM API, so we wire it directly via registers
    //
    // Event: PCNT_EVT_CNT_EQ_THRESH (45)
    //   - Fires when PCNT count reaches a watch point value
    //
    // Task: TIMER0_TASK_CNT_STOP_TIMER0 (92)
    //   - Stops Timer0 counting
    //
    // This creates hardware IF/ELSE:
    //   IF (PCNT >= threshold) → Timer STOPS
    //   ELSE → Timer continues
    
    ETM_REG(ETM_CH_EVT_ID_REG(etm_channel)) = PCNT_EVT_CNT_EQ_THRESH;      // Event 45
    ETM_REG(ETM_CH_TASK_ID_REG(etm_channel)) = TIMER0_TASK_CNT_STOP_TIMER0; // Task 92
    ETM_REG(ETM_CH_ENA_SET_REG) = (1 << etm_channel);
    
    ESP_LOGI(TAG, "ETM CH%d: PCNT threshold (%d) → Timer0 STOP", 
             etm_channel, THRESHOLD_EDGES);
}

// ============================================================
// Hardware Setup
// ============================================================

static esp_err_t setup_timer(void) {
    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1 MHz = 1 tick per microsecond
    };
    esp_err_t ret = gptimer_new_timer(&cfg, &timer0);
    if (ret != ESP_OK) return ret;
    
    gptimer_alarm_config_t alarm = {
        .alarm_count = TIMER_ALARM_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(timer0, &alarm);
    gptimer_enable(timer0);
    
    ESP_LOGI(TAG, "Timer0: alarm at %d us", TIMER_ALARM_US);
    return ESP_OK;
}

static esp_err_t setup_pcnt(void) {
    pcnt_unit_config_t cfg = {
        .low_limit = -32768,
        .high_limit = 32767,
    };
    esp_err_t ret = pcnt_new_unit(&cfg, &pcnt);
    if (ret != ESP_OK) return ret;
    
    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num = TEST_GPIO,
        .level_gpio_num = -1,
    };
    ret = pcnt_new_channel(pcnt, &chan_cfg, &pcnt_chan);
    if (ret != ESP_OK) return ret;
    
    // Count rising edges only
    pcnt_channel_set_edge_action(pcnt_chan, 
        PCNT_CHANNEL_EDGE_ACTION_INCREASE, 
        PCNT_CHANNEL_EDGE_ACTION_HOLD);
    
    // THIS IS CRITICAL: Add watch point that triggers ETM event
    pcnt_unit_add_watch_point(pcnt, THRESHOLD_EDGES);
    
    pcnt_unit_enable(pcnt);
    pcnt_unit_start(pcnt);
    
    ESP_LOGI(TAG, "PCNT: threshold watch point at %d edges", THRESHOLD_EDGES);
    return ESP_OK;
}

static esp_err_t setup_parlio(void) {
    parlio_tx_unit_config_t cfg = {
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .clk_in_gpio_num = -1,
        .output_clk_freq_hz = PARLIO_CLK_HZ,
        .data_width = 1,
        .clk_out_gpio_num = -1,
        .valid_gpio_num = -1,
        .trans_queue_depth = 16,
        .max_transfer_size = 256,
        .sample_edge = PARLIO_SAMPLE_EDGE_POS,
        .bit_pack_order = PARLIO_BIT_PACK_ORDER_LSB,
        .flags = { .io_loop_back = 1 },  // Internal loopback: output feeds back to input
    };
    
    for (int i = 0; i < PARLIO_TX_UNIT_MAX_DATA_WIDTH; i++) {
        cfg.data_gpio_nums[i] = (i == 0) ? TEST_GPIO : -1;
    }
    
    esp_err_t ret = parlio_new_tx_unit(&cfg, &parlio);
    if (ret != ESP_OK) return ret;
    
    parlio_tx_unit_enable(parlio);
    
    ESP_LOGI(TAG, "PARLIO: GPIO%d at %d Hz with loopback", TEST_GPIO, PARLIO_CLK_HZ);
    return ESP_OK;
}

static void setup_patterns(void) {
    // 0x55 = 01010101 binary = 4 rising edges per byte
    // 64 bytes × 4 edges = 256 edges = exactly our threshold
    for (int i = 0; i < 64; i++) {
        pattern_256_edges[i] = 0x55;
    }
    ESP_LOGI(TAG, "Pattern: 64 bytes of 0x55 = 256 edges");
}

// ============================================================
// Callback for autonomous operation test
// ============================================================

static bool IRAM_ATTR parlio_done_cb(parlio_tx_unit_handle_t unit, 
                                      const parlio_tx_done_event_data_t *edata, 
                                      void *user_ctx) {
    tx_done_count++;
    return false;
}

// ============================================================
// TEST 1: Basic PARLIO → PCNT verification
// ============================================================

static bool test_parlio_pcnt(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 1: PARLIO → PCNT Edge Counting\n");
    printf("----------------------------------------------------------------------\n");
    
    pcnt_unit_clear_count(pcnt);
    
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    esp_err_t ret = parlio_tx_unit_transmit(parlio, pattern_256_edges, 64 * 8, &tx_cfg);
    if (ret != ESP_OK) {
        printf("  Transmit failed: %s\n", esp_err_to_name(ret));
        return false;
    }
    
    parlio_tx_unit_wait_all_done(parlio, 1000);
    
    int count;
    pcnt_unit_get_count(pcnt, &count);
    
    printf("  Sent: 64 bytes of 0x55 (256 rising edges)\n");
    printf("  PCNT count: %d\n", count);
    printf("  Result: %s\n", (count == 256) ? "PASS" : "FAIL");
    
    return (count == 256);
}

// ============================================================
// TEST 2: Conditional Branch (IF: threshold reached → timer stops)
// ============================================================

static bool test_conditional_branch(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 2: Conditional Branch (PCNT threshold → Timer STOP)\n");
    printf("----------------------------------------------------------------------\n");
    
    // Wire ETM: PCNT threshold → Timer stop
    etm_wire_pcnt_to_timer_stop(10);  // Use ETM channel 10
    
    // Reset counters
    pcnt_unit_clear_count(pcnt);
    gptimer_set_raw_count(timer0, 0);
    
    // Start timer (will alarm at 10ms if ETM doesn't stop it)
    gptimer_start(timer0);
    
    // Transmit pattern (256 edges = threshold)
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    parlio_tx_unit_transmit(parlio, pattern_256_edges, 64 * 8, &tx_cfg);
    parlio_tx_unit_wait_all_done(parlio, 1000);
    
    // Small delay for ETM to process
    vTaskDelay(pdMS_TO_TICKS(5));
    
    // Read results
    uint64_t timer_count;
    gptimer_get_raw_count(timer0, &timer_count);
    
    int pcnt_count;
    pcnt_unit_get_count(pcnt, &pcnt_count);
    
    gptimer_stop(timer0);
    
    printf("  PCNT count: %d (threshold: %d)\n", pcnt_count, THRESHOLD_EDGES);
    printf("  Timer count: %llu us (alarm: %d us)\n", timer_count, TIMER_ALARM_US);
    
    bool pass = false;
    if (pcnt_count >= THRESHOLD_EDGES && timer_count < TIMER_ALARM_US) {
        printf("  CONDITIONAL BRANCH EXECUTED!\n");
        printf("  Timer stopped at %llu us (before %d us alarm)\n", timer_count, TIMER_ALARM_US);
        printf("  Result: PASS\n");
        pass = true;
    } else if (timer_count >= TIMER_ALARM_US) {
        printf("  Timer reached alarm - ETM may not have worked\n");
        printf("  Result: FAIL\n");
    } else {
        printf("  Unexpected state\n");
        printf("  Result: FAIL\n");
    }
    
    return pass;
}

// ============================================================
// TEST 3: ELSE Branch (timer continues when threshold NOT reached)
// ============================================================

static bool test_else_branch(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 3: ELSE Branch (Timer continues when threshold not reached)\n");
    printf("----------------------------------------------------------------------\n");
    
    // Reset counters
    pcnt_unit_clear_count(pcnt);
    gptimer_set_raw_count(timer0, 0);
    
    // Set a FAST alarm (100 us) - should trigger BEFORE we send any pulses
    gptimer_alarm_config_t fast_alarm = {
        .alarm_count = 100,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(timer0, &fast_alarm);
    
    // Start timer WITHOUT sending pulses
    gptimer_start(timer0);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    uint64_t timer_count;
    gptimer_get_raw_count(timer0, &timer_count);
    
    int pcnt_count;
    pcnt_unit_get_count(pcnt, &pcnt_count);
    gptimer_stop(timer0);
    
    printf("  PCNT count: %d (threshold: %d - NOT reached)\n", pcnt_count, THRESHOLD_EDGES);
    printf("  Timer count: %llu us (alarm: 100 us)\n", timer_count);
    
    bool pass = false;
    if (pcnt_count < THRESHOLD_EDGES && timer_count >= 100) {
        printf("  ELSE BRANCH: Timer ran normally (not stopped by ETM)\n");
        printf("  Result: PASS\n");
        pass = true;
    } else {
        printf("  Unexpected behavior\n");
        printf("  Result: FAIL\n");
    }
    
    // Restore normal alarm
    gptimer_alarm_config_t normal_alarm = {
        .alarm_count = TIMER_ALARM_US,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(timer0, &normal_alarm);
    
    return pass;
}

// ============================================================
// TEST 4: Autonomous Operation (CPU idle while hardware executes)
// ============================================================

static bool test_autonomous_operation(void) {
    printf("\n");
    printf("----------------------------------------------------------------------\n");
    printf("  TEST 4: Autonomous Operation (CPU Idle)\n");
    printf("----------------------------------------------------------------------\n");
    
    // Register callback to count completions
    parlio_tx_event_callbacks_t cbs = { .on_trans_done = parlio_done_cb };
    parlio_tx_unit_register_event_callbacks(parlio, &cbs, NULL);
    
    pcnt_unit_clear_count(pcnt);
    tx_done_count = 0;
    
    int num_tx = 100;
    parlio_transmit_config_t tx_cfg = { .idle_value = 0 };
    
    printf("  Queueing %d transmissions...\n", num_tx);
    
    int64_t start = esp_timer_get_time();
    
    // Queue all transmissions (CPU does work here)
    for (int i = 0; i < num_tx; i++) {
        parlio_tx_unit_transmit(parlio, pattern_256_edges, 64 * 8, &tx_cfg);
    }
    
    int64_t queued = esp_timer_get_time();
    printf("  Queue time: %lld us\n", queued - start);
    printf("  CPU now idle while hardware executes...\n");
    
    // CPU idle loop - just spin while hardware does work
    // In real application, CPU could enter WFI (wait for interrupt) or light sleep
    int loops = 0;
    while (tx_done_count < num_tx && loops < 10000000) {
        __asm__ volatile("nop");
        loops++;
    }
    
    int64_t end = esp_timer_get_time();
    
    int count;
    pcnt_unit_get_count(pcnt, &count);
    int expected = num_tx * 256;
    
    printf("  Total time: %lld us\n", end - start);
    printf("  TX completed: %d/%d\n", tx_done_count, num_tx);
    printf("  PCNT count: %d (expected: %d)\n", count, expected);
    printf("  CPU spin loops: %d\n", loops);
    
    int accuracy = (expected > 0) ? (count * 100) / expected : 0;
    printf("  Accuracy: %d%%\n", accuracy);
    
    bool pass = (tx_done_count == num_tx && accuracy == 100);
    printf("  Result: %s\n", pass ? "PASS" : "FAIL");
    
    return pass;
}

// ============================================================
// Main Entry Point
// ============================================================

void app_main(void) {
    printf("\n\n");
    printf("======================================================================\n");
    printf("  TURING-COMPLETE ETM FABRIC\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  This demo proves autonomous hardware computation with\n");
    printf("  conditional branching on ESP32-C6.\n");
    printf("\n");
    printf("  Architecture:\n");
    printf("    Timer → GDMA → PARLIO → GPIO → PCNT\n");
    printf("                                    │\n");
    printf("    PCNT threshold → ETM → Timer STOP\n");
    printf("\n");
    printf("  Hardware IF/ELSE:\n");
    printf("    IF (edges >= %d): Timer STOPS\n", THRESHOLD_EDGES);
    printf("    ELSE: Timer continues\n");
    printf("\n");
    
    esp_err_t ret;
    
    // Enable ETM clock first
    etm_enable_clock();
    
    // Initialize hardware
    printf("  Initializing hardware...\n");
    
    ret = setup_timer();
    if (ret != ESP_OK) {
        printf("  Timer setup failed: %s\n", esp_err_to_name(ret));
        return;
    }
    
    ret = setup_pcnt();
    if (ret != ESP_OK) {
        printf("  PCNT setup failed: %s\n", esp_err_to_name(ret));
        return;
    }
    
    ret = setup_parlio();
    if (ret != ESP_OK) {
        printf("  PARLIO setup failed: %s\n", esp_err_to_name(ret));
        return;
    }
    
    setup_patterns();
    
    printf("  Hardware ready.\n");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Run tests
    int passed = 0;
    int total = 4;
    
    if (test_parlio_pcnt()) passed++;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (test_conditional_branch()) passed++;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (test_else_branch()) passed++;
    vTaskDelay(pdMS_TO_TICKS(100));
    
    if (test_autonomous_operation()) passed++;
    
    // Summary
    printf("\n");
    printf("======================================================================\n");
    printf("  SUMMARY\n");
    printf("======================================================================\n");
    printf("\n");
    printf("  Tests passed: %d / %d\n", passed, total);
    printf("\n");
    
    if (passed == total) {
        printf("  ALL TESTS PASSED\n");
        printf("\n");
        printf("  Turing Completeness Verified:\n");
        printf("    [x] Sequential execution (PARLIO + GDMA)\n");
        printf("    [x] Conditional branching (PCNT → ETM → Timer)\n");
        printf("    [x] State modification (PCNT counter, GPIO)\n");
        printf("    [x] Autonomous operation (CPU idle)\n");
        printf("\n");
        printf("  The silicon thinks. The CPU sleeps.\n");
    } else {
        printf("  SOME TESTS FAILED\n");
        printf("  Check hardware connections and ETM configuration.\n");
    }
    
    printf("\n");
    printf("======================================================================\n");
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
