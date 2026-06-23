#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Run repeated Huangshan Runtime serial app installs.")
    parser.add_argument("port", help="Serial port, for example /dev/cu.usbserial-13220")
    parser.add_argument("--runs", type=int, default=10)
    parser.add_argument("--apps", nargs="+", default=["clock_test", "status_test"])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent / "runtime_apps")
    parser.add_argument("--delay", type=float, default=0.4)
    parser.add_argument("--chunk-bytes", type=int, default=48)
    args = parser.parse_args()

    script = Path(__file__).resolve().parent / "runtime_install_serial.py"
    results: list[tuple[int, str, int]] = []
    for index in range(args.runs):
        app_id = args.apps[index % len(args.apps)]
        package_dir = args.root / app_id
        cmd = [
            sys.executable,
            str(script),
            args.port,
            "--package-dir",
            str(package_dir),
            "--app-id",
            app_id,
            "--chunk-bytes",
            str(args.chunk_bytes),
            "--no-echo",
        ]
        print(f"[runtime-reliability] run {index + 1}/{args.runs}: {app_id}")
        proc = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(proc.stdout.rstrip())
        results.append((index + 1, app_id, proc.returncode))
        if proc.returncode != 0:
            break
        time.sleep(args.delay)

    successes = sum(1 for _, _, code in results if code == 0)
    total = len(results)
    rate = (successes / total * 100.0) if total else 0.0
    print()
    print(f"runtime install success: {successes}/{total} ({rate:.1f}%)")
    for run, app_id, code in results:
        print(f"  run {run}: {app_id} -> {'ok' if code == 0 else 'fail'}")
    return 0 if total == args.runs and successes == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
