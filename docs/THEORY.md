# Mathematical Foundations

This document covers the mathematics behind Pulse Arithmetic Lab.
No handwaving - if you want to understand what's really happening, read on.

---

## Table of Contents

1. [Q15 Fixed-Point Arithmetic](#q15-fixed-point-arithmetic)
2. [Pulse Counting as Addition](#pulse-counting-as-addition)
3. [Ternary Weights and Dot Products](#ternary-weights-and-dot-products)
4. [Complex Oscillators](#complex-oscillators)
5. [Kuramoto Synchronization](#kuramoto-synchronization)
6. [Equilibrium Propagation](#equilibrium-propagation)
7. [References](#references)

---

## Q15 Fixed-Point Arithmetic

### Why Fixed-Point?

The ESP32-C6 has no floating-point unit. While software float is possible,
it's slow (~50 cycles per operation). Fixed-point gives us:
- Single-cycle integer operations
- Predictable overflow behavior
- Exact reproducibility

### Q15 Format

Q15 uses 16-bit signed integers to represent numbers in [-1, 1):

```
Value = integer / 32768

Examples:
  32767 (0x7FFF) = +0.99997
  16384 (0x4000) = +0.5
      0 (0x0000) =  0.0
 -16384 (0xC000) = -0.5
 -32768 (0x8000) = -1.0
```

### Q15 Multiplication

To multiply two Q15 numbers and get a Q15 result:

```c
int16_t q15_mul(int16_t a, int16_t b) {
    int32_t result = (int32_t)a * (int32_t)b;
    return (int16_t)(result >> 15);
}
```

The 32-bit intermediate prevents overflow. The shift by 15 renormalizes.

### Overflow Handling

Q15 saturates at the boundaries:
- `0x7FFF + 0x0001` should clamp to `0x7FFF`, not wrap to `0x8000`
- We use explicit clamping or rely on compiler intrinsics

---

## Pulse Counting as Addition

### The Core Insight

A pulse counter is an accumulator. Each pulse increments the count by 1.
If we generate N pulses, the counter reads N. This is addition.

```
Initial: count = 0
Pulse 1: count = 1
Pulse 2: count = 2
...
Pulse N: count = N
```

### Hardware Implementation

The ESP32-C6 PCNT peripheral has:
- 4 independent units
- 16-bit signed counters (-32768 to +32767)
- Edge detection on GPIO pins
- Configurable increment/decrement per edge

```
GPIO pin → Edge detector → Counter register
              ↑
         Rising/falling
         edge select
```

### Throughput

At 160 MHz CPU clock, GPIO toggle takes ~6 cycles minimum.
Measured throughput: **1.1 million pulses/second** (~900 ns/pulse).

This is the fundamental operation rate for our system.

---

## Ternary Weights and Dot Products

### Standard Dot Product

```
y = Σ w_i * x_i
```

With floating-point weights, this requires N multiplications.

### Ternary Restriction

Restrict weights to {-1, 0, +1}:

```
w_i = +1: y += x_i  (add)
w_i =  0: y += 0    (skip)
w_i = -1: y -= x_i  (subtract)
```

No multiplication needed. Just conditional addition/subtraction.

### Hardware Implementation

For each neuron, we maintain two bitmasks:
- `pos_mask`: bits set where weight = +1
- `neg_mask`: bits set where weight = -1

```c
int dot_product(uint8_t* input, uint8_t pos_mask, uint8_t neg_mask) {
    int sum = 0;
    for (int i = 0; i < 8; i++) {
        if (pos_mask & (1 << i)) sum += input[i];
        if (neg_mask & (1 << i)) sum -= input[i];
    }
    return sum;
}
```

With PCNT, we go further: route positive-weight inputs to the increment
channel, negative-weight inputs to the decrement channel. The counter
computes the dot product in hardware.

### Parallel Computation

PARLIO transmits 8 bits simultaneously. With 4 PCNT units, we compute
4 dot products in parallel:

```
         PARLIO (8 GPIO pins)
         ↓↓↓↓↓↓↓↓
    ┌────┴┴──────┴┴────┐
    │  Routing Matrix  │
    └────┬┬──────┬┬────┘
         ↓↓      ↓↓
    [PCNT0] [PCNT1] [PCNT2] [PCNT3]
       ↓       ↓       ↓       ↓
     dot0    dot1    dot2    dot3
```

---

## Complex Oscillators

### Why Complex Numbers?

Real-valued neurons have magnitude but no phase. Complex neurons have both:

```
z = r * e^(iθ) = r*cos(θ) + i*r*sin(θ)
```

Phase encodes timing information. Two neurons can have the same magnitude
but different phases - they're distinguishable.

### Q15 Complex Representation

```c
typedef struct {
    int16_t real;  // Q15
    int16_t imag;  // Q15
} complex_q15_t;
```

Magnitude: `|z| = sqrt(real² + imag²)` (approximated for speed)
Phase: `θ = atan2(imag, real)` (stored as 8-bit index into sine table)

### Rotation

To rotate a complex number by angle φ:

```
z_new = z * e^(iφ) = (r + ij)(cos(φ) + i*sin(φ))
      = (r*cos(φ) - j*sin(φ)) + i(r*sin(φ) + j*cos(φ))
```

In Q15:
```c
int16_t new_real = q15_mul(z.real, cos_phi) - q15_mul(z.imag, sin_phi);
int16_t new_imag = q15_mul(z.real, sin_phi) + q15_mul(z.imag, cos_phi);
```

### Decay

Each band has a decay factor α ∈ (0, 1):

```
z_new = α * z_rotated
```

- Delta band: α = 0.98 (slow decay, long memory)
- Theta band: α = 0.90
- Alpha band: α = 0.70
- Gamma band: α = 0.30 (fast decay, rapid response)

---

## Kuramoto Synchronization

### The Kuramoto Model

N oscillators with natural frequencies ω_i, coupled with strength K:

```
dθ_i/dt = ω_i + (K/N) * Σ sin(θ_j - θ_i)
```

When K exceeds a critical value, oscillators synchronize.

### Order Parameter (Coherence)

The Kuramoto order parameter measures synchronization:

```
r * e^(iψ) = (1/N) * Σ e^(iθ_j)
```

- r ≈ 0: phases uniformly distributed (incoherent)
- r ≈ 1: all phases aligned (synchronized)

### Our Implementation

We compute coherence as:

```c
// Sum unit vectors (normalized by magnitude)
for each oscillator:
    if magnitude > threshold:
        sum_real += real / magnitude
        sum_imag += imag / magnitude
        count++

// Average
avg_real = sum_real / count
avg_imag = sum_imag / count

// Coherence = magnitude of average unit vector
coherence = sqrt(avg_real² + avg_imag²)
```

This measures phase alignment independent of amplitude.

### Coupling Between Bands

Our coupling is between bands, not within bands:

```
Band A ←→ Band B
```

When Band A's average phase leads Band B's, we adjust Band B's
phase velocity to catch up. This creates inter-band coordination.

---

## Equilibrium Propagation

### The Problem with Backpropagation

Standard backpropagation requires:
1. Forward pass: compute activations
2. Backward pass: compute gradients
3. Different circuits for forward and backward

This is biologically implausible and hardware-inefficient.

### The Equilibrium Propagation Solution

(Scellier & Bengio, 2017)

Key insight: In an energy-based system at equilibrium, gradients can be
computed from the difference between two equilibria.

### Algorithm

**Free Phase:**
1. Clamp input, let system evolve to equilibrium
2. Record correlations: `C_free[i][j] = s_i * s_j`

**Nudged Phase:**
1. Same input, but nudge output toward target: `s_out += β * (target - s_out)`
2. Let system evolve to new equilibrium
3. Record correlations: `C_nudged[i][j] = s_i * s_j`

**Weight Update:**
```
Δw[i][j] = η * (C_nudged[i][j] - C_free[i][j])
```

### Why It Works

The difference in correlations approximates the gradient of the loss
with respect to weights. As β → 0, this becomes exact.

Intuitively: the nudged phase shows what correlations *should* be
to produce the target. The free phase shows what they *are*. The
difference tells us how to change weights.

### Our Implementation

```c
// Free phase
inject_input(pattern);
for (int t = 0; t < FREE_STEPS; t++) {
    evolve_dynamics();
}
record_correlations(corr_free);

// Nudged phase
inject_input(pattern);
for (int t = 0; t < NUDGE_STEPS; t++) {
    evolve_dynamics();
    nudge_toward_target(target, NUDGE_STRENGTH);
}
record_correlations(corr_nudged);

// Update weights
for (int i = 0; i < N; i++) {
    for (int j = 0; j < N; j++) {
        weights[i][j] += LR * (corr_nudged[i][j] - corr_free[i][j]);
    }
}
```

### Connection to Oscillators

Our "correlations" are between oscillator phases:
```
C[i][j] = cos(θ_i - θ_j)
```

This measures phase alignment between oscillators. The learning rule
adjusts coupling to achieve desired phase relationships.

---

## References

### Foundational Papers

1. **Equilibrium Propagation**
   Scellier, B., & Bengio, Y. (2017). "Equilibrium Propagation: Bridging
   the Gap Between Energy-Based Models and Backpropagation."
   Frontiers in Computational Neuroscience.

2. **Kuramoto Model**
   Kuramoto, Y. (1984). "Chemical Oscillations, Waves, and Turbulence."
   Springer-Verlag.

3. **Ternary Neural Networks**
   Li, F., Zhang, B., & Liu, B. (2016). "Ternary Weight Networks."
   arXiv:1605.04711.

### Hardware References

4. **ESP32-C6 Technical Reference Manual**
   Espressif Systems. Chapter on PCNT and PARLIO peripherals.
   https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf

### Additional Reading

5. **Continuous-Time Recurrent Neural Networks**
   Hasani, R., et al. (2021). "Liquid Time-constant Networks."
   AAAI Conference on Artificial Intelligence.

6. **Neuromorphic Computing**
   Schuman, C. D., et al. (2017). "A Survey of Neuromorphic Computing
   and Neural Networks in Hardware." arXiv:1705.06963.

---

## Notation Summary

| Symbol | Meaning |
|--------|---------|
| Q15 | Fixed-point format: 16-bit signed, 15 fractional bits |
| z | Complex number (z = real + i*imag) |
| θ | Phase angle |
| r | Magnitude |
| α | Decay factor (0 < α < 1) |
| K | Coupling strength |
| β | Nudge strength |
| η | Learning rate |
| C[i][j] | Correlation between neurons i and j |

---

*"The purpose of computation is insight, not numbers."* — Richard Hamming
