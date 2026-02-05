# 01: Pulse Addition

**PCNT counts pulses. Counting is addition. This is hardware arithmetic.**

---

## What This Demonstrates

The ESP32-C6 has a Pulse Counter (PCNT) peripheral designed for rotary encoders. But we can use it for something else: **arithmetic**.

When you generate N pulses on a GPIO pin connected to PCNT, the counter register becomes N. Generate M more pulses, and it becomes N + M.

**The silicon is doing addition. No CPU required.**

---

## How It Works

```
GPIO 4 ──────────────────┐
   │                     │
   ├── Output: We toggle │
   │   this pin high/low │
   │                     │
   └── Input: PCNT       │
       counts rising     │
       edges             │
                         ▼
                    PCNT Register
                    (the sum)
```

We configure GPIO 4 as both input and output (internal loopback). When we toggle it high, PCNT sees a rising edge and increments. Toggle it low, nothing happens (we only count rising edges).

---

## The Code

The key parts:

### 1. Configure PCNT to count rising edges

```c
pcnt_channel_set_edge_action(
    pcnt_channel,
    PCNT_CHANNEL_EDGE_ACTION_INCREASE,  // Rising edge = +1
    PCNT_CHANNEL_EDGE_ACTION_HOLD       // Falling edge = no change
);
```

### 2. Generate pulses by toggling GPIO

```c
void generate_pulses(int count) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(PULSE_GPIO, 1);  // Rising edge - counted!
        gpio_set_level(PULSE_GPIO, 0);  // Falling edge - ignored
    }
}
```

### 3. Read the result

```c
int get_count(void) {
    int count = 0;
    pcnt_unit_get_count(pcnt_unit, &count);
    return count;
}
```

That's it. The rest is verification.

---

## Running It

```bash
# From pulse-arithmetic-lab/firmware/01_pulse_addition
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected Output

```
======================================================================
  PULSE ADDITION: PCNT Counts Pulses = Hardware Addition
======================================================================

  This demo shows that PCNT performs addition in hardware.
  We generate pulses on GPIO 4, and PCNT counts them.
  No CPU computation - the silicon does the math.

----------------------------------------------------------------------
  TEST 1: Basic Pulse Counting
----------------------------------------------------------------------

  Count 10 pulses
    Expected: 10
    Actual:   10
    Time:     X us (Y ns/pulse)
    Result:   PASS

  Count 100 pulses
    Expected: 100
    Actual:   100
    ...

----------------------------------------------------------------------
  TEST 2: Addition via Sequential Pulses
----------------------------------------------------------------------

  Addition Test: 5 + 3
    After 5 pulses: PCNT = 5
    After 3 more pulses: PCNT = 8
    Expected sum: 8
    Result: PASS

...

  ALL TESTS PASSED
```

---

## What You Should Observe

1. **PCNT always equals the number of pulses generated** - The counter is accurate.

2. **Generating A pulses, then B more, gives A+B** - Accumulation is addition.

3. **Speed: ~50-100 ns per pulse** - GPIO toggle is fast, PCNT is faster.

---

## What's Next?

This demo shows addition with ONE counter. But ESP32-C6 has FOUR PCNT units.

In the next demo (02_parallel_dot), we use PARLIO to send pulses to all 4 counters simultaneously. Four additions in parallel = the beginnings of a dot product.

---

## If Tests Fail

Check:
- Is GPIO 4 available? (Not used by USB, etc.)
- Is the board an ESP32-C6? (Other ESP32 variants have different PCNT)
- Try a different GPIO (edit PULSE_GPIO in the code)

Report issues at: https://github.com/EntroMorphic/pulse-arithmetic-lab/issues
