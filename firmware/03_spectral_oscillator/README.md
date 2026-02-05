# Demo 03: Spectral Oscillator

**Phase Dynamics and Kuramoto Coupling**

## What This Demonstrates

Previous demos showed static computation: input goes in, output comes out.
Real neural dynamics have **state that evolves over time**.

This demo introduces:
- **Complex-valued oscillators** - each neuron has phase and magnitude
- **Band-specific frequencies** - Delta (slow) to Gamma (fast)
- **Kuramoto coupling** - oscillators pull each other toward synchronization
- **Coherence measurement** - quantifying how synchronized the system is

No learning yet - just dynamics. Demo 04 adds equilibrium propagation.

## The Science

### Why Oscillators?

Biological neurons don't just fire discretely - they exhibit rhythmic activity
at multiple frequencies. Different frequency bands serve different purposes:

| Band  | Frequency | Role in Biology | Our Implementation |
|-------|-----------|-----------------|-------------------|
| Delta | 0.5-4 Hz  | Deep sleep, memory | Slow decay (0.98), long memory |
| Theta | 4-8 Hz    | Navigation, memory | Medium decay (0.90) |
| Alpha | 8-12 Hz   | Relaxed attention | Faster decay (0.70) |
| Gamma | 30-100 Hz | Active processing | Fast decay (0.30), rapid response |

### Kuramoto Coupling

When oscillators are coupled, they tend to synchronize. The Kuramoto model
describes this mathematically:

```
dθ/dt = ω + K/N * Σ sin(θ_j - θ_i)
```

Where K is coupling strength. When K is high enough, oscillators lock phase.

We implement this in fixed-point Q15 arithmetic for efficiency.

## Key Concepts

### Complex-Valued State

Each neuron maintains:
```c
typedef struct {
    int16_t real;  // Q15 format: -1.0 to +0.99997
    int16_t imag;
} complex_q15_t;
```

Phase = atan2(imag, real)
Magnitude = sqrt(real² + imag²)

### Band Organization

16 neurons total: 4 bands × 4 neurons each

```
Neurons 0-3:   Delta band (slowest)
Neurons 4-7:   Theta band
Neurons 8-11:  Alpha band
Neurons 12-15: Gamma band (fastest)
```

### Coherence

Measures phase alignment across neurons:
```
coherence = |mean(e^(i*phase))| 
```
- coherence ≈ 0: random phases
- coherence ≈ 1: all phases aligned

## Building and Flashing

```bash
# From this directory
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected Output

```
=== 03 Spectral Oscillator Demo ===

Step 0: Coherence = 0.23 (random initial phases)
  Delta: phase=45, 180, 270, 90   mag=16000, 15800, ...
  Gamma: phase=12, 34, 56, 78     mag=8000, 7500, ...

Step 10: Coherence = 0.45 (starting to synchronize)
...

Step 50: Coherence = 0.82 (highly synchronized)
  Delta: phase=45, 47, 44, 46    (clustered!)
  Gamma: phase=180, 182, 179, 181
```

## What to Try

1. **Change coupling strength** - increase `COUPLING_K` to see faster sync
2. **Asymmetric input** - inject different patterns, watch phase response
3. **Disable coupling** - set K=0, oscillators stay independent
4. **Band interactions** - can fast Gamma oscillators drive slow Delta?

## Connection to Learning

This demo shows that oscillator dynamics naturally create:
- **Stable attractors** (coherent states the system settles into)
- **Temporal integration** (slow bands remember, fast bands react)
- **Correlation structure** (phase relationships encode information)

Demo 04 will exploit these properties for learning without backpropagation.

## Files

- `main/spectral_oscillator.c` - Main implementation
- `main/CMakeLists.txt` - Component registration
- `CMakeLists.txt` - Project configuration

## Claims Tested

This demo supports **Claim 4** from CLAIMS.md:
> Spectral oscillators maintain phase state across time steps

Falsification: If phase coherence doesn't increase with coupling, the
Kuramoto dynamics are not working correctly.
