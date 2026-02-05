#!/usr/bin/env python3
"""
Reference implementations for Pulse Arithmetic Lab.

These NumPy implementations match the firmware behavior exactly.
Use them to verify hardware results or understand the algorithms.

Usage:
    python pulse_arithmetic.py          # Run all demos
    python pulse_arithmetic.py --demo 1 # Run specific demo
"""

import numpy as np
from typing import Tuple, List
import argparse

# =============================================================================
# Q15 Fixed-Point Arithmetic
# =============================================================================

Q15_ONE = 32767
Q15_HALF = 16384


def q15_mul(a: int, b: int) -> int:
    """Multiply two Q15 numbers, return Q15 result."""
    result = (a * b) >> 15
    return np.clip(result, -32768, 32767)


def q15_from_float(f: float) -> int:
    """Convert float [-1, 1) to Q15."""
    return int(np.clip(f * 32768, -32768, 32767))


def q15_to_float(q: int) -> float:
    """Convert Q15 to float."""
    return q / 32768.0


# =============================================================================
# Demo 01: Pulse Addition
# =============================================================================


def demo_pulse_addition():
    """
    Simulate PCNT pulse counting.

    The PCNT peripheral simply counts pulses. This is addition.
    """
    print("\n" + "=" * 70)
    print("  DEMO 01: Pulse Addition (Reference)")
    print("=" * 70)

    # Test cases matching firmware
    tests = [
        (10, "Count 10 pulses"),
        (100, "Count 100 pulses"),
        (1000, "Count 1000 pulses"),
        (10000, "Count 10000 pulses"),
    ]

    print("\n  Basic Pulse Counting:")
    for expected, name in tests:
        # Simulate PCNT: count = sum of pulses
        count = 0
        for _ in range(expected):
            count += 1  # Each pulse increments by 1

        status = "PASS" if count == expected else "FAIL"
        print(f"    {name}: expected={expected}, actual={count} [{status}]")

    # Addition tests
    print("\n  Addition via Sequential Pulses:")
    additions = [(5, 3), (100, 50), (1000, 2000)]

    for a, b in additions:
        count = 0
        for _ in range(a):
            count += 1
        after_a = count
        for _ in range(b):
            count += 1
        after_b = count

        expected = a + b
        status = "PASS" if after_b == expected else "FAIL"
        print(f"    {a} + {b} = {after_b} (expected {expected}) [{status}]")


# =============================================================================
# Demo 02: Parallel Dot Product
# =============================================================================


def ternary_dot_product(input_vec: np.ndarray, pos_mask: int, neg_mask: int) -> int:
    """
    Compute dot product with ternary weights {-1, 0, +1}.

    Args:
        input_vec: Input values (uint8)
        pos_mask: Bitmask for +1 weights
        neg_mask: Bitmask for -1 weights

    Returns:
        Dot product result
    """
    result = 0
    for i in range(len(input_vec)):
        if pos_mask & (1 << i):
            result += input_vec[i]
        if neg_mask & (1 << i):
            result -= input_vec[i]
    return result


def demo_parallel_dot():
    """
    Simulate parallel dot products with ternary weights.
    """
    print("\n" + "=" * 70)
    print("  DEMO 02: Parallel Dot Product (Reference)")
    print("=" * 70)

    # Weight patterns matching firmware
    # Neuron 0: [+1, +1, +1, +1]
    # Neuron 1: [-1, -1, -1, -1]
    # Neuron 2: [+1, -1, +1, -1]
    # Neuron 3: [+1, +1, -1, -1]

    neurons = [
        (0b1111, 0b0000, "[+1,+1,+1,+1]"),  # pos_mask, neg_mask, description
        (0b0000, 0b1111, "[-1,-1,-1,-1]"),
        (0b0101, 0b1010, "[+1,-1,+1,-1]"),
        (0b0011, 0b1100, "[+1,+1,-1,-1]"),
    ]

    test_inputs = [
        np.array([1, 1, 1, 1], dtype=np.uint8),
        np.array([10, 10, 10, 10], dtype=np.uint8),
        np.array([15, 0, 15, 0], dtype=np.uint8),
        np.array([1, 2, 3, 4], dtype=np.uint8),
        np.array([15, 15, 15, 15], dtype=np.uint8),
    ]

    for test_idx, input_vec in enumerate(test_inputs):
        print(f"\n  Test {test_idx + 1}: Input = {list(input_vec)}")
        print("    Neuron | Weight Pattern  | Result")
        print("    -------+-----------------+-------")

        for n_idx, (pos_mask, neg_mask, desc) in enumerate(neurons):
            result = ternary_dot_product(input_vec, pos_mask, neg_mask)
            print(f"       {n_idx}   | {desc:15s} | {result:5d}")


# =============================================================================
# Demo 03: Spectral Oscillator
# =============================================================================


class SpectralOscillator:
    """Complex-valued oscillator with band-specific dynamics."""

    # Band parameters
    BAND_NAMES = ["Delta", "Theta", "Alpha", "Gamma"]
    BAND_DECAY = [0.98, 0.90, 0.70, 0.30]
    BAND_FREQ = [0.1, 0.3, 1.0, 3.0]

    def __init__(self, num_bands=4, neurons_per_band=4):
        self.num_bands = num_bands
        self.neurons_per_band = neurons_per_band

        # Complex state: shape (bands, neurons, 2) for real/imag
        self.state = np.zeros((num_bands, neurons_per_band, 2), dtype=np.float64)
        self.phase_velocity = np.zeros((num_bands, neurons_per_band), dtype=np.float64)

        # Initialize with random phases
        np.random.seed(12345)  # Match firmware PRNG seed
        for b in range(num_bands):
            for n in range(neurons_per_band):
                phase = np.random.uniform(0, 2 * np.pi)
                self.state[b, n, 0] = np.cos(phase) * 0.9  # Start with magnitude ~0.9
                self.state[b, n, 1] = np.sin(phase) * 0.9
                self.phase_velocity[b, n] = self.BAND_FREQ[b]

    def evolve(self, input_energy: np.ndarray = None):
        """Single evolution step."""
        # 1. Inject input energy (if provided)
        if input_energy is not None:
            for b in range(self.num_bands):
                for n in range(self.neurons_per_band):
                    mag = np.sqrt(self.state[b, n, 0] ** 2 + self.state[b, n, 1] ** 2)
                    if mag < 0.5:
                        self.state[b, n, 0] += (
                            input_energy[n % len(input_energy)] * 0.01
                        )
                        self.state[b, n, 1] += (
                            input_energy[n % len(input_energy)] * 0.005
                        )

        # 2. Rotate oscillators
        for b in range(self.num_bands):
            angle = self.phase_velocity[b, 0] * 0.1  # Scale for stability
            cos_a = np.cos(angle)
            sin_a = np.sin(angle)

            for n in range(self.neurons_per_band):
                real = self.state[b, n, 0]
                imag = self.state[b, n, 1]

                # z_new = z * e^(i*angle)
                new_real = real * cos_a - imag * sin_a
                new_imag = real * sin_a + imag * cos_a

                # Apply decay
                decay = self.BAND_DECAY[b]
                self.state[b, n, 0] = new_real * decay
                self.state[b, n, 1] = new_imag * decay

    def get_coherence(self, band: int = None) -> float:
        """
        Compute Kuramoto order parameter.

        coherence = |mean(e^(i*theta))| = |mean(z/|z|)|
        """
        sum_real = 0.0
        sum_imag = 0.0
        count = 0

        bands = [band] if band is not None else range(self.num_bands)

        for b in bands:
            for n in range(self.neurons_per_band):
                mag = np.sqrt(self.state[b, n, 0] ** 2 + self.state[b, n, 1] ** 2)
                if mag > 0.01:  # Only count oscillators with meaningful magnitude
                    sum_real += self.state[b, n, 0] / mag
                    sum_imag += self.state[b, n, 1] / mag
                    count += 1

        if count == 0:
            return 0.0

        avg_real = sum_real / count
        avg_imag = sum_imag / count
        return np.sqrt(avg_real**2 + avg_imag**2)

    def get_band_magnitude(self, band: int) -> float:
        """Average magnitude of oscillators in a band."""
        total = 0.0
        for n in range(self.neurons_per_band):
            total += np.sqrt(self.state[band, n, 0] ** 2 + self.state[band, n, 1] ** 2)
        return total / self.neurons_per_band


def demo_spectral_oscillator():
    """Simulate spectral oscillator dynamics."""
    print("\n" + "=" * 70)
    print("  DEMO 03: Spectral Oscillator (Reference)")
    print("=" * 70)

    osc = SpectralOscillator()

    # Inject some energy
    input_energy = np.array([4.0, 4.0, 4.0, 4.0])
    for _ in range(10):
        osc.evolve(input_energy)

    print("\n  After 10 steps with input:")
    print("    Band   | Magnitude | Coherence")
    print("    -------+-----------+----------")
    for b, name in enumerate(SpectralOscillator.BAND_NAMES):
        mag = osc.get_band_magnitude(b)
        coh = osc.get_coherence(b)
        print(f"    {name:6s} | {mag:9.4f} | {coh:9.4f}")

    # Evolve without input
    for _ in range(50):
        osc.evolve(None)

    print("\n  After 50 more steps (no input):")
    print("    Band   | Magnitude | Coherence")
    print("    -------+-----------+----------")
    for b, name in enumerate(SpectralOscillator.BAND_NAMES):
        mag = osc.get_band_magnitude(b)
        coh = osc.get_coherence(b)
        print(f"    {name:6s} | {mag:9.4f} | {coh:9.4f}")

    print("\n  Expected: Delta decays slowest, Gamma fastest")


# =============================================================================
# Demo 04: Equilibrium Propagation
# =============================================================================


class EquilibriumPropNetwork:
    """Network that learns via equilibrium propagation."""

    def __init__(self, input_dim=4, hidden_dim=16, output_dim=4):
        self.input_dim = input_dim
        self.hidden_dim = hidden_dim
        self.output_dim = output_dim

        # Initialize weights randomly
        np.random.seed(12345)
        self.W_in = np.random.randn(hidden_dim, input_dim) * 0.1
        self.W_out = np.random.randn(output_dim, hidden_dim) * 0.1

        # State
        self.hidden = np.zeros(hidden_dim)
        self.output = np.zeros(output_dim)

    def forward(self, x: np.ndarray, steps: int = 30):
        """Run forward dynamics to equilibrium."""
        self.hidden = np.tanh(self.W_in @ x)

        for _ in range(steps):
            # Simple relaxation dynamics
            self.output = np.tanh(self.W_out @ self.hidden)
            # Hidden state is static in this simple version

    def nudged_forward(
        self, x: np.ndarray, target: np.ndarray, steps: int = 30, beta: float = 0.5
    ):
        """Run forward with output nudged toward target."""
        self.hidden = np.tanh(self.W_in @ x)

        for _ in range(steps):
            raw_output = self.W_out @ self.hidden
            nudged = raw_output + beta * (target - np.tanh(raw_output))
            self.output = np.tanh(nudged)

    def compute_correlations(self, x: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """Compute input-hidden and hidden-output correlations."""
        corr_in = np.outer(self.hidden, x)
        corr_out = np.outer(self.output, self.hidden)
        return corr_in, corr_out

    def train_step(
        self, x: np.ndarray, target: np.ndarray, lr: float = 0.01, beta: float = 0.5
    ):
        """One equilibrium propagation training step."""
        # Free phase
        self.forward(x)
        free_output = self.output.copy()
        corr_in_free, corr_out_free = self.compute_correlations(x)

        # Nudged phase
        self.nudged_forward(x, target, beta=beta)
        corr_in_nudged, corr_out_nudged = self.compute_correlations(x)

        # Weight updates
        self.W_in += lr * (corr_in_nudged - corr_in_free)
        self.W_out += lr * (corr_out_nudged - corr_out_free)

        # Return loss (MSE of free phase output)
        loss = np.mean((free_output - target) ** 2)
        return loss, free_output


def demo_equilibrium_prop():
    """Simulate equilibrium propagation learning."""
    print("\n" + "=" * 70)
    print("  DEMO 04: Equilibrium Propagation (Reference)")
    print("=" * 70)

    net = EquilibriumPropNetwork(input_dim=4, hidden_dim=16, output_dim=4)

    # Training patterns (matching firmware)
    patterns = [
        (np.array([0, 0, 15, 15]) / 15.0, np.array([1, 0, 0, 0])),  # Pattern 0
        (np.array([15, 15, 0, 0]) / 15.0, np.array([0, 0, 0, 1])),  # Pattern 1
    ]

    print("\n  Training 2 patterns for 150 epochs...")
    print("\n  Epoch | Loss    | Out[0]  | Out[1]")
    print("  ------+---------+---------+--------")

    for epoch in range(150):
        total_loss = 0
        outputs = []

        for x, target in patterns:
            loss, output = net.train_step(x, target, lr=0.01)
            total_loss += loss
            outputs.append(output[0])  # First output dimension

        if epoch % 25 == 0 or epoch == 149:
            print(
                f"  {epoch:5d} | {total_loss / 2:.5f} | {outputs[0]:7.4f} | {outputs[1]:7.4f}"
            )

    print("\n  Target: Pattern 0 → [1,0,0,0], Pattern 1 → [0,0,0,1]")
    print("  Success = outputs diverge (one high, one low)")


# =============================================================================
# Main
# =============================================================================


def main():
    parser = argparse.ArgumentParser(
        description="Pulse Arithmetic Reference Implementations"
    )
    parser.add_argument(
        "--demo", type=int, choices=[1, 2, 3, 4], help="Run specific demo (1-4)"
    )
    args = parser.parse_args()

    demos = {
        1: demo_pulse_addition,
        2: demo_parallel_dot,
        3: demo_spectral_oscillator,
        4: demo_equilibrium_prop,
    }

    if args.demo:
        demos[args.demo]()
    else:
        for demo_func in demos.values():
            demo_func()

    print("\n" + "=" * 70)
    print("  Reference implementations complete.")
    print("  Compare these results with firmware output to verify correctness.")
    print("=" * 70 + "\n")


if __name__ == "__main__":
    main()
