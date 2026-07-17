#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from runtime_package import RuntimePackageError, build_install_commands, fail, load_package_from_dir, load_package_from_json, safe_package_id
from runtime_transport import (
    INSTALL_CHUNK_BYTES,
    MAX_INSTALL_CHUNK_BYTES,
    RuntimeTransportError,
    SerialTransport,
    SerialTransportOptions,
    validate_flow_roundtrip_output,
)



def validate_flow_output(output: str, channel: str, sequence: int, payload: str, expected_total: int = 1) -> None:
    try:
        validate_flow_roundtrip_output(output, channel, sequence, payload, expected_total)
    except RuntimeTransportError as exc:
        fail("Info flow validation failed: " + str(exc) + "\nLast output:\n" + output[-2000:])


def serial_transport_options(args: argparse.Namespace) -> SerialTransportOptions:
    return SerialTransportOptions(
        port=args.port,
        baud=args.baud,
        command_wait=args.command_wait,
        final_wait=args.final_wait,
        ready_timeout=args.ready_timeout,
        echo=not args.no_echo,
        write_chunk_pause=args.write_chunk_pause,
    )


def validate_flow_persistence(args: argparse.Namespace, sequence: int, payload: str) -> str:
    output = ""
    with SerialTransport(serial_transport_options(args)) as transport:
        output += transport.flow_clear()
        output += transport.flow_send(args.flow_channel, sequence, payload)
    with SerialTransport(serial_transport_options(args)) as transport:
        output += transport.flow_status()
    return output

def has_standard_transport_command(args: argparse.Namespace) -> bool:
    return any((
        args.status_only,
        args.capabilities_only,
        args.sensors_only,
        args.power_only,
        args.display_only,
        args.display_brightness is not None,
        args.gpio_only,
        args.touch_only,
        args.rgb_only,
        args.rgb_color is not None,
        args.voice_only,
        args.audio_only,
        args.app_only,
        args.apps_only,
        args.launch_app is not None,
        args.stop_app,
        args.delete_app is not None,
        args.flow_status_only,
        args.flow_clear_only,
        args.flow_send_text is not None,
        args.flow_roundtrip_text is not None,
        args.abort_only,
        args.raw_command is not None,
    ))


def require_app_id_for_abort(args: argparse.Namespace) -> None:
    if args.abort_only and not args.app_id:
        fail("--abort-only requires --app-id")


def run_self_test() -> None:
    base = argparse.Namespace(
        status_only=False,
        capabilities_only=False,
        sensors_only=False,
        power_only=False,
        display_only=False,
        display_brightness=None,
        gpio_only=False,
        touch_only=False,
        rgb_only=False,
        rgb_color=None,
        voice_only=False,
        audio_only=False,
        app_only=False,
        apps_only=False,
        launch_app=None,
        stop_app=False,
        delete_app=None,
        flow_status_only=False,
        flow_clear_only=False,
        flow_send_text=None,
        flow_roundtrip_text=None,
        raw_command=None,
        abort_only=False,
        app_id=None,
    )
    status_args = argparse.Namespace(**vars(base))
    status_args.status_only = True
    assert has_standard_transport_command(status_args)
    package_args = argparse.Namespace(**vars(base))
    assert not has_standard_transport_command(package_args)
    raw_args = argparse.Namespace(**vars(base))
    raw_args.raw_command = "vb_runtime_status"
    assert has_standard_transport_command(raw_args)
    abort_args = argparse.Namespace(**vars(base))
    abort_args.abort_only = True
    original_raise_errors = getattr(__import__("runtime_package")._ERROR_MODE, "raise_errors", False)
    __import__("runtime_package")._ERROR_MODE.raise_errors = True
    try:
        try:
            require_app_id_for_abort(abort_args)
        except RuntimePackageError:
            pass
        else:
            raise AssertionError("missing app id for abort should fail")
    finally:
        __import__("runtime_package")._ERROR_MODE.raise_errors = original_raise_errors
    abort_args.app_id = "clock_test"
    require_app_id_for_abort(abort_args)
    assert has_standard_transport_command(abort_args)
    print("runtime_install_serial self-test ok")


def run_standard_transport_command(args: argparse.Namespace) -> int | None:
    if not has_standard_transport_command(args):
        return None
    try:
        with SerialTransport(serial_transport_options(args)) as transport:
            if args.status_only:
                print(transport.status().strip())
            elif args.capabilities_only:
                print(transport.capabilities())
            elif args.sensors_only:
                print(transport.sensors())
            elif args.power_only:
                print(transport.power())
            elif args.display_only:
                print(transport.display())
            elif args.display_brightness is not None:
                print(transport.display(args.display_brightness))
            elif args.gpio_only:
                print(transport.gpio())
            elif args.touch_only:
                print(transport.touch())
            elif args.rgb_only:
                print(transport.rgb())
            elif args.rgb_color is not None:
                print(transport.rgb(args.rgb_color))
            elif args.voice_only:
                print(transport.voice())
            elif args.audio_only:
                print(transport.audio())
            elif args.app_only:
                print(transport.app_status())
            elif args.apps_only:
                print(transport.apps())
            elif args.launch_app is not None:
                print(transport.launch_app(safe_package_id(args.launch_app)).strip())
            elif args.stop_app:
                print(transport.stop_app().strip())
            elif args.delete_app is not None:
                print(transport.delete_app(safe_package_id(args.delete_app)).strip())
            elif args.flow_status_only:
                print(transport.flow_status().strip())
            elif args.flow_clear_only:
                print(transport.flow_clear().strip())
            elif args.flow_send_text is not None:
                sequence = args.flow_seq if args.flow_seq is not None else (int(time.time()) & 0xFFFFFFFF)
                print(transport.flow_send(args.flow_channel, sequence, args.flow_send_text).strip())
            elif args.flow_roundtrip_text is not None:
                sequence = args.flow_seq if args.flow_seq is not None else (int(time.time()) & 0xFFFFFFFF)
                output = "".join((
                    transport.flow_clear(),
                    transport.flow_send(args.flow_channel, sequence, args.flow_roundtrip_text),
                    transport.flow_status(),
                ))
                validate_flow_output(output, args.flow_channel, sequence, args.flow_roundtrip_text)
                print(output.strip())
            elif args.abort_only:
                print(transport.abort_install(safe_package_id(args.app_id)).strip())
            elif args.raw_command is not None:
                print(transport.command(args.raw_command, wait=max(args.final_wait, args.command_wait)).strip())
    except Exception as exc:
        fail(str(exc))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a Huangshan Runtime app package over serial MSH.")
    parser.add_argument("port", nargs="?", help="Serial port, for example /dev/cu.usbserial-13220")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--package-dir", type=Path, help="Directory containing manifest.json/app.info, main.lua, and assets")
    source.add_argument("--package-json", type=Path, help="VibeBoard runtime package JSON with app.packageId and files")
    source.add_argument("--status-only", action="store_true", help="Read Runtime serial status and exit")
    source.add_argument("--capabilities-only", action="store_true", help="Read Runtime capability JSON and exit")
    source.add_argument("--sensors-only", action="store_true", help="Read built-in sensor JSON and exit")
    source.add_argument("--power-only", action="store_true", help="Read Runtime power JSON and exit")
    source.add_argument("--display-only", action="store_true", help="Read Runtime display JSON and exit")
    source.add_argument("--gpio-only", action="store_true", help="Read Runtime GPIO whitelist JSON and exit")
    source.add_argument("--touch-only", action="store_true", help="Read Runtime touch JSON and exit")
    source.add_argument("--rgb-only", action="store_true", help="Read Runtime RGB LED JSON and exit")
    source.add_argument("--rgb-color", help="Set Runtime RGB LED color, e.g. red, off, or 3366ff")
    source.add_argument("--display-brightness", type=int, help="Set Runtime display brightness, 0-100")
    source.add_argument("--voice-only", action="store_true", help="Read Runtime voice bridge JSON and exit")
    source.add_argument("--audio-only", action="store_true", help="Read Runtime audio playback JSON and exit")
    source.add_argument("--app-only", action="store_true", help="Read Runtime app manager status JSON and exit")
    source.add_argument("--apps-only", action="store_true", help="Read Runtime installed app list JSON and exit")
    source.add_argument("--launch-app", help="Launch an installed Runtime app by app id")
    source.add_argument("--stop-app", action="store_true", help="Stop the active Runtime app")
    source.add_argument("--delete-app", help="Delete a stopped Runtime app by app id")
    source.add_argument("--flow-status-only", action="store_true", help="Read Runtime info flow status and exit")
    source.add_argument("--flow-clear-only", action="store_true", help="Clear Runtime info flow history and exit")
    source.add_argument("--flow-send-text", help="Send one Runtime info flow UTF-8 text payload")
    source.add_argument("--flow-roundtrip-text", help="Clear, send, and read back one info flow frame in the same serial session")
    source.add_argument("--flow-persist-text", help="Send and validate one info flow frame restored after reopening serial")
    source.add_argument("--abort-only", action="store_true", help="Abort and clean staging for --app-id")
    source.add_argument("--raw-command", help="Send one diagnostic MSH command through SerialTransport and print the raw response")
    parser.add_argument("--app-id", help="Override package id")
    parser.add_argument("--flow-channel", default="pc.flow", help="Flow channel used by --flow-send-text")
    parser.add_argument("--flow-seq", type=int, help="Flow sequence used by --flow-send-text; defaults to current time")
    parser.add_argument("--baud", type=int, default=1_000_000)
    parser.add_argument("--chunk-bytes", type=int, default=INSTALL_CHUNK_BYTES)
    parser.add_argument("--command-wait", type=float, default=0.22)
    parser.add_argument("--final-wait", type=float, default=2.0)
    parser.add_argument("--ready-timeout", type=float, default=24.0)
    parser.add_argument("--write-chunk-pause", type=float, default=0.012,
                        help="Pause between 24-byte UART writes; lower only with per-command ACK checks enabled")
    parser.add_argument("--stop-before-end", action="store_true", help="Write package chunks but do not commit; used to verify staging safety")
    parser.add_argument("--no-echo", action="store_true")
    parser.add_argument("--self-test", action="store_true", help="Run offline CLI routing checks and exit")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0
    if not any((
        args.package_dir,
        args.package_json,
        args.status_only,
        args.capabilities_only,
        args.sensors_only,
        args.power_only,
        args.display_only,
        args.gpio_only,
        args.touch_only,
        args.rgb_only,
        args.rgb_color is not None,
        args.display_brightness is not None,
        args.voice_only,
        args.audio_only,
        args.app_only,
        args.apps_only,
        args.launch_app is not None,
        args.stop_app,
        args.delete_app is not None,
        args.flow_status_only,
        args.flow_clear_only,
        args.flow_send_text is not None,
        args.flow_roundtrip_text is not None,
        args.flow_persist_text is not None,
        args.abort_only,
        args.raw_command is not None,
    )):
        parser.error("use one Runtime command, --package-dir, --package-json, --abort-only, or --self-test")
    if not args.port:
        parser.error("port is required unless --self-test is used")
    if args.abort_only:
        require_app_id_for_abort(args)

    chunk_bytes = max(16, min(args.chunk_bytes, MAX_INSTALL_CHUNK_BYTES))
    package_id = ""
    files: dict[str, bytes] = {}
    commands: list[str] = []
    if args.package_dir:
        package_id, files = load_package_from_dir(args.package_dir, args.app_id)
    elif args.package_json:
        package_id, files = load_package_from_json(args.package_json, args.app_id)
    elif args.abort_only:
        package_id = safe_package_id(args.app_id)
    elif args.stop_before_end:
        parser.error("--stop-before-end requires --package-dir or --package-json")
    if package_id and files:
        commands = build_install_commands(package_id, files, chunk_bytes)
        if args.stop_before_end:
            commands = commands[:-1]


    if args.flow_persist_text is not None:
        sequence = args.flow_seq if args.flow_seq is not None else (int(time.time()) & 0xFFFFFFFF)
        output = validate_flow_persistence(args, sequence, args.flow_persist_text)
        validate_flow_output(output, args.flow_channel, sequence, args.flow_persist_text)
        print(output.strip())
        return 0

    handled = run_standard_transport_command(args)
    if handled is not None:
        return handled

    if commands:
        started = time.time()

        def on_progress(command: str, index: int, total: int) -> None:
            if not args.no_echo:
                return
            if command.startswith("vb_runtime_install_begin "):
                print("install_begin", flush=True)
            elif command.startswith("vb_runtime_install_end "):
                print("install_end", flush=True)
            elif command.startswith("vb_runtime_install_file ") and (index == 2 or index == total or (index % 25) == 0):
                print(f"install_file {index}/{total}", flush=True)

        try:
            with SerialTransport(serial_transport_options(args)) as transport:
                output = transport.install_package(
                    package_id,
                    files,
                    chunk_bytes=chunk_bytes,
                    progress=on_progress,
                    commit=not args.stop_before_end,
                )
        except Exception as exc:
            fail(str(exc))
        elapsed = time.time() - started
        if args.stop_before_end:
            if "[vb_runtime] install complete:" in output:
                print("Unexpected install completion while --stop-before-end was set", file=sys.stderr)
                print(output[-4000:], file=sys.stderr)
                return 1
            print(f"staged {package_id}: {len(files)} files, {len(commands)} commands, chunk={chunk_bytes} bytes, {elapsed:.1f}s")
            return 0
        expected = f"[vb_runtime] install complete: {package_id}"
        if expected not in output:
            print(f"Did not see final install line in last response; expected {expected!r}", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            return 1
        if f"[vb_runtime] active={package_id}" not in output and f"[vb_runtime] active app: {package_id}" not in output:
            print(f"Install completed but active app was not confirmed as {package_id}", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            return 1
        print(f"installed {package_id}: {len(files)} files, {len(commands)} commands, chunk={chunk_bytes} bytes, {elapsed:.1f}s")
        return 0

    if args.abort_only:
        try:
            with SerialTransport(serial_transport_options(args)) as transport:
                output = transport.abort_install(package_id)
        except Exception as exc:
            fail(str(exc))
        print(output.strip())
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
