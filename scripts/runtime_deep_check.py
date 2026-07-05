#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYTHON = sys.executable
PY_CACHE = "/tmp/huangshan-pycache"


@dataclass(frozen=True)
class Check:
    pass_name: str
    label: str
    cmd: list[str]
    cwd: Path = ROOT
    escalated_hint: str | None = None
    stream_output: bool = False


@dataclass(frozen=True)
class HardwareGate:
    port: str | None
    runs: int
    apps: tuple[str, ...]
    ble_name: str
    ble_no_cache: bool
    serial_only: bool
    ble_only: bool
    verify_voice: bool


def env() -> dict[str, str]:
    value = os.environ.copy()
    value.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    value.setdefault("PYTHONPYCACHEPREFIX", PY_CACHE)
    return value


def run_check(check: Check) -> int:
    rel_cwd = check.cwd.relative_to(ROOT) if check.cwd != ROOT else Path(".")
    print(f"[runtime-deep-check] {check.pass_name}: {check.label}")
    print("$ " + " ".join(check.cmd) + f"  # cwd={rel_cwd}", flush=True)
    check_env = env()
    if check.stream_output:
        check_env.setdefault("PYTHONUNBUFFERED", "1")
        proc = subprocess.Popen(
            check.cmd,
            cwd=check.cwd,
            env=check_env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=1,
        )
        assert proc.stdout is not None
        for line in proc.stdout:
            print(line, end="")
        code = proc.wait()
        print(f"[runtime-deep-check] {check.label} -> {'ok' if code == 0 else 'fail'}")
        return code
    completed = subprocess.run(
        check.cmd,
        cwd=check.cwd,
        env=check_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    output = completed.stdout.strip()
    if output:
        print(output)
    print(f"[runtime-deep-check] {check.label} -> {'ok' if completed.returncode == 0 else 'fail'}")
    return completed.returncode


def python_compile_targets() -> list[str]:
    return [
        "scripts/runtime_deep_check.py",
        "scripts/app_store_server.py",
        "scripts/runtime_architecture_audit.py",
        "scripts/runtime_transport.py",
        "scripts/runtime_install_serial.py",
        "scripts/runtime_install_ble.py",
        "scripts/runtime_package.py",
        "scripts/runtime_package_parity.py",
        "scripts/runtime_reliability_checks.py",
        "scripts/runtime_reliability.py",
        "scripts/runtime_reliability_ble.py",
        "scripts/runtime_full_reliability.py",
        "scripts/runtime_cold_recovery.py",
        "scripts/voice_bridge_common.py",
        "scripts/voice_bridge_serial.py",
        "scripts/voice_bridge_ble.py",
        "scripts/voice_terminal_verify.py",
        "scripts/voice_llm_openai.py",
        "scripts/voice_llm_zhipu.py",
    ]


def build_hardware_gate(config: HardwareGate | None) -> Check | None:
    if config is None:
        return None
    cmd = [
        PYTHON,
        "scripts/runtime_full_reliability.py",
    ]
    if config.port:
        cmd.append(config.port)
    cmd.extend(
        [
            "--runs",
            str(config.runs),
            "--apps",
            *config.apps,
            "--ble-name",
            config.ble_name,
            "--core",
        ]
    )
    if config.ble_no_cache:
        cmd.append("--ble-no-cache")
    if config.serial_only:
        cmd.append("--serial-only")
    if config.ble_only:
        cmd.append("--ble-only")
    if config.verify_voice:
        cmd.append("--verify-voice")
    return Check("hardware-gate", "runtime full reliability hardware gate", cmd, stream_output=True)


def build_checks(include_swift: bool, hardware_gate: HardwareGate | None = None) -> list[Check]:
    checks = [
        Check("pass1-architecture", "runtime deep check self-test", [PYTHON, "scripts/runtime_deep_check.py", "--self-test"]),
        Check("pass1-architecture", "runtime architecture audit", [PYTHON, "scripts/runtime_architecture_audit.py", "--self-test"]),
        Check("pass1-architecture", "app store bridge self-test", [PYTHON, "scripts/app_store_server.py", "--self-test"]),
        Check("pass1-architecture", "runtime transport self-test", [PYTHON, "scripts/runtime_transport.py", "--self-test"]),
        Check("pass2-packages", "runtime package validator self-test", [PYTHON, "scripts/runtime_package.py", "--self-test"]),
        Check("pass2-packages", "runtime package corpus", [PYTHON, "scripts/runtime_package.py", "--all"]),
        Check("pass2-packages", "python/swift package parity", [PYTHON, "scripts/runtime_package_parity.py"]),
        Check("pass2-packages", "runtime full reliability offline self-test", [PYTHON, "scripts/runtime_full_reliability.py", "--self-test"]),
        Check("pass3-tooling", "python compile selected runtime tools", [PYTHON, "-m", "py_compile", *python_compile_targets()]),
        Check("pass3-tooling", "serial CLI self-test", [PYTHON, "scripts/runtime_install_serial.py", "--self-test"]),
        Check("pass3-tooling", "BLE CLI self-test", [PYTHON, "scripts/runtime_install_ble.py", "--self-test"]),
        Check("pass3-tooling", "voice bridge common self-test", [PYTHON, "scripts/voice_bridge_common.py", "--self-test"]),
        Check("pass3-tooling", "voice terminal self-test", ["./scripts/voice_terminal_selftest.sh"]),
        Check("pass3-tooling", "git diff whitespace check", ["git", "diff", "--check"]),
    ]
    if include_swift:
        checks.append(
            Check(
                "pass3-tooling",
                "iOS Swift package tests",
                ["swift", "test"],
                cwd=ROOT / "mobile" / "ios" / "VibeBoardBLE",
                escalated_hint="SwiftPM may need user-level cache write access.",
            )
        )
    hardware_check = build_hardware_gate(hardware_gate)
    if hardware_check is not None:
        checks.append(hardware_check)
    return checks


def run_all(include_swift: bool, hardware_gate: HardwareGate | None = None) -> int:
    results: list[tuple[Check, int]] = []
    checks = build_checks(include_swift, hardware_gate=hardware_gate)
    for check in checks:
        code = run_check(check)
        results.append((check, code))
        if code != 0:
            break
    print()
    print("[runtime-deep-check] summary")
    for check, code in results:
        print(f"  {check.pass_name} / {check.label}: {'ok' if code == 0 else 'fail'}")
    expected = len(checks)
    if len(results) != expected:
        print(f"  skipped: {expected - len(results)}")
    return 0 if len(results) == expected and all(code == 0 for _, code in results) else 1


def hardware_requested(args: argparse.Namespace) -> bool:
    return any(
        [
            args.hardware_port is not None,
            args.hardware_runs is not None,
            args.hardware_apps is not None,
            args.hardware_ble_name is not None,
            args.hardware_ble_no_cache,
            args.hardware_serial_only,
            args.hardware_ble_only,
            args.hardware_voice,
        ]
    )


def build_hardware_config(args: argparse.Namespace, parser: argparse.ArgumentParser) -> HardwareGate | None:
    if not hardware_requested(args):
        return None
    runs = args.hardware_runs if args.hardware_runs is not None else 1
    apps = args.hardware_apps if args.hardware_apps is not None else ["touch_stage"]
    ble_name = args.hardware_ble_name if args.hardware_ble_name is not None else "VibeBoard"
    if args.hardware_serial_only and args.hardware_ble_only:
        parser.error("--hardware-serial-only and --hardware-ble-only are mutually exclusive")
    if runs <= 0:
        parser.error("--hardware-runs must be greater than 0")
    if not apps:
        parser.error("--hardware-apps must contain at least one app id")
    if not ble_name:
        parser.error("--hardware-ble-name must not be empty")
    if not args.hardware_ble_only and not args.hardware_port:
        parser.error("--hardware-port is required unless --hardware-ble-only is used")
    return HardwareGate(
        port=args.hardware_port,
        runs=runs,
        apps=tuple(apps),
        ble_name=ble_name,
        ble_no_cache=args.hardware_ble_no_cache,
        serial_only=args.hardware_serial_only,
        ble_only=args.hardware_ble_only,
        verify_voice=args.hardware_voice,
    )


def run_self_test() -> None:
    assert "scripts/runtime_deep_check.py" in python_compile_targets()
    assert any(check.label == "runtime deep check self-test" for check in build_checks(include_swift=False))
    gate = HardwareGate(
        port="/dev/cu.test",
        runs=2,
        apps=("touch_stage", "status_test"),
        ble_name="VibeBoard",
        ble_no_cache=True,
        serial_only=False,
        ble_only=False,
        verify_voice=True,
    )
    check = build_hardware_gate(gate)
    assert check is not None
    assert check.stream_output
    for token in ["scripts/runtime_full_reliability.py", "/dev/cu.test", "--core", "--ble-no-cache", "--verify-voice"]:
        assert token in check.cmd
    assert "touch_stage" in check.cmd
    assert "status_test" in check.cmd
    ble_only = build_hardware_gate(
        HardwareGate(
            port=None,
            runs=1,
            apps=("touch_stage",),
            ble_name="VibeBoard",
            ble_no_cache=False,
            serial_only=False,
            ble_only=True,
            verify_voice=False,
        )
    )
    assert ble_only is not None
    assert "--ble-only" in ble_only.cmd
    assert build_hardware_gate(None) is None
    print("runtime_deep_check self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Run three-pass Huangshan Runtime code checks, with an optional hardware gate.")
    parser.add_argument("--skip-swift", action="store_true", help="Skip iOS Swift tests when SwiftPM cache access is unavailable.")
    parser.add_argument("--hardware-port", help="Run the optional hardware gate with this serial port, for example /dev/cu.usbserial-13220.")
    parser.add_argument("--hardware-runs", type=int, default=None, help="Install/launch iterations for each hardware transport; default is 1 when the hardware gate is enabled.")
    parser.add_argument("--hardware-apps", nargs="+", default=None, help="Runtime app ids to install during the hardware gate; default is touch_stage.")
    parser.add_argument("--hardware-ble-name", default=None, help="BLE local name for the hardware gate; default is VibeBoard.")
    parser.add_argument("--hardware-ble-no-cache", action="store_true", help="Ignore cached BLE peripheral identifier/address during the hardware gate.")
    parser.add_argument("--hardware-serial-only", action="store_true", help="Run only the serial half of the hardware gate.")
    parser.add_argument("--hardware-ble-only", action="store_true", help="Run only the BLE half of the hardware gate.")
    parser.add_argument("--hardware-voice", action="store_true", help="Also run the slower voice capture check during the hardware gate.")
    parser.add_argument("--self-test", action="store_true", help="Run offline checks for this wrapper and exit.")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    hardware_gate = build_hardware_config(args, parser)
    return run_all(include_swift=not args.skip_swift, hardware_gate=hardware_gate)


if __name__ == "__main__":
    raise SystemExit(main())
