# 02: Parallel Dot Product

**PARLIO + PCNT = 4 neurons computing simultaneously.**

---

## What This Demonstrates

In demo 01, we showed that PCNT counts pulses (= addition). But we only used ONE counter.

The ESP32-C6 has FOUR PCNT units. If we can send pulses to all four at once, we get four additions in parallel.

Enter PARLIO (Parallel I/O): it transmits 8 bits simultaneously at 10 MHz. We map:
- Bits 0-1 → Neuron 0 (+/- channels)
- Bits 2-3 → Neuron 1 (+/- channels)
- Bits 4-5 → Neuron 2 (+/- channels)
- Bits 6-7 → Neuron 3 (+/- channels)

**One byte transmitted = all 4 neurons receive pulses.**

---

## How It Works

```
                      PARLIO (8-bit parallel output)
                              │
        ┌───────────┬─────────┼─────────┬───────────┐
        │           │         │         │           │
        ▼           ▼         ▼         ▼           ▼
    [bit 0,1]   [bit 2,3]  [bit 4,5]  [bit 6,7]
        │           │         │         │
        ▼           ▼         ▼         ▼
    ┌───────┐   ┌───────┐ ┌───────┐ ┌───────┐
    │ PCNT  │   │ PCNT  │ │ PCNT  │ │ PCNT  │
    │ Unit 0│   │ Unit 1│ │ Unit 2│ │ Unit 3│
    │ +  -  │   │ +  -  │ │ +  -  │ │ +  -  │
    └───────┘   └───────┘ └───────┘ └───────┘
        │           │         │         │
        ▼           ▼         ▼         ▼
      dot[0]      dot[1]    dot[2]    dot[3]
```

---

## Ternary Weights

Neural networks normally compute: `output = sum(weight[i] * input[i])`

That requires multiplication. But if we restrict weights to {-1, 0, +1}:
- weight = +1: add input to accumulator (send pulses to + channel)
- weight = -1: subtract input (send pulses to - channel)
- weight = 0: do nothing (send no pulses)

**No multiply instruction needed.** Multiplication becomes routing.

---

## The Code

### Weight representation

```c
typedef struct {
    uint32_t pos_mask;  // Bit i set = weight[i] is +1
    uint32_t neg_mask;  // Bit i set = weight[i] is -1
} ternary_weights_t;
```

### Generating the pulse pattern

```c
for (int i = 0; i < INPUT_DIM; i++) {
    uint8_t val = inputs[i];
    
    for (int p = 0; p < val; p++) {
        uint8_t pulse_byte = 0;
        
        for (int n = 0; n < NUM_NEURONS; n++) {
            if (weights[n].pos_mask & (1 << i)) {
                pulse_byte |= (1 << (n * 2));      // Positive channel
            }
            if (weights[n].neg_mask & (1 << i)) {
                pulse_byte |= (1 << (n * 2 + 1));  // Negative channel
            }
        }
        
        pattern[idx++] = pulse_byte;  // Rising edge
        pattern[idx++] = 0x00;        // Falling edge
    }
}
```

### PCNT dual-channel for +/-

```c
// Positive channel: count UP on rising edge
pcnt_channel_set_edge_action(pcnt_ch_pos[n],
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    PCNT_CHANNEL_EDGE_ACTION_HOLD);

// Negative channel: count DOWN on rising edge
pcnt_channel_set_edge_action(pcnt_ch_neg[n],
    PCNT_CHANNEL_EDGE_ACTION_DECREASE,  // ← This is the key!
    PCNT_CHANNEL_EDGE_ACTION_HOLD);
```

The PCNT register holds `(positive_pulses - negative_pulses)` directly.

---

## Running It

```bash
cd firmware/02_parallel_dot
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected Output

```
======================================================================
  PARALLEL DOT PRODUCT: PARLIO + PCNT = 4 Neurons Simultaneously
======================================================================

----------------------------------------------------------------------
  VERIFICATION: Compare Hardware vs Reference
----------------------------------------------------------------------

  Test 1: Unit input [1,1,1,1]
    Input: [1, 1, 1, 1]
    Neuron | Weight Pattern | Reference | Hardware | Match
    -------+----------------+-----------+----------+------
       0   | [+1,+1,+1,+1] |       4   |      4   |  OK
       1   | [-1,-1,-1,-1] |      -4   |     -4   |  OK
       2   | [+1,-1,+1,-1] |       0   |      0   |  OK
       3   | [+1,+1,-1,-1] |       0   |      0   |  OK
    Result: PASS

...

----------------------------------------------------------------------
  BENCHMARK: Throughput Measurement
----------------------------------------------------------------------

  1000 iterations completed
  Total time: X.XX ms
  Per dot product: X.X us
  Throughput: XXXX dot products/second

  Note: Each 'dot product' computes 4 neurons in PARALLEL.
  Effective rate: XXXXX neuron-updates/second
```

---

## What You Should Observe

1. **Hardware matches reference exactly** - No accumulation errors.

2. **4 neurons compute simultaneously** - Not sequential.

3. **Ternary weights work** - +1 adds, -1 subtracts, both verified.

4. **Throughput scales** - Compare to demo 01's single-counter rate.

---

## What's Next?

We now have parallel dot products with ternary weights. This is the core of neural network inference.

But real neurons have *dynamics* - they don't just compute, they evolve over time. Demo 03 adds spectral oscillators: complex-valued state that rotates, decays, and couples.

---

## If Tests Fail

The parallel configuration uses more GPIOs (4-11). If some fail:
- Check for GPIO conflicts (USB, flash, etc.)
- Try different GPIO assignments
- Report the specific failure pattern

Report issues at: https://github.com/EntroMorphic/pulse-arithmetic-lab/issues
