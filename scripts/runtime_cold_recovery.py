#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from runtime_package import load_package_from_dir  # noqa: E402
from runtime_transport import INSTALL_CHUNK_BYTES, RuntimeTransportError, SerialTransport, SerialTransportOptions  # noqa: E402


ACTIVE_PATTERNS = (
    re.compile(r"\[vb_runtime\]\s+active=([a-z][a-z0-9_]*)"),
    re.compile(r"\[vb_runtime\]\s+active app:\s*([a-z][a-z0-9_]*)"),
)
RECOVERY_PATTERNS = (
    re.compile(r"\[vb_runtime\] removed stale staging: \.__install_([a-z][a-z0-9_]*)"),
    re.compile(r"\[vb_runtime\] restored backup app: ([a-z][a-z0-9_]*)"),
)
READY_PATTERNS = (
    "[vb_runtime] start api=",
    "[vb_runtime] running=1",
)


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(1)


def parse_active_app(text: str) -> str | None:
    hits: list[str] = []
    for pattern in ACTIVE_PATTERNS:
        hits.extend(match.group(1) for match in pattern.finditer(text))
    return hits[-1] if hits else None


def parse_recovery_hits(text: str) -> list[str]:
    hits: list[str] = []
    for pattern in RECOVERY_PATTERNS:
        hits.extend(match.group(1) for match in pattern.finditer(text))
    return hits


def print_output(title: str, text: str) -> None:
    print(title)
    output = text.rstrip()
    if output:
        print(output)


def install_package(
    transport: SerialTransport,
    package_dir: Path,
    app_id: str,
    chunk_bytes: int,
    *,
    commit: bool,
    label: str,
) -> str:
    package_id, files = load_package_from_dir(package_dir, app_id)
    try:
        output = transport.install_package(package_id, files, chunk_bytes=chunk_bytes, commit=commit)
    except RuntimeTransportError as exc:
        fail(f"{label} failed: {exc}")
    print_output(label, output)
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify cold-start Runtime recovery after a staged but uncommitted install.")
    parser.add_argument("port", help="Serial port, for example /dev/cu.usbserial-13220")
    parser.add_argument("--active-app", required=True, help="App expected to remain active across recovery")
    parser.add_argument("--stage-app", required=True, help="App to leave in staging before reboot")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent / "runtime_apps")
    parser.add_argument("--chunk-bytes", type=int, default=INSTALL_CHUNK_BYTES)
    parser.add_argument("--boot-seconds", type=float, default=10.0)
    parser.add_argument("--baud", type=int, default=1_000_000)
    parser.add_argument("--ready-timeout", type=float, default=24.0)
    args = parser.parse_args()

    active_dir = args.root / args.active_app
    stage_dir = args.root / args.stage_app
    if not active_dir.is_dir():
        fail(f"missing active app package: {active_dir}")
    if not stage_dir.is_dir():
        fail(f"missing stage app package: {stage_dir}")
    if args.active_app == args.stage_app:
        fail("--active-app and --stage-app must be different")

    options = SerialTransportOptions(
        port=args.port,
        baud=args.baud,
        command_wait=0.22,
        final_wait=2.0,
        ready_timeout=args.ready_timeout,
        echo=False,
    )
    with SerialTransport(options) as transport:
        install_package(
            transport,
            active_dir,
            args.active_app,
            args.chunk_bytes,
            commit=True,
            label=f"[cold-recovery] install active app: {args.active_app}",
        )
        install_package(
            transport,
            stage_dir,
            args.stage_app,
            args.chunk_bytes,
            commit=False,
            label=f"[cold-recovery] stage interrupted app: {args.stage_app}",
        )

        status_before = transport.status()
        print_output("[cold-recovery] status before reboot", status_before)
        active_before = parse_active_app(status_before)
        if active_before != args.active_app:
            fail(f"expected active app before reboot to remain {args.active_app!r}, got {active_before!r}")

        boot_log = transport.reset_and_capture_boot(args.boot_seconds)
        print_output("[cold-recovery] boot capture after forced reset", boot_log)
        recovery_hits = parse_recovery_hits(boot_log)
        if args.stage_app not in recovery_hits:
            fail(f"did not observe staged app {args.stage_app!r} being recovered/removed in boot log")

        ready_after = transport.wait_for_runtime(args.ready_timeout)
        if not any(pattern in ready_after for pattern in READY_PATTERNS):
            fail("Runtime did not become ready after reset. Last output:\n" + ready_after[-2000:])
        status_after = transport.status()
        print_output("[cold-recovery] status after reboot", ready_after + status_after)
        active_after = parse_active_app(ready_after + status_after)
        if active_after != args.active_app:
            fail(f"expected active app after reboot to remain {args.active_app!r}, got {active_after!r}")

    print()
    print('cold-start recovery: ok')
    print(f'  active app preserved: {args.active_app}')
    print(f'  staged app cleaned on boot: {args.stage_app}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
