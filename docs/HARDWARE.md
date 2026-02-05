# Hardware Reference

Register-level documentation for ESP32-C6 peripherals used in Pulse Arithmetic Lab.

---

## Table of Contents

1. [ESP32-C6 Overview](#esp32-c6-overview)
2. [PCNT (Pulse Counter)](#pcnt-pulse-counter)
3. [PARLIO (Parallel I/O)](#parlio-parallel-io)
4. [GPIO Configuration](#gpio-configuration)
5. [Timing and Performance](#timing-and-performance)
6. [Pin Assignments](#pin-assignments)

---

## ESP32-C6 Overview

### Key Specifications

| Feature | Value |
|---------|-------|
| Core | RISC-V single-core, 160 MHz |
| SRAM | 512 KB |
| Flash | 4 MB (on module) |
| GPIO | 22 pins |
| PCNT Units | 4 |
| PARLIO TX Units | 1 |
| Operating Voltage | 3.3V |

### Why ESP32-C6?

1. **PCNT peripheral** - Hardware pulse counting without CPU intervention
2. **PARLIO peripheral** - 8-bit parallel output in a single cycle
3. **Low cost** - ~$5 for DevKit
4. **Good documentation** - ESP-IDF provides clean APIs
5. **RISC-V core** - Open architecture, no licensing issues

### Limitations

- No floating-point unit (FPU) - we use Q15 fixed-point
- Single core - no parallel CPU threads
- 16-bit PCNT counters - overflow at 32767

---

## PCNT (Pulse Counter)

### Overview

The PCNT peripheral counts pulses on GPIO pins without CPU intervention.
Each unit has:
- One 16-bit signed counter (-32768 to +32767)
- Two channels (we typically use one)
- Configurable edge actions (increment, decrement, hold)
- Glitch filter (optional)

### Register Map

Base address: `0x60017000` (PCNT)

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | U0_CONF0 | Unit 0 configuration |
| 0x04 | U0_CONF1 | Unit 0 configuration (cont.) |
| 0x08 | U0_CONF2 | Unit 0 configuration (cont.) |
| 0x30 | U0_CNT | Unit 0 counter value |
| ... | ... | Units 1-3 follow same pattern |

### Configuration (ESP-IDF API)

```c
#include "driver/pulse_cnt.h"

// Create unit
pcnt_unit_config_t unit_config = {
    .high_limit = 32767,
    .low_limit = -32768,
};
pcnt_unit_handle_t unit;
pcnt_new_unit(&unit_config, &unit);

// Create channel
pcnt_chan_config_t chan_config = {
    .edge_gpio_num = GPIO_NUM,
    .level_gpio_num = -1,  // Not used
};
pcnt_channel_handle_t channel;
pcnt_new_channel(unit, &chan_config, &channel);

// Configure edge actions
pcnt_channel_set_edge_action(channel,
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge: +1
    PCNT_CHANNEL_EDGE_ACTION_HOLD       // Falling edge: no change
);

// Enable and start
pcnt_unit_enable(unit);
pcnt_unit_clear_count(unit);
pcnt_unit_start(unit);

// Read count
int count;
pcnt_unit_get_count(unit, &count);
```

### Edge Actions

| Action | Effect on Counter |
|--------|-------------------|
| INCREASE | +1 |
| DECREASE | -1 |
| HOLD | No change |

For ternary weights:
- Weight +1: Route to INCREASE channel
- Weight -1: Route to DECREASE channel
- Weight 0: Don't connect

### Glitch Filter

Optional filter to ignore pulses shorter than threshold:

```c
pcnt_glitch_filter_config_t filter_config = {
    .max_glitch_ns = 1000,  // Ignore pulses < 1μs
};
pcnt_unit_set_glitch_filter(unit, &filter_config);
```

We disable filtering for maximum throughput.

### Overflow Handling

Counter wraps at limits. For counts > 32767, use watch points:

```c
pcnt_unit_add_watch_point(unit, 30000);  // Trigger at 30000

// In callback:
void on_reach(pcnt_unit_handle_t unit, ...) {
    overflow_count++;
    pcnt_unit_clear_count(unit);
}
```

We avoid this complexity by keeping counts < 32767.

---

## PARLIO (Parallel I/O)

### Overview

PARLIO transmits/receives multiple bits simultaneously on parallel GPIO pins.
TX mode sends 8 bits per clock cycle.

### Key Features

- Up to 8 data pins (configurable width)
- Clock output pin
- DMA support for continuous streaming
- Configurable clock frequency

### Register Map

Base address: `0x60015000` (PARL_IO)

| Offset | Register | Description |
|--------|----------|-------------|
| 0x00 | TX_CFG0 | TX configuration |
| 0x04 | TX_CFG1 | TX configuration (cont.) |
| 0x08 | TX_DATA | TX data register |
| ... | ... | ... |

### Configuration (ESP-IDF API)

```c
#include "driver/parlio_tx.h"

// Configure TX unit
parlio_tx_unit_config_t tx_config = {
    .clk_src = PARLIO_CLK_SRC_DEFAULT,
    .data_width = 8,  // 8 parallel bits
    .clk_out_gpio_num = CLK_GPIO,
    .clk_in_gpio_num = -1,  // Not used for TX
    .output_clk_freq_hz = 1000000,  // 1 MHz
    .trans_queue_depth = 4,
    .max_transfer_size = 256,
    .valid_gpio_num = -1,  // No valid signal
};

parlio_tx_unit_handle_t tx_unit;
parlio_new_tx_unit(&tx_config, &tx_unit);

// Assign data GPIOs
gpio_num_t data_gpios[] = {4, 5, 6, 7, 8, 9, 10, 11};
parlio_tx_unit_set_data_gpio(tx_unit, data_gpios);

// Enable
parlio_tx_unit_enable(tx_unit);

// Transmit data
uint8_t data[] = {0b10101010, 0b11001100, ...};
parlio_transmit_config_t transmit_cfg = {
    .idle_value = 0,
};
parlio_tx_unit_transmit(tx_unit, data, sizeof(data), &transmit_cfg);
```

### Timing

At 1 MHz PARLIO clock:
- 1 byte transmitted per μs
- 8 bits appear simultaneously on GPIO pins
- Connected PCNT units count rising edges

### Wiring for Dot Product

```
PARLIO Data[0] ──┬── PCNT0 CH0 (+1 weight)
                 └── PCNT0 CH1 (-1 weight)

PARLIO Data[1] ──┬── PCNT0 CH0 (if +1)
                 └── PCNT0 CH1 (if -1)

... (routing determined by weight matrix)
```

In practice, we use GPIO loopback and software routing.

---

## GPIO Configuration

### Pin Modes

```c
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << GPIO_NUM),
    .mode = GPIO_MODE_INPUT_OUTPUT,  // Both for loopback
    .pull_down_en = GPIO_PULLDOWN_ENABLE,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
};
gpio_config(&io_conf);
```

### Toggle Speed

Direct register access for maximum speed:

```c
// Set high
GPIO.out_w1ts.val = (1 << GPIO_NUM);

// Set low  
GPIO.out_w1tc.val = (1 << GPIO_NUM);
```

Measured toggle rate: ~26 MHz (38 ns per transition).

### Loopback

For demos, we connect GPIO output to PCNT input on the same pin:

```c
.mode = GPIO_MODE_INPUT_OUTPUT
```

The pin drives output and PCNT reads input simultaneously.

---

## Timing and Performance

### Measured Performance

| Operation | Time | Rate |
|-----------|------|------|
| GPIO toggle (register) | 38 ns | 26 MHz |
| GPIO toggle (API) | 100 ns | 10 MHz |
| PCNT read | 50 ns | 20 MHz |
| Pulse count (sustained) | 900 ns/pulse | 1.1 MHz |
| Q15 multiply | 6 cycles | 26 MHz |
| Oscillator evolution step | 103 μs | 9.7 kHz |
| Learning step | 7.2 ms | 139 Hz |

### Bottlenecks

1. **GPIO toggle** - Limited by bus speed, not CPU
2. **PCNT read latency** - Must wait for count to stabilize
3. **Q15 multiply chain** - Each oscillator needs multiple multiplies

### Optimization Tips

1. Use direct register access for GPIO
2. Batch PCNT reads (read once per computation, not per pulse)
3. Precompute sine/cosine tables
4. Avoid division (use shifts or lookup tables)

---

## Pin Assignments

### Demo 01: Pulse Addition

| Function | GPIO |
|----------|------|
| Pulse output / PCNT input | 4 |

### Demo 02: Parallel Dot Product

| Function | GPIO |
|----------|------|
| PARLIO Data[0] / Neuron 0 | 4 |
| PARLIO Data[1] / Neuron 0 | 5 |
| PARLIO Data[2] / Neuron 1 | 6 |
| PARLIO Data[3] / Neuron 1 | 7 |
| PARLIO Data[4] / Neuron 2 | 8 |
| PARLIO Data[5] / Neuron 2 | 9 |
| PARLIO Data[6] / Neuron 3 | 10 |
| PARLIO Data[7] / Neuron 3 | 11 |

### Available GPIOs

ESP32-C6 DevKitC-1 exposes:
- GPIO 0-7 (directly usable)
- GPIO 8-15 (directly usable)
- GPIO 18-23 (directly usable)

Reserved:
- GPIO 12-13: USB
- GPIO 16-17: UART

### Strapping Pins

| GPIO | Strapping Function |
|------|-------------------|
| 8 | Boot mode |
| 9 | Boot mode |
| 15 | JTAG |

Avoid driving these during boot.

---

## References

1. **ESP32-C6 Technical Reference Manual**
   https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf

2. **ESP-IDF PCNT Driver**
   https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/pcnt.html

3. **ESP-IDF PARLIO Driver**
   https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/api-reference/peripherals/parlio.html

4. **ESP32-C6-DevKitC-1 Schematic**
   https://dl.espressif.com/dl/schematics/esp32-c6-devkitc-1-schematics.pdf

---

*"Hardware eventually fails. Software eventually works."* — Unknown
