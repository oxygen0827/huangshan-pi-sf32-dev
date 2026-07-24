#!/usr/bin/env python3
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
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from runtime_package import load_package_from_dir, validate_package


ROOT_DIR = Path(__file__).resolve().parents[1]
CODEX_PET_APP_DIR = ROOT_DIR / "scripts" / "runtime_apps" / "codex_pet"
CRYPTO_SCRIPT = ROOT_DIR / "scripts" / "hpet_crypto.js"
CONVERTER_SCRIPT = ROOT_DIR / "scripts" / "build_hpet_petdex.js"
DEFAULT_KEY_DIR = Path.home() / ".vibeboard" / "companion" / "keys"
DEFAULT_CACHE_DIR = Path.home() / ".vibeboard" / "companion" / "packages"
HPET_FILES = ("hpet.json", "catalog.txt", "preload.bin", "preview.webp", "signature.ed25519")
PAYLOAD_FILES = ("catalog.txt", "preload.bin", "preview.webp")
BOARD_STATE_MAPPING = {
    "idle": "idle",
    "running": "run",
    "ready": "wave",
    "needs": "review",
    "blocked": "failed",
}
SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,23}$")
MAX_ARCHIVE_BYTES = 4 * 1024 * 1024
MAX_UNCOMPRESSED_BYTES = 3 * 1024 * 1024
EXPECTED_PRELOADED_BYTES = 160 * 173 * 3 * 5 * 2


class HpetError(RuntimeError):
    pass


@dataclass(frozen=True)
class HpetPackage:
    digest: str
    manifest: dict[str, Any]
    files: dict[str, bytes]

    @property
    def slug(self) -> str:
        return str(self.manifest["slug"])


def _node() -> str:
    return os.environ.get("NODE", "node")


def _canonical_json(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _run_node(args: list[str], *, timeout: float = 120.0) -> subprocess.CompletedProcess[str]:
    try:
        return subprocess.run(
            [_node(), *args],
            check=True,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout,
            cwd=ROOT_DIR,
        )
    except FileNotFoundError as exc:
        raise HpetError("Node.js is required for .hpet conversion and Ed25519 verification") from exc
    except subprocess.TimeoutExpired as exc:
        raise HpetError(".hpet helper exceeded its deadline") from exc
    except subprocess.CalledProcessError as exc:
        message = (exc.stderr or exc.stdout or "helper failed").strip()
        raise HpetError(message) from exc


def ensure_signing_keys(key_dir: Path = DEFAULT_KEY_DIR) -> tuple[Path, Path]:
    configured_private = os.environ.get("VIBEBOARD_HPET_PRIVATE_KEY")
    configured_public = os.environ.get("VIBEBOARD_HPET_PUBLIC_KEY")
    if bool(configured_private) != bool(configured_public):
        raise HpetError("VIBEBOARD_HPET_PRIVATE_KEY and VIBEBOARD_HPET_PUBLIC_KEY must be set together")
    if configured_private and configured_public:
        private_path = Path(configured_private).expanduser()
        public_path = Path(configured_public).expanduser()
        if not private_path.is_file() or not public_path.is_file():
            raise HpetError("configured .hpet signing key does not exist")
        return private_path, public_path
    private_path = key_dir.expanduser() / "hpet-ed25519-private.pem"
    public_path = key_dir.expanduser() / "hpet-ed25519-public.pem"
    if not private_path.exists() or not public_path.exists():
        key_dir.mkdir(parents=True, exist_ok=True)
        os.chmod(key_dir, 0o700)
        _run_node([str(CRYPTO_SCRIPT), "generate", str(private_path), str(public_path)], timeout=15.0)
    os.chmod(private_path, 0o600)
    return private_path, public_path


def _sign(payload: bytes, private_key: Path) -> bytes:
    with tempfile.TemporaryDirectory(prefix="hpet-sign-") as temp_text:
        temp = Path(temp_text)
        payload_path = temp / "payload.bin"
        signature_path = temp / "signature.bin"
        payload_path.write_bytes(payload)
        _run_node([str(CRYPTO_SCRIPT), "sign", str(private_key), str(payload_path), str(signature_path)], timeout=15.0)
        signature = signature_path.read_bytes()
    if len(signature) != 64:
        raise HpetError("Ed25519 helper returned an invalid signature")
    return signature


def _verify(payload: bytes, signature: bytes, public_key: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="hpet-verify-") as temp_text:
        temp = Path(temp_text)
        payload_path = temp / "payload.bin"
        signature_path = temp / "signature.bin"
        payload_path.write_bytes(payload)
        signature_path.write_bytes(signature)
        _run_node([str(CRYPTO_SCRIPT), "verify", str(public_key), str(payload_path), str(signature_path)], timeout=15.0)


def _zip_bytes(files: Mapping[str, bytes]) -> bytes:
    from io import BytesIO

    output = BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in HPET_FILES:
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, files[name])
    return output.getvalue()


def _validated_text(value: object, label: str, limit: int) -> str:
    if not isinstance(value, str) or not value.strip() or len(value) > limit or "\x00" in value:
        raise HpetError(f"invalid {label}")
    return value.strip()


def _validate_manifest(manifest: object) -> dict[str, Any]:
    if not isinstance(manifest, dict):
        raise HpetError("hpet.json must be an object")
    if manifest.get("schemaVersion") != 1 or manifest.get("kind") != "vibeboard-codex-pet":
        raise HpetError("unsupported .hpet manifest")
    slug = _validated_text(manifest.get("slug"), "slug", 24)
    if SAFE_SLUG.fullmatch(slug) is None:
        raise HpetError("invalid .hpet slug")
    _validated_text(manifest.get("name"), "name", 80)
    _validated_text(manifest.get("author"), "author", 80)
    _validated_text(manifest.get("sourceUrl"), "sourceUrl", 500)
    source_sha = _validated_text(manifest.get("sourceSha256"), "sourceSha256", 64)
    if re.fullmatch(r"[0-9a-f]{64}", source_sha) is None:
        raise HpetError("invalid source SHA-256")
    target = manifest.get("target")
    if not isinstance(target, dict) or target.get("appId") != "codex_pet":
        raise HpetError(".hpet target must be codex_pet")
    if target.get("width") != 160 or target.get("height") != 173:
        raise HpetError("unsupported .hpet frame dimensions")
    if target.get("framesPerState") != 2 or target.get("frameMs") != 180:
        raise HpetError("unsupported .hpet animation timing")
    states = manifest.get("states")
    if states != BOARD_STATE_MAPPING:
        raise HpetError(".hpet must provide the five semantic Codex Pet states")
    file_rows = manifest.get("files")
    if not isinstance(file_rows, list) or len(file_rows) != len(PAYLOAD_FILES):
        raise HpetError("invalid .hpet file index")
    indexed: dict[str, dict[str, Any]] = {}
    for row in file_rows:
        if not isinstance(row, dict) or row.get("path") not in PAYLOAD_FILES:
            raise HpetError("invalid .hpet file entry")
        path = str(row["path"])
        if path in indexed or not isinstance(row.get("size"), int) or not 0 < int(row["size"]) <= MAX_UNCOMPRESSED_BYTES:
            raise HpetError("invalid .hpet file size")
        if re.fullmatch(r"[0-9a-f]{64}", str(row.get("sha256") or "")) is None:
            raise HpetError("invalid .hpet file SHA-256")
        indexed[path] = row
    if set(indexed) != set(PAYLOAD_FILES):
        raise HpetError(".hpet file index is incomplete")
    return dict(manifest)


def build_hpet_from_conversion(
    conversion_dir: Path,
    conversion: Mapping[str, Any],
    output_path: Path,
    *,
    private_key: Path,
) -> HpetPackage:
    payloads = {name: (conversion_dir / name).read_bytes() for name in PAYLOAD_FILES}
    manifest: dict[str, Any] = {
        "schemaVersion": 1,
        "kind": "vibeboard-codex-pet",
        "slug": str(conversion["slug"]),
        "name": str(conversion["name"]),
        "author": str(conversion["author"]),
        "license": str(conversion.get("license") or "unspecified"),
        "sourceUrl": str(conversion["sourceUrl"]),
        "sourceSha256": str(conversion["sourceSha256"]),
        "converterVersion": 1,
        "target": {
            "appId": "codex_pet",
            "runtimeProfile": "huangshan-pi",
            "width": 160,
            "height": 173,
            "framesPerState": 2,
            "frameMs": 180,
            "preloadedBytes": EXPECTED_PRELOADED_BYTES,
        },
        "states": dict(BOARD_STATE_MAPPING),
        "files": [
            {"path": name, "size": len(payloads[name]), "sha256": _sha256(payloads[name])}
            for name in PAYLOAD_FILES
        ],
    }
    manifest = _validate_manifest(manifest)
    manifest_bytes = _canonical_json(manifest) + b"\n"
    signature = _sign(b"HPET1\n" + manifest_bytes, private_key)
    archive_files = {"hpet.json": manifest_bytes, **payloads, "signature.ed25519": signature}
    blob = _zip_bytes(archive_files)
    if len(blob) > MAX_ARCHIVE_BYTES:
        raise HpetError(f".hpet archive exceeds {MAX_ARCHIVE_BYTES} bytes")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(blob)
    return HpetPackage(_sha256(blob), manifest, archive_files)


def read_hpet(blob: bytes, *, public_key: Path) -> HpetPackage:
    if not blob or len(blob) > MAX_ARCHIVE_BYTES:
        raise HpetError("invalid .hpet archive size")
    from io import BytesIO

    files: dict[str, bytes] = {}
    try:
        with zipfile.ZipFile(BytesIO(blob), "r") as archive:
            infos = archive.infolist()
            if len(infos) != len(HPET_FILES) or {item.filename for item in infos} != set(HPET_FILES):
                raise HpetError(".hpet contains unexpected files")
            if any(item.is_dir() or item.filename.startswith(("/", "\\")) or ".." in Path(item.filename).parts for item in infos):
                raise HpetError(".hpet contains an unsafe path")
            if sum(item.file_size for item in infos) > MAX_UNCOMPRESSED_BYTES:
                raise HpetError(".hpet uncompressed payload is too large")
            for info in infos:
                files[info.filename] = archive.read(info)
    except (zipfile.BadZipFile, OSError) as exc:
        raise HpetError("invalid .hpet ZIP archive") from exc
    try:
        manifest_raw = json.loads(files["hpet.json"].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise HpetError("invalid hpet.json") from exc
    manifest = _validate_manifest(manifest_raw)
    canonical = _canonical_json(manifest) + b"\n"
    if files["hpet.json"] != canonical:
        raise HpetError("hpet.json is not canonically encoded")
    rows = {str(row["path"]): row for row in manifest["files"]}
    for name in PAYLOAD_FILES:
        if len(files[name]) != rows[name]["size"] or _sha256(files[name]) != rows[name]["sha256"]:
            raise HpetError(f".hpet payload integrity failed: {name}")
    if len(files["signature.ed25519"]) != 64:
        raise HpetError("invalid Ed25519 signature length")
    _verify(b"HPET1\n" + canonical, files["signature.ed25519"], public_key)
    expected_catalog = f"VBPETS1\n{manifest['slug']}|{manifest['name']}|{manifest['author']}\n".encode("utf-8")
    if files["catalog.txt"] != expected_catalog:
        raise HpetError("catalog.txt does not match hpet.json")
    return HpetPackage(_sha256(blob), manifest, files)


def compose_codex_pet_runtime(package: HpetPackage) -> tuple[str, dict[str, bytes]]:
    package_id, files = load_package_from_dir(CODEX_PET_APP_DIR, "codex_pet")
    files["assets/pets/catalog.txt"] = package.files["catalog.txt"]
    files["assets/pets/preload.bin"] = package.files["preload.bin"]
    return validate_package(package_id, files)


def build_petdex_hpet(
    entry: Mapping[str, Any],
    *,
    cache_dir: Path = DEFAULT_CACHE_DIR,
    key_dir: Path = DEFAULT_KEY_DIR,
) -> tuple[Path, HpetPackage, Path]:
    private_key, public_key = ensure_signing_keys(key_dir)
    with tempfile.TemporaryDirectory(prefix="petdex-convert-") as temp_text:
        temp = Path(temp_text)
        entry_path = temp / "entry.json"
        conversion_dir = temp / "converted"
        entry_path.write_text(json.dumps(dict(entry), ensure_ascii=False), encoding="utf-8")
        _run_node(
            [str(CONVERTER_SCRIPT), "--entry", str(entry_path), "--output", str(conversion_dir)],
            timeout=180.0,
        )
        conversion = json.loads((conversion_dir / "conversion.json").read_text(encoding="utf-8"))
        temporary_hpet = temp / f"{conversion['slug']}.hpet"
        package = build_hpet_from_conversion(conversion_dir, conversion, temporary_hpet, private_key=private_key)
        cache_dir = cache_dir.expanduser()
        cache_dir.mkdir(parents=True, exist_ok=True)
        output_path = cache_dir / f"{package.digest}.hpet"
        if not output_path.exists():
            shutil.copyfile(temporary_hpet, output_path)
        validated = read_hpet(output_path.read_bytes(), public_key=public_key)
    return output_path, validated, public_key


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="hpet-package-") as temp_text:
        temp = Path(temp_text)
        key_dir = temp / "keys"
        private_key, public_key = ensure_signing_keys(key_dir)
        converted = temp / "converted"
        converted.mkdir()
        catalog = CODEX_PET_APP_DIR / "assets" / "pets" / "catalog.txt"
        preload = CODEX_PET_APP_DIR / "assets" / "pets" / "preload.bin"
        converted.joinpath("catalog.txt").write_bytes(catalog.read_bytes())
        converted.joinpath("preload.bin").write_bytes(preload.read_bytes())
        converted.joinpath("preview.webp").write_bytes(b"RIFF\x04\x00\x00\x00WEBP")
        catalog_fields = catalog.read_text(encoding="utf-8").splitlines()[1].split("|")
        source_path = CODEX_PET_APP_DIR / "assets" / "pets" / catalog_fields[0] / "source.json"
        source = json.loads(source_path.read_text(encoding="utf-8"))
        conversion = {
            "slug": catalog_fields[0],
            "name": catalog_fields[1],
            "author": catalog_fields[2],
            "license": "unspecified",
            "sourceUrl": source["sourceUrl"],
            "sourceSha256": source["sourceSha256"],
        }
        output = temp / "pet.hpet"
        built = build_hpet_from_conversion(converted, conversion, output, private_key=private_key)
        loaded = read_hpet(output.read_bytes(), public_key=public_key)
        if loaded.digest != built.digest or loaded.slug != catalog_fields[0]:
            raise AssertionError(".hpet round trip failed")
        package_id, runtime_files = compose_codex_pet_runtime(loaded)
        if package_id != "codex_pet" or runtime_files["assets/pets/preload.bin"] != preload.read_bytes():
            raise AssertionError(".hpet Runtime composition failed")
        tampered = bytearray(output.read_bytes())
        tampered[-8] ^= 1
        try:
            read_hpet(bytes(tampered), public_key=public_key)
        except HpetError:
            pass
        else:
            raise AssertionError("tampered .hpet passed validation")
    print("hpet_package self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and verify signed VibeBoard .hpet packages")
    parser.add_argument("--entry", type=Path, help="Petdex manifest entry JSON")
    parser.add_argument("--output", type=Path, help="Copy the generated package to this path")
    parser.add_argument("--verify", type=Path, help="Verify an existing .hpet")
    parser.add_argument("--public-key", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    if args.verify:
        public_key = args.public_key or ensure_signing_keys()[1]
        package = read_hpet(args.verify.read_bytes(), public_key=public_key)
        print(json.dumps({"ok": True, "digest": package.digest, "manifest": package.manifest}, ensure_ascii=False))
        return 0
    if not args.entry:
        parser.error("--entry or --verify is required")
    entry = json.loads(args.entry.read_text(encoding="utf-8"))
    if not isinstance(entry, dict):
        parser.error("--entry must contain a JSON object")
    path, package, _ = build_petdex_hpet(entry)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, args.output)
        path = args.output
    print(json.dumps({"path": str(path), "digest": package.digest, "manifest": package.manifest}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except HpetError as exc:
        print(f"hpet_package: {exc}", file=sys.stderr)
        raise SystemExit(1)
