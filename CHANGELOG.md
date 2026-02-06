# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.3.0] - 2026-02-06

### Added

- **Claim 6 VERIFIED**: Self-modification via coherence
  - Implemented coherence feedback loop in Demo 03
  - Added `evolve_step_with_feedback()` function for homeostatic coupling modulation
  - Added ablation test comparing WITH vs WITHOUT coherence feedback
  - Results: Coupling variance 0 (without) vs 1.16 (with feedback)

### Changed

- Demo 03: Now includes coherence feedback ablation test
- CLAIMS.md: Updated Claim 6 status from "NOT YET TESTED" to "VERIFIED"

### Milestone

**All 6 scientific claims now verified on hardware.**

| Claim | Status |
|-------|--------|
| 1. Pulse counting = addition | VERIFIED |
| 2. Parallel computation | VERIFIED |
| 3. Ternary eliminates multiply | VERIFIED |
| 4. Oscillators maintain phase | VERIFIED |
| 5. Equilibrium propagation learns | VERIFIED |
| 6. Self-modification via coherence | VERIFIED |

## [0.2.0] - 2026-02-05

### Added

- `docs/THEORY.md` - Mathematical foundations (Q15, Kuramoto, equilibrium propagation)
- `docs/HARDWARE.md` - Register-level documentation for PCNT and PARLIO
- `reference/pulse_arithmetic.py` - NumPy implementations of all 4 demos
- `notebooks/pulse_arithmetic_lab.ipynb` - Interactive Colab notebook
- `tests/verify_claims.py` - Automated claim verification script

### Documentation

- Complete documentation suite now available
- Colab notebook for zero-install exploration
- Reference implementations for comparison with hardware

## [0.1.2] - 2026-02-05

### Fixed

- Demo 03: Coherence metric now uses proper Kuramoto order parameter
  - Changed from `|mean(z)|` to `|mean(z/|z|)|`
  - This measures phase alignment independent of amplitude
  - Delta band now shows 23710 coherence (moderately aligned)
- Updated CLAIMS.md with detailed falsification results for all claims

### Documentation

- Added falsification status table to CLAIMS.md
- Added detailed test results for each claim

## [0.1.1] - 2026-02-05

### Hardware Falsification Complete

All demos tested on actual ESP32-C6 hardware (ESP32-C6FH4, revision v0.2).

**Test Results:**

| Demo | Status | Details |
|------|--------|---------|
| 01 Pulse Addition | PASS | 8/8 tests, 1.11M pulses/sec |
| 02 Parallel Dot | PASS | 5/5 tests, hardware matches reference exactly |
| 03 Spectral Oscillator | PASS | Band decay verified, coherence fixed in 0.1.2 |
| 04 Equilibrium Prop | PASS | Loss reduced 44%, 99.2% target separation |

### Fixed

- Demo 01: Reduced benchmark from 100k to 30k pulses to stay within PCNT 16-bit range
- Added note about PCNT overflow limits

## [0.1.0] - 2026-02-05

### Added

- Initial release of Pulse Arithmetic Lab
- **Demo 01: Pulse Addition** - PCNT peripheral counts GPIO pulses as addition
- **Demo 02: Parallel Dot Product** - PARLIO + PCNT for 4 parallel dot products
- **Demo 03: Spectral Oscillator** - Complex oscillators with Kuramoto coupling
- **Demo 04: Equilibrium Propagation** - Learning without backpropagation
- Documentation
  - README.md with project overview
  - SETUP.md with 10-minute quickstart
  - CLAIMS.md with 6 falsifiable claims
  - Individual README for each demo
- MIT License
- Contributing guidelines

### Hardware Support

- ESP32-C6 (primary target)
- Tested with ESP-IDF v5.4

### Known Limitations

- Reference NumPy implementations not yet added
- Colab notebook not yet created

---

## Version History Summary

| Version | Date | Highlights |
|---------|------|------------|
| 0.3.0 | 2026-02-06 | **ALL 6 CLAIMS VERIFIED** - Claim 6 ablation study complete |
| 0.2.0 | 2026-02-05 | Documentation suite, reference implementations |
| 0.1.2 | 2026-02-05 | Fixed coherence metric, updated CLAIMS.md |
| 0.1.1 | 2026-02-05 | Hardware falsification complete |
| 0.1.0 | 2026-02-05 | Initial release with 4 demos |

## Planned for Future Releases

### v1.0.0 (Planned)
- Video tutorials
- Additional hardware platforms
- Performance optimization guide
