#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from pathlib import Path


SAFE_APP_ID = re.compile(r"^[a-z][a-z0-9_]{0,14}$")
SAFE_PATH = re.compile(
    r"^(manifest\.json|app\.info|main\.lua|files\.txt|README\.md|"
    r"(?:assets|images|fonts|lib)/[A-Za-z0-9_./-]+\."
    r"(?:json|txt|png|jpg|jpeg|bin|ttf|otf|lua))$"
)


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    sys.exit(code)


def safe_package_id(value: str | None) -> str:
    if not value or not SAFE_APP_ID.match(value):
        fail(f"Unsafe runtime app id: {value!r}")
    return value


def safe_package_path(value: str) -> str:
    value = value.replace("\\", "/")
    if value.startswith("/") or ".." in value or "//" in value or not SAFE_PATH.match(value):
        fail(f"Unsafe package path: {value!r}")
    return value


def validate_package(package_id: str, files: dict[str, bytes]) -> tuple[str, dict[str, bytes]]:
    safe_package_id(package_id)
    if "main.lua" not in files:
        fail("Runtime package must include main.lua")
    if "manifest.json" not in files and "app.info" not in files:
        fail("Runtime package must include manifest.json or app.info")
    return package_id, dict(sorted(files.items()))


def load_package_from_dir(package_dir: Path, app_id: str | None) -> tuple[str, dict[str, bytes]]:
    package_dir = package_dir.resolve()
    if not package_dir.is_dir():
        fail(f"Package directory does not exist: {package_dir}")
    package_id = safe_package_id(app_id or package_dir.name)
    files: dict[str, bytes] = {}
    for path in sorted(package_dir.rglob("*")):
        if not path.is_file():
            continue
        rel = safe_package_path(path.relative_to(package_dir).as_posix())
        files[rel] = path.read_bytes()
    return validate_package(package_id, files)


def load_package_from_json(package_json: Path, app_id: str | None) -> tuple[str, dict[str, bytes]]:
    data = json.loads(package_json.read_text(encoding="utf-8"))
    package_id = safe_package_id(
        app_id or data.get("app", {}).get("packageId") or data.get("app", {}).get("appId")
    )
    raw_files = data.get("files") or {}
    if not isinstance(raw_files, dict):
        fail("Runtime package JSON must contain a files object")
    files = {
        safe_package_path(path): str(contents if contents is not None else "").encode("utf-8")
        for path, contents in raw_files.items()
    }
    return validate_package(package_id, files)


def build_install_commands(package_id: str, files: dict[str, bytes], chunk_bytes: int) -> list[str]:
    commands = [f"vb_runtime_install_begin {package_id}"]
    for path, data in sorted(files.items()):
        if not data:
            commands.append(f"vb_runtime_install_file {package_id} {path} 0 -")
            continue
        for offset in range(0, len(data), chunk_bytes):
            chunk = data[offset : offset + chunk_bytes]
            commands.append(f"vb_runtime_install_file {package_id} {path} {offset} {chunk.hex()}")
    commands.append(f"vb_runtime_install_end {package_id}")
    return commands
