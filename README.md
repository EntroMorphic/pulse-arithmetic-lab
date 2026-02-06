# Pulse Arithmetic Lab

**Hardware neural computation on a $5 microcontroller.**

Pulse counters do addition. Parallel I/O enables parallelism. Oscillators encode state in phase. Learning emerges from running the same physics twice.

```
ESP32-C6 @ 160 MHz
├── PCNT (Pulse Counter) ──► Hardware addition
├── PARLIO (Parallel I/O) ──► 4 neurons compute simultaneously  
├── Spectral Oscillators ──► Phase encodes information
└── Equilibrium Propagation ──► Learning without backprop
```

**Results (All 6 Claims Verified on Hardware):**
- 1.1M pulses/sec hardware addition
- 57K neuron-updates/sec parallel inference
- 99.2% target separation in learning
- Self-modifying dynamics via coherence feedback
- $5 hardware, no GPU, no floating point

---

## Quick Start

**Requirements:**
- ESP32-C6 DevKit (~$8)
- USB-C cable
- 10 minutes

```bash
# Clone
git clone https://github.com/EntroMorphic/pulse-arithmetic-lab.git
cd pulse-arithmetic-lab

# Setup ESP-IDF (one time)
# See SETUP.md for detailed instructions

# Build and flash the first demo
cd firmware/01_pulse_addition
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

You should see pulses being counted by hardware. That's addition. Keep going.

---

## The Journey

| Demo | What You Learn | Time |
|------|----------------|------|
| [01_pulse_addition](firmware/01_pulse_addition/) | PCNT counts pulses = addition | 5 min |
| [02_parallel_dot](firmware/02_parallel_dot/) | PARLIO + PCNT = parallel dot product | 10 min |
| [03_spectral_oscillator](firmware/03_spectral_oscillator/) | Phase dynamics, Kuramoto coupling, coherence feedback | 15 min |
| [04_equilibrium_prop](firmware/04_equilibrium_prop/) | Learning without backpropagation | 20 min |

Each demo is self-contained. Read the code. Run it. Change things. Break it. Fix it.

---

## The Claims

We make specific, falsifiable claims. See [CLAIMS.md](CLAIMS.md) for details.

**All 6 claims verified on ESP32-C6 hardware (2026-02-06):**

| # | Claim | Status | Key Result |
|---|-------|--------|------------|
| 1 | Pulse counting = addition | **VERIFIED** | 1.11M pulses/sec |
| 2 | Parallel computation | **VERIFIED** | 4 neurons simultaneous |
| 3 | Ternary eliminates multiply | **VERIFIED** | Exact match, 0 error |
| 4 | Oscillators maintain phase | **VERIFIED** | Kuramoto coherence works |
| 5 | Equilibrium propagation learns | **VERIFIED** | 99.2% separation |
| 6 | Self-modification via coherence | **VERIFIED** | Ablation proves feedback |

Each claim has explicit falsification conditions and tests you can run.

---

## What This Is

A teaching lab for understanding how pulse counting and parallel I/O can perform neural computation. The goal is **reproduction and understanding**, not production deployment.

## What This Isn't

- Not a ML framework (use PyTorch for that)
- Not claiming to beat GPUs (different problem space)
- Not biologically realistic (engineering, not neuroscience)
- Not the fastest possible implementation (clarity > speed)

---

## Hardware

**Required:** ESP32-C6-DevKitC-1 or compatible

**Why ESP32-C6?**
- PCNT: 4 pulse counter units, 16-bit, hardware filtering
- PARLIO: 8-bit parallel I/O at 10 MHz
- RISC-V: Clean architecture, good tooling
- Price: ~$8 including USB

**No additional components needed.** The demos use internal loopback (PARLIO output → PCNT input via GPIO).

---

## Repository Structure

```
pulse-arithmetic-lab/
├── README.md           # You are here
├── SETUP.md            # Hardware setup, ESP-IDF installation
├── CLAIMS.md           # Falsifiable claims and tests
├── firmware/
│   ├── 01_pulse_addition/      # Simplest: PCNT counts pulses
│   ├── 02_parallel_dot/        # PARLIO + PCNT = dot product
│   ├── 03_spectral_oscillator/ # Phase dynamics
│   ├── 04_equilibrium_prop/    # Full learning demo
│   └── reference/              # NumPy reference implementations
├── notebooks/
│   └── concepts.ipynb          # Visualize concepts (no hardware needed)
├── docs/
│   ├── THEORY.md               # Mathematical background
│   └── HARDWARE.md             # Register-level details
└── tests/
    └── verify_claims.py        # Automated claim verification
```

---

## FAQ

**Q: Do I need to understand neural networks?**
A: Basic familiarity helps, but the demos build from first principles.

**Q: Can I use a different ESP32 variant?**
A: The ESP32-C6 has specific PARLIO capabilities. Other variants may work with modifications.

**Q: Is this related to neuromorphic computing?**
A: Similar goals (efficient neural computation), different approach (commodity hardware, not custom silicon).

**Q: Can I use this for my project?**
A: Yes. MIT license. Attribution appreciated.

---

## Citation

```bibtex
@misc{pulsearithmetic2026,
  title={Pulse Arithmetic Lab: Hardware Neural Computation on ESP32-C6},
  author={EntroMorphic Research},
  year={2026},
  url={https://github.com/EntroMorphic/pulse-arithmetic-lab}
}
```

---

## Acknowledgments

This research is funded by EntroMorphic, LLC.

The equilibrium propagation algorithm is based on work by Scellier & Bengio (2017).

---

## License

MIT License. See [LICENSE](LICENSE).

---

**The hardware is already doing the math. We just had to see it.**
