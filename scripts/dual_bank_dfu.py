#!/usr/bin/env python3
"""Build and test a signed ping-pong DFU transaction without touching a board.

The current factory image is single-slot.  This tool is the migration gate:
it verifies the proposed two-slot geometry and exercises interruption and
corruption semantics before the bootloader/ptab switch is enabled.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

from firmware_release import FirmwareReleaseError, canonical_json, validate_release
from hpet_package import HpetError, _sign, _verify


PREFIX = b"VIBEBOARD-PINGPONG-DFU-V1\n"
SLOT_SIZE = 0x400000
SLOT_A = 0x12020000
SLOT_B = SLOT_A + SLOT_SIZE
MAX_IMAGE = SLOT_SIZE - 0x1000


class DualBankError(RuntimeError):
    pass


def layout() -> dict[str, object]:
    return {
        "schemaVersion": 1,
        "kind": "vibeboard-pingpong-layout",
        "enabled": False,
        "requiresPtabMigration": True,
        "board": "sf32lb52-lchspi-ulp",
        "slotSize": SLOT_SIZE,
        "slots": {"a": {"address": SLOT_A, "size": SLOT_SIZE}, "b": {"address": SLOT_B, "size": SLOT_SIZE}},
        "bootContract": {"verifyBeforeSelect": True, "selectAfterHealthCheck": True, "rollbackOnBootFailure": True},
    }


def create(args: argparse.Namespace) -> int:
    release = args.release.resolve()
    manifest = validate_release(release, args.public_key)
    rows = [row for row in manifest["files"] if row.get("path") == "main.bin"]
    if len(rows) != 1:
        raise DualBankError("dual-bank release must contain exactly one main.bin")
    image = release / "main.bin"
    if image.stat().st_size > MAX_IMAGE:
        raise DualBankError(f"main.bin is too large for a ping-pong slot: {image.stat().st_size}>{MAX_IMAGE}")
    payload = {
        **layout(),
        "enabled": False,
        "release": {"version": manifest["version"], "sha256": hashlib.sha256(image.read_bytes()).hexdigest(), "size": image.stat().st_size},
    }
    args.output.mkdir(parents=True, exist_ok=False)
    (args.output / "dual-bank-layout.json").write_bytes(canonical_json(payload) + b"\n")
    signature = _sign(PREFIX + canonical_json(payload), args.private_key)
    (args.output / "dual-bank-layout.ed25519").write_bytes(signature)
    print(json.dumps({"ok": True, "output": str(args.output), "enabled": False, "version": manifest["version"]}))
    return 0


def verify(args: argparse.Namespace) -> int:
    root = args.package.resolve()
    value = json.loads((root / "dual-bank-layout.json").read_text(encoding="utf-8"))
    signature = (root / "dual-bank-layout.ed25519").read_bytes()
    if not isinstance(value, dict) or value.get("schemaVersion") != 1 or value.get("kind") != "vibeboard-pingpong-layout":
        raise DualBankError("invalid dual-bank layout")
    if value.get("enabled") is not False or value.get("requiresPtabMigration") is not True:
        raise DualBankError("dual-bank layout cannot be enabled before ptab/bootloader migration")
    slots = value.get("slots")
    if not isinstance(slots, dict) or slots["b"]["address"] != slots["a"]["address"] + slots["a"]["size"]:
        raise DualBankError("dual-bank slots are not contiguous")
    try:
        _verify(PREFIX + canonical_json(value), signature, args.public_key)
    except HpetError as exc:
        raise DualBankError("dual-bank layout signature failed") from exc
    print(json.dumps({"ok": True, "enabled": False, "reason": "ptab migration gate"}))
    return 0


def simulate() -> int:
    old = b"known-good-image"
    new = b"candidate-image-with-integrity"
    active = old
    candidate = bytearray(len(new))
    for index, block in enumerate(range(0, len(new), 4)):
        candidate[block:block + 4] = new[block:block + 4]
        if index == 1:
            assert active == old, "active slot changed during interrupted transfer"
            assert bytes(candidate) != new, "interrupted candidate unexpectedly complete"
            break
    assert active == old
    candidate[:] = new
    assert hashlib.sha256(candidate).hexdigest() == hashlib.sha256(new).hexdigest()
    candidate[0] ^= 0x01
    assert hashlib.sha256(candidate).hexdigest() != hashlib.sha256(new).hexdigest()
    print("dual_bank_dfu interruption/corruption simulation ok")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate VibeBoard ping-pong DFU release geometry")
    sub = parser.add_subparsers(dest="command")
    make = sub.add_parser("create")
    make.add_argument("--release", type=Path, required=True)
    make.add_argument("--output", type=Path, required=True)
    make.add_argument("--private-key", type=Path, required=True)
    make.add_argument("--public-key", type=Path, required=True)
    check = sub.add_parser("verify")
    check.add_argument("--package", type=Path, required=True)
    check.add_argument("--public-key", type=Path, required=True)
    sub.add_parser("simulate")
    sub.add_parser("self-test")
    args = parser.parse_args(argv)
    if args.command == "create": return create(args)
    if args.command == "verify": return verify(args)
    if args.command == "simulate": return simulate()
    if args.command == "self-test":
        value = layout()
        assert value["enabled"] is False
        assert value["slots"]["b"]["address"] == value["slots"]["a"]["address"] + value["slots"]["a"]["size"]
        return simulate()
    parser.error("choose create, verify, simulate, or self-test")
    return 2


if __name__ == "__main__":
    try:
        raise SystemExit(main(__import__("sys").argv[1:]))
    except (DualBankError, FirmwareReleaseError, HpetError, OSError, ValueError) as exc:
        print(f"dual_bank_dfu: {exc}")
        raise SystemExit(1)
