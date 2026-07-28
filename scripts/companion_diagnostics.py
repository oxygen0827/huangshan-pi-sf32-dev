#!/usr/bin/env python3
"""Redacted one-click support bundles for the loopback Companion service."""
from __future__ import annotations

import asyncio
import inspect
import json
import re
import time
import zipfile
from pathlib import Path
from typing import Any


SECRET_KEY = re.compile(r"(authorization|cookie|token|secret|password|private.?key|api.?key|hook.*command)", re.I)
SECRET_VALUE = re.compile(r"(?i)(bearer\s+)[a-z0-9._~+/-]+|((?:api[_ -]?key|token|secret|password)\s*[=:]\s*)\S+")


def redact(value: object, key: str = "") -> object:
    if SECRET_KEY.search(key):
        return "[redacted]"
    if isinstance(value, dict):
        return {str(name): redact(item, str(name)) for name, item in value.items()}
    if isinstance(value, list):
        return [redact(item, key) for item in value]
    if isinstance(value, str):
        return SECRET_VALUE.sub(lambda match: (match.group(1) or match.group(2) or "") + "[redacted]", value)
    return value


async def _command_snapshot(device: object) -> dict[str, object]:
    commands = getattr(device, "commands", None)
    if commands is None:
        return {"connected": False, "reason": "no board transport"}
    snapshot: dict[str, object] = {"connected": bool(getattr(device, "connected", False))}
    for name in ("status", "capabilities", "app_status", "codex_pet", "power", "sensors", "audio"):
        method = getattr(commands, name, None)
        if not callable(method):
            continue
        try:
            value = method()
            if inspect.isawaitable(value):
                value = await asyncio.wait_for(value, timeout=8)
            try:
                snapshot[name] = json.loads(value) if isinstance(value, str) and value.lstrip().startswith("{") else value
            except ValueError:
                snapshot[name] = value
        except Exception as exc:
            snapshot[name] = {"error": type(exc).__name__}
    return snapshot


def _log_tail(path: Path, limit: int = 96 * 1024) -> str:
    try:
        with path.open("rb") as handle:
            handle.seek(0, 2)
            handle.seek(max(0, handle.tell() - limit))
            return handle.read().decode("utf-8", "replace")
    except OSError:
        return ""


async def create_support_bundle(state: object) -> Path:
    state_dir = Path(getattr(state, "state_dir"))
    output_dir = state_dir / "support"
    output_dir.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    target = output_dir / f"vibeboard-support-{stamp}.zip"
    jobs = getattr(state, "jobs", {})
    job_rows = []
    for job in list(jobs.values())[-20:]:
        to_dict = getattr(job, "to_dict", None)
        if callable(to_dict):
            job_rows.append(to_dict())
    payload = {
        "schemaVersion": 1,
        "createdAt": int(time.time()),
        "companion": getattr(state, "status")(),
        "firmware": getattr(state, "firmware").status(),
        "board": await _command_snapshot(getattr(state, "device")),
        "recentJobs": job_rows,
        "redaction": "credentials, tokens, private keys, and hook commands are removed",
    }
    log_dir = Path.home() / ".vibeboard" / "companion"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        archive.writestr("diagnostics.json", json.dumps(redact(payload), ensure_ascii=False, indent=2) + "\n")
        for name in ("companion.log", "companion.previous.log"):
            text = _log_tail(log_dir / name)
            if text:
                archive.writestr(f"logs/{name}", str(redact(text)) + "\n")
    return target


def run_self_test() -> None:
    value = redact({"token": "secret", "nested": {"password": "pw"}, "message": "Bearer abc123"})
    assert value == {"token": "[redacted]", "nested": {"password": "[redacted]"}, "message": "Bearer [redacted]"}
    value = redact({"hookCommand": "python deploy.py", "safe": "hello"})
    assert value == {"hookCommand": "[redacted]", "safe": "hello"}
    print("companion_diagnostics self-test ok")
