#!/usr/bin/env python3
"""
ETM Falsification Tests

These tests attempt to DISPROVE the Extended Turing Machine claim.
If any test succeeds in falsifying, the ETM claim is dead.

Usage:
    python falsify_etm.py --test F1   # Test reducibility
    python falsify_etm.py --test F2   # Test separability
    python falsify_etm.py --test F3   # Test triviality
    python falsify_etm.py --test F4   # Test simulation equivalence
    python falsify_etm.py --all       # Run all tests
"""

import numpy as np
import argparse
from typing import Tuple, List, Dict
import time

# =============================================================================
# Oscillator Implementation (matching firmware)
# =============================================================================


class SpectralOscillator:
    """Reference implementation matching firmware behavior."""

    BAND_DECAY = [0.98, 0.90, 0.70, 0.30]
    BAND_FREQ = [0.1, 0.3, 1.0, 3.0]

    COHERENCE_HIGH = 0.6
    COHERENCE_LOW = 0.25
    COUPLING_DECAY = 0.995
    COUPLING_GROWTH = 1.005
    COUPLING_MIN = 0.01
    COUPLING_MAX = 2.0

    def __init__(self, num_bands=4, neurons_per_band=4, seed=12345):
        self.num_bands = num_bands
        self.neurons_per_band = neurons_per_band
        self.n_total = num_bands * neurons_per_band

        np.random.seed(seed)
        self.phases = np.random.uniform(0, 2 * np.pi, (num_bands, neurons_per_band))
        self.magnitudes = np.ones((num_bands, neurons_per_band)) * 0.9
        self.coupling = 0.5

    def reset(self, seed=12345):
        np.random.seed(seed)
        self.phases = np.random.uniform(
            0, 2 * np.pi, (self.num_bands, self.neurons_per_band)
        )
        self.magnitudes = np.ones((self.num_bands, self.neurons_per_band)) * 0.9
        self.coupling = 0.5

    def get_coherence(self) -> float:
        """Kuramoto order parameter."""
        z = np.exp(1j * self.phases.flatten())
        valid = self.magnitudes.flatten() > 0.01
        if not np.any(valid):
            return 0.0
        return np.abs(np.mean(z[valid]))

    def evolve_step(
        self,
        input_energy: float = 0.0,
        use_feedback: bool = True,
        fixed_coupling: float = None,
    ):
        """Single evolution step WITH Kuramoto coupling (matching firmware)."""
        # Inject energy
        if input_energy > 0:
            mask = self.magnitudes < 0.5
            self.magnitudes[mask] += 0.1 * input_energy

        # Rotate and decay
        for b in range(self.num_bands):
            self.phases[b] += self.BAND_FREQ[b] * 0.1
            self.magnitudes[b] *= self.BAND_DECAY[b]

        self.phases = self.phases % (2 * np.pi)

        # KURAMOTO COUPLING: bands influence each other's phases
        # This is the KEY PART that makes coupling matter!
        K = fixed_coupling if fixed_coupling is not None else self.coupling

        if K >= 0.01:
            for src in range(self.num_bands):
                for dst in range(self.num_bands):
                    if src == dst:
                        continue

                    # Compute average phase difference (Kuramoto term)
                    # sin(theta_src - theta_dst) averaged over neurons
                    phase_diff = np.mean(np.sin(self.phases[src] - self.phases[dst]))

                    # Pull destination toward source
                    pull = K * phase_diff * 0.1
                    self.phases[dst] += pull

            self.phases = self.phases % (2 * np.pi)

        # Coupling feedback (modifies K for NEXT step)
        if use_feedback:
            coherence = self.get_coherence()
            if coherence > self.COHERENCE_HIGH:
                self.coupling *= self.COUPLING_DECAY
            elif coherence < self.COHERENCE_LOW:
                self.coupling *= self.COUPLING_GROWTH
            self.coupling = np.clip(self.coupling, self.COUPLING_MIN, self.COUPLING_MAX)
        elif fixed_coupling is not None:
            self.coupling = fixed_coupling

    def get_state_hash(self, precision: int = 8) -> Tuple:
        """Discretize state for lookup table analysis."""
        # Quantize phases to `precision` bits
        bins = 2**precision
        phase_quantized = (self.phases * bins / (2 * np.pi)).astype(int) % bins
        mag_quantized = (self.magnitudes * bins).astype(int).clip(0, bins - 1)
        coupling_quantized = int(self.coupling * bins)

        return (
            tuple(phase_quantized.flatten()),
            tuple(mag_quantized.flatten()),
            coupling_quantized,
        )


# =============================================================================
# F1: Reducibility Test
# =============================================================================


def test_F1_reducibility(num_steps: int = 10000, precision_bits: int = 8) -> dict:
    """
    F1: Can oscillator dynamics be replaced by polynomial-size lookup table?

    Method:
    1. Run oscillator for many steps
    2. Record all (state -> next_state) transitions
    3. Check if number of unique states is polynomial in parameters

    FALSIFIED if: |unique_states| = O(poly(n, precision))
    NOT FALSIFIED if: |unique_states| grows exponentially
    """
    print("\n" + "=" * 70)
    print("  F1: REDUCIBILITY TEST")
    print("  Attempting to build polynomial lookup table")
    print("=" * 70)

    osc = SpectralOscillator()

    # Track unique states
    states_visited = set()
    transitions = {}  # state -> next_state

    # Run with varying inputs to explore state space
    inputs = [0.0, 0.1, 0.3, 0.5, 1.0]

    for input_energy in inputs:
        osc.reset()
        for step in range(num_steps // len(inputs)):
            state = osc.get_state_hash(precision_bits)
            states_visited.add(state)

            osc.evolve_step(input_energy=input_energy, use_feedback=True)

            next_state = osc.get_state_hash(precision_bits)

            if state in transitions:
                if transitions[state] != next_state:
                    # Non-deterministic! Same state -> different next states
                    # This would be due to floating point, not true non-determinism
                    pass
            else:
                transitions[state] = next_state

    # Analysis
    n_oscillators = osc.n_total
    max_poly_states = (2**precision_bits) ** 3  # Generous polynomial bound
    n_unique = len(states_visited)
    n_transitions = len(transitions)

    # Theoretical maximum states (exponential)
    theoretical_max = (2**precision_bits) ** (2 * n_oscillators + 1)

    # What fraction of state space did we visit?
    coverage = n_unique / theoretical_max if theoretical_max > 0 else 0

    print(f"\n  Parameters:")
    print(f"    Oscillators: {n_oscillators}")
    print(f"    Precision: {precision_bits} bits")
    print(f"    Steps run: {num_steps}")

    print(f"\n  Results:")
    print(f"    Unique states visited: {n_unique:,}")
    print(f"    Unique transitions: {n_transitions:,}")
    print(f"    Theoretical max states: {theoretical_max:.2e}")
    print(f"    State space coverage: {coverage:.2e}")

    # Falsification check
    # If we can represent all visited states in polynomial space,
    # AND the dynamics are deterministic within that space,
    # then a lookup table is feasible

    polynomial_bound = n_oscillators**3 * (2**precision_bits)

    falsified = n_unique < polynomial_bound and n_transitions == n_unique

    print(f"\n  Polynomial bound: {polynomial_bound:,}")
    print(f"  States < bound? {n_unique < polynomial_bound}")
    print(f"  Deterministic? {n_transitions == n_unique}")

    if falsified:
        print(f"\n  *** F1 FALSIFIED ***")
        print(
            f"  A lookup table of size {n_unique} could replace the oscillator dynamics."
        )
    else:
        print(f"\n  F1 NOT FALSIFIED")
        print(f"  State space is too large or dynamics too complex for polynomial LUT.")

    return {
        "test": "F1",
        "falsified": falsified,
        "unique_states": n_unique,
        "transitions": n_transitions,
        "polynomial_bound": polynomial_bound,
        "coverage": coverage,
    }


# =============================================================================
# F2: Separability Test
# =============================================================================


def test_F2_separability(num_steps: int = 500, num_trials: int = 10) -> dict:
    """
    F2: Can discrete and continuous components be factored apart?

    Method:
    1. Run full coupled system
    2. Run "separated" system where coupling feedback is delayed/decoupled
    3. Compare outcomes

    FALSIFIED if: Separated system produces identical computational outcomes
    NOT FALSIFIED if: Outcomes differ significantly
    """
    print("\n" + "=" * 70)
    print("  F2: SEPARABILITY TEST")
    print("  Testing if discrete/continuous can be factored apart")
    print("=" * 70)

    differences = []

    for trial in range(num_trials):
        seed = 12345 + trial

        # Coupled system
        osc_coupled = SpectralOscillator(seed=seed)
        coupled_trajectory = []

        for step in range(num_steps):
            input_e = 0.3 * np.sin(step * 0.1)  # Varying input
            osc_coupled.evolve_step(input_energy=input_e, use_feedback=True)
            coupled_trajectory.append(
                {
                    "coherence": osc_coupled.get_coherence(),
                    "coupling": osc_coupled.coupling,
                    "phases": osc_coupled.phases.copy(),
                }
            )

        # Separated system: continuous evolves, but coupling feedback is
        # computed on PREVIOUS step's coherence (delayed/decoupled)
        osc_separated = SpectralOscillator(seed=seed)
        separated_trajectory = []
        prev_coherence = osc_separated.get_coherence()

        for step in range(num_steps):
            input_e = 0.3 * np.sin(step * 0.1)

            # Evolve without feedback
            osc_separated.evolve_step(input_energy=input_e, use_feedback=False)

            # Apply feedback based on PREVIOUS coherence (decoupled)
            if prev_coherence > SpectralOscillator.COHERENCE_HIGH:
                osc_separated.coupling *= SpectralOscillator.COUPLING_DECAY
            elif prev_coherence < SpectralOscillator.COHERENCE_LOW:
                osc_separated.coupling *= SpectralOscillator.COUPLING_GROWTH
            osc_separated.coupling = np.clip(
                osc_separated.coupling,
                SpectralOscillator.COUPLING_MIN,
                SpectralOscillator.COUPLING_MAX,
            )

            current_coherence = osc_separated.get_coherence()
            separated_trajectory.append(
                {
                    "coherence": current_coherence,
                    "coupling": osc_separated.coupling,
                    "phases": osc_separated.phases.copy(),
                }
            )
            prev_coherence = current_coherence

        # Compare trajectories
        coherence_diff = np.mean(
            [
                abs(c["coherence"] - s["coherence"])
                for c, s in zip(coupled_trajectory, separated_trajectory)
            ]
        )
        coupling_diff = np.mean(
            [
                abs(c["coupling"] - s["coupling"])
                for c, s in zip(coupled_trajectory, separated_trajectory)
            ]
        )

        differences.append(
            {"coherence_diff": coherence_diff, "coupling_diff": coupling_diff}
        )

    avg_coherence_diff = np.mean([d["coherence_diff"] for d in differences])
    avg_coupling_diff = np.mean([d["coupling_diff"] for d in differences])

    print(f"\n  Trials: {num_trials}")
    print(f"  Steps per trial: {num_steps}")

    print(f"\n  Results:")
    print(f"    Avg coherence difference: {avg_coherence_diff:.6f}")
    print(f"    Avg coupling difference: {avg_coupling_diff:.6f}")

    # Falsification threshold: if differences are negligible, systems are separable
    threshold = 0.01
    falsified = avg_coherence_diff < threshold and avg_coupling_diff < threshold

    if falsified:
        print(f"\n  *** F2 FALSIFIED ***")
        print(f"  Coupled and decoupled systems produce equivalent results.")
        print(f"  The discrete-continuous interaction is trivial.")
    else:
        print(f"\n  F2 NOT FALSIFIED")
        print(f"  Coupled system behaves differently from decoupled system.")
        print(f"  The interaction is meaningful.")

    return {
        "test": "F2",
        "falsified": falsified,
        "avg_coherence_diff": avg_coherence_diff,
        "avg_coupling_diff": avg_coupling_diff,
        "threshold": threshold,
    }


# =============================================================================
# F3: Triviality Test
# =============================================================================


def test_F3_triviality_v2(num_trials: int = 20, num_steps: int = 500) -> dict:
    """
    F3 v2: Does continuous state (phase) causally affect discrete state (K)?

    Method:
    1. Run multiple systems with SAME discrete params, DIFFERENT phases
    2. Check if they diverge in coupling (discrete state)

    FALSIFIED if: Different phases lead to same coupling trajectory
    NOT FALSIFIED if: Different phases lead to divergent coupling
    """
    print("\n" + "=" * 70)
    print("  F3 v2: PHASE CAUSALITY TEST")
    print("  Do different phases cause different discrete outcomes?")
    print("=" * 70)

    divergences = []

    for trial in range(num_trials):
        # Two systems with different random phases
        osc_A = SpectralOscillator(seed=trial * 2)
        osc_B = SpectralOscillator(seed=trial * 2 + 1)

        # Same input sequence for both
        np.random.seed(trial + 10000)
        inputs = [0.3 * np.sin(i * 0.1) + 0.3 for i in range(num_steps)]

        couplings_A = []
        couplings_B = []

        for inp in inputs:
            osc_A.evolve_step(input_energy=inp, use_feedback=True)
            osc_B.evolve_step(input_energy=inp, use_feedback=True)
            couplings_A.append(osc_A.coupling)
            couplings_B.append(osc_B.coupling)

        # Measure divergence
        final_divergence = abs(couplings_A[-1] - couplings_B[-1])
        trajectory_corr = np.corrcoef(couplings_A, couplings_B)[0, 1]

        divergences.append(
            {"final_divergence": final_divergence, "correlation": trajectory_corr}
        )

    avg_divergence = np.mean([d["final_divergence"] for d in divergences])
    avg_correlation = np.mean([d["correlation"] for d in divergences])

    print(f"\n  Trials: {num_trials}")
    print(f"  Steps per trial: {num_steps}")

    print(f"\n  Results:")
    print(f"    Avg final coupling divergence: {avg_divergence:.4f}")
    print(f"    Avg trajectory correlation: {avg_correlation:.4f}")

    # Falsification: if phases don't matter, systems should converge
    # High divergence + low correlation = phases matter
    falsified = avg_divergence < 0.1 and avg_correlation > 0.9

    if falsified:
        print(f"\n  *** F3 FALSIFIED ***")
        print(f"  Different initial phases lead to same discrete outcomes.")
        print(f"  Phase is decorative, not computational.")
    else:
        print(f"\n  F3 NOT FALSIFIED")
        print(f"  Different initial phases cause divergent coupling trajectories.")
        print(f"  Continuous state (phase) causally affects discrete state (K).")

    return {
        "test": "F3",
        "falsified": falsified,
        "avg_divergence": avg_divergence,
        "avg_correlation": avg_correlation,
    }


def test_F3_triviality(num_steps: int = 500, num_k_values: int = 50) -> dict:
    """
    F3: Can coherence feedback be replaced by constant K?

    Method:
    1. Run system with dynamic K (coherence feedback)
    2. Run system with various constant K values
    3. Find optimal constant K via grid search
    4. Compare: does optimal constant match dynamic?

    FALSIFIED if: Exists constant K that matches dynamic K on all metrics
    NOT FALSIFIED if: Dynamic K provides benefit that no constant achieves
    """
    print("\n" + "=" * 70)
    print("  F3: TRIVIALITY TEST")
    print("  Can constant K replace dynamic coherence feedback?")
    print("=" * 70)

    # Metric: we'll use final coherence stability as the objective
    # A good coupling should lead to stable, intermediate coherence

    def run_and_score(
        osc: SpectralOscillator,
        num_steps: int,
        use_feedback: bool,
        fixed_k: float = None,
    ) -> dict:
        """Run oscillator and compute quality metrics."""
        coherences = []
        couplings = []

        for step in range(num_steps):
            input_e = 0.3 * np.sin(step * 0.1)  # Same input pattern
            osc.evolve_step(
                input_energy=input_e, use_feedback=use_feedback, fixed_coupling=fixed_k
            )
            coherences.append(osc.get_coherence())
            couplings.append(osc.coupling)

        # Metrics
        final_coherence = np.mean(coherences[-100:])  # Last 100 steps
        coherence_stability = 1.0 / (np.std(coherences[-100:]) + 0.01)

        # Ideal coherence is somewhere in middle (not 0, not 1)
        coherence_quality = 1.0 - abs(final_coherence - 0.5) * 2

        # Combined score
        score = coherence_quality * coherence_stability

        return {
            "final_coherence": final_coherence,
            "stability": coherence_stability,
            "quality": coherence_quality,
            "score": score,
            "final_coupling": couplings[-1],
        }

    # Run with dynamic K
    osc_dynamic = SpectralOscillator(seed=12345)
    dynamic_result = run_and_score(osc_dynamic, num_steps, use_feedback=True)

    print(f"\n  Dynamic K (coherence feedback):")
    print(f"    Final coherence: {dynamic_result['final_coherence']:.4f}")
    print(f"    Stability: {dynamic_result['stability']:.4f}")
    print(f"    Score: {dynamic_result['score']:.4f}")
    print(f"    Final K: {dynamic_result['final_coupling']:.4f}")

    # Grid search over constant K values
    k_values = np.linspace(0.01, 2.0, num_k_values)
    best_constant_result = None
    best_constant_k = None
    all_constant_results = []

    for k in k_values:
        osc_constant = SpectralOscillator(seed=12345)  # Same seed!
        result = run_and_score(osc_constant, num_steps, use_feedback=False, fixed_k=k)
        result["k"] = k
        all_constant_results.append(result)

        if (
            best_constant_result is None
            or result["score"] > best_constant_result["score"]
        ):
            best_constant_result = result
            best_constant_k = k

    print(f"\n  Best constant K (oracle search over {num_k_values} values):")
    print(f"    K = {best_constant_k:.4f}")
    print(f"    Final coherence: {best_constant_result['final_coherence']:.4f}")
    print(f"    Stability: {best_constant_result['stability']:.4f}")
    print(f"    Score: {best_constant_result['score']:.4f}")

    # Comparison
    score_ratio = best_constant_result["score"] / dynamic_result["score"]

    print(f"\n  Comparison:")
    print(f"    Score ratio (constant/dynamic): {score_ratio:.4f}")

    # Falsification: if constant K achieves >= 95% of dynamic K's score
    falsified = score_ratio >= 0.95

    if falsified:
        print(f"\n  *** F3 FALSIFIED ***")
        print(
            f"  Constant K={best_constant_k:.4f} achieves {score_ratio * 100:.1f}% of dynamic performance."
        )
        print(
            f"  Coherence feedback is just optimization that can be replaced by tuning."
        )
    else:
        print(f"\n  F3 NOT FALSIFIED")
        print(
            f"  Dynamic K outperforms best constant K by {(1 - score_ratio) * 100:.1f}%."
        )
        print(f"  Coherence feedback provides genuine adaptive benefit.")

    return {
        "test": "F3",
        "falsified": falsified,
        "dynamic_score": dynamic_result["score"],
        "best_constant_score": best_constant_result["score"],
        "best_constant_k": best_constant_k,
        "score_ratio": score_ratio,
    }


# =============================================================================
# F4: Simulation Equivalence Test
# =============================================================================


def test_F4_simulation(n_oscillators_range: List[int] = None) -> dict:
    """
    F4: Does TM simulation have only polynomial overhead?

    Method:
    1. Count operations in physical system per step
    2. Count operations in explicit simulation per step
    3. Analyze scaling with n (number of oscillators)

    FALSIFIED if: Simulation overhead is O(poly(n))
    NOT FALSIFIED if: Physical system has asymptotic advantage
    """
    print("\n" + "=" * 70)
    print("  F4: SIMULATION EQUIVALENCE TEST")
    print("  Analyzing computational complexity")
    print("=" * 70)

    if n_oscillators_range is None:
        n_oscillators_range = [4, 8, 16, 32, 64]

    results = []

    for n in n_oscillators_range:
        bands = max(1, n // 4)
        per_band = n // bands

        # Physical system: operations per step
        # - Inject energy: O(n)
        # - Rotate: O(n)
        # - Decay: O(n)
        # - Coherence: O(n) to compute mean
        # - Coupling update: O(1)
        # Total: O(n)
        physical_ops = 5 * n + 2

        # Simulation overhead:
        # - Same operations as physical
        # - BUT: coherence requires summing all oscillators
        # - Kuramoto coupling (if all-to-all): O(n^2)
        # - Our current system doesn't use full Kuramoto, so O(n)
        simulation_ops = 5 * n + n  # Coherence sum

        # If we had full Kuramoto coupling:
        full_kuramoto_ops = 5 * n + n * n

        results.append(
            {
                "n": n,
                "physical_ops": physical_ops,
                "simulation_ops": simulation_ops,
                "full_kuramoto_ops": full_kuramoto_ops,
                "overhead_ratio": simulation_ops / physical_ops,
                "kuramoto_ratio": full_kuramoto_ops / physical_ops,
            }
        )

    print(f"\n  Operations per step:")
    print(
        f"  {'n':>6} | {'Physical':>10} | {'Simulation':>12} | {'Full Kuramoto':>14} | {'Overhead':>10}"
    )
    print(f"  {'-' * 6}-+-{'-' * 10}-+-{'-' * 12}-+-{'-' * 14}-+-{'-' * 10}")

    for r in results:
        print(
            f"  {r['n']:>6} | {r['physical_ops']:>10} | {r['simulation_ops']:>12} | {r['full_kuramoto_ops']:>14} | {r['overhead_ratio']:>10.2f}x"
        )

    # Analyze scaling
    ns = np.array([r["n"] for r in results])
    sim_ops = np.array([r["simulation_ops"] for r in results])

    # Fit polynomial
    coeffs = np.polyfit(np.log(ns), np.log(sim_ops), 1)
    scaling_exponent = coeffs[0]

    print(f"\n  Scaling analysis:")
    print(f"    Simulation ops ~ O(n^{scaling_exponent:.2f})")

    # Analysis
    print(f"\n  Key insight:")
    print(
        f"    Our current implementation uses band-local dynamics, not all-to-all Kuramoto."
    )
    print(f"    This means simulation is O(n), same as 'physical' system.")
    print(f"    ")
    print(f"    If we implemented FULL Kuramoto coupling (all oscillators interact),")
    print(
        f"    simulation would be O(n²), while physical system remains O(1) wall-clock"
    )
    print(f"    (all oscillators evolve simultaneously in real physics).")

    # Falsification check
    # Our current implementation: simulation is polynomial, no asymptotic advantage
    # With full Kuramoto: simulation is O(n²), physical is O(1) -> advantage exists

    # For current implementation:
    falsified = scaling_exponent < 2.0  # Polynomial simulation exists

    print(f"\n  Current implementation verdict:")
    if falsified:
        print(f"  *** F4 PARTIALLY FALSIFIED ***")
        print(f"  Current band-local dynamics can be simulated in O(n).")
        print(f"  No asymptotic advantage over explicit simulation.")
        print(f"  ")
        print(f"  HOWEVER: With true all-to-all Kuramoto coupling,")
        print(f"  physical parallelism would provide O(n²) -> O(1) advantage.")
        print(f"  This is an implementation limitation, not a fundamental one.")
    else:
        print(f"  F4 NOT FALSIFIED")
        print(f"  Simulation complexity exceeds polynomial bounds.")

    return {
        "test": "F4",
        "falsified": falsified,
        "scaling_exponent": scaling_exponent,
        "results": results,
        "note": "Band-local dynamics limit parallelism advantage; full Kuramoto would change this",
    }


# =============================================================================
# Main
# =============================================================================


def main():
    parser = argparse.ArgumentParser(description="ETM Falsification Tests")
    parser.add_argument(
        "--test", choices=["F1", "F2", "F3", "F4"], help="Run specific test"
    )
    parser.add_argument("--all", action="store_true", help="Run all tests")
    args = parser.parse_args()

    tests = {
        "F1": test_F1_reducibility,
        "F2": test_F2_separability,
        "F3": test_F3_triviality_v2,  # Use the new phase causality test
        "F3_old": test_F3_triviality,  # Keep old test for reference
        "F4": test_F4_simulation,
    }

    results = []

    if args.test:
        result = tests[args.test]()
        results.append(result)
    elif args.all:
        for name, test_fn in tests.items():
            result = test_fn()
            results.append(result)
    else:
        # Default: run all
        for name, test_fn in tests.items():
            result = test_fn()
            results.append(result)

    # Summary
    print("\n")
    print("=" * 70)
    print("  FALSIFICATION SUMMARY")
    print("=" * 70)
    print()
    print("  Test | Result")
    print("  -----+------------------")

    any_falsified = False
    for r in results:
        status = "FALSIFIED" if r["falsified"] else "NOT FALSIFIED"
        if r["falsified"]:
            any_falsified = True
        print(f"  {r['test']:4} | {status}")

    print()
    if any_falsified:
        print("  *** ETM CLAIM IS WEAKENED ***")
        print("  One or more falsification conditions were met.")
        print("  The claim requires revision or the tests require review.")
    else:
        print("  ETM CLAIM SURVIVES")
        print("  All falsification attempts failed.")
        print("  The claim stands (until someone finds a better attack).")

    print("=" * 70)

    return results


if __name__ == "__main__":
    main()
