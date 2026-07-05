#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import runtime_reliability_checks
from runtime_reliability_checks import (
    format_section_outputs,
    normalize_rgb_color,
    validate_capabilities_json,
    validate_display_json,
    validate_gpio_json,
    validate_power_json,
    validate_rgb_json,
    validate_sensors_json,
    validate_touch_json,
    validate_voice_json,
)
from runtime_transport import INSTALL_CHUNK_BYTES, RuntimeTransportError, validate_flow_roundtrip_output


ACTIVE_PATTERNS = (
    re.compile(r"\[vb_runtime\]\s+active=([a-z][a-z0-9_]*)"),
    re.compile(r"\[vb_runtime\]\s+active app:\s*([a-z][a-z0-9_]*)"),
    re.compile(r"\bactive=([a-z][a-z0-9_]*)"),
)
def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def parse_active_app(text: str) -> str | None:
    hits: list[str] = []
    for pattern in ACTIVE_PATTERNS:
        hits.extend(match.group(1) for match in pattern.finditer(text))
    return hits[-1] if hits else None


def parse_json_line(text: str) -> dict[str, object] | None:
    for line in reversed(text.splitlines()):
        value = line.strip()
        if not (value.startswith("{") and value.endswith("}")):
            continue
        try:
            data = json.loads(value)
        except json.JSONDecodeError:
            continue
        if isinstance(data, dict):
            return data
    return None


def run_cmd(
    cmd: list[str],
    *,
    env_overrides: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env_overrides:
        env.update(env_overrides)
    return subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)


def run_ble(script: Path, *extra: str) -> subprocess.CompletedProcess[str]:
    return run_cmd([str(script), *extra], env_overrides={"PYTHONDONTWRITEBYTECODE": "1"})


def run_voice_bridge_ble(script: Path, args: argparse.Namespace, duration_ms: int, reply_text: str, log_jsonl: Path) -> subprocess.CompletedProcess[str]:
    return run_ble(
        script,
        *ble_common_args(args),
        "--duration-ms",
        str(duration_ms),
        "--reply-text",
        reply_text,
        "--log-jsonl",
        str(log_jsonl),
        "--no-echo",
    )


def run_voice_verify(script: Path, log_jsonl: Path) -> subprocess.CompletedProcess[str]:
    return run_cmd([sys.executable, str(script), "--log-jsonl", str(log_jsonl)])


def argument_validation_error(args: argparse.Namespace) -> str | None:
    if args.runs <= 0:
        return "--runs must be greater than 0"
    if not args.apps:
        return "--apps must contain at least one app id"
    if args.stage_apps is not None and not args.stage_apps:
        return "--stage-apps must contain at least one app id when provided"
    if args.delay < 0:
        return "--delay must be zero or greater"
    if args.chunk_bytes <= 0:
        return "--chunk-bytes must be greater than 0"
    if args.verify_display_brightness < 0 or args.verify_display_brightness > 100:
        return "--verify-display-brightness must be between 0 and 100"
    if not normalize_rgb_color(args.verify_rgb_color):
        return "--verify-rgb-color must be six hex digits"
    if args.voice_duration_ms <= 0:
        return "--voice-duration-ms must be greater than 0"
    if args.verify_voice and not args.voice_reply_text:
        return "--verify-voice requires a non-empty --voice-reply-text"
    if not args.name:
        return "--name must not be empty"
    return None


def run_self_test() -> None:
    runtime_reliability_checks.run_self_test()
    base = argparse.Namespace(
        runs=1,
        apps=["clock_test", "status_test"],
        stage_apps=None,
        delay=0.0,
        chunk_bytes=INSTALL_CHUNK_BYTES,
        verify_display_brightness=70,
        verify_rgb_color="3366ff",
        verify_voice=False,
        voice_duration_ms=800,
        voice_reply_text="ok",
        name="VibeBoard",
    )
    def clone(**changes):
        values = vars(base).copy()
        values.update(changes)
        return argparse.Namespace(**values)

    assert argument_validation_error(base) is None
    bad_runs = clone(runs=0)
    assert "--runs" in (argument_validation_error(bad_runs) or "")
    no_apps = clone(apps=[])
    assert "--apps" in (argument_validation_error(no_apps) or "")
    bad_stage_apps = clone(stage_apps=[])
    assert "--stage-apps" in (argument_validation_error(bad_stage_apps) or "")
    bad_chunk = clone(chunk_bytes=0)
    assert "--chunk-bytes" in (argument_validation_error(bad_chunk) or "")
    bad_brightness = clone(verify_display_brightness=-1)
    assert "--verify-display-brightness" in (argument_validation_error(bad_brightness) or "")
    bad_rgb = clone(verify_rgb_color="12345g")
    assert "--verify-rgb-color" in (argument_validation_error(bad_rgb) or "")
    bad_voice = clone(verify_voice=True, voice_reply_text="")
    assert "--verify-voice" in (argument_validation_error(bad_voice) or "")
    bad_name = clone(name="")
    assert "--name" in (argument_validation_error(bad_name) or "")
    print("runtime_reliability_ble self-test ok")


def print_step_output(prefix: str, proc: subprocess.CompletedProcess[str]) -> None:
    print(prefix)
    output = proc.stdout.rstrip()
    if output:
        print(output)


def print_text_output(prefix: str, text: str) -> None:
    print(prefix)
    output = text.rstrip()
    if output:
        print(output)


def ensure_package_dir(root: Path, app_id: str) -> Path:
    package_dir = root / app_id
    if not package_dir.is_dir():
        fail(f"runtime app package does not exist: {package_dir}")
    return package_dir


def query_active_app(script: Path, args: argparse.Namespace) -> tuple[str | None, str, int]:
    proc = run_ble(script, *ble_common_args(args), "--status-only", "--no-echo")
    output = proc.stdout
    return parse_active_app(output), output, proc.returncode


def query_json_args(script: Path, args: argparse.Namespace, *extra: str) -> tuple[dict[str, object] | None, str, int]:
    proc = run_ble(script, *ble_common_args(args), *extra, "--no-echo")
    return parse_json_line(proc.stdout), proc.stdout, proc.returncode


def query_json_command(script: Path, args: argparse.Namespace, option: str) -> tuple[dict[str, object] | None, str, int]:
    return query_json_args(script, args, option)


def ble_common_args(args: argparse.Namespace) -> list[str]:
    extra: list[str] = []
    if args.name:
        extra.extend(["--name", args.name])
    if args.no_cache:
        extra.append("--no-cache")
    return extra


def pick_stage_app(apps: list[str], stage_apps: list[str] | None, active_app: str, run_index: int) -> str:
    pool = stage_apps if stage_apps else apps
    if not pool:
        return active_app
    for offset in range(len(pool)):
        candidate = pool[(run_index + offset) % len(pool)]
        if candidate != active_app or len(pool) == 1:
            return candidate
    return active_app


def run_flow_validation(script: Path, args: argparse.Namespace, channel: str, payload: str) -> tuple[int, str]:
    sequence = int(time.time()) & 0xFFFFFFFF
    proc = run_ble(
        script,
        *ble_common_args(args),
        "--flow-persist-text",
        payload,
        "--flow-channel",
        channel,
        "--flow-seq",
        str(sequence),
        "--no-echo",
    )
    if proc.returncode != 0:
        return proc.returncode, proc.stdout

    try:
        validate_flow_roundtrip_output(proc.stdout, channel, sequence, payload, expected_total=1, expected_retained=1)
    except RuntimeTransportError as exc:
        return 1, proc.stdout.rstrip() + f"\n[runtime-reliability-ble] validation error: {exc}\n"
    return 0, proc.stdout

def run_display_validation(script: Path, args: argparse.Namespace, brightness: int) -> tuple[int, str]:
    if brightness < 0 or brightness > 100:
        return 1, f"[runtime-reliability-ble] validation error: invalid display brightness {brightness!r}"

    sections: list[tuple[str, str]] = []
    before_data, before_output, before_code = query_json_args(script, args, "--display-only")
    sections.append(("display-before", before_output))
    if before_code != 0:
        return before_code, format_section_outputs(sections)
    error = validate_display_json(before_data)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    original_brightness = before_data.get("brightness") if before_data else None
    set_data, set_output, set_code = query_json_args(script, args, "--display-brightness", str(brightness))
    sections.append(("display-set", set_output))
    if set_code != 0:
        return set_code, format_section_outputs(sections)
    error = validate_display_json(set_data, expected_brightness=brightness)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    after_data, after_output, after_code = query_json_args(script, args, "--display-only")
    sections.append(("display-after", after_output))
    if after_code != 0:
        return after_code, format_section_outputs(sections)
    error = validate_display_json(after_data, expected_brightness=brightness)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    if isinstance(original_brightness, int) and original_brightness != brightness:
        restore_data, restore_output, restore_code = query_json_args(script, args, "--display-brightness", str(original_brightness))
        sections.append(("display-restore", restore_output))
        if restore_code != 0:
            return restore_code, format_section_outputs(sections)
        error = validate_display_json(restore_data, expected_brightness=original_brightness)
        if error:
            return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    return 0, format_section_outputs(sections)


def run_rgb_validation(script: Path, args: argparse.Namespace, color: str) -> tuple[int, str]:
    target_color = normalize_rgb_color(color)
    if not target_color:
        return 1, f"[runtime-reliability-ble] validation error: invalid rgb color {color!r}"

    sections: list[tuple[str, str]] = []
    before_data, before_output, before_code = query_json_args(script, args, "--rgb-only")
    sections.append(("rgb-before", before_output))
    if before_code != 0:
        return before_code, format_section_outputs(sections)
    error = validate_rgb_json(before_data)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    original_color = normalize_rgb_color(before_data.get("color") if before_data else None)
    set_data, set_output, set_code = query_json_args(script, args, "--rgb-color", target_color)
    sections.append(("rgb-set", set_output))
    if set_code != 0:
        return set_code, format_section_outputs(sections)
    error = validate_rgb_json(set_data, expected_color=target_color)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    after_data, after_output, after_code = query_json_args(script, args, "--rgb-only")
    sections.append(("rgb-after", after_output))
    if after_code != 0:
        return after_code, format_section_outputs(sections)
    error = validate_rgb_json(after_data, expected_color=target_color)
    if error:
        return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    if original_color and original_color != target_color:
        restore_data, restore_output, restore_code = query_json_args(script, args, "--rgb-color", original_color)
        sections.append(("rgb-restore", restore_output))
        if restore_code != 0:
            return restore_code, format_section_outputs(sections)
        error = validate_rgb_json(restore_data, expected_color=original_color)
        if error:
            return 1, format_section_outputs(sections) + f"\n[runtime-reliability-ble] validation error: {error}"

    return 0, format_section_outputs(sections)


def run_json_validation(
    script: Path,
    args: argparse.Namespace,
    option: str,
    validator,
) -> tuple[int, str]:
    data, output, returncode = query_json_command(script, args, option)
    if returncode != 0:
        return returncode, output

    error = validator(data)
    if not error:
        return 0, output

    message = output.rstrip()
    if message:
        message += "\n"
    message += f"[runtime-reliability-ble] validation error: {error}\n"
    return 1, message


def main() -> int:
    parser = argparse.ArgumentParser(description="Run repeated Huangshan Runtime BLE app installs.")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--apps", nargs="+", default=["clock_test", "status_test"])
    parser.add_argument("--stage-apps", nargs="+", help="Apps used for --verify-abort staging. Defaults to --apps.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent / "runtime_apps")
    parser.add_argument("--delay", type=float, default=0.8)
    parser.add_argument("--chunk-bytes", type=int, default=INSTALL_CHUNK_BYTES)
    parser.add_argument(
        "--verify-capabilities",
        action="store_true",
        help="After each install, read capabilities JSON and validate the Runtime API advertisement.",
    )
    parser.add_argument(
        "--verify-sensors",
        action="store_true",
        help="After each install, read sensors JSON and validate the built-in sensor snapshot API.",
    )
    parser.add_argument(
        "--verify-power",
        action="store_true",
        help="After each install, read power JSON and validate the battery/charger snapshot API.",
    )
    parser.add_argument(
        "--verify-touch",
        action="store_true",
        help="After each install, read touch JSON and validate the Runtime touch snapshot API.",
    )
    parser.add_argument(
        "--verify-gpio",
        action="store_true",
        help="After each install, read GPIO JSON and validate the KEY1/KEY2 whitelist API.",
    )
    parser.add_argument(
        "--verify-display",
        action="store_true",
        help="After each install, read/set display brightness JSON and validate the Runtime display API.",
    )
    parser.add_argument("--verify-display-brightness", type=int, default=70, help="Display brightness used by --verify-display.")
    parser.add_argument(
        "--verify-flow",
        action="store_true",
        help="After each install, send/status/clear one info flow frame and validate the Runtime flow API.",
    )
    parser.add_argument("--flow-text", default="flow-regression-ok", help="Info flow payload text used by --verify-flow.")
    parser.add_argument("--flow-channel", default="pc.flow", help="Info flow channel used by --verify-flow.")
    parser.add_argument(
        "--verify-rgb",
        action="store_true",
        help="After each install, set and read RGB LED JSON and validate the Runtime RGB API.",
    )
    parser.add_argument("--verify-rgb-color", default="3366ff", help="RGB LED color used by --verify-rgb.")
    parser.add_argument(
        "--verify-voice-status",
        action="store_true",
        help="After each install, read voice bridge status JSON without starting capture.",
    )
    parser.add_argument(
        "--verify-voice",
        action="store_true",
        help="After each install, run one BLE voice capture + reply round and verify the generated evidence log.",
    )
    parser.add_argument("--voice-duration-ms", type=int, default=800, help="Voice capture duration for --verify-voice.")
    parser.add_argument("--voice-reply-text", default="BLE语音回写验证 OK", help="Reply text sent back to the board during --verify-voice.")
    parser.add_argument("--voice-log-jsonl", type=Path, default=Path(__file__).resolve().parent.parent / "captures" / "voice_runtime_reliability_ble.jsonl", help="Evidence log path used by --verify-voice.")
    parser.add_argument(
        "--verify-abort",
        action="store_true",
        help="After each successful install, stage another app, confirm active app stays the same, then abort the staged install.",
    )
    parser.add_argument("--name", default="VibeBoard", help="BLE local name to scan for")
    parser.add_argument("--no-cache", action="store_true", help="Ignore cached BLE peripheral identifier/address")
    parser.add_argument("--self-test", action="store_true", help="Run offline argument validation checks and exit.")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0
    validation_error = argument_validation_error(args)
    if validation_error:
        parser.error(validation_error)

    script = Path(__file__).resolve().parent / "runtime_install_ble.sh"
    voice_script = Path(__file__).resolve().parent / "voice_bridge_ble.sh"
    voice_verify_script = Path(__file__).resolve().parent / "voice_terminal_verify.py"
    if not script.is_file():
        fail(f"BLE install wrapper not found: {script}")

    results: list[tuple[int, str, str, int]] = []
    capability_checks = 0
    capability_successes = 0
    sensor_checks = 0
    sensor_successes = 0
    power_checks = 0
    power_successes = 0
    touch_checks = 0
    touch_successes = 0
    gpio_checks = 0
    gpio_successes = 0
    display_checks = 0
    display_successes = 0
    flow_checks = 0
    flow_successes = 0
    rgb_checks = 0
    rgb_successes = 0
    voice_status_checks = 0
    voice_status_successes = 0
    voice_checks = 0
    voice_successes = 0
    abort_checks = 0
    abort_successes = 0

    for index in range(args.runs):
        app_id = args.apps[index % len(args.apps)]
        package_dir = ensure_package_dir(args.root, app_id)
        proc = run_ble(
            script,
            *ble_common_args(args),
            "--package-dir",
            str(package_dir),
            "--app-id",
            app_id,
            "--chunk-bytes",
            str(args.chunk_bytes),
            "--no-echo",
        )
        print_step_output(f"[runtime-reliability] install {index + 1}/{args.runs}: {app_id}", proc)
        results.append((index + 1, app_id, "install", proc.returncode))
        if proc.returncode != 0:
            break

        active_app, status_output, status_code = query_active_app(script, args)
        print_step_output(
            f"[runtime-reliability] status after install {index + 1}/{args.runs}",
            subprocess.CompletedProcess([], status_code, status_output, None),
        )
        if status_code != 0 or active_app != app_id:
            results.append((index + 1, app_id, "status", 1))
            break

        if args.verify_capabilities:
            capability_checks += 1
            cap_code, cap_output = run_json_validation(
                script,
                args,
                "--capabilities-only",
                lambda data: validate_capabilities_json(data, require_gpio=args.verify_gpio, require_ble=True),
            )
            print_text_output(
                f"[runtime-reliability-ble] capabilities after install {index + 1}/{args.runs}",
                cap_output,
            )
            results.append((index + 1, app_id, "capabilities", cap_code))
            if cap_code != 0:
                break
            capability_successes += 1

        if args.verify_sensors:
            sensor_checks += 1
            sensor_code, sensor_output = run_json_validation(
                script,
                args,
                "--sensors-only",
                validate_sensors_json,
            )
            print_text_output(
                f"[runtime-reliability-ble] sensors after install {index + 1}/{args.runs}",
                sensor_output,
            )
            results.append((index + 1, app_id, "sensors", sensor_code))
            if sensor_code != 0:
                break
            sensor_successes += 1

        if args.verify_power:
            power_checks += 1
            power_code, power_output = run_json_validation(
                script,
                args,
                "--power-only",
                validate_power_json,
            )
            print_text_output(
                f"[runtime-reliability-ble] power after install {index + 1}/{args.runs}",
                power_output,
            )
            results.append((index + 1, app_id, "power", power_code))
            if power_code != 0:
                break
            power_successes += 1

        if args.verify_touch:
            touch_checks += 1
            touch_code, touch_output = run_json_validation(
                script,
                args,
                "--touch-only",
                validate_touch_json,
            )
            print_text_output(
                f"[runtime-reliability-ble] touch after install {index + 1}/{args.runs}",
                touch_output,
            )
            results.append((index + 1, app_id, "touch", touch_code))
            if touch_code != 0:
                break
            touch_successes += 1

        if args.verify_gpio:
            gpio_checks += 1
            gpio_code, gpio_output = run_json_validation(
                script,
                args,
                "--gpio-only",
                validate_gpio_json,
            )
            print_text_output(
                f"[runtime-reliability-ble] gpio after install {index + 1}/{args.runs}",
                gpio_output,
            )
            results.append((index + 1, app_id, "gpio", gpio_code))
            if gpio_code != 0:
                break
            gpio_successes += 1

        if args.verify_display:
            display_checks += 1
            display_code, display_output = run_display_validation(
                script,
                args,
                args.verify_display_brightness,
            )
            print_text_output(
                f"[runtime-reliability-ble] display after install {index + 1}/{args.runs}",
                display_output,
            )
            results.append((index + 1, app_id, "display", display_code))
            if display_code != 0:
                break
            display_successes += 1

        if args.verify_flow:
            flow_checks += 1
            flow_code, flow_output = run_flow_validation(
                script,
                args,
                args.flow_channel,
                args.flow_text,
            )
            print_text_output(
                f"[runtime-reliability-ble] flow after install {index + 1}/{args.runs}",
                flow_output,
            )
            results.append((index + 1, app_id, "flow", flow_code))
            if flow_code != 0:
                break
            flow_successes += 1

        if args.verify_rgb:
            rgb_checks += 1
            rgb_code, rgb_output = run_rgb_validation(
                script,
                args,
                args.verify_rgb_color,
            )
            print_text_output(
                f"[runtime-reliability-ble] rgb after install {index + 1}/{args.runs}",
                rgb_output,
            )
            results.append((index + 1, app_id, "rgb", rgb_code))
            if rgb_code != 0:
                break
            rgb_successes += 1


        if args.verify_voice_status:
            voice_status_checks += 1
            voice_status_code, voice_status_output = run_json_validation(
                script,
                args,
                "--voice-only",
                validate_voice_json,
            )
            print_text_output(
                f"[runtime-reliability-ble] voice status after install {index + 1}/{args.runs}",
                voice_status_output,
            )
            results.append((index + 1, app_id, "voice-status", voice_status_code))
            if voice_status_code != 0:
                break
            voice_status_successes += 1

        if args.verify_voice:
            voice_checks += 1
            voice_proc = run_voice_bridge_ble(
                voice_script,
                args,
                args.voice_duration_ms,
                args.voice_reply_text,
                args.voice_log_jsonl,
            )
            print_step_output(
                f"[runtime-reliability-ble] voice after install {index + 1}/{args.runs}",
                voice_proc,
            )
            results.append((index + 1, app_id, "voice", voice_proc.returncode))
            if voice_proc.returncode != 0:
                break

            voice_verify = run_voice_verify(voice_verify_script, args.voice_log_jsonl)
            print_step_output(
                f"[runtime-reliability-ble] voice evidence after install {index + 1}/{args.runs}",
                voice_verify,
            )
            results.append((index + 1, app_id, "voice-evidence", voice_verify.returncode))
            if voice_verify.returncode != 0:
                break
            voice_successes += 1

        if args.verify_abort:
            staged_app = pick_stage_app(args.apps, args.stage_apps, app_id, index)
            staged_dir = ensure_package_dir(args.root, staged_app)
            staged_proc = run_ble(
                script,
                *ble_common_args(args),
                "--package-dir",
                str(staged_dir),
                "--app-id",
                staged_app,
                "--chunk-bytes",
                str(args.chunk_bytes),
                "--stop-before-end",
                "--no-echo",
            )
            print_step_output(f"[runtime-reliability-ble] stage {index + 1}/{args.runs}: {staged_app}", staged_proc)
            abort_checks += 1
            results.append((index + 1, staged_app, "stage", staged_proc.returncode))
            if staged_proc.returncode != 0:
                break

            staged_active, staged_status_output, staged_status_code = query_active_app(script, args)
            print_step_output(
                f"[runtime-reliability-ble] status after staging {index + 1}/{args.runs}",
                subprocess.CompletedProcess([], staged_status_code, staged_status_output, None),
            )
            if staged_status_code != 0 or staged_active != app_id:
                results.append((index + 1, staged_app, "stage-status", 1))
                break

            abort_proc = run_ble(script, *ble_common_args(args), "--abort-only", "--app-id", staged_app, "--no-echo")
            print_step_output(f"[runtime-reliability-ble] abort {index + 1}/{args.runs}: {staged_app}", abort_proc)
            results.append((index + 1, staged_app, "abort", abort_proc.returncode))
            if abort_proc.returncode != 0:
                break

            final_active, final_status_output, final_status_code = query_active_app(script, args)
            print_step_output(
                f"[runtime-reliability-ble] status after abort {index + 1}/{args.runs}",
                subprocess.CompletedProcess([], final_status_code, final_status_output, None),
            )
            if final_status_code != 0 or final_active != app_id:
                results.append((index + 1, staged_app, "abort-status", 1))
                break
            abort_successes += 1

        time.sleep(args.delay)

    install_successes = sum(1 for _, _, kind, code in results if kind == "install" and code == 0)
    install_total = sum(1 for _, _, kind, _ in results if kind == "install")
    install_rate = (install_successes / install_total * 100.0) if install_total else 0.0
    print()
    print(f"runtime BLE install success: {install_successes}/{install_total} ({install_rate:.1f}%)")
    if args.verify_capabilities:
        capability_rate = (capability_successes / capability_checks * 100.0) if capability_checks else 0.0
        print(f"runtime BLE capabilities checks: {capability_successes}/{capability_checks} ({capability_rate:.1f}%)")
    if args.verify_sensors:
        sensor_rate = (sensor_successes / sensor_checks * 100.0) if sensor_checks else 0.0
        print(f"runtime BLE sensors checks: {sensor_successes}/{sensor_checks} ({sensor_rate:.1f}%)")
    if args.verify_power:
        power_rate = (power_successes / power_checks * 100.0) if power_checks else 0.0
        print(f"runtime BLE power checks: {power_successes}/{power_checks} ({power_rate:.1f}%)")
    if args.verify_touch:
        touch_rate = (touch_successes / touch_checks * 100.0) if touch_checks else 0.0
        print(f"runtime BLE touch checks: {touch_successes}/{touch_checks} ({touch_rate:.1f}%)")
    if args.verify_gpio:
        gpio_rate = (gpio_successes / gpio_checks * 100.0) if gpio_checks else 0.0
        print(f"runtime BLE gpio checks: {gpio_successes}/{gpio_checks} ({gpio_rate:.1f}%)")
    if args.verify_display:
        display_rate = (display_successes / display_checks * 100.0) if display_checks else 0.0
        print(f"runtime BLE display checks: {display_successes}/{display_checks} ({display_rate:.1f}%)")
    if args.verify_flow:
        flow_rate = (flow_successes / flow_checks * 100.0) if flow_checks else 0.0
        print(f"runtime BLE flow checks: {flow_successes}/{flow_checks} ({flow_rate:.1f}%)")
    if args.verify_rgb:
        rgb_rate = (rgb_successes / rgb_checks * 100.0) if rgb_checks else 0.0
        print(f"runtime BLE rgb checks: {rgb_successes}/{rgb_checks} ({rgb_rate:.1f}%)")
    if args.verify_voice_status:
        voice_status_rate = (voice_status_successes / voice_status_checks * 100.0) if voice_status_checks else 0.0
        print(f"runtime BLE voice status checks: {voice_status_successes}/{voice_status_checks} ({voice_status_rate:.1f}%)")
    if args.verify_voice:
        voice_rate = (voice_successes / voice_checks * 100.0) if voice_checks else 0.0
        print(f"runtime BLE voice checks: {voice_successes}/{voice_checks} ({voice_rate:.1f}%)")
    if args.verify_abort:
        abort_rate = (abort_successes / abort_checks * 100.0) if abort_checks else 0.0
        print(f"runtime BLE abort checks: {abort_successes}/{abort_checks} ({abort_rate:.1f}%)")
    for run, app_id, kind, code in results:
        print(f"  run {run}: {kind} {app_id} -> {'ok' if code == 0 else 'fail'}")
    if any(code != 0 for _, _, _, code in results):
        return 1
    if install_total != args.runs or install_successes != install_total:
        return 1
    if args.verify_capabilities and capability_successes != capability_checks:
        return 1
    if args.verify_sensors and sensor_successes != sensor_checks:
        return 1
    if args.verify_power and power_successes != power_checks:
        return 1
    if args.verify_touch and touch_successes != touch_checks:
        return 1
    if args.verify_gpio and gpio_successes != gpio_checks:
        return 1
    if args.verify_display and display_successes != display_checks:
        return 1
    if args.verify_flow and flow_successes != flow_checks:
        return 1
    if args.verify_rgb and rgb_successes != rgb_checks:
        return 1
    if args.verify_voice_status and voice_status_successes != voice_status_checks:
        return 1
    if args.verify_voice and voice_successes != voice_checks:
        return 1
    if args.verify_abort and abort_successes != abort_checks:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
