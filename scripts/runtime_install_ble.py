#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import sys
import time
from pathlib import Path

from runtime_package import build_install_commands, fail, load_package_from_dir, load_package_from_json


SERVICE_UUID = "454d5452-0100-0000-5453-4e4954524256"
COMMAND_UUID = "454d5452-0200-0000-5453-4e4954524256"
STATUS_UUID = "454d5452-0300-0000-5453-4e4954524256"
DEFAULT_DEVICE_NAME = "VibeBoard"
CACHE_PATH = Path.home() / ".vibeboard" / "huangshan_ble.json"


def load_cache(path: Path) -> dict[str, str]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_cache(path: Path, data: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def ensure_bleak():
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        fail(
            "Python package 'bleak' is required for BLE install.\n"
            "Install it in a project-local virtual environment, for example:\n"
            "  python3 -m venv .venv-ble\n"
            "  .venv-ble/bin/python -m pip install bleak\n"
            f"Import error: {exc}",
            2,
        )
    return BleakClient, BleakScanner


def decode_status(data: bytes | bytearray | str | None) -> str:
    if data is None:
        return ""
    if isinstance(data, str):
        return data
    return bytes(data).decode("utf-8", "replace").replace("\x00", "").strip()


async def scan_devices(name: str, timeout: float) -> list[object]:
    _, BleakScanner = ensure_bleak()
    devices = await BleakScanner.discover(timeout=timeout, return_adv=False)
    matches = [device for device in devices if (getattr(device, "name", None) or "") == name]
    for device in devices:
        print(f"{getattr(device, 'address', '')}\t{getattr(device, 'name', '')}")
    return matches


async def find_device(name: str, timeout: float, cache_path: Path, no_cache: bool) -> object:
    _, BleakScanner = ensure_bleak()
    cache = {} if no_cache else load_cache(cache_path)
    cached_address = cache.get("address")
    if cached_address:
        device = await BleakScanner.find_device_by_address(cached_address, timeout=min(timeout, 5.0))
        if device:
            print(f"reusing cached BLE peripheral: {cached_address}")
            return device

    print(f"scanning for BLE device named {name!r}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: (d.name or adv.local_name or "") == name,
        timeout=timeout,
    )
    if not device:
        fail(f"Could not find BLE device named {name!r}")
    save_cache(cache_path, {"name": name, "address": getattr(device, "address", "")})
    return device


async def read_status(client) -> str:
    try:
        return decode_status(await client.read_gatt_char(STATUS_UUID))
    except Exception as exc:  # pragma: no cover - depends on platform BLE stack
        return f"read_status_failed: {exc}"


async def write_command(client, command: str, response_wait: float, echo: bool) -> str:
    if echo:
        print(f"> {command}")
    payload = (command + "\n").encode("utf-8")
    await client.write_gatt_char(COMMAND_UUID, payload, response=True)
    await asyncio.sleep(response_wait)
    status = await read_status(client)
    if echo and status:
        print(status)
    return status


async def hold_connection(client, seconds: float, keepalive_period: float, response_wait: float, echo: bool) -> None:
    if seconds == 0:
        return

    started = time.monotonic()
    deadline = None if seconds < 0 else started + seconds
    print("holding BLE connection" + (" until Ctrl-C" if deadline is None else f" for {seconds:g}s"))

    try:
        while deadline is None or time.monotonic() < deadline:
            sleep_for = keepalive_period
            if deadline is not None:
                sleep_for = min(sleep_for, max(0.0, deadline - time.monotonic()))
            if sleep_for > 0:
                await asyncio.sleep(sleep_for)
            if deadline is not None and time.monotonic() >= deadline:
                break
            status = await write_command(client, "status", response_wait, echo)
            if not status or status.startswith("read_status_failed"):
                fail(f"BLE keepalive failed: {status!r}")
    except KeyboardInterrupt:
        print("disconnect requested")


async def connect_and_run(args: argparse.Namespace) -> int:
    BleakClient, _ = ensure_bleak()
    device = await find_device(args.name, args.scan_timeout, args.cache, args.no_cache)
    print(f"connecting {getattr(device, 'address', '')} {getattr(device, 'name', '')}...")
    async with BleakClient(device, timeout=args.connect_timeout) as client:
        if not client.is_connected:
            fail("BLE connection failed")
        print("connected")
        try:
            await client.start_notify(STATUS_UUID, lambda _sender, data: print(f"< {decode_status(data)}"))
        except Exception as exc:
            print(f"warning: could not subscribe status notifications: {exc}", file=sys.stderr)

        initial = await write_command(client, "status", args.response_wait, not args.no_echo)
        if args.connect_only:
            print(f"connection verified: {initial}")
            await hold_connection(
                client,
                args.hold_seconds,
                args.keepalive_period,
                args.response_wait,
                not args.no_echo,
            )
            return 0
        if args.status_only:
            print(f"status: {initial}")
            return 0
        if args.sensors_only:
            sensors = await write_command(client, "sensors", args.response_wait, not args.no_echo)
            print(sensors)
            return 0

        if args.package_dir:
            package_id, files = load_package_from_dir(args.package_dir, args.app_id)
        elif args.package_json:
            package_id, files = load_package_from_json(args.package_json, args.app_id)
        else:
            return 0

        chunk_bytes = max(16, min(args.chunk_bytes, 240))
        commands = build_install_commands(package_id, files, chunk_bytes)
        if args.stop_before_end:
            commands = commands[:-1]

        last_status = ""
        started = time.time()
        for command in commands:
            last_status = await write_command(client, command, args.response_wait, not args.no_echo)
        if not args.stop_before_end:
            last_status = await write_command(client, "status", args.final_wait, not args.no_echo)

        elapsed = time.time() - started
        if args.stop_before_end:
            print(f"staged {package_id}: {len(files)} files, {len(commands)} commands, chunk={chunk_bytes} bytes")
            return 0
        if f"active={package_id}" not in last_status:
            fail(f"BLE install finished but active app was not confirmed as {package_id}; last_status={last_status!r}")
        print(
            f"installed {package_id} over BLE: {len(files)} files, "
            f"{len(commands)} commands, chunk={chunk_bytes} bytes, {elapsed:.1f}s"
        )
        return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a Huangshan Runtime app package over BLE GATT.")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--package-dir", type=Path, help="Directory containing manifest.json/app.info, main.lua, and assets")
    source.add_argument("--package-json", type=Path, help="VibeBoard runtime package JSON with app.packageId and files")
    parser.add_argument("--app-id", help="Override package id")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help="BLE local name to scan for")
    parser.add_argument("--scan-only", action="store_true", help="Only scan and list nearby BLE devices")
    parser.add_argument("--connect-only", action="store_true", help="Connect to the board, read status, and optionally hold the link")
    parser.add_argument("--status-only", action="store_true", help="Connect and read Runtime BLE status")
    parser.add_argument("--sensors-only", action="store_true", help="Connect and read built-in sensor JSON")
    parser.add_argument("--cache", type=Path, default=CACHE_PATH, help="Peripheral cache path for reconnects")
    parser.add_argument("--no-cache", action="store_true", help="Ignore cached peripheral identifier/address")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=12.0)
    parser.add_argument("--chunk-bytes", type=int, default=48)
    parser.add_argument("--response-wait", type=float, default=0.12)
    parser.add_argument("--final-wait", type=float, default=0.8)
    parser.add_argument("--hold-seconds", type=float, default=0.0, help="Keep --connect-only connected for N seconds; use -1 until Ctrl-C")
    parser.add_argument("--keepalive-period", type=float, default=5.0, help="Status keepalive interval while holding a BLE connection")
    parser.add_argument("--stop-before-end", action="store_true")
    parser.add_argument("--no-echo", action="store_true")
    args = parser.parse_args()

    if args.scan_only:
        asyncio.run(scan_devices(args.name, args.scan_timeout))
        return 0
    if not args.connect_only and not args.status_only and not args.sensors_only and not args.package_dir and not args.package_json:
        parser.error("use --connect-only, --status-only, --sensors-only, --scan-only, --package-dir, or --package-json")
    return asyncio.run(connect_and_run(args))


if __name__ == "__main__":
    raise SystemExit(main())
