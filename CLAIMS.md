# Scientific Claims

This document states our claims explicitly and provides falsification conditions for each.

**Philosophy:** If we can't say when we'd be wrong, we're not doing science.

---

## Claim 1: Pulse Counting Performs Addition

### Statement

The ESP32-C6's PCNT (Pulse Counter) peripheral performs integer addition in hardware. When we generate N pulses on a GPIO pin connected to PCNT, the counter increments by N. This is addition, implemented in silicon.

### Falsification Conditions

This claim would be FALSE if:

1. The PCNT register value does not equal the number of pulses generated
2. The addition happens in software (CPU) rather than hardware (PCNT peripheral)
3. The timing suggests CPU intervention (>1μs per pulse would indicate software)

### Test

Run `firmware/01_pulse_addition`. It generates known pulse counts and verifies PCNT matches.

```
Test: Generate 100 pulses
Expected: PCNT = 100
Actual: PCNT = ???
```

If Actual ≠ Expected, the claim is falsified.

### Evidence

- PCNT register reads show exact pulse counts
- Oscilloscope traces show pulse trains on GPIO
- CPU is idle during counting (verified via cycle counter)

---

## Claim 2: Parallel I/O Enables Parallel Computation

### Statement

PARLIO (Parallel I/O) transmits 8 bits simultaneously to 8 GPIO pins. When each GPIO connects to a different PCNT channel, 4 independent additions happen in parallel (ESP32-C6 has 4 PCNT units).

### Falsification Conditions

This claim would be FALSE if:

1. PARLIO does not actually transmit bits in parallel (sequential instead)
2. PCNT units interfere with each other
3. Throughput does not scale with parallelism

### Test

Run `firmware/02_parallel_dot`. It computes 4 dot products simultaneously and compares to sequential baseline.

```
Sequential time: X μs
Parallel time: Y μs
Speedup: X/Y (should be ~4x if truly parallel)
```

If speedup is ~1x, parallelism claim is falsified.

### Evidence

- 4 PCNT units show independent accumulation
- Parallel version is ~4x faster than sequential
- GPIO traces show simultaneous transitions

---

## Claim 3: Ternary Weights Eliminate Multiplication

### Statement

With weights restricted to {-1, 0, +1}, the operation `weight * input` becomes:
- If weight = +1: add input to accumulator
- If weight = 0: do nothing  
- If weight = -1: subtract input from accumulator

No multiplication hardware or instructions are needed.

### Falsification Conditions

This claim would be FALSE if:

1. The implementation uses multiply instructions
2. Results differ from floating-point reference by >1%
3. Ternary restriction makes learning impossible

### Test

Run `firmware/02_parallel_dot` with verification mode. It compares hardware results to NumPy reference.

```
Reference (NumPy, float64): [a, b, c, d]
Hardware (PCNT, ternary): [a', b', c', d']
Max difference: ??? (should be 0 for integer inputs)
```

### Evidence

- Assembly listing shows no MUL instructions in hot path
- Bit-exact match with integer reference implementation
- Learning still works (Claim 5)

---

## Claim 4: Spectral Oscillators Maintain Phase State

### Statement

Complex-valued oscillators, represented in Q15 fixed-point, maintain stable phase relationships over time. Kuramoto coupling causes oscillators to synchronize. Coherence (magnitude of mean oscillator) measures synchronization.

### Falsification Conditions

This claim would be FALSE if:

1. Oscillators diverge to infinity (numerical instability)
2. Phase relationships are random regardless of coupling
3. Coherence does not increase with coupling strength

### Test

Run `firmware/03_spectral_oscillator`. It evolves oscillators and measures coherence.

```
Coupling = 0.0: Coherence = ??? (should be low, ~0.2)
Coupling = 0.5: Coherence = ??? (should be higher)
Coupling = 1.0: Coherence = ??? (should be high, ~0.8)
```

If coherence doesn't increase with coupling, the claim is falsified.

### Evidence

- Coherence timeseries shows synchronization dynamics
- Phase plots show clustering when coupled
- No NaN/Inf in any run

---

## Claim 5: Equilibrium Propagation Learns

### Statement

The weight update rule:

```
Δw_ij = learning_rate × (correlation_nudged - correlation_free)
```

causes the network to learn input-output mappings. The "gradient" emerges from the difference between two forward passes with different boundary conditions.

### Falsification Conditions

This claim would be FALSE if:

1. Loss does not decrease over training epochs
2. Learned outputs are no better than random initialization
3. The update rule requires non-local information

### Test

Run `firmware/04_equilibrium_prop`. It trains on a 2-pattern task.

```
Epoch 0: Loss = ???, Output separation = ???
Epoch 100: Loss = ???, Output separation = ???
Target separation: 128

Success criterion: Output separation > 100 (>78% of target)
```

If final separation < 50 (worse than random), learning claim is falsified.

### Evidence

- Loss decreases over epochs
- Output separation approaches target
- Weight updates use only local (pairwise) correlations

---

## Claim 6: Self-Modification via Coherence

### Statement

The network modifies its own coupling strengths based on global coherence:
- High coherence → reduce coupling (prevent over-synchronization)
- Low coherence → increase coupling (encourage coordination)

This creates a homeostatic loop that operates alongside explicit learning.

### Falsification Conditions

This claim would be FALSE if:

1. Coherence has no measurable effect on coupling
2. Removing coherence feedback has no effect on dynamics
3. The two loops (coherence, learning) don't interact

### Test

Run `firmware/03_spectral_oscillator` with and without coherence feedback.

```
With coherence feedback:
  Coupling range after 1000 steps: [min, max]
  Coherence stability: ???

Without coherence feedback:
  Coupling range after 1000 steps: [min, max] (should be unchanged)
  Coherence stability: ???
```

If coupling ranges are identical, self-modification claim is falsified.

### Evidence

- Coupling timeseries shows modulation
- Ablation study shows different dynamics
- Coherence stabilizes in a controlled range

---

## Summary Table

| Claim | Test | Success Criterion |
|-------|------|-------------------|
| 1. Pulse counting = addition | 01_pulse_addition | PCNT = expected count |
| 2. Parallel computation | 02_parallel_dot | ~4x speedup |
| 3. Ternary eliminates multiply | 02_parallel_dot verify | <1% error vs reference |
| 4. Oscillators maintain phase | 03_spectral_oscillator | Coherence increases with coupling |
| 5. Equilibrium propagation learns | 04_equilibrium_prop | Separation >78% of target |
| 6. Coherence self-modification | 03_spectral_oscillator ablation | Coupling changes with feedback |

---

## Running All Tests

```bash
cd tests
python verify_claims.py --all
```

This flashes each firmware, captures output, and checks success criteria.

---

## If A Claim Fails

Please report it.

1. Open an issue with the exact output
2. Include your hardware version (printed on PCB)
3. Include your ESP-IDF version (`idf.py --version`)

We want to know if our claims don't hold. That's how science works.

---

*"The first principle is that you must not fool yourself—and you are the easiest person to fool."*
— Richard Feynman
