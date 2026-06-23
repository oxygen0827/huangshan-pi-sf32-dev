#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - depends on SDK python env
    print(f"pyserial is not available: {exc}", file=sys.stderr)
    sys.exit(2)

from runtime_package import build_install_commands, fail, load_package_from_dir, load_package_from_json

READY_PATTERNS = (
    "[vb_runtime] registered",
    "[vb_runtime] start",
    "[vb_runtime] api=",
    "[vb_runtime] serial",
)


def read_text(ser: serial.Serial, seconds: float) -> str:
    end = time.time() + seconds
    chunks = bytearray()
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.extend(data)
    return bytes((b & 0x7F) for b in chunks).decode("utf-8", "replace")


def write_line_slow(ser: serial.Serial, line: str) -> None:
    data = line.encode("utf-8") + b"\r\n"
    for index in range(0, len(data), 24):
        ser.write(data[index : index + 24])
        ser.flush()
        time.sleep(0.012)


def send_command(ser: serial.Serial, command: str, wait: float, echo: bool) -> str:
    if echo:
        print(f"> {command}")
    write_line_slow(ser, command)
    text = read_text(ser, wait)
    if echo and text.strip():
        print(text.rstrip())
    return text


def wait_for_runtime(ser: serial.Serial, seconds: float) -> str:
    start = time.time()
    seen = ""
    while time.time() - start < seconds:
        seen += read_text(ser, 0.25)
        if any(pattern in seen for pattern in READY_PATTERNS):
            return seen
        ser.write(b"\r\nvb_runtime_status\r\n")
        ser.flush()
        seen += read_text(ser, 0.5)
        if "[vb_runtime] api=" in seen:
            return seen
    return seen


def main() -> int:
    parser = argparse.ArgumentParser(description="Install a Huangshan Runtime app package over serial MSH.")
    parser.add_argument("port", help="Serial port, for example /dev/cu.usbserial-13220")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--package-dir", type=Path, help="Directory containing manifest.json/app.info, main.lua, and assets")
    source.add_argument("--package-json", type=Path, help="VibeBoard runtime package JSON with app.packageId and files")
    parser.add_argument("--app-id", help="Override package id")
    parser.add_argument("--baud", type=int, default=1_000_000)
    parser.add_argument("--chunk-bytes", type=int, default=48)
    parser.add_argument("--command-wait", type=float, default=0.22)
    parser.add_argument("--final-wait", type=float, default=2.0)
    parser.add_argument("--ready-timeout", type=float, default=24.0)
    parser.add_argument("--stop-before-end", action="store_true", help="Write package chunks but do not commit; used to verify staging safety")
    parser.add_argument("--no-echo", action="store_true")
    args = parser.parse_args()

    chunk_bytes = max(16, min(args.chunk_bytes, 240))
    if args.package_dir:
        package_id, files = load_package_from_dir(args.package_dir, args.app_id)
    else:
        package_id, files = load_package_from_json(args.package_json, args.app_id)
    commands = build_install_commands(package_id, files, chunk_bytes)
    if args.stop_before_end:
        commands = commands[:-1]

    ser = serial.Serial(
        port=None,
        baudrate=args.baud,
        bytesize=8,
        parity="N",
        stopbits=1,
        timeout=0.05,
        write_timeout=2,
    )
    ser.dtr = False
    ser.rts = False
    ser.port = args.port
    ser.open()
    ser.dtr = False
    ser.rts = False
    try:
        ready = wait_for_runtime(ser, args.ready_timeout)
        if "[vb_runtime] api=" not in ready and "[vb_runtime] start" not in ready and "[vb_runtime] registered" not in ready:
            fail("Runtime did not become ready on serial. Last output:\n" + ready[-2000:])
        output = ""
        for command in commands:
            output += send_command(ser, command, args.command_wait, not args.no_echo)
        output += send_command(ser, "vb_runtime_status", args.final_wait, not args.no_echo)
    finally:
        ser.close()

    if args.stop_before_end:
        if "[vb_runtime] install complete:" in output:
            print("Unexpected install completion while --stop-before-end was set", file=sys.stderr)
            print(output[-4000:], file=sys.stderr)
            return 1
        print(f"staged {package_id}: {len(files)} files, {len(commands)} commands, chunk={chunk_bytes} bytes")
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

    print(f"installed {package_id}: {len(files)} files, {len(commands)} commands, chunk={chunk_bytes} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
