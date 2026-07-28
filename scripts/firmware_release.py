#!/usr/bin/env python3
"""Create, verify, and apply signed Huangshan Pi firmware releases.

Normal pet deployment remains BLE-only.  Firmware is a separate, USB-cabled
recovery operation: a release contains the exact flash table, bootloader and
Runtime image that were built and tested together.  Applying an earlier signed
release is the rollback operation.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

from companion_paths import companion_root
from hpet_package import HpetError, _sign, _verify


ROOT_DIR = companion_root()
FLASH_SCRIPT = ROOT_DIR / "scripts" / "flash.py"
RUNTIME_HEALTH_SCRIPT = ROOT_DIR / "scripts" / "runtime_install_serial.py"
RELEASE_MANIFEST = "firmware-release.json"
RELEASE_SIGNATURE = "firmware-release.ed25519"
SIGNATURE_PREFIX = b"VIBEBOARD-FIRMWARE-RELEASE-V1\n"
DEFAULT_BOARD = "sf32lb52-lchspi-ulp"
SAFE_VERSION = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,63}$")
SAFE_PATH = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._/-]{0,159}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


class FirmwareReleaseError(RuntimeError):
    pass


def canonical_json(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(128 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_relative_path(value: object) -> str:
    text = str(value or "")
    if not SAFE_PATH.fullmatch(text) or text.startswith("/") or ".." in Path(text).parts:
        raise FirmwareReleaseError(f"unsafe firmware artifact path: {text!r}")
    return text


def read_flash_manifest(build_dir: Path) -> tuple[dict[str, Any], list[dict[str, str]]]:
    source = build_dir / "sftool_param.json"
    try:
        value = json.loads(source.read_text(encoding="utf-8"))
        write_flash = value["write_flash"]
        files = write_flash["files"]
    except (OSError, ValueError, KeyError, TypeError) as exc:
        raise FirmwareReleaseError(f"invalid sftool_param.json in {build_dir}") from exc
    if not isinstance(files, list) or not files:
        raise FirmwareReleaseError("sftool_param.json has no write_flash files")
    clean_files: list[dict[str, str]] = []
    seen_paths: set[str] = set()
    seen_addresses: set[str] = set()
    for row in files:
        if not isinstance(row, dict):
            raise FirmwareReleaseError("invalid write_flash file entry")
        path = safe_relative_path(row.get("path"))
        address = str(row.get("address") or "")
        if not re.fullmatch(r"0x[0-9a-fA-F]{1,8}", address):
            raise FirmwareReleaseError(f"invalid flash address for {path}")
        if path in seen_paths or address.lower() in seen_addresses:
            raise FirmwareReleaseError("duplicate write_flash artifact path or address")
        artifact = build_dir / path
        if not artifact.is_file() or artifact.stat().st_size <= 0:
            raise FirmwareReleaseError(f"missing or empty build artifact: {path}")
        seen_paths.add(path)
        seen_addresses.add(address.lower())
        clean_files.append({"path": path, "address": address})
    return value, clean_files


def release_keys(args: argparse.Namespace, *, signing: bool) -> tuple[Path | None, Path]:
    private_value = args.private_key or os.environ.get("VIBEBOARD_FIRMWARE_PRIVATE_KEY")
    public_value = args.public_key or os.environ.get("VIBEBOARD_FIRMWARE_PUBLIC_KEY")
    if signing and not private_value:
        raise FirmwareReleaseError(
            "a production firmware release requires --private-key or VIBEBOARD_FIRMWARE_PRIVATE_KEY"
        )
    if not public_value:
        raise FirmwareReleaseError(
            "verification requires --public-key or VIBEBOARD_FIRMWARE_PUBLIC_KEY; do not trust a key supplied by the release"
        )
    private_path = Path(private_value).expanduser() if private_value else None
    public_path = Path(public_value).expanduser()
    if signing and (private_path is None or not private_path.is_file()):
        raise FirmwareReleaseError("firmware private signing key does not exist")
    if not public_path.is_file():
        raise FirmwareReleaseError("firmware public verification key does not exist")
    return private_path, public_path


def validate_release(release_dir: Path, public_key: Path) -> dict[str, Any]:
    manifest_path = release_dir / RELEASE_MANIFEST
    signature_path = release_dir / RELEASE_SIGNATURE
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        signature = signature_path.read_bytes()
    except (OSError, ValueError) as exc:
        raise FirmwareReleaseError(f"missing or invalid firmware release metadata in {release_dir}") from exc
    if not isinstance(manifest, dict):
        raise FirmwareReleaseError("firmware release manifest must be an object")
    if manifest.get("schemaVersion") != 1 or manifest.get("kind") != "vibeboard-firmware":
        raise FirmwareReleaseError("unsupported firmware release manifest")
    board = manifest.get("board")
    version = manifest.get("version")
    files = manifest.get("files")
    if not isinstance(board, str) or not board or not isinstance(version, str) or not SAFE_VERSION.fullmatch(version):
        raise FirmwareReleaseError("invalid firmware release board or version")
    if not isinstance(files, list) or not files:
        raise FirmwareReleaseError("firmware release has no artifacts")
    try:
        _verify(SIGNATURE_PREFIX + canonical_json(manifest), signature, public_key)
    except HpetError as exc:
        raise FirmwareReleaseError(f"firmware release signature verification failed: {exc}") from exc
    if len(signature) != 64:
        raise FirmwareReleaseError("firmware release signature has invalid length")

    seen_paths: set[str] = set()
    seen_addresses: set[str] = set()
    expected_flash: list[dict[str, str]] = []
    for row in files:
        if not isinstance(row, dict):
            raise FirmwareReleaseError("invalid firmware artifact entry")
        path = safe_relative_path(row.get("path"))
        address = str(row.get("address") or "")
        digest = str(row.get("sha256") or "")
        size = row.get("size")
        if path in seen_paths or address.lower() in seen_addresses:
            raise FirmwareReleaseError("duplicate firmware artifact path or address")
        if not re.fullmatch(r"0x[0-9a-fA-F]{1,8}", address) or not SHA256.fullmatch(digest):
            raise FirmwareReleaseError("invalid firmware artifact digest or address")
        if not isinstance(size, int) or size <= 0:
            raise FirmwareReleaseError("invalid firmware artifact size")
        artifact = release_dir / path
        if not artifact.is_file() or artifact.stat().st_size != size or sha256_file(artifact) != digest:
            raise FirmwareReleaseError(f"firmware artifact integrity check failed: {path}")
        seen_paths.add(path)
        seen_addresses.add(address.lower())
        expected_flash.append({"path": path, "address": address})

    _, actual_flash = read_flash_manifest(release_dir)
    if actual_flash != expected_flash:
        raise FirmwareReleaseError("sftool flash layout does not match the signed release manifest")
    return manifest


def create_release(args: argparse.Namespace) -> int:
    build_dir = args.build_dir.resolve()
    output_dir = args.output.resolve()
    if not SAFE_VERSION.fullmatch(args.version):
        raise FirmwareReleaseError("version must contain only letters, numbers, dot, underscore, plus, or hyphen")
    if output_dir.exists():
        raise FirmwareReleaseError(f"refusing to overwrite existing release directory: {output_dir}")
    private_key, public_key = release_keys(args, signing=True)
    assert private_key is not None
    source_sftool, flash_files = read_flash_manifest(build_dir)
    output_dir.mkdir(parents=True)
    try:
        copied: list[dict[str, Any]] = []
        for item in flash_files:
            relative = item["path"]
            source = build_dir / relative
            target = output_dir / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            copied.append(
                {
                    "path": relative,
                    "address": item["address"],
                    "size": target.stat().st_size,
                    "sha256": sha256_file(target),
                }
            )
        (output_dir / "sftool_param.json").write_bytes(canonical_json(source_sftool) + b"\n")
        manifest = {
            "schemaVersion": 1,
            "kind": "vibeboard-firmware",
            "board": args.board,
            "version": args.version,
            "files": copied,
        }
        (output_dir / RELEASE_MANIFEST).write_bytes(canonical_json(manifest) + b"\n")
        signature = _sign(SIGNATURE_PREFIX + canonical_json(manifest), private_key)
        (output_dir / RELEASE_SIGNATURE).write_bytes(signature)
        validate_release(output_dir, public_key)
    except Exception:
        shutil.rmtree(output_dir, ignore_errors=True)
        raise
    print(json.dumps({"ok": True, "release": str(output_dir), "version": args.version, "files": len(copied)}))
    return 0


def verify_release(args: argparse.Namespace) -> int:
    _, public_key = release_keys(args, signing=False)
    manifest = validate_release(args.release.resolve(), public_key)
    print(json.dumps({"ok": True, "release": str(args.release.resolve()), "board": manifest["board"], "version": manifest["version"]}))
    return 0


def write_last_success(release_dir: Path, manifest: dict[str, Any]) -> None:
    state_dir = Path.home() / ".vibeboard" / "companion" / "firmware"
    state_dir.mkdir(parents=True, exist_ok=True)
    target = state_dir / "last-success.json"
    payload = {"release": str(release_dir), "board": manifest["board"], "version": manifest["version"]}
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=state_dir, delete=False) as handle:
        json.dump(payload, handle, sort_keys=True, separators=(",", ":"))
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())
        temporary = Path(handle.name)
    os.replace(temporary, target)


def apply_release(args: argparse.Namespace) -> int:
    _, public_key = release_keys(args, signing=False)
    release_dir = args.release.resolve()
    manifest = validate_release(release_dir, public_key)
    if args.board and args.board != manifest["board"]:
        raise FirmwareReleaseError("--board does not match the signed firmware release")
    if not args.dry_run and args.confirm != "UPDATE_FIRMWARE":
        raise FirmwareReleaseError("set --confirm UPDATE_FIRMWARE after connecting the board by USB")
    command = [sys.executable]
    if getattr(sys, "frozen", False):
        command.append("--firmware-flash")
    else:
        command.append(str(FLASH_SCRIPT))
    command.extend([
        "--build-dir",
        str(release_dir),
        "--board",
        str(manifest["board"]),
        "--port",
        args.port,
        "--confirm-boot",
    ])
    if args.attempts:
        command.extend(["--attempts", str(args.attempts)])
    if args.dry_run:
        command.append("--dry-run")
    print("[firmware] verified release", manifest["version"])
    print("[firmware] invoking", " ".join(command))
    result = subprocess.run(command, cwd=ROOT_DIR)
    if result.returncode == 0 and not args.dry_run and not args.skip_runtime_health:
        health_command = [sys.executable]
        if getattr(sys, "frozen", False):
            health_command.append("--firmware-health")
        else:
            health_command.append(str(RUNTIME_HEALTH_SCRIPT))
        health_command.extend([args.port, "--status-only", "--no-echo"])
        print("[firmware] verifying Runtime health")
        health = subprocess.run(health_command, cwd=ROOT_DIR, text=True, capture_output=True, timeout=45)
        if health.returncode != 0:
            output = (health.stdout + health.stderr).strip()[-1200:]
            print(f"[firmware] Runtime health check failed: {output}", file=sys.stderr)
            return health.returncode or 1
        print("[firmware] Runtime health confirmed")
    if result.returncode == 0 and not args.dry_run:
        write_last_success(release_dir, manifest)
        print(f"[firmware] active release recorded: {manifest['version']}")
    return result.returncode


def self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="firmware-release-self-test-") as temporary:
        root = Path(temporary)
        build = root / "build"
        output = root / "release"
        keys = root / "keys"
        keys.mkdir()
        private = keys / "private.pem"
        public = keys / "public.pem"
        from hpet_package import _run_node, CRYPTO_SCRIPT

        _run_node([str(CRYPTO_SCRIPT), "generate", str(private), str(public)], timeout=15.0)
        (build / "bootloader").mkdir(parents=True)
        (build / "ftab").mkdir(parents=True)
        (build / "bootloader" / "bootloader.bin").write_bytes(b"boot")
        (build / "main.bin").write_bytes(b"main")
        (build / "ftab" / "ftab.bin").write_bytes(b"ftab")
        layout = {"chip": "SF32LB52", "memory": "NOR", "write_flash": {"verify": True, "files": [
            {"path": "bootloader/bootloader.bin", "address": "0x12010000"},
            {"path": "main.bin", "address": "0x12020000"},
            {"path": "ftab/ftab.bin", "address": "0x12000000"},
        ]}}
        (build / "sftool_param.json").write_bytes(canonical_json(layout) + b"\n")
        create_release(argparse.Namespace(
            build_dir=build, output=output, board=DEFAULT_BOARD, version="self-test",
            private_key=str(private), public_key=str(public),
        ))
        verify_release(argparse.Namespace(release=output, private_key=None, public_key=str(public)))
        (output / "main.bin").write_bytes(b"tampered")
        try:
            validate_release(output, public)
        except FirmwareReleaseError:
            pass
        else:
            raise AssertionError("tampered firmware release passed verification")
    print("firmware_release self-test ok")
    return 0


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create, verify, and apply signed VibeBoard firmware releases.")
    parser.add_argument("--self-test", action="store_true")
    subparsers = parser.add_subparsers(dest="command")
    create = subparsers.add_parser("create", help="package a tested build into a signed release directory")
    create.add_argument("--build-dir", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--version", required=True)
    create.add_argument("--board", default=DEFAULT_BOARD)
    create.add_argument("--private-key", type=Path)
    create.add_argument("--public-key", type=Path)
    verify = subparsers.add_parser("verify", help="verify a signed release directory")
    verify.add_argument("--release", type=Path, required=True)
    verify.add_argument("--public-key", type=Path)
    verify.add_argument("--private-key", type=Path)
    apply = subparsers.add_parser("apply", help="USB flash a verified release; use an earlier release to roll back")
    apply.add_argument("--release", type=Path, required=True)
    apply.add_argument("--port", required=True)
    apply.add_argument("--board")
    apply.add_argument("--public-key", type=Path)
    apply.add_argument("--private-key", type=Path)
    apply.add_argument("--attempts", type=int)
    apply.add_argument("--dry-run", action="store_true")
    apply.add_argument("--skip-runtime-health", action="store_true")
    apply.add_argument("--confirm")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.self_test:
        return self_test()
    if args.command == "create":
        return create_release(args)
    if args.command == "verify":
        return verify_release(args)
    if args.command == "apply":
        return apply_release(args)
    raise FirmwareReleaseError("choose create, verify, or apply")


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except FirmwareReleaseError as exc:
        print(f"firmware_release: {exc}", file=sys.stderr)
        raise SystemExit(1)
    except HpetError as exc:
        print(f"firmware_release: signing helper failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
