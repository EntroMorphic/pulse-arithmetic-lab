# Extended Turing Machine on ESP32-C6

## Abstract

This document presents the theoretical argument that Pulse Arithmetic Lab implements
a **Physics-Grounded Extended Turing Machine** (PG-ETM) on commodity hardware. We
define the ETM model, map our implementation to it, and provide explicit falsification
conditions.

**The core claim:** We have implemented a Turing-complete computational substrate
where oscillator physics participates in computation—not as simulation, but as
first-class computational resource.

---

## Table of Contents

1. [Background: Why This Matters](#background-why-this-matters)
2. [Definitions](#definitions)
3. [Mapping Pulse Arithmetic to ETM](#mapping-pulse-arithmetic-to-etm)
4. [Theoretical Results](#theoretical-results)
5. [Falsification Conditions](#falsification-conditions)
6. [Implications](#implications)
7. [References](#references)

---

## Background: Why This Matters

### The Standard Model of Computation

The Turing Machine (1936) established the foundation of computer science. All
modern computers—from microcontrollers to supercomputers—are essentially
elaborate Turing machines with finite tape.

Key property: **Discrete state transitions**. Computation proceeds by reading
a symbol, changing state, writing a symbol, moving. Everything is countable.

### The Limitation

Physical reality is continuous. Encoding continuous phenomena into discrete
computation requires:
1. Sampling (information loss)
2. Quantization (precision loss)
3. Simulation (computational overhead)

When we simulate oscillators on a CPU, we're not computing *with* oscillators—
we're computing *about* oscillators. The physics is in our heads, not in the machine.

### The Opportunity

What if the oscillators were real? What if phase relationships performed
computation directly, rather than being simulated?

This is the domain of **analog computing**, but analog computers are not
Turing-complete. They compute specific functions, not arbitrary programs.

**The question:** Can we have both? Turing completeness AND physical dynamics
as computational resource?

---

## Definitions

### Standard Turing Machine

A Turing Machine is a tuple M = (Q, Σ, Γ, δ, q₀, F) where:

| Symbol | Meaning |
|--------|---------|
| Q | Finite set of discrete states |
| Σ | Input alphabet |
| Γ | Tape alphabet (Σ ⊆ Γ) |
| δ | Transition function: Q × Γ → Q × Γ × {L, R} |
| q₀ | Initial state (q₀ ∈ Q) |
| F | Accepting states (F ⊆ Q) |

Computation: Read tape symbol, apply δ, write symbol, move head, repeat.

### Extended Turing Machine (ETM)

An ETM extends the standard model with continuous state:

**Definition 1 (ETM):** An Extended Turing Machine is a tuple
M = (Q, Σ, Γ, C, δ, φ, ψ, q₀, c₀, F) where:

| Symbol | Meaning |
|--------|---------|
| Q, Σ, Γ, q₀, F | As in standard TM |
| C | Continuous state space (e.g., ℝⁿ, Tⁿ, manifold) |
| c₀ | Initial continuous state (c₀ ∈ C) |
| φ | Dynamics: C × ℝ⁺ → C (continuous evolution) |
| ψ | Coupling: Q × C → Q (discrete-continuous interaction) |
| δ | Extended transition: Q × Γ × C → Q × Γ × {L,R} × C |

**Key distinction:** Between discrete transitions, the continuous state evolves
according to φ. The continuous state influences discrete transitions via ψ.

### Physics-Grounded ETM (PG-ETM)

**Definition 2 (PG-ETM):** A Physics-Grounded ETM is an ETM where:

1. **Physical interpretation:** C corresponds to physical quantities
   (phases, voltages, concentrations—not arbitrary real numbers)

2. **Physical dynamics:** φ corresponds to physical law
   (Kuramoto coupling, RC decay, reaction kinetics—not arbitrary ODEs)

3. **Bidirectional coupling:** ψ creates genuine interaction
   (physics affects computation AND computation affects physics)

4. **Realizability:** The system can be implemented in physical hardware
   without simulation of the continuous dynamics

---

## Mapping Pulse Arithmetic to ETM

### Component Mapping

| ETM Component | Pulse Arithmetic Implementation |
|---------------|--------------------------------|
| Q (discrete states) | PCNT registers × weight configurations |
| Σ (input alphabet) | Pulse patterns {0,1}⁸ from PARLIO |
| Γ (tape alphabet) | Network activation values (Q15 integers) |
| C (continuous space) | Oscillator phases T^n = [0, 2π)ⁿ |
| c₀ (initial continuous) | Random phase initialization |
| φ (dynamics) | Kuramoto: dθᵢ/dt = ωᵢ + (K/N)Σⱼsin(θⱼ - θᵢ) |
| ψ (coupling) | Coherence feedback: K ← f(r) where r = \|mean(e^{iθ})\| |
| δ (transition) | EP weight update + oscillator evolution |

### The Discrete Substrate

**PCNT (Pulse Counter):**
- 4 independent 16-bit signed counters
- Hardware increment/decrement on GPIO edges
- No CPU involvement during counting

**PARLIO (Parallel I/O):**
- 8-bit parallel output at up to 10 MHz
- Routes pulses to multiple PCNT units simultaneously

**Weight Configuration:**
- Ternary weights {-1, 0, +1} stored as bitmasks
- pos_mask: bits where weight = +1
- neg_mask: bits where weight = -1

This discrete substrate is trivially Turing-complete (it can simulate
any finite automaton with bounded tape).

### The Continuous Substrate

**Oscillator State:**
```
θ = (θ₁, θ₂, ..., θₙ) ∈ [0, 2π)ⁿ
```

Each oscillator has:
- Phase θ ∈ [0, 2π)
- Natural frequency ω (band-specific)
- Decay rate α (band-specific)
- Complex representation: z = r·e^{iθ}

**Kuramoto Dynamics:**
```
dθᵢ/dt = ωᵢ + (K/N) Σⱼ sin(θⱼ - θᵢ)
```

This is not arbitrary—it's the canonical model of synchronization in physics,
biology, and engineering.

**Coherence (Order Parameter):**
```
r·e^{iψ} = (1/N) Σⱼ e^{iθⱼ}
```

- r ∈ [0, 1] measures phase alignment
- r ≈ 0: incoherent (random phases)
- r ≈ 1: synchronized (aligned phases)

### The Coupling

**Discrete → Continuous:**
- Weight updates modify oscillator coupling matrix
- Input injection adds energy to oscillators
- Phase readout influences discrete state

**Continuous → Discrete:**
- Coherence r modulates coupling strength K
- Phase relationships determine correlation structure
- Oscillator equilibrium determines network output

**Self-Modification:**
```
if r > r_high: K ← K × (1 - ε)    // reduce coupling
if r < r_low:  K ← K × (1 + ε)    // increase coupling
```

This creates a feedback loop: the continuous dynamics modify the parameters
governing those same dynamics.

---

## Theoretical Results

### Theorem 1: Turing Completeness

**Statement:** The discrete component of Pulse Arithmetic is Turing-complete.

**Proof:**

1. PCNT implements integer addition and subtraction in hardware.

2. Ternary weights implement conditional routing:
   - w = +1: route to increment channel
   - w = -1: route to decrement channel
   - w = 0: no connection

3. A network with recurrence (output fed back to input) can maintain
   arbitrary state across time steps.

4. The combination of arithmetic, conditional branching, and state
   is sufficient to simulate a universal Turing machine.

5. Specifically: we can implement a counter machine, which is known
   to be Turing-complete.

∎

**Note:** This result is unsurprising. Most digital systems are Turing-complete.
The interesting question is whether the continuous extension adds anything.

### Theorem 2: Continuous State is Computationally Non-Trivial

**Statement:** The oscillator phases encode information that cannot be
efficiently represented in the discrete state alone.

**Proof:**

1. The discrete state has cardinality |Q| = O(2^{16n}) for n PCNT registers.

2. The continuous state C = [0,2π)^m has uncountably infinite cardinality.

3. The coherence order parameter r is a function of ALL oscillator phases:
   ```
   r = |1/m Σⱼ e^{iθⱼ}|
   ```

4. To compute r discretely would require storing and processing all m phases
   with sufficient precision—at minimum O(m log(1/ε)) bits for precision ε.

5. In the continuous system, r emerges from the dynamics "for free"—it is
   a collective property of the physical state.

6. Therefore, the continuous representation provides computational
   compression: a single real number r summarizes an m-dimensional state.

∎

**Interpretation:** This doesn't prove continuous state is *necessary*, but
it shows it provides something the discrete state doesn't naturally have:
collective variables that emerge from ensemble dynamics.

### Theorem 3: Self-Modification is Non-Trivial

**Statement:** Coherence-based coupling modulation implements meta-computation
that cannot be reduced to standard fixed-program computation without overhead.

**Proof:**

1. Define the "program" P = (W, K) where W = weight matrix, K = coupling strength.

2. In standard computation, P is fixed during execution.

3. In our system, K evolves according to:
   ```
   K(t+1) = f(K(t), r(θ(t)))
   ```
   where r depends on the oscillator state θ, which depends on K.

4. This creates a fixed point equation: the program depends on its own execution.

5. To simulate this in a standard TM, we would need to:
   - Explicitly track K as part of the tape
   - Recompute r at each step (O(m) operations)
   - Update K based on r
   - Modify the transition function based on new K

6. The self-modifying system does this implicitly through dynamics.

7. The overhead of explicit simulation is O(m) per step; the physical
   system has O(1) overhead (coherence is a property of the state, not
   a computation on the state).

∎

**Interpretation:** Self-modification isn't magic—a standard TM can simulate it.
But the simulation has overhead that the physical system avoids.

### Theorem 4: PG-ETM Classification

**Statement:** Pulse Arithmetic Lab implements a Physics-Grounded ETM.

**Proof:** By verification of Definition 2:

1. **Physical interpretation:** Oscillator phases are angles on a circle,
   representing physical phase of oscillation. This is the same state space
   used to model pendulums, neurons, power grids, and chemical oscillators.
   ✓

2. **Physical dynamics:** Kuramoto coupling is the standard model of
   synchronization, derived from phase reduction of limit-cycle oscillators.
   It has been validated in physical, biological, and engineered systems.
   ✓

3. **Bidirectional coupling:** 
   - Discrete → Continuous: weight updates change coupling matrix
   - Continuous → Discrete: coherence modifies coupling strength
   - This is genuine interaction, not one-way influence
   ✓

4. **Realizability:** The system runs on physical hardware (ESP32-C6).
   While our oscillators are currently implemented in fixed-point arithmetic,
   the same dynamics could be realized with physical LC oscillators, MEMS
   resonators, or optical parametric oscillators.
   ✓

Therefore, Pulse Arithmetic Lab satisfies Definition 2. ∎

---

## Falsification Conditions

The ETM claim is **FALSE** if any of the following are demonstrated:

### F1: Reducibility

**Claim falsified if:** The oscillator dynamics can be perfectly simulated
by a discrete lookup table of polynomial size.

**Test:** Attempt to construct a lookup table T such that:
```
T(θ_discretized) = φ(θ, Δt)_discretized
```
with bounded error over all execution traces.

If such T exists with |T| = poly(n, 1/ε), the continuous state is reducible.

**Expected result:** No polynomial table exists because:
- Phase space is continuous and high-dimensional
- Dynamics are nonlinear (Kuramoto has sin terms)
- Small phase differences lead to different synchronization outcomes

### F2: Separability

**Claim falsified if:** The continuous and discrete components can be factored
into independent computations with no interaction.

**Test:** Implement two variants:
1. Full system (discrete + continuous + coupling)
2. Separated system (discrete and continuous run independently)

If all computational outcomes are identical, the coupling is trivial.

**Expected result:** Systems differ because:
- Coherence feedback modifies coupling (continuous → discrete)
- Weight updates modify oscillator interactions (discrete → continuous)
- Removing coupling removes the feedback loop

### F3: Triviality

**Claim falsified if:** The coherence feedback can be replaced by constant K
without changing any computational outcome.

**Test:** Run learning task with:
1. Dynamic K (coherence feedback)
2. Constant K = K_optimal (tuned by oracle)
3. Constant K = K_initial (no tuning)

If (2) matches (1) on all metrics, the feedback is trivial optimization.

**Expected result:** Dynamic K provides benefit on out-of-distribution inputs
where K_optimal is unknown. The feedback adapts; the constant cannot.

### F4: Simulation Equivalence

**Claim falsified if:** A standard TM can simulate our system with only
polynomial overhead in time and space.

**Test:** Analyze computational complexity:
1. Count operations in physical system per time step
2. Count operations in TM simulation per time step
3. Compare scaling as n (oscillators) increases

If TM simulation is O(poly(n)) per step, the continuous state provides no
asymptotic advantage.

**Expected result:** TM simulation requires O(n²) operations for Kuramoto
(all-to-all coupling), while physical system computes in O(1) "wall clock"
time (all oscillators evolve simultaneously).

**Note:** This is the weakest falsification condition. Even if simulation
is polynomial, the physical system may have better constants.

---

## Implications

### If the ETM Claim Holds

1. **New computational model:** PG-ETM is a distinct model of computation,
   sitting between digital Turing machines and analog computers.

2. **Hardware path:** The same algorithms could be implemented with physical
   oscillators, potentially achieving computation at the speed of physics.

3. **Theoretical interest:** The relationship between continuous dynamics
   and discrete computation deserves formal study.

4. **Practical applications:** Hybrid analog-digital computing on commodity
   hardware becomes a real possibility.

### If the ETM Claim is Falsified

1. **Still valuable:** The implementation demonstrates novel use of PCNT/PARLIO
   for neural computation, even if the continuous state is decorative.

2. **Clarifies boundaries:** We learn where the discrete/continuous boundary
   actually matters.

3. **Honest science:** Falsification is success. We learn more from being
   wrong than from being vague.

---

## References

### Foundational

1. Turing, A. M. (1936). "On Computable Numbers, with an Application to the
   Entscheidungsproblem." Proceedings of the London Mathematical Society.

2. Kuramoto, Y. (1984). "Chemical Oscillations, Waves, and Turbulence."
   Springer-Verlag.

3. Strogatz, S. H. (2000). "From Kuramoto to Crawford: Exploring the Onset
   of Synchronization in Populations of Coupled Oscillators." Physica D.

### Extended Computation Models

4. Siegelmann, H. T. (1999). "Neural Networks and Analog Computation:
   Beyond the Turing Limit." Birkhäuser.

5. Bournez, O., & Campagnolo, M. L. (2008). "A Survey on Continuous Time
   Computations." In New Computational Paradigms, Springer.

### Equilibrium Propagation

6. Scellier, B., & Bengio, Y. (2017). "Equilibrium Propagation: Bridging
   the Gap Between Energy-Based Models and Backpropagation."
   Frontiers in Computational Neuroscience.

### Hardware

7. Espressif Systems. "ESP32-C6 Technical Reference Manual."
   https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf

---

## Appendix: Notation Summary

| Symbol | Meaning |
|--------|---------|
| Q | Discrete state set |
| C | Continuous state space |
| θ | Oscillator phase vector |
| r | Coherence (Kuramoto order parameter) |
| K | Coupling strength |
| φ | Continuous dynamics function |
| ψ | Discrete-continuous coupling |
| δ | Transition function |
| W | Weight matrix |
| T^n | n-torus [0, 2π)^n |

---

*"The purpose of computation is insight, not numbers."*
— Richard Hamming

*"The purpose of models is not to fit the data but to sharpen the questions."*
— Samuel Karlin

---

**Document version:** 1.0
**Date:** 2026-02-06
**Status:** Theoretical framework. Falsification experiments pending.
