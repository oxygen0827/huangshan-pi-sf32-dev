#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

from runtime_transport import INSTALL_CHUNK_BYTES


def run_step(label: str, cmd: list[str], env: dict[str, str] | None = None) -> int:
    print(f"[runtime-full-reliability] {label}")
    print("$ " + " ".join(cmd), flush=True)
    run_env = os.environ.copy() if env is None else env.copy()
    run_env.setdefault("PYTHONUNBUFFERED", "1")
    proc = subprocess.Popen(
        cmd,
        text=True,
        env=run_env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        bufsize=1,
    )
    if proc.stdout is None:
        proc.kill()
        print(f"[runtime-full-reliability] {label} -> fail")
        return 1
    for line in proc.stdout:
        print(line, end="")
    code = proc.wait()
    print(f"[runtime-full-reliability] {label} -> {'ok' if code == 0 else 'fail'}")
    return code


def add_common_checks(cmd: list[str], args: argparse.Namespace) -> None:
    if args.verify_capabilities:
        cmd.append("--verify-capabilities")
    if args.verify_sensors:
        cmd.append("--verify-sensors")
    if args.verify_power:
        cmd.append("--verify-power")
    if args.verify_touch:
        cmd.append("--verify-touch")
    if args.verify_gpio:
        cmd.append("--verify-gpio")
    if args.verify_display:
        cmd.append("--verify-display")
        cmd.extend(["--verify-display-brightness", str(args.verify_display_brightness)])
    if args.verify_rgb:
        cmd.append("--verify-rgb")
        cmd.extend(["--verify-rgb-color", args.verify_rgb_color])
    if args.verify_flow:
        cmd.append("--verify-flow")
        cmd.extend(["--flow-text", args.flow_text, "--flow-channel", args.flow_channel])
    if args.verify_voice_status:
        cmd.append("--verify-voice-status")
    if args.verify_voice:
        cmd.append("--verify-voice")
        cmd.extend(["--voice-duration-ms", str(args.voice_duration_ms)])


def is_rgb_color(value: str) -> bool:
    return len(value) == 6 and all(ch in "0123456789abcdefABCDEF" for ch in value)


def argument_validation_error(args: argparse.Namespace) -> str | None:
    if args.runs <= 0:
        return "--runs must be greater than 0"
    if not args.apps:
        return "--apps must contain at least one app id"
    if args.chunk_bytes <= 0:
        return "--chunk-bytes must be greater than 0"
    if args.verify_display_brightness < 0 or args.verify_display_brightness > 100:
        return "--verify-display-brightness must be between 0 and 100"
    if not is_rgb_color(args.verify_rgb_color):
        return "--verify-rgb-color must be six hex digits"
    if args.voice_duration_ms <= 0:
        return "--voice-duration-ms must be greater than 0"
    if not args.ble_name:
        return "--ble-name must not be empty"
    return None


def run_self_test() -> None:
    audit_script = Path(__file__).resolve().parent / "runtime_architecture_audit.py"
    audit = subprocess.run(
        [sys.executable, str(audit_script), "--self-test"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if audit.returncode != 0:
        raise AssertionError("runtime architecture audit failed:\n" + audit.stdout)
    base = argparse.Namespace(
        runs=1,
        apps=["touch_stage"],
        chunk_bytes=INSTALL_CHUNK_BYTES,
        verify_display_brightness=70,
        verify_rgb_color="3366ff",
        voice_duration_ms=600,
        ble_name="VibeBoard",
    )
    def clone(**changes):
        values = vars(base).copy()
        values.update(changes)
        return argparse.Namespace(**values)

    assert argument_validation_error(base) is None
    assert "--runs" in (argument_validation_error(clone(runs=0)) or "")
    assert "--apps" in (argument_validation_error(clone(apps=[])) or "")
    assert "--chunk-bytes" in (argument_validation_error(clone(chunk_bytes=0)) or "")
    assert "--verify-display-brightness" in (argument_validation_error(clone(verify_display_brightness=101)) or "")
    assert "--verify-rgb-color" in (argument_validation_error(clone(verify_rgb_color="notrgb")) or "")
    assert "--voice-duration-ms" in (argument_validation_error(clone(voice_duration_ms=0)) or "")
    assert "--ble-name" in (argument_validation_error(clone(ble_name="")) or "")
    print("runtime_full_reliability self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run Huangshan Runtime serial and BLE reliability checks sequentially. "
            "Use this when one board exposes both transports; the install session is shared."
        )
    )
    parser.add_argument(
        "port",
        nargs="?",
        help="Serial port, required unless --package-only or --ble-only is used, for example /dev/cu.usbserial-13220",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--apps", nargs="+", default=["touch_stage"])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent / "runtime_apps")
    parser.add_argument("--chunk-bytes", type=int, default=INSTALL_CHUNK_BYTES)
    parser.add_argument("--skip-package-validate", action="store_true", help="Skip host-side package validation before hardware checks.")
    parser.add_argument("--package-only", action="store_true", help="Validate selected packages and exit without touching hardware.")
    parser.add_argument("--serial-only", action="store_true", help="Run only the serial half.")
    parser.add_argument("--ble-only", action="store_true", help="Run only the BLE half.")
    parser.add_argument("--ble-name", default="VibeBoard", help="BLE local name to scan for.")
    parser.add_argument("--ble-no-cache", action="store_true", help="Ignore cached BLE peripheral identifier/address.")
    parser.add_argument(
        "--core",
        action="store_true",
        help="Enable the current core Runtime API checks: capabilities, sensors, power, display, touch, gpio, rgb, flow, and voice status.",
    )
    parser.add_argument("--verify-capabilities", action="store_true")
    parser.add_argument("--verify-sensors", action="store_true")
    parser.add_argument("--verify-power", action="store_true")
    parser.add_argument("--verify-touch", action="store_true")
    parser.add_argument("--verify-gpio", action="store_true")
    parser.add_argument("--verify-display", action="store_true")
    parser.add_argument("--verify-display-brightness", type=int, default=70)
    parser.add_argument("--verify-rgb", action="store_true")
    parser.add_argument("--verify-rgb-color", default="3366ff")
    parser.add_argument("--verify-flow", action="store_true")
    parser.add_argument("--flow-text", default="flow-regression-ok")
    parser.add_argument("--flow-channel", default="pc.flow")
    parser.add_argument("--verify-voice-status", action="store_true", help="Also read voice bridge status JSON without starting capture.")
    parser.add_argument("--verify-voice", action="store_true", help="Also run voice bridge checks; slower and records audio.")
    parser.add_argument("--voice-duration-ms", type=int, default=600)
    parser.add_argument("--self-test", action="store_true", help="Run offline argument validation checks and exit.")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0
    validation_error = argument_validation_error(args)
    if validation_error:
        parser.error(validation_error)

    if args.serial_only and args.ble_only:
        parser.error("--serial-only and --ble-only are mutually exclusive")
    if args.package_only and args.skip_package_validate:
        parser.error("--package-only cannot be combined with --skip-package-validate")
    if not args.ble_only and not args.package_only and not args.port:
        parser.error("port is required unless --package-only or --ble-only is used")

    if args.core:
        args.verify_capabilities = True
        args.verify_sensors = True
        args.verify_power = True
        args.verify_touch = True
        args.verify_gpio = True
        args.verify_display = True
        args.verify_rgb = True
        args.verify_flow = True
        args.verify_voice_status = True

    root = Path(__file__).resolve().parent
    serial_script = root / "runtime_reliability.sh"
    ble_script = root / "runtime_reliability_ble.sh"
    package_script = root / "runtime_package.py"

    base_apps = ["--apps", *args.apps]
    results: list[tuple[str, int]] = []

    if not args.skip_package_validate:
        for app_id in args.apps:
            package_cmd = [str(package_script), "--package-dir", str(args.root / app_id)]
            code = run_step(f"package-validate {app_id}", package_cmd)
            results.append((f"package {app_id}", code))
            if code != 0:
                break

    if args.package_only or (results and results[-1][1] != 0):
        print()
        print("[runtime-full-reliability] summary")
        for label, code in results:
            print(f"  {label}: {'ok' if code == 0 else 'fail'}")
        return 0 if results and all(code == 0 for _, code in results) else 1

    if not args.ble_only:
        serial_cmd = [
            str(serial_script),
            args.port,
            "--runs",
            str(args.runs),
            "--root",
            str(args.root),
            "--chunk-bytes",
            str(args.chunk_bytes),
            *base_apps,
        ]
        add_common_checks(serial_cmd, args)
        results.append(("serial", run_step("serial", serial_cmd)))

    if not args.serial_only:
        ble_cmd = [
            str(ble_script),
            "--runs",
            str(args.runs),
            "--root",
            str(args.root),
            "--chunk-bytes",
            str(args.chunk_bytes),
            "--name",
            args.ble_name,
            *base_apps,
        ]
        if args.ble_no_cache:
            ble_cmd.append("--no-cache")
        add_common_checks(ble_cmd, args)
        env = os.environ.copy()
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        results.append(("ble", run_step("ble", ble_cmd, env=env)))

    print()
    print("[runtime-full-reliability] summary")
    for label, code in results:
        print(f"  {label}: {'ok' if code == 0 else 'fail'}")
    return 0 if results and all(code == 0 for _, code in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
