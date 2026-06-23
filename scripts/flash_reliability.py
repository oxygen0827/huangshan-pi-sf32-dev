#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run repeated Huangshan flash attempts and report success rate.")
    parser.add_argument("port", nargs="?", help="Serial port, e.g. /dev/cu.usbserial-110")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--attempts-per-run", type=int, default=1)
    parser.add_argument("--delay", type=float, default=2)
    parser.add_argument("--confirm-boot", action="store_true")
    parser.add_argument("--allow-usbmodem", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    script = Path(__file__).with_name("flash.py")
    results: list[tuple[int, float]] = []

    for run in range(1, args.runs + 1):
        cmd = [
            sys.executable,
            str(script),
            "--attempts",
            str(args.attempts_per_run),
        ]
        if args.confirm_boot:
            cmd.append("--confirm-boot")
        if args.allow_usbmodem:
            cmd.append("--allow-usbmodem")
        if args.dry_run:
            cmd.append("--dry-run")
        if args.port:
            cmd.append(args.port)

        print(f"\n=== reliability run {run}/{args.runs} ===")
        started = time.monotonic()
        proc = subprocess.run(cmd)
        elapsed = time.monotonic() - started
        results.append((proc.returncode, elapsed))
        print(f"=== run {run} exit={proc.returncode} elapsed={elapsed:.1f}s ===")
        if run < args.runs and args.delay > 0:
            time.sleep(args.delay)

    successes = sum(1 for code, _elapsed in results if code == 0)
    total = len(results)
    rate = (successes / total * 100) if total else 0.0
    print("\n=== reliability summary ===")
    for idx, (code, elapsed) in enumerate(results, start=1):
        status = "PASS" if code == 0 else "FAIL"
        print(f"run {idx}: {status} exit={code} elapsed={elapsed:.1f}s")
    print(f"success: {successes}/{total} ({rate:.1f}%)")
    return 0 if successes == total else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
