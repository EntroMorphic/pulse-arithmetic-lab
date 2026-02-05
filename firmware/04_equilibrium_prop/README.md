# Demo 04: Equilibrium Propagation

**Learning Without Backpropagation**

## What This Demonstrates

This is the culmination of demos 01-03. We now have a system that **learns**
without the traditional backward pass algorithm.

```
Traditional Neural Network:           Equilibrium Propagation:
1. Forward pass: compute output       1. Free phase: evolve to equilibrium
2. Backward pass: compute gradients   2. Nudged phase: clamp output, evolve again  
3. Update: w -= lr * gradient         3. Update: w += lr * (nudged - free)
```

The gradient emerges from the **difference between two forward passes**
with different boundary conditions. No separate backward algorithm needed.

## The Science

### Equilibrium Propagation (Scellier & Bengio, 2017)

The key insight: in a system at equilibrium, the gradient of an energy
function can be computed by comparing two equilibrium states.

**Free phase:**
- Present input, let system evolve until stable
- Record neuron correlations: `corr_free[i][j] = state_i * state_j`

**Nudged phase:**
- Same input, but "nudge" output toward target
- Let system evolve to new equilibrium
- Record correlations: `corr_nudged[i][j] = state_i * state_j`

**Weight update:**
```
w[i][j] += learning_rate * (corr_nudged[i][j] - corr_free[i][j])
```

This is mathematically equivalent to backpropagation but uses only
local, Hebbian-like computations.

### Why This Matters

1. **Hardware friendly** - no need for separate forward/backward circuits
2. **Biologically plausible** - neurons only need local information
3. **Energy efficient** - dynamics do the gradient computation
4. **Continuous time** - works with analog/physical systems

## Implementation Details

### Architecture

```
Input (4 dims)
    ↓
Spectral Oscillators (16 neurons, 4 bands)
    ↓
Readout (from Delta band - slowest, most integrated)
```

### Training Loop

```c
for each pattern:
    // Free phase
    inject_input(pattern);
    for (i = 0; i < FREE_STEPS; i++) {
        evolve_dynamics();
    }
    record_free_correlations();
    
    // Nudged phase  
    inject_input(pattern);
    for (i = 0; i < NUDGE_STEPS; i++) {
        evolve_dynamics();
        nudge_output_toward_target();
    }
    record_nudged_correlations();
    
    // Update weights
    update_weights(corr_nudged - corr_free);
```

### Parameters

| Parameter | Value | Purpose |
|-----------|-------|---------|
| FREE_PHASE_STEPS | 30 | Steps to reach free equilibrium |
| NUDGE_PHASE_STEPS | 30 | Steps to reach nudged equilibrium |
| NUDGE_STRENGTH | 0.5 | How hard to push toward target |
| LEARNING_RATE | 0.005 | Weight update magnitude |

## Building and Flashing

```bash
# From this directory
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Expected Output

```
=== 04 Equilibrium Propagation Demo ===

Training 2 patterns...

Epoch 0:
  Pattern 0: target=[1,0,0,0], output=[0.52,0.48,0.51,0.49], error=0.48
  Pattern 1: target=[0,0,0,1], output=[0.51,0.49,0.50,0.50], error=0.49
  Avg error: 0.485

Epoch 10:
  Pattern 0: target=[1,0,0,0], output=[0.78,0.22,0.31,0.19], error=0.23
  Pattern 1: target=[0,0,0,1], output=[0.21,0.30,0.28,0.79], error=0.22
  Avg error: 0.225

Epoch 50:
  Pattern 0: target=[1,0,0,0], output=[0.94,0.08,0.12,0.06], error=0.08
  Pattern 1: target=[0,0,0,1], output=[0.07,0.11,0.09,0.93], error=0.08
  Avg error: 0.080

Learning complete! System discriminates patterns.
```

## What to Try

1. **Add more patterns** - how many can the system learn?
2. **Change nudge strength** - too weak = no learning, too strong = instability
3. **Modify band weights** - which frequency bands are most important?
4. **Longer sequences** - can the system learn temporal patterns?

## Connection to Previous Demos

| Demo | Contribution |
|------|-------------|
| 01_pulse_addition | PCNT provides efficient counting |
| 02_parallel_dot | PARLIO enables parallel weight application |
| 03_spectral_oscillator | Phase dynamics provide the equilibrating system |
| **04_equilibrium_prop** | **Learning emerges from dynamics** |

## Files

- `main/equilibrium_prop.c` - Main implementation
- `main/CMakeLists.txt` - Component registration
- `CMakeLists.txt` - Project configuration

## Claims Tested

This demo supports **Claim 5** from CLAIMS.md:
> Equilibrium propagation enables learning without explicit gradients

Falsification: If error doesn't decrease over epochs, or if random weight
updates perform equally well, the equilibrium propagation mechanism has failed.

## References

- Scellier, B., & Bengio, Y. (2017). "Equilibrium Propagation: Bridging the Gap
  Between Energy-Based Models and Backpropagation"
- Laborieux, A., et al. (2021). "Scaling Equilibrium Propagation to Deep
  ConvNets by Drastically Reducing its Gradient Estimator Bias"
