# Documentation Index

## Pulse Arithmetic Lab Documents

| Document | Purpose |
|----------|---------|
| [THEORY.md](THEORY.md) | Mathematical foundations |
| [HARDWARE.md](HARDWARE.md) | ESP32-C6 register-level details |
| [ETM.md](ETM.md) | Extended Turing Machine formalization |
| [CPU_FREE_BOUNDARY.md](CPU_FREE_BOUNDARY.md) | What runs with/without CPU |
| [TERNARY_TURING_MACHINE.md](TERNARY_TURING_MACHINE.md) | Path to full Turing completeness |

## Related Documents in reflex-os

The `reflex-os` repository contains additional implementation details and proofs:

| Document | Purpose | Path |
|----------|---------|------|
| TURING_FABRIC.md | ETM fabric architecture and test results | `reflex-os/TURING_FABRIC.md` |
| SILICON_GRAIL.md | Original proof of conditional branching | `reflex-os/docs/SILICON_GRAIL.md` |
| ARCHITECTURE.md | Overall system architecture | `reflex-os/docs/ARCHITECTURE.md` |
| ETM_FABRIC_CFC.md | ETM + Closed-form Continuous-time networks | `reflex-os/docs/ETM_FABRIC_CFC.md` |

## Key Implementation Files

| File | What It Proves | Path |
|------|----------------|------|
| silicon_grail_wired.c | Conditional branch in hardware (4/4 tests pass) | `reflex-os/main/silicon_grail_wired.c` |
| multi_peripheral_etm.c | 3-state autonomous state machine | `reflex-os/main/multi_peripheral_etm.c` |
| turing_fabric.c | Basic ETM fabric demo | `reflex-os/main/turing_fabric.c` |
| state_machine_fabric.c | Multi-state autonomous execution | `reflex-os/main/state_machine_fabric.c` |

## Verification History

| Date | Commit | What Was Verified |
|------|--------|-------------------|
| 2026-02-03 | 0a939db | Silicon Grail - 4/4 tests PASS |
| 2026-02-03 | 7be9fbd | Silicon Grail NOT FALSIFIED - 12/12 tests |
| 2026-02-04 | f07e3bd | Turing-complete ETM fabric with multi-state execution |
| 2026-02-06 | 923a340 | Claim 7 added to pulse-arithmetic-lab |
| 2026-02-06 | eaf0088 | Ternary Turing Machine architecture documented |

## Quick Navigation

**"I want to understand the theory"**
→ Start with [THEORY.md](THEORY.md), then [ETM.md](ETM.md)

**"I want to see it work"**
→ Run `firmware/05_turing_fabric/`, read its [README](../firmware/05_turing_fabric/README.md)

**"I want to verify the claims"**
→ See [CLAIMS.md](../CLAIMS.md) for all 7 claims and their tests

**"I want to understand what's CPU-free"**
→ [CPU_FREE_BOUNDARY.md](CPU_FREE_BOUNDARY.md)

**"I want to know how to make it fully Turing-complete"**
→ [TERNARY_TURING_MACHINE.md](TERNARY_TURING_MACHINE.md)

**"I want the raw proof code"**
→ `reflex-os/main/silicon_grail_wired.c`
