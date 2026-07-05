#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import time
from pathlib import Path

from runtime_package import RuntimePackageError, build_install_commands, fail, load_package_from_dir, load_package_from_json, safe_package_id
from runtime_transport import (
    BLETransport,
    BLETransportOptions,
    DEFAULT_BLE_CACHE_PATH,
    DEFAULT_DEVICE_NAME,
    INSTALL_CHUNK_BYTES,
    MAX_INSTALL_CHUNK_BYTES,
    RuntimeTransportError,
    scan_ble_devices,
    validate_flow_roundtrip_output,
)


async def scan_devices(name: str, timeout: float) -> int:
    devices = await scan_ble_devices(name, timeout)
    matches = [device for device in devices if device.name == name]
    for device in devices:
        print(f"{device.address}	{device.name}")
    print(f"matched {len(matches)} device(s) named {name!r}")
    return 0


def validate_flow_output(status: str, channel: str, sequence: int, payload: str, expected_total: int = 1) -> None:
    try:
        validate_flow_roundtrip_output(status, channel, sequence, payload, expected_total)
    except RuntimeTransportError as exc:
        fail("Info flow validation failed: " + str(exc) + "\nLast output:\n" + status[-2000:])


def run_ble_task(coro) -> int:
    try:
        result = asyncio.run(coro)
    except RuntimeTransportError as exc:
        fail(str(exc))
    except Exception as exc:
        name = type(exc).__name__
        module = type(exc).__module__
        if name.startswith("Bleak") or module.startswith("bleak"):
            fail(f"BLE is not available or not permitted on this Mac: {exc}")
        fail(str(exc))
    return int(result or 0)


def ble_transport_options(args: argparse.Namespace) -> BLETransportOptions:
    return BLETransportOptions(
        name=args.name,
        cache=args.cache,
        no_cache=args.no_cache,
        scan_timeout=args.scan_timeout,
        connect_timeout=args.connect_timeout,
        connect_settle=args.connect_settle,
        service_timeout=args.service_timeout,
        response_wait=args.response_wait,
        final_wait=args.final_wait,
        disconnect_pause=args.disconnect_pause,
        echo=not args.no_echo,
    )


async def validate_flow_persistence(args: argparse.Namespace, sequence: int, payload: str) -> str:
    output_parts: list[str] = []
    async with BLETransport(ble_transport_options(args)) as transport:
        output_parts.append(await transport.flow_clear())
        output_parts.append(await transport.flow_send(args.flow_channel, sequence, payload))
    async with BLETransport(ble_transport_options(args)) as transport:
        output_parts.append(await transport.flow_status())
    return "\n".join(part.strip() for part in output_parts if part)


def selected_standard_command_names(args: argparse.Namespace) -> list[str]:
    checks = (
        ("--status-only", getattr(args, "status_only", False)),
        ("--capabilities-only", getattr(args, "capabilities_only", False)),
        ("--sensors-only", getattr(args, "sensors_only", False)),
        ("--power-only", getattr(args, "power_only", False)),
        ("--display-only", getattr(args, "display_only", False)),
        ("--display-brightness", getattr(args, "display_brightness", None) is not None),
        ("--gpio-only", getattr(args, "gpio_only", False)),
        ("--touch-only", getattr(args, "touch_only", False)),
        ("--rgb-only", getattr(args, "rgb_only", False)),
        ("--rgb-color", getattr(args, "rgb_color", None) is not None),
        ("--voice-only", getattr(args, "voice_only", False)),
        ("--app-only", getattr(args, "app_only", False)),
        ("--apps-only", getattr(args, "apps_only", False)),
        ("--launch-app", getattr(args, "launch_app", None) is not None),
        ("--stop-app", getattr(args, "stop_app", False)),
        ("--delete-app", getattr(args, "delete_app", None) is not None),
        ("--flow-status-only", getattr(args, "flow_status_only", False)),
        ("--flow-clear-only", getattr(args, "flow_clear_only", False)),
        ("--flow-send-text", getattr(args, "flow_send_text", None) is not None),
        ("--flow-persist-text", getattr(args, "flow_persist_text", None) is not None),
        ("--abort-only", getattr(args, "abort_only", False)),
    )
    return [name for name, selected in checks if selected]


def has_standard_transport_command(args: argparse.Namespace) -> bool:
    return bool(selected_standard_command_names(args))


def selected_action_names(args: argparse.Namespace) -> list[str]:
    actions: list[str] = []
    if getattr(args, "scan_only", False):
        actions.append("--scan-only")
    if getattr(args, "connect_only", False):
        actions.append("--connect-only")
    actions.extend(selected_standard_command_names(args))
    if getattr(args, "package_dir", None) or getattr(args, "package_json", None):
        actions.append("install")
    return actions


def argument_selection_error(args: argparse.Namespace) -> str | None:
    if getattr(args, "progress_every", 1) <= 0:
        return "--progress-every must be greater than 0"
    if getattr(args, "stop_before_end", False) and not (getattr(args, "package_dir", None) or getattr(args, "package_json", None)):
        return "--stop-before-end requires --package-dir or --package-json"
    actions = selected_action_names(args)
    if not actions:
        return "use exactly one BLE Runtime action, --scan-only, --connect-only, --package-dir, --package-json, or --self-test"
    if len(actions) > 1:
        return "use exactly one BLE Runtime action; selected: " + ", ".join(actions)
    return None


def is_read_only_standard_command(args: argparse.Namespace) -> bool:
    return any((
        args.status_only,
        args.capabilities_only,
        args.sensors_only,
        args.power_only,
        args.display_only,
        args.gpio_only,
        args.touch_only,
        args.rgb_only,
        args.voice_only,
        args.app_only,
        args.apps_only,
        args.flow_status_only,
    )) and not any((
        args.display_brightness is not None,
        args.rgb_color is not None,
        args.launch_app is not None,
        args.stop_app,
        args.delete_app is not None,
        args.flow_clear_only,
        args.flow_send_text is not None,
        args.flow_persist_text is not None,
        args.abort_only,
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
        app_only=False,
        apps_only=False,
        launch_app=None,
        stop_app=False,
        delete_app=None,
        flow_status_only=False,
        flow_clear_only=False,
        flow_send_text=None,
        flow_persist_text=None,
        abort_only=False,
        package_dir=None,
        package_json=None,
        scan_only=False,
        connect_only=False,
        stop_before_end=False,
        progress_every=25,
        app_id=None,
    )
    empty_args = argparse.Namespace(**vars(base))
    assert argument_selection_error(empty_args)

    status_args = argparse.Namespace(**vars(base))
    status_args.status_only = True
    assert selected_action_names(status_args) == ["--status-only"]
    assert argument_selection_error(status_args) is None
    assert has_standard_transport_command(status_args)
    assert is_read_only_standard_command(status_args)

    launch_args = argparse.Namespace(**vars(base))
    launch_args.launch_app = "clock_test"
    assert selected_action_names(launch_args) == ["--launch-app"]
    assert argument_selection_error(launch_args) is None
    assert has_standard_transport_command(launch_args)
    assert not is_read_only_standard_command(launch_args)

    scan_conflict_args = argparse.Namespace(**vars(status_args))
    scan_conflict_args.scan_only = True
    assert "selected" in (argument_selection_error(scan_conflict_args) or "")

    package_args = argparse.Namespace(**vars(base))
    package_args.package_dir = Path("scripts/runtime_apps/clock_test")
    assert selected_action_names(package_args) == ["install"]
    assert argument_selection_error(package_args) is None
    assert is_transport_install(package_args)

    package_conflict_args = argparse.Namespace(**vars(package_args))
    package_conflict_args.status_only = True
    assert "selected" in (argument_selection_error(package_conflict_args) or "")

    staged_args = argparse.Namespace(**vars(package_args))
    staged_args.stop_before_end = True
    assert argument_selection_error(staged_args) is None
    assert is_transport_install(staged_args)

    staged_without_package_args = argparse.Namespace(**vars(base))
    staged_without_package_args.stop_before_end = True
    assert "--stop-before-end" in (argument_selection_error(staged_without_package_args) or "")

    bad_progress_args = argparse.Namespace(**vars(package_args))
    bad_progress_args.progress_every = 0
    assert "--progress-every" in (argument_selection_error(bad_progress_args) or "")

    abort_args = argparse.Namespace(**vars(base))
    abort_args.abort_only = True
    assert selected_action_names(abort_args) == ["--abort-only"]
    assert argument_selection_error(abort_args) is None
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

    assert "connect_and_run" in globals()
    print("runtime_install_ble self-test ok")


async def run_standard_transport_command_once(args: argparse.Namespace) -> int:
    if args.flow_persist_text is not None:
        sequence = args.flow_seq if args.flow_seq is not None else (int(time.time()) & 0xFFFFFFFF)
        output = await validate_flow_persistence(args, sequence, args.flow_persist_text)
        validate_flow_output(output, args.flow_channel, sequence, args.flow_persist_text)
        print(output.strip())
        return 0
    if args.abort_only:
        require_app_id_for_abort(args)
        package_id = safe_package_id(args.app_id)
        async with BLETransport(ble_transport_options(args)) as transport:
            print(await transport.abort_install(package_id))
        return 0

    async with BLETransport(ble_transport_options(args)) as transport:
        if args.status_only:
            print((await transport.status()).strip())
        elif args.capabilities_only:
            print(await transport.capabilities())
        elif args.sensors_only:
            print(await transport.sensors())
        elif args.power_only:
            print(await transport.power())
        elif args.display_only:
            print(await transport.display())
        elif args.display_brightness is not None:
            print(await transport.display(args.display_brightness))
        elif args.gpio_only:
            print(await transport.gpio())
        elif args.touch_only:
            print(await transport.touch())
        elif args.rgb_only:
            print(await transport.rgb())
        elif args.rgb_color is not None:
            print(await transport.rgb(args.rgb_color))
        elif args.voice_only:
            print(await transport.voice())
        elif args.app_only:
            print(await transport.app_status())
        elif args.apps_only:
            print(await transport.apps())
        elif args.launch_app is not None:
            print((await transport.launch_app(safe_package_id(args.launch_app))).strip())
        elif args.stop_app:
            print((await transport.stop_app()).strip())
        elif args.delete_app is not None:
            print((await transport.delete_app(safe_package_id(args.delete_app))).strip())
        elif args.flow_status_only:
            print((await transport.flow_status()).strip())
        elif args.flow_clear_only:
            print((await transport.flow_clear()).strip())
        elif args.flow_send_text is not None:
            sequence = args.flow_seq if args.flow_seq is not None else (int(time.time()) & 0xFFFFFFFF)
            print((await transport.flow_send(args.flow_channel, sequence, args.flow_send_text)).strip())
    return 0


async def run_standard_transport_command(args: argparse.Namespace) -> int:
    attempts = max(1, args.read_retries + 1) if is_read_only_standard_command(args) else 1
    last_error: Exception | None = None
    for attempt in range(1, attempts + 1):
        try:
            return await run_standard_transport_command_once(args)
        except SystemExit:
            raise
        except Exception as exc:
            last_error = exc
            if attempt >= attempts:
                break
            print(f"warning: BLE read attempt {attempt}/{attempts} failed: {exc}; retrying", file=sys.stderr)
            await asyncio.sleep(args.retry_delay)
    fail(str(last_error) if last_error else "BLE command failed")


def is_transport_install(args: argparse.Namespace) -> bool:
    if not (args.package_dir or args.package_json):
        return False
    return not any((
        args.connect_only,
        args.status_only,
        args.capabilities_only,
        args.sensors_only,
        args.power_only,
        args.display_only,
        args.display_brightness is not None,
        args.gpio_only,
        args.touch_only,
        args.rgb_only,
        args.voice_only,
        args.app_only,
        args.apps_only,
        bool(args.launch_app),
        args.stop_app,
        bool(args.delete_app),
        bool(args.rgb_color),
        args.flow_status_only,
        args.flow_clear_only,
        args.flow_send_text is not None,
        args.flow_persist_text is not None,
        args.abort_only,
    ))


async def install_with_runtime_transport(args: argparse.Namespace) -> int:
    if args.package_dir:
        package_id, files = load_package_from_dir(args.package_dir, args.app_id)
    else:
        package_id, files = load_package_from_json(args.package_json, args.app_id)
    chunk_bytes = max(16, min(args.chunk_bytes, MAX_INSTALL_CHUNK_BYTES))
    command_count = len(build_install_commands(package_id, files, chunk_bytes))
    started = time.time()

    def on_progress(command: str, index: int, total: int) -> None:
        if not args.no_echo:
            return
        if command.startswith("vb_runtime_install_begin "):
            print("install_begin", flush=True)
        elif command.startswith("vb_runtime_install_end "):
            print("install_end", flush=True)
        elif command.startswith("vb_runtime_install_file ") and (index == 2 or index == total or (index % args.progress_every) == 0):
            print(f"install_file {index}/{total}", flush=True)

    try:
        async with BLETransport(ble_transport_options(args)) as transport:
            await transport.install_package(
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
        command_count -= 1
        print(
            f"staged {package_id} over BLE: {len(files)} files, "
            f"{command_count} commands, chunk={chunk_bytes} bytes, {elapsed:.1f}s"
        )
        return 0
    print(
        f"installed {package_id} over BLE: {len(files)} files, "
        f"{command_count} commands, chunk={chunk_bytes} bytes, {elapsed:.1f}s"
    )
    return 0


async def connect_and_run(args: argparse.Namespace) -> int:
    if not args.connect_only:
        fail("connect_and_run is reserved for --connect-only")

    async with BLETransport(ble_transport_options(args)) as transport:
        label = f" {transport.connection_label}" if transport.connection_label else ""
        print(f"connected{label}")
        initial = await transport.verify_connection(timeout=max(4.0, args.final_wait + 3.0))
        print(f"connection verified: {initial}")
        if args.hold_seconds != 0:
            print(
                "holding BLE connection"
                + (" until Ctrl-C" if args.hold_seconds < 0 else f" for {args.hold_seconds:g}s")
            )
            try:
                await transport.hold_connection(args.hold_seconds, keepalive_period=args.keepalive_period)
            except KeyboardInterrupt:
                print("disconnect requested")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a Huangshan Runtime app package over BLE GATT.")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--package-dir", type=Path, help="Directory containing manifest.json/app.info, main.lua, and assets")
    source.add_argument("--package-json", type=Path, help="VibeBoard runtime package JSON with app.packageId and files")
    parser.add_argument("--app-id", help="Override package id")
    parser.add_argument("--flow-channel", default="pc.flow", help="Flow channel used by --flow-send-text")
    parser.add_argument("--flow-seq", type=int, help="Flow sequence used by --flow-send-text; defaults to current time")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help="BLE local name to scan for")
    parser.add_argument("--scan-only", action="store_true", help="Only scan and list nearby BLE devices")
    parser.add_argument("--connect-only", action="store_true", help="Connect to the board, read status, and optionally hold the link")
    parser.add_argument("--status-only", action="store_true", help="Connect and read Runtime BLE status")
    parser.add_argument("--capabilities-only", action="store_true", help="Connect and read Runtime capability JSON")
    parser.add_argument("--sensors-only", action="store_true", help="Connect and read built-in sensor JSON")
    parser.add_argument("--power-only", action="store_true", help="Connect and read Runtime power JSON")
    parser.add_argument("--display-only", action="store_true", help="Connect and read Runtime display JSON")
    parser.add_argument("--gpio-only", action="store_true", help="Connect and read Runtime GPIO whitelist JSON")
    parser.add_argument("--touch-only", action="store_true", help="Connect and read Runtime touch JSON")
    parser.add_argument("--rgb-only", action="store_true", help="Connect and read Runtime RGB LED JSON")
    parser.add_argument("--voice-only", action="store_true", help="Connect and read Runtime voice bridge JSON")
    parser.add_argument("--app-only", action="store_true", help="Connect and read Runtime app manager status JSON")
    parser.add_argument("--apps-only", action="store_true", help="Connect and read Runtime installed app list JSON")
    parser.add_argument("--launch-app", help="Launch an installed Runtime app by app id")
    parser.add_argument("--stop-app", action="store_true", help="Stop the active Runtime app")
    parser.add_argument("--delete-app", help="Delete a stopped Runtime app by app id")
    parser.add_argument("--rgb-color", help="Set Runtime RGB LED color, e.g. red, off, or 3366ff")
    parser.add_argument("--display-brightness", type=int, help="Set Runtime display brightness, 0-100")
    parser.add_argument("--flow-status-only", action="store_true", help="Connect and read Runtime info flow status")
    parser.add_argument("--flow-clear-only", action="store_true", help="Connect and clear Runtime info flow history")
    parser.add_argument("--flow-send-text", help="Connect and send one Runtime info flow UTF-8 text payload")
    parser.add_argument("--flow-persist-text", help="Send and validate one info flow frame restored after reconnecting BLE")
    parser.add_argument("--abort-only", action="store_true", help="Abort and clean staging for --app-id")
    parser.add_argument("--cache", type=Path, default=DEFAULT_BLE_CACHE_PATH, help="Peripheral cache path for reconnects")
    parser.add_argument("--no-cache", action="store_true", help="Ignore cached peripheral identifier/address")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=12.0)
    parser.add_argument("--connect-settle", type=float, default=0.35, help="Seconds to wait after connect before GATT commands")
    parser.add_argument("--service-timeout", type=float, default=6.0, help="Seconds to wait for GATT service discovery")
    parser.add_argument("--chunk-bytes", type=int, default=INSTALL_CHUNK_BYTES)
    parser.add_argument("--response-wait", type=float, default=0.12)
    parser.add_argument("--final-wait", type=float, default=0.8)
    parser.add_argument("--disconnect-pause", type=float, default=0.8, help="Pause after stopping notifications before disconnect")
    parser.add_argument("--progress-every", type=int, default=25, help="Emit quiet progress every N install commands")
    parser.add_argument("--hold-seconds", type=float, default=0.0, help="Keep --connect-only connected for N seconds; use -1 until Ctrl-C")
    parser.add_argument("--keepalive-period", type=float, default=5.0, help="Status keepalive interval while holding a BLE connection")
    parser.add_argument("--read-retries", type=int, default=1, help="Retry read-only standard BLE commands after transient disconnects")
    parser.add_argument("--retry-delay", type=float, default=0.8, help="Seconds to wait before a read-only BLE retry")
    parser.add_argument("--stop-before-end", action="store_true")
    parser.add_argument("--no-echo", action="store_true")
    parser.add_argument("--self-test", action="store_true", help="Run offline CLI routing checks and exit")
    args = parser.parse_args()

    if args.self_test:
        run_self_test()
        return 0
    selection_error = argument_selection_error(args)
    if selection_error:
        parser.error(selection_error)
    if args.abort_only:
        require_app_id_for_abort(args)
    if args.scan_only:
        return run_ble_task(scan_devices(args.name, args.scan_timeout))
    if has_standard_transport_command(args):
        return run_ble_task(run_standard_transport_command(args))
    if is_transport_install(args):
        return run_ble_task(install_with_runtime_transport(args))
    return run_ble_task(connect_and_run(args))


if __name__ == "__main__":
    raise SystemExit(main())
