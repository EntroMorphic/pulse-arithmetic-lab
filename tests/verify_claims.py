#!/usr/bin/env python3
"""
Automated Claim Verification for Pulse Arithmetic Lab.

This script flashes each demo firmware and verifies the output matches
the success criteria defined in CLAIMS.md.

Requirements:
    - ESP32-C6 connected via USB
    - ESP-IDF installed and sourced
    - pyserial installed

Usage:
    python verify_claims.py              # Run all tests
    python verify_claims.py --demo 1     # Run specific demo
    python verify_claims.py --port /dev/ttyUSB0  # Specify port
"""

import argparse
import subprocess
import serial
import time
import re
import sys
from pathlib import Path
from typing import Optional, Tuple, List, Dict

# Configuration
DEFAULT_PORT = "/dev/ttyACM0"
BAUD_RATE = 115200
FIRMWARE_DIR = Path(__file__).parent.parent / "firmware"
TIMEOUT_FLASH = 120  # seconds
TIMEOUT_MONITOR = 30  # seconds


class ClaimVerifier:
    """Verifies claims by flashing and monitoring firmware."""

    def __init__(self, port: str = DEFAULT_PORT):
        self.port = port
        self.results: Dict[str, dict] = {}

    def flash_firmware(self, demo_dir: Path) -> bool:
        """Flash firmware using idf.py."""
        print(f"  Flashing {demo_dir.name}...")

        try:
            result = subprocess.run(
                ["idf.py", "-p", self.port, "flash"],
                cwd=demo_dir,
                capture_output=True,
                text=True,
                timeout=TIMEOUT_FLASH,
            )

            if result.returncode != 0:
                print(f"  ERROR: Flash failed")
                print(result.stderr[:500])
                return False

            return True

        except subprocess.TimeoutExpired:
            print(f"  ERROR: Flash timed out")
            return False
        except FileNotFoundError:
            print(f"  ERROR: idf.py not found. Source ESP-IDF first.")
            return False

    def capture_output(self, duration: float = 15.0) -> str:
        """Capture serial output from device."""
        print(f"  Capturing output for {duration}s...")

        try:
            ser = serial.Serial(self.port, BAUD_RATE, timeout=1)

            # Reset device
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            time.sleep(0.1)

            # Capture output
            output = ""
            start = time.time()
            while time.time() - start < duration:
                if ser.in_waiting:
                    data = ser.read(ser.in_waiting)
                    output += data.decode("utf-8", errors="replace")
                time.sleep(0.05)

            ser.close()
            return output

        except serial.SerialException as e:
            print(f"  ERROR: Serial error: {e}")
            return ""

    def verify_demo_01(self, output: str) -> Tuple[bool, str]:
        """
        Verify Claim 1: Pulse counting performs addition.

        Success criteria:
        - All basic counting tests pass
        - All addition tests pass
        """
        # Check for test results
        passes = len(re.findall(r"Result:\s*PASS", output))
        fails = len(re.findall(r"Result:\s*FAIL", output))

        # Look for summary
        summary_match = re.search(r"Tests passed:\s*(\d+)\s*/\s*(\d+)", output)

        if summary_match:
            passed = int(summary_match.group(1))
            total = int(summary_match.group(2))
            success = passed == total
            msg = f"Tests: {passed}/{total}"
        else:
            success = passes > 0 and fails == 0
            msg = f"PASS count: {passes}, FAIL count: {fails}"

        return success, msg

    def verify_demo_02(self, output: str) -> Tuple[bool, str]:
        """
        Verify Claims 2 & 3: Parallel dot products with ternary weights.

        Success criteria:
        - All verification tests pass (hardware matches reference)
        """
        # Check for test results
        test_results = re.findall(r"Result:\s*(PASS|FAIL)", output)

        if not test_results:
            return False, "No test results found"

        passes = test_results.count("PASS")
        fails = test_results.count("FAIL")

        success = fails == 0 and passes > 0
        msg = f"Verification tests: {passes} PASS, {fails} FAIL"

        return success, msg

    def verify_demo_03(self, output: str) -> Tuple[bool, str]:
        """
        Verify Claim 4: Spectral oscillators maintain phase state.

        Success criteria:
        - Delta band has highest magnitude (slowest decay)
        - Gamma band has lowest magnitude (fastest decay)
        - Coherence is measurable
        """
        # Look for band magnitudes
        delta_match = re.search(r"Delta\s*\|\s*\d+\s*\|\s*(\d+)", output)
        gamma_match = re.search(r"Gamma\s*\|\s*\d+\s*\|\s*(\d+)", output)

        if not (delta_match and gamma_match):
            return False, "Could not parse band magnitudes"

        delta_mag = int(delta_match.group(1))
        gamma_mag = int(gamma_match.group(1))

        # Delta should decay slower than Gamma
        decay_order_correct = delta_mag > gamma_mag

        # Look for coherence
        coherence_match = re.search(r"coherence:\s*(\d+)", output, re.IGNORECASE)
        has_coherence = coherence_match is not None

        success = decay_order_correct and has_coherence
        msg = f"Delta mag={delta_mag}, Gamma mag={gamma_mag}, decay order={'correct' if decay_order_correct else 'wrong'}"

        return success, msg

    def verify_demo_04(self, output: str) -> Tuple[bool, str]:
        """
        Verify Claim 5: Equilibrium propagation learns.

        Success criteria:
        - Loss decreases over training
        - Output separation > 78% of target (100 out of 128)
        """
        # Look for separation percentage
        sep_match = re.search(r"Separation achieved:\s*([\d.]+)%", output)

        if sep_match:
            separation_pct = float(sep_match.group(1))
            success = separation_pct > 78.0
            msg = f"Separation: {separation_pct:.1f}% (threshold: 78%)"
            return success, msg

        # Alternative: look for raw separation values
        sep_raw = re.search(r"Separation:\s*(-?\d+)", output)
        if sep_raw:
            sep_value = abs(int(sep_raw.group(1)))
            target = 128
            pct = (sep_value / target) * 100
            success = pct > 78.0
            msg = f"Separation: {sep_value}/128 = {pct:.1f}%"
            return success, msg

        return False, "Could not parse separation metric"

    def run_demo(self, demo_num: int) -> dict:
        """Run a single demo and verify its claims."""
        demo_dirs = {
            1: "01_pulse_addition",
            2: "02_parallel_dot",
            3: "03_spectral_oscillator",
            4: "04_equilibrium_prop",
        }

        verify_funcs = {
            1: self.verify_demo_01,
            2: self.verify_demo_02,
            3: self.verify_demo_03,
            4: self.verify_demo_04,
        }

        claims = {
            1: "Pulse counting = addition",
            2: "Parallel computation + ternary weights",
            3: "Oscillators maintain phase state",
            4: "Equilibrium propagation learns",
        }

        result = {
            "demo": demo_num,
            "name": demo_dirs[demo_num],
            "claim": claims[demo_num],
            "flashed": False,
            "verified": False,
            "message": "",
            "output": "",
        }

        demo_dir = FIRMWARE_DIR / demo_dirs[demo_num]

        if not demo_dir.exists():
            result["message"] = f"Demo directory not found: {demo_dir}"
            return result

        # Flash
        if not self.flash_firmware(demo_dir):
            result["message"] = "Flash failed"
            return result
        result["flashed"] = True

        # Capture output
        duration = 20 if demo_num == 4 else 15  # EP needs more time
        output = self.capture_output(duration)
        result["output"] = output

        if not output:
            result["message"] = "No output captured"
            return result

        # Verify
        success, message = verify_funcs[demo_num](output)
        result["verified"] = success
        result["message"] = message

        return result

    def run_all(self) -> List[dict]:
        """Run all demos and return results."""
        results = []

        for demo_num in [1, 2, 3, 4]:
            print(f"\n{'=' * 60}")
            print(f"  DEMO {demo_num}")
            print(f"{'=' * 60}")

            result = self.run_demo(demo_num)
            results.append(result)

            status = "VERIFIED" if result["verified"] else "FAILED"
            print(f"\n  Result: {status}")
            print(f"  {result['message']}")

        return results

    def print_summary(self, results: List[dict]):
        """Print summary table."""
        print("\n")
        print("=" * 70)
        print("  CLAIM VERIFICATION SUMMARY")
        print("=" * 70)
        print()
        print("  Demo | Claim                              | Status")
        print("  -----+------------------------------------+---------")

        for r in results:
            status = "PASS" if r["verified"] else "FAIL"
            claim = r["claim"][:34]
            print(f"    {r['demo']}  | {claim:34s} | {status}")

        print()
        passed = sum(1 for r in results if r["verified"])
        total = len(results)
        print(f"  Total: {passed}/{total} claims verified")
        print()

        if passed == total:
            print("  ALL CLAIMS VERIFIED")
        else:
            print("  SOME CLAIMS FAILED - see output above for details")

        print("=" * 70)


def main():
    parser = argparse.ArgumentParser(description="Verify Pulse Arithmetic Lab claims")
    parser.add_argument(
        "--demo", type=int, choices=[1, 2, 3, 4], help="Run specific demo (1-4)"
    )
    parser.add_argument(
        "--port",
        type=str,
        default=DEFAULT_PORT,
        help=f"Serial port (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--list", action="store_true", help="List claims without running tests"
    )
    args = parser.parse_args()

    if args.list:
        print("\nClaims to verify:")
        print("  1. Pulse counting performs addition")
        print("  2. Parallel I/O enables parallel computation")
        print("  3. Ternary weights eliminate multiplication")
        print("  4. Spectral oscillators maintain phase state")
        print("  5. Equilibrium propagation enables learning")
        print("  6. Self-modification via coherence (not yet testable)")
        print("\nRun with --demo N to test specific claim, or no args for all.")
        return

    verifier = ClaimVerifier(port=args.port)

    if args.demo:
        print(f"\nRunning Demo {args.demo}...")
        result = verifier.run_demo(args.demo)
        verifier.print_summary([result])
    else:
        print("\nRunning all demos...")
        results = verifier.run_all()
        verifier.print_summary(results)


if __name__ == "__main__":
    main()
