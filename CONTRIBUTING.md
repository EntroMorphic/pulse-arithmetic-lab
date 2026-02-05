# Contributing to Pulse Arithmetic Lab

Thank you for your interest in contributing! This project aims to make
hardware neural computation accessible and reproducible.

## Ways to Contribute

### 1. Try the Demos and Report Issues

The most valuable contribution is trying to reproduce our results:

- Flash each demo to your ESP32-C6
- Compare your output to expected output in READMEs
- Report discrepancies as GitHub issues

**When reporting issues, include:**
- ESP-IDF version (`idf.py --version`)
- Hardware (ESP32-C6-DevKitC-1 or other)
- Full serial output
- What you expected vs what happened

### 2. Falsify Our Claims

We've made specific, falsifiable claims in `CLAIMS.md`. Help us test them:

- Design experiments that could disprove our claims
- Run existing falsification tests
- Document any claim that fails its falsification condition

**A failed claim is still a valuable contribution!** Science progresses
through falsification. If you find a claim that doesn't hold, that's
important information.

### 3. Add Reference Implementations

We need NumPy implementations that match the firmware behavior:

```
reference/
├── 01_pulse_addition.py    # Software implementation of pulse counting
├── 02_parallel_dot.py      # Software dot product for comparison
├── 03_spectral_oscillator.py
└── 04_equilibrium_prop.py
```

Reference implementations should:
- Use only NumPy (no PyTorch/TensorFlow)
- Match firmware behavior exactly
- Include comparison tests

### 4. Improve Documentation

- Fix typos and unclear explanations
- Add diagrams (ASCII art preferred for terminal display)
- Translate to other languages
- Add missing details to hardware setup

### 5. Port to Other Hardware

Currently we support ESP32-C6. Ports welcome for:
- ESP32-S3 (has SIMD instructions)
- RP2040/RP2350 (different peripheral model)
- STM32 (different ecosystem)

## Code Style

### C Code (Firmware)

- Use ESP-IDF conventions
- 4-space indentation
- Descriptive variable names
- Comments explaining "why" not "what"

```c
// Good: explains the constraint
// PCNT max is 32767, so we reset before overflow
if (count > 30000) {
    pcnt_unit_clear_count(unit);
}

// Bad: just repeats the code
// Check if count > 30000
if (count > 30000) {
    pcnt_unit_clear_count(unit);
}
```

### Python Code (Reference/Tests)

- Follow PEP 8
- Type hints appreciated
- Docstrings for public functions

## Pull Request Process

1. **Fork and branch** from `main`
2. **One feature per PR** - keep changes focused
3. **Test locally** - ensure code compiles and runs
4. **Update docs** - if behavior changes, update README
5. **Describe changes** - explain what and why in PR description

### PR Title Format

```
feat: Add STM32 port for demo 01
fix: Correct phase wraparound in oscillator
docs: Clarify PCNT setup steps
test: Add falsification test for Claim 3
```

## Questions?

- Open a GitHub issue for questions
- Tag with `question` label
- We respond within a few days

## Code of Conduct

Be respectful and constructive. We're all here to learn.

- Assume good faith
- Focus on the science, not the person
- Welcome newcomers
- Celebrate falsification (it's how science works!)

## License

By contributing, you agree that your contributions will be licensed
under the MIT License.
