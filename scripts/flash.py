#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


DEFAULT_BOARD = "sf32lb52-lchspi-ulp"
DEFAULT_BAUD = 1_000_000
FLASH_FAILURE_RE = re.compile(
    r"(Failed to connect|Failed to download stub|Operation timed out|Receive timeout|"
    r"Error reading frame|Resource busy|Permission denied)",
    re.IGNORECASE,
)
BOOT_OK_RE = re.compile(
    r"(SFBL|Found lcd co5300|display on|littlevgl2rtt|Huangshan_Home|Board_Diagnostics)",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class FlashFile:
    path: str
    address: str


@dataclass(frozen=True)
class PortInfo:
    path: str
    role: str
    recommended: bool


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_build_dir(board: str) -> Path:
    return project_root() / "project" / f"build_{board}_hcpu"


def classify_port(path: str) -> PortInfo:
    lowered = path.lower()
    role = "uart-bridge"
    if "usbmodem" in lowered or "ttyacm" in lowered:
        role = "usb-cdc"
    recommended = any(token in lowered for token in ("usbserial", "ttyusb"))
    return PortInfo(path=path, role=role, recommended=recommended)


def list_serial_ports() -> list[PortInfo]:
    system = platform.system()
    names: list[str] = []

    if system == "Darwin":
        names = [str(path) for path in Path("/dev").glob("cu.*")]
    elif system == "Linux":
        for pattern in ("ttyUSB*", "ttyACM*"):
            names.extend(str(path) for path in Path("/dev").glob(pattern))
    elif system == "Windows":
        # Keep Windows simple and explicit. pyserial enumeration is optional and
        # not guaranteed to be present before the SDK environment is active.
        names = [f"COM{i}" for i in range(1, 33)]

    return sorted(
        (classify_port(path) for path in names if re.search(r"usbserial|usbmodem|ttyUSB|ttyACM|^COM\d+$", path, re.I)),
        key=lambda port: (0 if port.recommended else 1, port.path),
    )


def choose_port(port: str | None, *, allow_usbmodem: bool) -> PortInfo:
    if port:
        info = classify_port(port)
        if info.role == "usb-cdc" and not allow_usbmodem:
            raise SystemExit(
                f"{port} looks like a USB CDC device, not the Huangshan CH340 UART bridge. "
                "Pass --allow-usbmodem if you really want to use it."
            )
        return info

    ports = list_serial_ports()
    recommended = [item for item in ports if item.recommended]
    if len(recommended) == 1:
        return recommended[0]
    if len(recommended) > 1:
        choices = "\n".join(f"  {item.path} ({item.role})" for item in recommended)
        raise SystemExit(f"Multiple Huangshan-like UART ports found. Pass one explicitly:\n{choices}")
    if len(ports) == 1 and allow_usbmodem:
        return ports[0]

    if ports:
        choices = "\n".join(f"  {item.path} ({item.role})" for item in ports)
        raise SystemExit(
            "No recommended Huangshan UART bridge was found. "
            "Expected a /dev/cu.usbserial* or /dev/ttyUSB* CH340-style port.\n"
            f"Visible serial ports:\n{choices}"
        )
    raise SystemExit("No USB serial ports found.")


def read_flash_manifest(build_dir: Path) -> list[FlashFile]:
    manifest = build_dir / "sftool_param.json"
    if manifest.exists():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        files = data.get("write_flash", {}).get("files", [])
        return [FlashFile(path=item["path"], address=item["address"]) for item in files]

    return [
        FlashFile("bootloader/bootloader.bin", "0x12010000"),
        FlashFile("main.bin", "0x12020000"),
        FlashFile("ftab/ftab.bin", "0x12000000"),
    ]


def validate_artifacts(build_dir: Path, files: list[FlashFile]) -> None:
    missing: list[str] = []
    empty: list[str] = []
    for item in files:
        path = build_dir / item.path
        if not path.exists():
            missing.append(item.path)
        elif path.stat().st_size <= 0:
            empty.append(item.path)
    if missing or empty:
        parts = []
        if missing:
            parts.append(f"missing: {', '.join(missing)}")
        if empty:
            parts.append(f"empty: {', '.join(empty)}")
        raise SystemExit(f"Build artifacts are not ready in {build_dir}: {'; '.join(parts)}")


def find_sftool() -> str:
    found = shutil.which("sftool")
    if found:
        return found

    home = Path.home()
    candidates = sorted((home / ".sifli" / "tools" / "sftool").glob("*/sftool"), reverse=True)
    for candidate in candidates:
        if candidate.exists() and os.access(candidate, os.X_OK):
            return str(candidate)

    raise SystemExit("sftool not found. Run through scripts/flash.sh or source the SiFli SDK export script first.")


def check_port_exists(port: PortInfo) -> None:
    if platform.system() == "Windows":
        return
    if not Path(port.path).exists():
        raise SystemExit(f"Serial port does not exist: {port.path}")


def check_port_busy(port: PortInfo) -> None:
    if platform.system() == "Windows" or not shutil.which("lsof"):
        return
    proc = subprocess.run(["lsof", port.path], text=True, capture_output=True)
    if proc.returncode == 0 and proc.stdout.strip():
        raise SystemExit(f"Serial port is busy:\n{proc.stdout.strip()}")


def pulse_reset(port: PortInfo, *, quiet: bool = False) -> None:
    try:
        import serial  # type: ignore
    except Exception:
        if not quiet:
            print("pyserial is not available; skipping RTS reset pulse.", file=sys.stderr)
        return

    try:
        ser = serial.Serial(port.path, baudrate=DEFAULT_BAUD, timeout=0.05, write_timeout=1)
        ser.rts = True
        time.sleep(0.25)
        ser.rts = False
        time.sleep(0.25)
        ser.close()
    except Exception as exc:
        if not quiet:
            print(f"RTS reset pulse failed: {exc}", file=sys.stderr)


def run_sftool_once(sftool: str, build_dir: Path, port: PortInfo, files: list[FlashFile]) -> tuple[int, str, float]:
    args = [
        sftool,
        "-p",
        port.path,
        "-c",
        "SF32LB52",
        "-m",
        "nor",
        "write_flash",
    ]
    args.extend(f"{item.path}@{item.address}" for item in files)

    started = time.monotonic()
    proc = subprocess.run(args, cwd=build_dir, text=True, capture_output=True)
    elapsed = time.monotonic() - started
    output = "\n".join(part for part in (proc.stdout, proc.stderr) if part)
    return proc.returncode, output, elapsed


def confirm_boot(port: PortInfo, *, seconds: float, baud: int) -> tuple[bool, str]:
    try:
        import serial  # type: ignore
    except Exception as exc:
        return False, f"pyserial is not available: {exc}"

    data = bytearray()
    try:
        ser = serial.Serial(port.path, baudrate=baud, timeout=0.05, write_timeout=1)
        ser.rts = True
        time.sleep(0.25)
        ser.rts = False
        time.sleep(0.25)
        end = time.time() + seconds
        while time.time() < end:
            chunk = ser.read(4096)
            if chunk:
                data.extend(chunk)
        ser.close()
    except Exception as exc:
        return False, f"boot monitor failed: {exc}"

    text = data.decode("utf-8", "replace")
    return bool(BOOT_OK_RE.search(text)), text


def print_port_list() -> None:
    ports = list_serial_ports()
    if not ports:
        print("No USB serial ports found.")
        return
    for item in ports:
        marker = "recommended" if item.recommended else "candidate"
        print(f"{item.path}\t{item.role}\t{marker}")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Robust Huangshan Pi SF32LB52 UART flasher.")
    parser.add_argument("port_arg", nargs="?", help="Serial port, e.g. /dev/cu.usbserial-110")
    parser.add_argument("-p", "--port", help="Serial port, e.g. /dev/cu.usbserial-110")
    parser.add_argument("--board", default=os.environ.get("BOARD", DEFAULT_BOARD))
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--attempts", type=int, default=int(os.environ.get("FLASH_ATTEMPTS", "3")))
    parser.add_argument("--retry-delay", type=float, default=float(os.environ.get("FLASH_RETRY_DELAY", "2")))
    parser.add_argument("--allow-usbmodem", action="store_true", help="Allow /dev/cu.usbmodem* ports.")
    parser.add_argument("--pre-reset", action="store_true", help="Pulse RTS before each attempt.")
    parser.add_argument("--confirm-boot", action="store_true", help="Reset and capture boot logs after flashing.")
    parser.add_argument("--boot-seconds", type=float, default=float(os.environ.get("FLASH_BOOT_SECONDS", "10")))
    parser.add_argument("--boot-baud", type=int, default=int(os.environ.get("FLASH_BOOT_BAUD", str(DEFAULT_BAUD))))
    parser.add_argument("--dry-run", action="store_true", help="Validate inputs and print the sftool command without flashing.")
    parser.add_argument("--list-ports", action="store_true", help="List detected serial ports and exit.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.list_ports:
        print_port_list()
        return 0

    build_dir = args.build_dir or default_build_dir(args.board)
    build_dir = build_dir.resolve()
    files = read_flash_manifest(build_dir)
    validate_artifacts(build_dir, files)

    port = choose_port(args.port or args.port_arg, allow_usbmodem=args.allow_usbmodem)
    check_port_exists(port)
    check_port_busy(port)
    sftool = find_sftool()

    command_preview = " ".join(
        [
            sftool,
            "-p",
            port.path,
            "-c",
            "SF32LB52",
            "-m",
            "nor",
            "write_flash",
            *[f"{item.path}@{item.address}" for item in files],
        ]
    )
    print(f"build_dir: {build_dir}")
    print(f"port: {port.path} ({port.role})")
    print(f"sftool: {sftool}")
    print(f"command: {command_preview}")

    if args.dry_run:
        print("dry run: not flashing")
        return 0

    last_output = ""
    for attempt in range(1, max(args.attempts, 1) + 1):
        print(f"\n[flash] attempt {attempt}/{args.attempts}")
        if args.pre_reset:
            pulse_reset(port)
        code, output, elapsed = run_sftool_once(sftool, build_dir, port, files)
        last_output = output
        if output.strip():
            print(output.rstrip())
        print(f"[flash] attempt {attempt} exit={code} elapsed={elapsed:.1f}s")
        if code == 0 and not FLASH_FAILURE_RE.search(output):
            if args.confirm_boot:
                ok, boot_log = confirm_boot(port, seconds=args.boot_seconds, baud=args.boot_baud)
                print(f"[boot] confirmed={ok} capture_seconds={args.boot_seconds:g}")
                if boot_log.strip():
                    print(boot_log.rstrip())
                if not ok:
                    return 3
            return 0
        if attempt < args.attempts:
            print(f"[flash] retrying in {args.retry_delay:g}s")
            time.sleep(args.retry_delay)

    if FLASH_FAILURE_RE.search(last_output):
        print("[flash] failure looks like serial/download handshake trouble.", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
