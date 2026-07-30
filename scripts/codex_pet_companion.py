#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import re
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Callable, Mapping, Protocol

from codex_pet_appserver import CodexAppServerClient, CodexAppServerError, public_error, resolve_codex_bin
from companion_paths import companion_root
from companion_firmware import FirmwareManager, FirmwareManagerError
from companion_diagnostics import create_support_bundle
from companion_diagnostics import run_self_test as diagnostics_self_test
from companion_firmware import run_self_test as firmware_self_test
from codex_pet_firmware_update import check_for_firmware_update
from codex_pet_firmware_update import run_self_test as firmware_update_check_self_test
from companion_state import JobJournal, PackageCache
from companion_state import run_self_test as companion_state_self_test
from firmware_release import FirmwareReleaseError
from hpet_package import (
    DEFAULT_CACHE_DIR,
    DEFAULT_KEY_DIR,
    HpetError,
    HpetPackage,
    build_petdex_hpet,
    compose_codex_pet_runtime,
    ensure_signing_keys,
    read_hpet,
)
from runtime_transport import load_ble_cache, save_ble_cache


ROOT_DIR = companion_root()
WEB_PATH = ROOT_DIR / "scripts" / "codex_pet_web.html"
PETDEX_MANIFEST_URL = "https://petdex.dev/api/manifest"
PETDEX_CONFIG_PATH = ROOT_DIR / "scripts" / "petdex_pets.json"
PETDEX_STATE_CONTRACT_PATH = ROOT_DIR / "scripts" / "petdex_state_contract.json"
DEFAULT_STATE_DIR = Path.home() / ".vibeboard" / "companion"
DEFAULT_HOOKS_PATH = Path.home() / ".codex" / "hooks.json"
SESSION_TTL_SECONDS = 15 * 60
CATALOG_TTL_SECONDS = 10 * 60
MAX_JSON_BYTES = 64 * 1024
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_SPRITESHEET_BYTES = 16 * 1024 * 1024
SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,23}$")
SAFE_DIGEST = re.compile(r"^[0-9a-f]{64}$")
SAFE_SUPPORT_BUNDLE = re.compile(r"^vibeboard-support-[0-9]{8}-[0-9]{6}-[a-z0-9-]{1,32}[.]zip$")
HOOK_EVENTS = ("SessionStart", "PermissionRequest", "UserPromptSubmit", "PreToolUse", "PostToolUse", "Stop")
_PETDEX_STATE_CONTRACT = json.loads(PETDEX_STATE_CONTRACT_PATH.read_text(encoding="utf-8"))
PETDEX_STATE_ROWS = {
    str(state["id"]): int(state["row"])
    for state in _PETDEX_STATE_CONTRACT["states"]
}
if _PETDEX_STATE_CONTRACT.get("schemaVersion") != 1 or list(PETDEX_STATE_ROWS.values()) != list(range(9)):
    raise RuntimeError("invalid Petdex state contract")
HOOK_WIRE_EVENTS = {
    "SessionStart": "sessionStart",
    "PermissionRequest": "permissionRequest",
    "UserPromptSubmit": "userPromptSubmit",
    "PreToolUse": "preToolUse",
    "PostToolUse": "postToolUse",
    "Stop": "stop",
}
HOOK_TRUST_REFRESH_SECONDS = 60.0


class CompanionError(RuntimeError):
    pass


def _cached_v2_hpet_for_slug(
    cache_dir: Path,
    public_key: Path,
    slug: str,
    *,
    reader: Callable[..., HpetPackage] = read_hpet,
) -> tuple[Path, HpetPackage]:
    try:
        candidates = sorted(
            cache_dir.glob("*.hpet"),
            key=lambda path: path.stat().st_mtime,
            reverse=True,
        )
    except OSError as exc:
        raise HpetError("could not inspect the local .hpet cache") from exc
    for path in candidates:
        try:
            package = reader(path.read_bytes(), public_key=public_key)
        except (HpetError, OSError):
            continue
        target = package.manifest.get("target")
        if (
            package.slug == slug
            and package.manifest.get("schemaVersion") == 2
            and isinstance(target, dict)
            and target.get("preloadVersion") == 2
            and target.get("stateCount") == 9
            and target.get("frameMs") == 120
        ):
            return path, package
    raise HpetError(f"no verified nine-state v2 package is cached for {slug}")


class CompanionDevice(Protocol):
    connected: bool
    commands: object

    async def reconnect(self, *, force_fresh: bool = False) -> None: ...

    async def install_codex_pet(
        self,
        files: dict[str, bytes],
        slug: str,
        *,
        progress: Any | None = None,
    ) -> dict[str, object]: ...


def _atomic_json(path: Path, value: object, *, mode: int = 0o600) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(name, mode)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def _read_json(path: Path, default: object) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return default


def _coerce_float(value: object, default: float) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if result == result else default


def _coerce_progress(value: object) -> int:
    try:
        return max(0, min(100, int(value or 0)))
    except (TypeError, ValueError):
        return 0


def _fetch_json(url: str, *, max_bytes: int, timeout: float = 20.0) -> object:
    request = urllib.request.Request(
        _valid_petdex_manifest_url(url),
        headers={"User-Agent": "VibeBoard-Companion/1.0"},
    )
    opener = urllib.request.build_opener(_PetdexManifestRedirectHandler())
    with opener.open(request, timeout=timeout) as response:
        declared = int(response.headers.get("content-length") or 0)
        if declared > max_bytes:
            raise CompanionError("Petdex manifest is too large")
        data = response.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise CompanionError("Petdex manifest is too large")
    return json.loads(data.decode("utf-8"))


def _fetch_bytes(url: str, *, max_bytes: int, timeout: float = 25.0) -> tuple[bytes, str]:
    request = urllib.request.Request(
        _valid_petdex_asset_url(url),
        headers={"User-Agent": "VibeBoard-Companion/1.0"},
    )
    opener = urllib.request.build_opener(_PetdexAssetRedirectHandler())
    with opener.open(request, timeout=timeout) as response:
        declared = int(response.headers.get("content-length") or 0)
        if declared > max_bytes:
            raise CompanionError("Petdex spritesheet is too large")
        data = response.read(max_bytes + 1)
        content_type = str(response.headers.get("content-type") or "application/octet-stream").split(";", 1)[0]
    if len(data) > max_bytes:
        raise CompanionError("Petdex spritesheet is too large")
    if content_type not in {"image/webp", "image/png"}:
        raise CompanionError("Petdex spritesheet has an unsupported content type")
    return data, content_type


def _valid_petdex_asset_url(value: object) -> str:
    text = str(value or "")
    parsed = urllib.parse.urlsplit(text)
    try:
        port = parsed.port
    except ValueError as exc:
        raise CompanionError("Petdex entry contains a non-allowlisted asset URL") from exc
    if (
        parsed.scheme != "https"
        or parsed.hostname != "assets.petdex.dev"
        or parsed.username is not None
        or parsed.password is not None
        or port not in (None, 443)
        or bool(parsed.fragment)
    ):
        raise CompanionError("Petdex entry contains a non-allowlisted asset URL")
    return text


def _valid_petdex_redirect_url(current: str, location: str) -> str:
    if not location:
        raise CompanionError("Petdex asset redirect has no location")
    return _valid_petdex_asset_url(urllib.parse.urljoin(current, location))


class _PetdexAssetRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(
        self,
        request: urllib.request.Request,
        file_pointer: Any,
        code: int,
        message: str,
        headers: Any,
        new_url: str,
    ) -> urllib.request.Request | None:
        safe_url = _valid_petdex_redirect_url(request.full_url, new_url)
        return super().redirect_request(
            request, file_pointer, code, message, headers, safe_url
        )


def _valid_petdex_manifest_url(value: object) -> str:
    text = str(value or "")
    parsed = urllib.parse.urlsplit(text)
    try:
        port = parsed.port
    except ValueError as exc:
        raise CompanionError("Petdex manifest contains an invalid redirect URL") from exc
    if (
        parsed.scheme != "https"
        or parsed.hostname != "petdex.dev"
        or parsed.username is not None
        or parsed.password is not None
        or port not in (None, 443)
        or bool(parsed.fragment)
    ):
        raise CompanionError("Petdex manifest redirect left the allowlist")
    return text


class _PetdexManifestRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(
        self,
        request: urllib.request.Request,
        file_pointer: Any,
        code: int,
        message: str,
        headers: Any,
        new_url: str,
    ) -> urllib.request.Request | None:
        safe_url = _valid_petdex_manifest_url(urllib.parse.urljoin(request.full_url, new_url))
        return super().redirect_request(request, file_pointer, code, message, headers, safe_url)


def normalize_petdex_entry(value: Mapping[str, object]) -> dict[str, object]:
    slug = str(value.get("slug") or "")
    if SAFE_SLUG.fullmatch(slug) is None:
        raise CompanionError("Petdex entry has an invalid slug")
    name = " ".join(str(value.get("displayName") or value.get("name") or slug).split())[:80] or slug
    author = " ".join(str(value.get("submittedBy") or value.get("author") or "Petdex creator").split())[:80]
    return {
        "slug": slug,
        "displayName": name,
        "submittedBy": author,
        "kind": " ".join(str(value.get("kind") or "pet").split())[:32],
        "license": " ".join(str(value.get("license") or "unspecified").split())[:120],
        "spritesheetUrl": _valid_petdex_asset_url(value.get("spritesheetUrl")),
        "petJsonUrl": _valid_petdex_asset_url(value.get("petJsonUrl")),
        "sourceUrl": f"https://petdex.dev/pets/{slug}",
        "previewUrl": f"/api/pets/{slug}/spritesheet",
        "stateRows": dict(PETDEX_STATE_ROWS),
    }


class PetdexCatalog:
    def __init__(self, *, cache_path: Path, manifest_url: str = PETDEX_MANIFEST_URL) -> None:
        self.cache_path = cache_path
        self.manifest_url = manifest_url
        self._pets: dict[str, dict[str, object]] = {}
        self._loaded_at = 0.0
        self._live = False
        self._error: str | None = None
        self._refreshing = False
        self._lock = threading.Lock()

    def _fallback(self) -> list[dict[str, object]]:
        value = _read_json(PETDEX_CONFIG_PATH, {})
        rows = value.get("pets") if isinstance(value, dict) else []
        output = []
        for row in rows if isinstance(rows, list) else []:
            if not isinstance(row, dict):
                continue
            candidate = dict(row)
            candidate["displayName"] = candidate.get("name")
            candidate["submittedBy"] = candidate.get("author")
            candidate.setdefault("kind", "pet")
            try:
                output.append(normalize_petdex_entry(candidate))
            except CompanionError:
                continue
        return output

    def refresh(self, *, force: bool = False) -> tuple[list[dict[str, object]], bool, str | None]:
        with self._lock:
            if self._pets and not force and time.time() - self._loaded_at < CATALOG_TTL_SECONDS:
                return list(self._pets.values()), self._live, self._error

        if not force:
            cached = _read_json(self.cache_path, {})
            saved_at = cached.get("savedAt") if isinstance(cached, dict) else None
            cached_rows = cached.get("pets") if isinstance(cached, dict) else None
            rows = []
            for raw in cached_rows if isinstance(cached_rows, list) else []:
                if isinstance(raw, dict):
                    try:
                        rows.append(normalize_petdex_entry(raw))
                    except CompanionError:
                        continue
            if rows:
                live = isinstance(saved_at, int) and time.time() - saved_at < CATALOG_TTL_SECONDS
                error = None if live else "stale_cache"
                with self._lock:
                    self._pets = {str(row["slug"]): row for row in rows}
                    self._loaded_at = time.time()
                    self._live = live
                    self._error = error
                if not live:
                    self.refresh_in_background()
                return rows, live, error
            rows = self._fallback()
            if rows:
                with self._lock:
                    self._pets = {str(row["slug"]): row for row in rows}
                    self._loaded_at = time.time()
                    self._live = False
                    self._error = "bootstrap_catalog"
                self.refresh_in_background()
                return rows, False, "bootstrap_catalog"

        error: str | None = None
        rows = []
        try:
            value = _fetch_json(self.manifest_url, max_bytes=MAX_MANIFEST_BYTES)
            raw_rows = value.get("pets") if isinstance(value, dict) else None
            if not isinstance(raw_rows, list):
                raise CompanionError("Petdex manifest has no pet list")
            for raw in raw_rows:
                if not isinstance(raw, dict):
                    continue
                try:
                    rows.append(normalize_petdex_entry(raw))
                except CompanionError:
                    continue
            if not rows:
                raise CompanionError("Petdex manifest has no compatible entries")
            _atomic_json(self.cache_path, {"savedAt": int(time.time()), "pets": rows})
        except (OSError, ValueError, urllib.error.URLError, CompanionError) as exc:
            error = type(exc).__name__
            cached = _read_json(self.cache_path, {})
            cached_rows = cached.get("pets") if isinstance(cached, dict) else None
            for raw in cached_rows if isinstance(cached_rows, list) else []:
                if isinstance(raw, dict):
                    try:
                        rows.append(normalize_petdex_entry(raw))
                    except CompanionError:
                        continue
            if not rows:
                rows = self._fallback()
        pets = {str(row["slug"]): row for row in rows}
        with self._lock:
            self._pets = pets
            self._loaded_at = time.time()
            self._live = error is None
            self._error = error
        return list(pets.values()), error is None, error

    def refresh_in_background(self) -> None:
        with self._lock:
            if self._refreshing:
                return
            self._refreshing = True

        def run() -> None:
            try:
                self.refresh(force=True)
            finally:
                with self._lock:
                    self._refreshing = False

        threading.Thread(target=run, name="petdex-catalog-refresh", daemon=True).start()

    def list(self, query: str = "", offset: int = 0, limit: int = 24) -> dict[str, object]:
        rows, live, error = self.refresh()
        query = " ".join(query.lower().split())[:80]
        if query:
            rows = [row for row in rows if query in f"{row['slug']} {row['displayName']} {row['submittedBy']}".lower()]
        rows.sort(key=lambda row: (str(row["slug"]) != "shinchan", str(row["displayName"]).lower()))
        total = len(rows)
        offset = max(0, offset)
        limit = max(1, min(limit, 60))
        return {"pets": rows[offset:offset + limit], "total": total, "offset": offset, "limit": limit, "live": live, "error": error}

    def get(self, slug: str) -> dict[str, object]:
        if SAFE_SLUG.fullmatch(slug) is None:
            raise CompanionError("invalid pet slug")
        self.refresh()
        pet = self._pets.get(slug)
        if pet is None:
            self.refresh(force=True)
            pet = self._pets.get(slug)
        if pet is None:
            raise CompanionError("Petdex pet not found")
        return dict(pet)


def _hook_trust_remediation() -> dict[str, object]:
    return {
        "action": "review_hooks",
        "command": "/hooks",
        "restartRequired": True,
    }


def _hook_trust_snapshot(
    trust_status: str,
    *,
    trusted: bool | None,
    checked: bool,
    events: list[str] | None = None,
    error: str | None = None,
) -> dict[str, object]:
    result: dict[str, object] = {
        "trusted": trusted,
        "trustStatus": trust_status,
        "trustChecked": checked,
        "untrustedEvents": list(events or []),
        "trustError": error,
        "trustCheckedAt": int(time.time() * 1000) if checked else 0,
    }
    if trusted is not True and trust_status not in {"checking", "not_bound"}:
        result["remediation"] = _hook_trust_remediation()
    return result


def parse_codex_hook_trust(
    value: object,
    *,
    hooks_path: Path,
    hook_script: Path,
) -> dict[str, object]:
    data = value.get("data") if isinstance(value, dict) else None
    if not isinstance(data, list):
        raise CompanionError("Codex hooks/list response has no data list")
    expected_by_wire = {wire: event for event, wire in HOOK_WIRE_EVENTS.items()}
    status_rank = {"trusted": 0, "managed": 0, "disabled": 1, "untrusted": 2, "modified": 3}
    observed: dict[str, list[str]] = {event: [] for event in HOOK_EVENTS}
    resolved_hooks_path = hooks_path.expanduser().resolve()
    resolved_hook_script = hook_script.expanduser().resolve()

    for entry in data:
        hooks = entry.get("hooks") if isinstance(entry, dict) else None
        for hook in hooks if isinstance(hooks, list) else []:
            if not isinstance(hook, dict):
                continue
            source_path = hook.get("sourcePath")
            command = hook.get("command")
            wire_event = hook.get("eventName")
            if not isinstance(source_path, str) or not isinstance(command, str) or not isinstance(wire_event, str):
                continue
            if Path(source_path).expanduser().resolve() != resolved_hooks_path:
                continue
            if str(resolved_hook_script) not in command or "codex_pet_hook.py" not in command:
                continue
            event = expected_by_wire.get(wire_event)
            if event is None:
                continue
            trust = hook.get("trustStatus")
            if not isinstance(trust, str) or trust not in status_rank:
                trust = "untrusted"
            if hook.get("enabled") is not True:
                trust = "disabled"
            observed[event].append(trust)

    missing = [event for event in HOOK_EVENTS if not observed[event]]
    event_status = {
        event: max(statuses, key=lambda item: status_rank[item])
        for event, statuses in observed.items()
        if statuses
    }
    modified = [event for event in HOOK_EVENTS if event_status.get(event) == "modified"]
    untrusted = [event for event in HOOK_EVENTS if event_status.get(event) == "untrusted"]
    disabled = [event for event in HOOK_EVENTS if event_status.get(event) == "disabled"]
    if modified:
        aggregate = "modified"
        attention = modified + untrusted + disabled + missing
    elif untrusted:
        aggregate = "untrusted"
        attention = untrusted + disabled + missing
    elif disabled:
        aggregate = "disabled"
        attention = disabled + missing
    elif missing:
        aggregate = "incomplete"
        attention = missing
    else:
        aggregate = "trusted"
        attention = []
    return _hook_trust_snapshot(
        aggregate,
        trusted=aggregate == "trusted",
        checked=True,
        events=list(dict.fromkeys(attention)),
    )


class CodexHookTrustProbe:
    def __init__(
        self,
        *,
        loop: asyncio.AbstractEventLoop,
        workspace: Path,
        hooks_path: Path,
        hook_script: Path,
        refresh_seconds: float = HOOK_TRUST_REFRESH_SECONDS,
    ) -> None:
        self.loop = loop
        self.workspace = workspace.expanduser().resolve()
        self.hooks_path = hooks_path.expanduser().resolve()
        self.hook_script = hook_script.expanduser().resolve()
        self.refresh_seconds = refresh_seconds
        self._lock = threading.Lock()
        self._snapshot = _hook_trust_snapshot("checking", trusted=None, checked=False)
        self._fingerprint: tuple[tuple[int, int], ...] | None = None
        self._checked_at = 0.0
        self._refreshing = False

    def _source_fingerprint(self) -> tuple[tuple[int, int], ...]:
        paths = [self.hooks_path, self.hooks_path.with_name("config.toml")]
        try:
            paths.append(Path(resolve_codex_bin()).expanduser().resolve())
        except CodexAppServerError:
            pass
        values = []
        for path in paths:
            try:
                stat = path.stat()
                values.append((stat.st_mtime_ns, stat.st_size))
            except OSError:
                values.append((0, 0))
        return tuple(values)

    def invalidate(self) -> None:
        with self._lock:
            self._checked_at = 0.0
            self._fingerprint = None

    def status(self, *, bound: bool) -> dict[str, object]:
        if not bound:
            return _hook_trust_snapshot("not_bound", trusted=False, checked=True)
        fingerprint = self._source_fingerprint()
        schedule = False
        with self._lock:
            stale = (
                self._fingerprint != fingerprint
                or time.monotonic() - self._checked_at >= self.refresh_seconds
            )
            if stale and not self._refreshing:
                self._refreshing = True
                schedule = True
            snapshot = dict(self._snapshot)
            snapshot["untrustedEvents"] = list(self._snapshot.get("untrustedEvents", []))
            if isinstance(self._snapshot.get("remediation"), dict):
                snapshot["remediation"] = dict(self._snapshot["remediation"])
        if schedule:
            try:
                asyncio.run_coroutine_threadsafe(self._refresh(fingerprint), self.loop)
            except RuntimeError as exc:
                self._finish_refresh(
                    fingerprint,
                    _hook_trust_snapshot("unknown", trusted=None, checked=True, error=type(exc).__name__),
                )
        return snapshot

    def _finish_refresh(self, fingerprint: tuple[tuple[int, int], ...], snapshot: dict[str, object]) -> None:
        with self._lock:
            self._snapshot = snapshot
            self._fingerprint = fingerprint
            self._checked_at = time.monotonic()
            self._refreshing = False

    async def _refresh(self, fingerprint: tuple[tuple[int, int], ...]) -> None:
        try:
            async with CodexAppServerClient(request_timeout=5.0) as client:
                value = await client.request("hooks/list", {"cwds": [str(self.workspace)]})
            snapshot = parse_codex_hook_trust(
                value,
                hooks_path=self.hooks_path,
                hook_script=self.hook_script,
            )
        except (CodexAppServerError, CompanionError, OSError, ValueError) as exc:
            message = " ".join(public_error(exc).split()) if isinstance(exc, CodexAppServerError) else type(exc).__name__
            snapshot = _hook_trust_snapshot(
                "unknown",
                trusted=None,
                checked=True,
                error=message[:180] or type(exc).__name__,
            )
        self._finish_refresh(fingerprint, snapshot)


class CodexHookBinding:
    def __init__(
        self,
        hooks_path: Path = DEFAULT_HOOKS_PATH,
        *,
        python_path: Path | None = None,
        trust_probe: CodexHookTrustProbe | None = None,
    ) -> None:
        self.hooks_path = hooks_path.expanduser()
        self.python_path = (python_path or Path(sys.executable)).resolve()
        self.hook_script = (ROOT_DIR / "scripts" / "codex_pet_hook.py").resolve()
        configured_agent = os.environ.get("VIBEBOARD_COMPANION_AGENT")
        self.agent_path = Path(configured_agent).expanduser().resolve() if configured_agent else None
        self.trust_probe = trust_probe

    @property
    def command(self) -> str:
        if self.agent_path is not None:
            return (
                f"{shlex.quote(str(self.agent_path))} --hook "
                f"{shlex.quote(str(self.hook_script))} --companion-managed"
            )
        return f"{shlex.quote(str(self.python_path))} {shlex.quote(str(self.hook_script))} --companion-managed"

    def _is_managed(self, hook: object) -> bool:
        if not isinstance(hook, dict):
            return False
        command = str(hook.get("command") or "")
        return command == self.command or (str(self.hook_script) in command and "codex_pet_hook.py" in command)

    def status(self) -> dict[str, object]:
        value = _read_json(self.hooks_path, {})
        hooks = value.get("hooks") if isinstance(value, dict) else None
        events = []
        if isinstance(hooks, dict):
            for event in HOOK_EVENTS:
                groups = hooks.get(event)
                found = False
                for group in groups if isinstance(groups, list) else []:
                    items = group.get("hooks") if isinstance(group, dict) else None
                    if any(self._is_managed(item) for item in items if isinstance(items, list)):
                        found = True
                        break
                if found:
                    events.append(event)
        result: dict[str, object] = {
            "detected": self.hooks_path.parent.exists(),
            "bound": len(events) == len(HOOK_EVENTS),
            "events": events,
            "hooksPath": str(self.hooks_path),
        }
        if self.trust_probe is None:
            result.update(
                _hook_trust_snapshot(
                    "unknown" if result["bound"] else "not_bound",
                    trusted=None if result["bound"] else False,
                    checked=False,
                )
            )
        else:
            result.update(self.trust_probe.status(bound=bool(result["bound"])))
        return result

    def bind(self) -> dict[str, object]:
        value = _read_json(self.hooks_path, {})
        if not isinstance(value, dict):
            raise CompanionError("existing Codex hooks.json must contain an object")
        hooks = value.setdefault("hooks", {})
        if not isinstance(hooks, dict):
            raise CompanionError("existing Codex hooks field must contain an object")
        for event in HOOK_EVENTS:
            groups = hooks.setdefault(event, [])
            if not isinstance(groups, list):
                raise CompanionError(f"existing Codex hook event {event} must contain a list")
            for group in groups:
                if isinstance(group, dict) and isinstance(group.get("hooks"), list):
                    group["hooks"] = [item for item in group["hooks"] if not self._is_managed(item)]
            groups[:] = [group for group in groups if not (isinstance(group, dict) and group.get("hooks") == [])]
            managed: dict[str, object] = {
                "hooks": [{"type": "command", "command": self.command, "timeout": 3}]
            }
            if event in {"PermissionRequest", "PreToolUse", "PostToolUse"}:
                managed["matcher"] = "*"
            groups.append(managed)
        if self.hooks_path.exists():
            backup = self.hooks_path.with_suffix(".json.vibeboard-backup")
            if not backup.exists():
                backup.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.hooks_path, backup)
        _atomic_json(self.hooks_path, value)
        if self.trust_probe is not None:
            self.trust_probe.invalidate()
        return self.status()

    def unbind(self) -> dict[str, object]:
        value = _read_json(self.hooks_path, {})
        if not isinstance(value, dict):
            return self.status()
        hooks = value.get("hooks")
        if isinstance(hooks, dict):
            for event in list(hooks):
                groups = hooks.get(event)
                if not isinstance(groups, list):
                    continue
                for group in groups:
                    if isinstance(group, dict) and isinstance(group.get("hooks"), list):
                        group["hooks"] = [item for item in group["hooks"] if not self._is_managed(item)]
                groups[:] = [group for group in groups if not (isinstance(group, dict) and group.get("hooks") == [])]
                if not groups:
                    del hooks[event]
        _atomic_json(self.hooks_path, value)
        if self.trust_probe is not None:
            self.trust_probe.invalidate()
        return self.status()


@dataclass
class CompanionJob:
    job_id: str
    kind: str
    slug: str
    status: str = "queued"
    stage: str = "queued"
    progress: int = 0
    message: str = "Waiting"
    digest: str | None = None
    download_url: str | None = None
    result: dict[str, object] | None = None
    log: list[str] = field(default_factory=list)
    created_at: float = field(default_factory=time.time)
    updated_at: float = field(default_factory=time.time)
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    _persist: Callable[["CompanionJob"], None] | None = field(default=None, repr=False)

    @classmethod
    def from_dict(
        cls,
        value: Mapping[str, object],
        *,
        persist: Callable[["CompanionJob"], None] | None = None,
    ) -> "CompanionJob":
        log = value.get("log")
        result = value.get("result")
        created_at = value.get("createdAt")
        updated_at = value.get("updatedAt")
        return cls(
            job_id=str(value.get("jobId") or ""),
            kind=str(value.get("kind") or "unknown"),
            slug=str(value.get("slug") or "unknown"),
            status=str(value.get("status") or "failed"),
            stage=str(value.get("stage") or "unknown"),
            progress=_coerce_progress(value.get("progress")),
            message=str(value.get("message") or "")[:240],
            digest=str(value["digest"]) if isinstance(value.get("digest"), str) else None,
            download_url=str(value["downloadUrl"]) if isinstance(value.get("downloadUrl"), str) else None,
            result=dict(result) if isinstance(result, dict) else None,
            log=[str(item)[:500] for item in log[-160:]] if isinstance(log, list) else [],
            created_at=_coerce_float(created_at, time.time()),
            updated_at=_coerce_float(updated_at, time.time()),
            _persist=persist,
        )

    def _notify(self) -> None:
        if self._persist is not None:
            self._persist(self)

    def update(self, *, stage: str | None = None, progress: int | None = None, message: str | None = None) -> None:
        with self._lock:
            if stage is not None:
                self.stage = stage
            if progress is not None:
                self.progress = max(0, min(100, int(progress)))
            if message is not None:
                self.message = message[:240]
            self.updated_at = time.time()
        self._notify()

    def append(self, value: str) -> None:
        with self._lock:
            self.log.append(value[:500])
            self.log[:] = self.log[-160:]
            self.updated_at = time.time()
        self._notify()

    def to_dict(self) -> dict[str, object]:
        with self._lock:
            return {
                "jobId": self.job_id,
                "kind": self.kind,
                "slug": self.slug,
                "status": self.status,
                "stage": self.stage,
                "progress": self.progress,
                "message": self.message,
                "digest": self.digest,
                "downloadUrl": self.download_url,
                "result": self.result,
                "log": list(self.log),
                "createdAt": self.created_at,
                "updatedAt": self.updated_at,
            }


class CompanionState:
    def __init__(
        self,
        *,
        loop: asyncio.AbstractEventLoop,
        device: CompanionDevice,
        state_dir: Path = DEFAULT_STATE_DIR,
        hooks_path: Path = DEFAULT_HOOKS_PATH,
        ble_cache: Path | None = None,
        workspace: Path = ROOT_DIR,
    ) -> None:
        self.loop = loop
        self.device = device
        self.state_dir = state_dir.expanduser()
        self.cache_dir = self.state_dir / "packages"
        self.key_dir = self.state_dir / "keys"
        self.package_cache = PackageCache(self.cache_dir)
        active = _read_json(self.state_dir / "active.json", {})
        active_digest = active.get("digest") if isinstance(active, dict) else None
        previous_digest = active.get("previousDigest") if isinstance(active, dict) else None
        self.package_cache.prune(
            active_digest=active_digest if isinstance(active_digest, str) else None,
            preserve=(previous_digest,) if isinstance(previous_digest, str) else (),
        )
        self.job_journal = JobJournal(self.state_dir / "jobs.json")
        self.catalog = PetdexCatalog(cache_path=self.state_dir / "petdex-manifest.json")
        self.hooks = CodexHookBinding(hooks_path)
        self.hooks.trust_probe = CodexHookTrustProbe(
            loop=loop,
            workspace=workspace,
            hooks_path=self.hooks.hooks_path,
            hook_script=self.hooks.hook_script,
        )
        self.ble_cache = ble_cache
        self.firmware = FirmwareManager(self.state_dir, root=ROOT_DIR)
        self._jobs_lock = threading.Lock()
        self.jobs = {
            str(row["jobId"]): CompanionJob.from_dict(row, persist=self._persist_job)
            for row in self.job_journal.rows(limit=200)
            if isinstance(row.get("jobId"), str)
        }
        self._install_lock: asyncio.Lock | None = None
        self._sessions: dict[str, float] = {}
        self._sessions_lock = threading.Lock()

    def issue_session(self) -> dict[str, object]:
        token = secrets.token_urlsafe(32)
        expires = time.time() + SESSION_TTL_SECONDS
        with self._sessions_lock:
            self._sessions = {key: value for key, value in self._sessions.items() if value > time.time()}
            self._sessions[token] = expires
        return {"token": token, "expiresAt": int(expires * 1000)}

    def valid_session(self, token: str) -> bool:
        with self._sessions_lock:
            expires = self._sessions.get(token, 0)
        return bool(token and expires > time.time())

    def status(self) -> dict[str, object]:
        transport = getattr(getattr(self.device, "commands", None), "transport", None)
        label = str(getattr(transport, "connection_label", "") or "")
        cache = load_ble_cache(self.ble_cache) if self.ble_cache else {}
        capabilities = getattr(self.device, "runtime_capabilities", {})
        if not isinstance(capabilities, dict):
            capabilities = {}
        if self.device.connected and cache.get("address"):
            self._remember_board_keychain(cache)
        return {
            "companion": {"connected": True, "version": 1},
            "codex": self.hooks.status(),
            "firmware": self.firmware.status(),
            "jobs": self.job_journal.summary(),
            "packageCache": self.package_cache.status(),
            "board": {
                "connected": bool(self.device.connected),
                "name": cache.get("name") or "VibeBoard",
                "identity": (cache.get("address") or label).split(" ", 1)[0][-8:],
                "runtimeApi": capabilities.get("rt"),
                "bleInstall": (capabilities.get("ins") or {}).get("ble") == 1
                if isinstance(capabilities.get("ins"), dict) else False,
            },
        }

    def health(self) -> dict[str, object]:
        checks: list[dict[str, object]] = []

        def add(name: str, status: str, detail: str) -> None:
            checks.append({"name": name, "status": status, "detail": detail})

        resources = (
            ("web", WEB_PATH),
            ("petdexConfig", PETDEX_CONFIG_PATH),
            ("stateContract", PETDEX_STATE_CONTRACT_PATH),
            ("runtimeTemplate", ROOT_DIR / "scripts" / "runtime_apps" / "codex_pet" / "manifest.json"),
        )
        for name, path in resources:
            add(name, "ok" if path.is_file() else "error", str(path))

        try:
            self.state_dir.mkdir(parents=True, exist_ok=True)
            fd, probe_name = tempfile.mkstemp(prefix=".health.", dir=self.state_dir)
            os.close(fd)
            os.unlink(probe_name)
        except OSError as exc:
            add("stateDirectory", "error", f"not writable: {exc}")
        else:
            add("stateDirectory", "ok", str(self.state_dir))

        cache = self.package_cache.status()
        add("packageCache", "ok" if cache["withinLimits"] else "error", f"{cache['entries']} packages, {cache['bytes']} bytes")
        add("board", "ok" if self.device.connected else "warning", "connected" if self.device.connected else "waiting for VibeBoard")
        hook_status = self.hooks.status()
        add("codexHooks", "ok" if hook_status.get("bound") else "warning", str(hook_status.get("trustStatus") or "not bound"))
        service_ready = not any(item["status"] == "error" for item in checks)
        return {
            "schemaVersion": 1,
            "checkedAt": int(time.time()),
            "serviceReady": service_ready,
            "boardReady": service_ready and bool(self.device.connected),
            "checks": checks,
        }

    def _remember_board_keychain(self, cache: Mapping[str, str]) -> None:
        if sys.platform != "darwin" or not cache.get("address"):
            return
        value = json.dumps({"name": cache.get("name"), "address": cache.get("address")}, separators=(",", ":"))
        try:
            subprocess.run(
                ["/usr/bin/security", "add-generic-password", "-U", "-s", "dev.vibeboard.companion.board", "-a", "default", "-w", value],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=5,
            )
        except (OSError, subprocess.SubprocessError):
            pass

    def get_job(self, job_id: str) -> CompanionJob | None:
        with self._jobs_lock:
            return self.jobs.get(job_id)

    def list_jobs(self, *, limit: int = 100) -> list[dict[str, object]]:
        return self.job_journal.rows(limit=limit)

    def _persist_job(self, job: CompanionJob) -> None:
        snapshot = job.to_dict()
        self.job_journal.upsert(snapshot, force=snapshot.get("status") in {"done", "failed"})

    def _new_job(self, kind: str, slug: str) -> CompanionJob:
        job = CompanionJob(f"pet-{secrets.token_hex(8)}", kind, slug, _persist=self._persist_job)
        with self._jobs_lock:
            self.jobs[job.job_id] = job
            cutoff = time.time() - 24 * 60 * 60
            self.jobs = {key: value for key, value in self.jobs.items() if value.created_at >= cutoff}
        self._persist_job(job)
        return job

    def firmware_available(self) -> dict[str, object]:
        return self.firmware.available()

    def firmware_update_check(self) -> dict[str, object]:
        capabilities = getattr(self.device, "runtime_capabilities", {})
        if not isinstance(capabilities, Mapping):
            capabilities = {}
        return check_for_firmware_update(
            self.firmware,
            board_connected=bool(self.device.connected),
            runtime_capabilities=capabilities,
        )

    def start_firmware_job(self, *, version: str | None = None, rollback: bool = False) -> CompanionJob:
        if not rollback:
            check = self.firmware_update_check()
            if check.get("state") != "update_available":
                raise CompanionError(str(check.get("message") or "no verified firmware update is available"))
            latest = check.get("latest")
            if not isinstance(latest, str):
                raise CompanionError("firmware update check did not return a release version")
            if version is not None and version != latest:
                raise CompanionError("only the latest verified firmware release can be installed")
            version = latest
        job = self._new_job("firmware_rollback" if rollback else "firmware_update", "firmware")

        def progress(stage: str, percent: int, message: str) -> None:
            job.update(stage=stage, progress=percent, message=message)
            job.append(message)

        async def run() -> None:
            job.status = "running"
            try:
                if rollback:
                    result = await asyncio.to_thread(self.firmware.rollback, progress)
                else:
                    result = await asyncio.to_thread(self.firmware.update, version, progress)
                job.result = result
                job.status = "done"
                job.update(stage="complete", progress=100, message="Firmware update completed")
            except (FirmwareManagerError, FirmwareReleaseError) as exc:
                job.status = "failed"
                job.update(stage="failed", message=str(exc))
                job.append(f"ERROR {type(exc).__name__}: {exc}")
            except Exception as exc:
                job.status = "failed"
                job.update(stage="failed", message=str(exc))
                job.append(f"ERROR {type(exc).__name__}: {exc}")

        asyncio.run_coroutine_threadsafe(run(), self.loop)
        return job

    def start_support_bundle_job(self) -> CompanionJob:
        job = self._new_job("support_bundle", "support")

        async def run() -> None:
            job.status = "running"
            job.update(stage="collect", progress=15, message="Collecting redacted board and Companion diagnostics")
            try:
                bundle = await create_support_bundle(self, job_id=job.job_id)
                job.result = {
                    "bundlePath": bundle.name,
                    "downloadPath": f"/v1/support/bundles/{job.job_id}",
                    "bytes": bundle.stat().st_size,
                }
                job.status = "done"
                job.update(stage="complete", progress=100, message="Support bundle is ready")
            except Exception as exc:
                job.status = "failed"
                job.update(stage="failed", message=str(exc))
                job.append(f"ERROR {type(exc).__name__}: {exc}")

        asyncio.run_coroutine_threadsafe(run(), self.loop)
        return job

    def support_bundle(self, job_id: str) -> Path | None:
        job = self.get_job(job_id)
        if job is None or job.kind != "support_bundle" or job.status != "done" or not isinstance(job.result, dict):
            return None
        value = job.result.get("bundlePath")
        if not isinstance(value, str) or SAFE_SUPPORT_BUNDLE.fullmatch(value) is None:
            return None
        root = (self.state_dir / "support").resolve()
        candidate = (root / value).resolve()
        if candidate.parent != root:
            return None
        return candidate if candidate.is_file() else None

    def start_package_job(self, slug: str, *, install: bool, expected_digest: str | None = None) -> CompanionJob:
        if expected_digest is not None and SAFE_DIGEST.fullmatch(expected_digest) is None:
            raise CompanionError("invalid expected package digest")
        pet = self.catalog.get(slug)
        job = self._new_job("install" if install else "build", slug)
        asyncio.run_coroutine_threadsafe(
            self._run_package_job(job, pet, install=install, expected_digest=expected_digest),
            self.loop,
        )
        return job

    async def _build_package(self, job: CompanionJob, pet: Mapping[str, object]) -> tuple[Path, HpetPackage, Path]:
        job.status = "running"
        job.update(stage="download", progress=8, message="Downloading Petdex source")
        job.append(f"source petdex:{pet['slug']}")
        try:
            package_path, package, public_key = await asyncio.to_thread(
                build_petdex_hpet,
                pet,
                cache_dir=self.cache_dir,
                key_dir=self.key_dir,
            )
        except HpetError as exc:
            if "fetch failed" not in str(exc).lower():
                raise
            source_error = str(exc)
            try:
                _, public_key = await asyncio.to_thread(ensure_signing_keys, self.key_dir)
                package_path, package = await asyncio.to_thread(
                    _cached_v2_hpet_for_slug,
                    self.cache_dir,
                    public_key,
                    str(pet["slug"]),
                )
            except HpetError as cache_error:
                raise HpetError(
                    f"{source_error}; verified cache fallback unavailable: {cache_error}"
                ) from exc
            job.append(f"source fetch failed; using verified v2 cache digest={package.digest}")
        job.digest = package.digest
        job.download_url = f"/api/packages/{package.digest}.hpet"
        job.update(stage="verify", progress=30, message="Signature and animation states verified")
        job.append(f"verified hpet digest={package.digest} bytes={package_path.stat().st_size}")
        active = _read_json(self.state_dir / "active.json", {})
        active_digest = active.get("digest") if isinstance(active, dict) else None
        self.package_cache.prune(
            active_digest=active_digest if isinstance(active_digest, str) else None,
            preserve=(package.digest,),
        )
        return package_path, package, public_key

    async def _run_package_job(
        self,
        job: CompanionJob,
        pet: Mapping[str, object],
        *,
        install: bool,
        expected_digest: str | None,
    ) -> None:
        try:
            package_path, package, _ = await self._build_package(job, pet)
            if expected_digest is not None and package.digest != expected_digest:
                raise CompanionError("package digest does not match the deployment link")
            if not install:
                job.status = "done"
                job.update(stage="complete", progress=100, message="Package ready")
                job.result = {"digest": package.digest, "path": str(package_path)}
                return
            if self._install_lock is None:
                self._install_lock = asyncio.Lock()
            async with self._install_lock:
                await self._install(job, package)
        except Exception as exc:
            job.status = "failed"
            job.update(stage="failed", message=str(exc))
            job.append(f"ERROR {type(exc).__name__}: {exc}")

    async def _install(self, job: CompanionJob, package: HpetPackage) -> None:
        previous = _read_json(self.state_dir / "active.json", {})
        previous_digest = previous.get("digest") if isinstance(previous, dict) else None
        _, files = await asyncio.to_thread(compose_codex_pet_runtime, package)

        def progress(command: str, index: int, total: int) -> None:
            if command.startswith("vb_runtime_install_begin"):
                job.update(stage="transfer", progress=36, message="Starting transactional install")
            elif command.startswith(("vb_runtime_install_file", "vb_runtime_install_bulk")):
                job.update(stage="transfer", progress=36 + int(index / max(total, 1) * 48), message=f"Transferring {index}/{total}")
            elif command.startswith("vb_runtime_install_end"):
                job.update(stage="restart", progress=88, message="Committing and restarting Codex Pet")

        def rollback_progress(command: str, index: int, total: int) -> None:
            if command.startswith("vb_runtime_install_begin"):
                job.update(stage="rollback", progress=40, message="Restoring the previous pet")
            elif command.startswith(("vb_runtime_install_file", "vb_runtime_install_bulk")):
                percent = 40 + int(index / max(total, 1) * 48)
                job.update(stage="rollback", progress=min(88, percent), message=f"Restoring {index}/{total}")
            elif command.startswith("vb_runtime_install_end"):
                job.update(stage="rollback", progress=90, message="Restarting the restored pet")

        job.append(f"compose runtime files={len(files)} bytes={sum(map(len, files.values()))}")
        try:
            result = await self.device.install_codex_pet(files, package.slug, progress=progress)
        except Exception as install_error:
            if isinstance(previous_digest, str) and SAFE_DIGEST.fullmatch(previous_digest) and previous_digest != package.digest:
                previous_path = self.cache_dir / f"{previous_digest}.hpet"
                if previous_path.is_file():
                    job.append(f"restoring previous pet digest={previous_digest}")
                    job.update(stage="rollback", progress=38, message="Deployment failed; restoring the previous pet")
                    try:
                        await self.device.reconnect()
                        public_key = ensure_signing_keys(self.key_dir)[1]
                        previous_package = await asyncio.to_thread(read_hpet, previous_path.read_bytes(), public_key=public_key)
                        _, previous_files = await asyncio.to_thread(compose_codex_pet_runtime, previous_package)
                        await self.device.install_codex_pet(
                            previous_files,
                            previous_package.slug,
                            progress=rollback_progress,
                        )
                        job.append("previous pet restored")
                        job.update(stage="rollback", progress=94, message="Previous pet restored; reporting deployment failure")
                    except Exception as rollback_error:
                        job.append(f"rollback also failed: {type(rollback_error).__name__}: {rollback_error}")
            raise install_error
        job.update(stage="verify", progress=96, message="Verifying board animation")
        _atomic_json(
            self.state_dir / "active.json",
            {
                "slug": package.slug,
                "digest": package.digest,
                "previousDigest": previous_digest if isinstance(previous_digest, str) else None,
                "installedAt": int(time.time()),
            },
        )
        self.package_cache.prune(
            active_digest=package.digest,
            preserve=(previous_digest,) if isinstance(previous_digest, str) else (),
        )
        job.result = dict(result)
        job.status = "done"
        job.update(stage="complete", progress=100, message=f"{package.manifest['name']} is active")

    def package_blob(self, digest: str) -> bytes:
        if SAFE_DIGEST.fullmatch(digest) is None:
            raise CompanionError("invalid package digest")
        path = self.cache_dir / f"{digest}.hpet"
        if not path.is_file():
            raise CompanionError("package not found")
        blob = path.read_bytes()
        public_key = ensure_signing_keys(self.key_dir)[1]
        package = read_hpet(blob, public_key=public_key)
        if package.digest != digest:
            raise CompanionError("cached package digest mismatch")
        return blob

    def pet_asset(self, slug: str) -> tuple[bytes, str]:
        pet = self.catalog.get(slug)
        source_url = str(pet["spritesheetUrl"])
        suffix = ".png" if urllib.parse.urlsplit(source_url).path.lower().endswith(".png") else ".webp"
        digest = hashlib.sha256(source_url.encode("utf-8")).hexdigest()
        asset_dir = self.state_dir / "assets"
        asset_path = asset_dir / f"{digest}{suffix}"
        content_type = "image/png" if suffix == ".png" else "image/webp"
        if asset_path.is_file() and 0 < asset_path.stat().st_size <= MAX_SPRITESHEET_BYTES:
            return asset_path.read_bytes(), content_type
        data, content_type = _fetch_bytes(source_url, max_bytes=MAX_SPRITESHEET_BYTES)
        asset_dir.mkdir(parents=True, exist_ok=True)
        fd, temp_name = tempfile.mkstemp(prefix=f".{asset_path.name}.", dir=asset_dir)
        try:
            with os.fdopen(fd, "wb") as handle:
                handle.write(data)
                handle.flush()
                os.fsync(handle.fileno())
            os.replace(temp_name, asset_path)
        finally:
            if os.path.exists(temp_name):
                os.unlink(temp_name)
        return data, content_type

    def pair_board(self) -> CompanionJob:
        job = self._new_job("pair", "board")

        async def run() -> None:
            try:
                job.status = "running"
                job.update(stage="pair", progress=20, message="Scanning for VibeBoard")
                await self.device.reconnect(force_fresh=True)
                job.status = "done"
                job.update(stage="complete", progress=100, message="VibeBoard connected")
            except Exception as exc:
                job.status = "failed"
                job.update(stage="failed", message=str(exc))

        asyncio.run_coroutine_threadsafe(run(), self.loop)
        return job


def parse_install_url(value: str) -> str:
    parsed = urllib.parse.urlsplit(value)
    if parsed.scheme != "vibeboard" or parsed.netloc != "pet" or parsed.path != "/install":
        raise CompanionError("invalid VibeBoard install URL")
    query = urllib.parse.parse_qs(parsed.query, strict_parsing=True)
    if query.get("source") != ["petdex"] or len(query.get("slug", [])) != 1:
        raise CompanionError("install URL must identify one Petdex pet")
    slug = query["slug"][0]
    if SAFE_SLUG.fullmatch(slug) is None:
        raise CompanionError("invalid install URL pet slug")
    digest = query.get("digest")
    if digest is not None and (len(digest) != 1 or SAFE_DIGEST.fullmatch(digest[0]) is None):
        raise CompanionError("invalid install URL package digest")
    return slug


class CompanionHandler(BaseHTTPRequestHandler):
    server_version = "VibeBoardCompanion/1.0"

    @property
    def state(self) -> CompanionState:
        return self.server.companion_state  # type: ignore[attr-defined]

    def log_message(self, fmt: str, *args: Any) -> None:
        print("[companion]", fmt % args)

    def _origin_allowed(self) -> bool:
        origin = self.headers.get("origin")
        if not origin:
            return self.client_address[0] in {"127.0.0.1", "::1"}
        port = self.server.server_port  # type: ignore[attr-defined]
        local_origins = {
            f"http://127.0.0.1:{port}",
            f"http://localhost:{port}",
            f"http://[::1]:{port}",
        }
        configured = {item.strip() for item in os.environ.get("VIBEBOARD_COMPANION_ORIGINS", "").split(",") if item.strip()}
        return origin in local_origins or origin in configured

    def _host_allowed(self) -> bool:
        host = self.headers.get("host", "")
        try:
            hostname = urllib.parse.urlsplit(f"//{host}").hostname
        except ValueError:
            return False
        return (hostname or "").lower() in {"127.0.0.1", "localhost", "::1"}

    def _authorized(self) -> bool:
        header = self.headers.get("authorization", "")
        return header.startswith("Bearer ") and self.state.valid_session(header[7:])

    def _send(self, status: int, body: bytes, content_type: str, *, download: str | None = None) -> None:
        self.send_response(status)
        self.send_header("content-type", content_type)
        self.send_header("content-length", str(len(body)))
        self.send_header("cache-control", "no-store")
        if download:
            self.send_header("content-disposition", f'attachment; filename="{download}"')
        origin = self.headers.get("origin")
        if origin and self._origin_allowed():
            self.send_header("access-control-allow-origin", origin)
            self.send_header("vary", "origin")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, status: int, value: object) -> None:
        self._send(status, json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"), "application/json; charset=utf-8")

    def _error(self, status: int, message: str) -> None:
        self._json(status, {"error": message})

    def _read_json(self) -> dict[str, object]:
        try:
            length = int(self.headers.get("content-length") or 0)
        except ValueError as exc:
            raise CompanionError("invalid content-length") from exc
        if length < 0 or length > MAX_JSON_BYTES:
            raise CompanionError("request body is too large")
        if not length:
            return {}
        value = json.loads(self.rfile.read(length).decode("utf-8"))
        if not isinstance(value, dict):
            raise CompanionError("request body must be an object")
        return value

    def do_OPTIONS(self) -> None:
        if not self._host_allowed() or not self._origin_allowed():
            self._error(403, "origin not allowed")
            return
        self.send_response(204)
        self.send_header("access-control-allow-origin", self.headers.get("origin", ""))
        self.send_header("access-control-allow-methods", "GET, POST, OPTIONS")
        self.send_header("access-control-allow-headers", "authorization, content-type")
        self.send_header("access-control-max-age", "600")
        if self.headers.get("access-control-request-private-network", "").lower() == "true":
            self.send_header("access-control-allow-private-network", "true")
            self.send_header("vary", "origin, access-control-request-private-network")
        self.end_headers()

    def do_GET(self) -> None:
        try:
            if not self._host_allowed() or not self._origin_allowed():
                self._error(403, "origin not allowed")
                return
            parsed = urllib.parse.urlsplit(self.path)
            path = parsed.path
            if path in {"/", "/index.html", "/pets"}:
                self._send(200, WEB_PATH.read_bytes(), "text/html; charset=utf-8")
                return
            if path == "/api/pets":
                query = urllib.parse.parse_qs(parsed.query)
                q = query.get("query", [""])[0]
                offset = int(query.get("offset", ["0"])[0])
                limit = int(query.get("limit", ["24"])[0])
                self._json(200, self.state.catalog.list(q, offset, limit))
                return
            asset_match = re.fullmatch(r"/api/pets/([a-z0-9][a-z0-9-]{0,23})/spritesheet", path)
            if asset_match:
                data, content_type = self.state.pet_asset(asset_match.group(1))
                self._send(200, data, content_type)
                return
            if path.startswith("/api/pets/"):
                slug = urllib.parse.unquote(path.removeprefix("/api/pets/"))
                self._json(200, {"pet": self.state.catalog.get(slug)})
                return
            match = re.fullmatch(r"/api/packages/([0-9a-f]{64})[.]hpet", path)
            if match:
                blob = self.state.package_blob(match.group(1))
                self._send(200, blob, "application/vnd.vibeboard.hpet+zip", download=f"{match.group(1)}.hpet")
                return
            if path == "/v1/status":
                self._json(200, self.state.status())
                return
            if path == "/v1/health":
                self._json(200, self.state.health())
                return
            if path == "/v1/firmware/status":
                self._json(200, self.state.status()["firmware"])
                return
            if path == "/v1/firmware/available":
                self._json(200, self.state.firmware_available())
                return
            if path == "/v1/firmware/check":
                self._json(200, self.state.firmware_update_check())
                return
            if path == "/v1/jobs":
                if not self._authorized():
                    self._error(401, "valid Companion session required")
                    return
                query = urllib.parse.parse_qs(parsed.query)
                limit = int(query.get("limit", ["100"])[0])
                self._json(200, {"jobs": self.state.list_jobs(limit=limit)})
                return
            if path.startswith("/v1/jobs/"):
                if not self._authorized():
                    self._error(401, "valid Companion session required")
                    return
                job = self.state.get_job(urllib.parse.unquote(path.removeprefix("/v1/jobs/")))
                if job is None:
                    self._error(404, "job not found")
                    return
                self._json(200, job.to_dict())
                return
            support_match = re.fullmatch(r"/v1/support/bundles/([a-z0-9-]+)", path)
            if support_match:
                if not self._authorized():
                    self._error(401, "valid Companion session required")
                    return
                bundle = self.state.support_bundle(support_match.group(1))
                if bundle is None or not bundle.is_file():
                    self._error(404, "support bundle not ready")
                    return
                self._send(200, bundle.read_bytes(), "application/zip", download=bundle.name)
                return
            self._error(404, "not found")
        except (CompanionError, HpetError, ValueError) as exc:
            self._error(400, str(exc))
        except Exception as exc:
            self._error(500, f"{type(exc).__name__}: {exc}")

    def do_POST(self) -> None:
        try:
            if not self._host_allowed() or not self._origin_allowed():
                self._error(403, "origin not allowed")
                return
            path = urllib.parse.urlsplit(self.path).path
            if path == "/v1/session":
                self._json(200, self.state.issue_session())
                return
            if not self._authorized():
                self._error(401, "valid Companion session required")
                return
            if path == "/v1/board/pair":
                self._read_json()
                job = self.state.pair_board()
                self._json(202, {"jobId": job.job_id})
                return
            if path == "/v1/firmware/update":
                body = self._read_json()
                version = body.get("version")
                if version is not None and not isinstance(version, str):
                    raise CompanionError("firmware version must be a string")
                job = self.state.start_firmware_job(version=version)
                self._json(202, {"jobId": job.job_id})
                return
            if path == "/v1/firmware/rollback":
                self._read_json()
                job = self.state.start_firmware_job(rollback=True)
                self._json(202, {"jobId": job.job_id})
                return
            if path == "/v1/support/bundle":
                self._read_json()
                job = self.state.start_support_bundle_job()
                self._json(202, {"jobId": job.job_id})
                return
            if path == "/v1/codex/bind":
                self._read_json()
                self._json(200, {"codex": self.state.hooks.bind()})
                return
            if path == "/v1/codex/unbind":
                self._read_json()
                self._json(200, {"codex": self.state.hooks.unbind()})
                return
            match = re.fullmatch(r"/api/packages/([a-z0-9][a-z0-9-]{0,23})", path)
            if match:
                self._read_json()
                job = self.state.start_package_job(match.group(1), install=False)
                self._json(202, {"jobId": job.job_id})
                return
            if path == "/v1/pets/install":
                body = self._read_json()
                slug = str(body.get("slug") or "")
                digest_value = body.get("digest")
                digest = str(digest_value) if digest_value is not None else None
                job = self.state.start_package_job(slug, install=True, expected_digest=digest)
                self._json(202, {"jobId": job.job_id})
                return
            self._error(404, "not found")
        except (CompanionError, HpetError, ValueError, json.JSONDecodeError) as exc:
            self._error(400, str(exc))
        except Exception as exc:
            self._error(500, f"{type(exc).__name__}: {exc}")


class CompanionServer:
    def __init__(self, state: CompanionState, *, host: str = "127.0.0.1", port: int = 8790, open_browser: bool = True) -> None:
        if host not in {"127.0.0.1", "::1"}:
            raise CompanionError("Companion HTTP server must remain loopback-only")
        self.state = state
        self.host = host
        self.port = port
        self.open_browser = open_browser
        self.httpd: ThreadingHTTPServer | None = None
        self.thread: threading.Thread | None = None

    def start(self) -> str:
        self.httpd = ThreadingHTTPServer((self.host, self.port), CompanionHandler)
        self.httpd.companion_state = self.state  # type: ignore[attr-defined]
        self.thread = threading.Thread(target=self.httpd.serve_forever, name="codex-pet-companion-http", daemon=True)
        self.thread.start()
        url = f"http://{self.host}:{self.httpd.server_port}"
        if self.open_browser:
            webbrowser.open(url)
        print(f"VibeBoard Companion: {url}", flush=True)
        return url

    def close(self) -> None:
        if self.httpd is not None:
            self.httpd.shutdown()
            self.httpd.server_close()
        if self.thread is not None:
            self.thread.join(timeout=3.0)
        self.httpd = None
        self.thread = None


class OfflineDevice:
    def __init__(self) -> None:
        self.connected = False
        self.commands = None

    async def reconnect(self, *, force_fresh: bool = False) -> None:
        raise CompanionError("no physical VibeBoard is attached to offline preview mode")

    async def install_codex_pet(self, files: dict[str, bytes], slug: str, *, progress: Any | None = None) -> dict[str, object]:
        raise CompanionError("no physical VibeBoard is attached to offline preview mode")


async def _standalone(port: int, open_browser: bool) -> None:
    state = CompanionState(loop=asyncio.get_running_loop(), device=OfflineDevice(), state_dir=Path(tempfile.gettempdir()) / "vibeboard-companion-preview")
    server = CompanionServer(state, port=port, open_browser=open_browser)
    server.start()
    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        server.close()


def run_self_test() -> None:
    firmware_self_test()
    firmware_update_check_self_test()
    unconfigured_state = CompanionState.__new__(CompanionState)
    unconfigured_state.device = OfflineDevice()

    class UnconfiguredFirmware:
        def status(self) -> dict[str, object]:
            return {"configured": False, "updateMode": "verified_usb_recovery", "wirelessDfu": False}

        def available(self) -> dict[str, object]:
            raise AssertionError("an unconfigured update must not read the feed")

    unconfigured_state.firmware = UnconfiguredFirmware()
    try:
        unconfigured_state.start_firmware_job()
    except CompanionError as exc:
        assert "尚未配置" in str(exc)
    else:
        raise AssertionError("firmware update started without a successful Companion check")
    diagnostics_self_test()
    companion_state_self_test()
    assert parse_install_url("vibeboard://pet/install?source=petdex&slug=shinchan") == "shinchan"
    for bad in ("https://pet/install?source=petdex&slug=shinchan", "vibeboard://pet/install?source=other&slug=shinchan", "vibeboard://pet/install?source=petdex&slug=../bad"):
        try:
            parse_install_url(bad)
        except CompanionError:
            pass
        else:
            raise AssertionError(f"unsafe install URL passed: {bad}")
    normalized = normalize_petdex_entry({
        "slug": "shinchan",
        "displayName": "Shinchan",
        "submittedBy": "adamholter",
        "spritesheetUrl": "https://assets.petdex.dev/pets/shinchan/sprite.webp",
        "petJsonUrl": "https://assets.petdex.dev/pets/shinchan/pet.json",
    })
    assert normalized["stateRows"] == {
        "idle": 0,
        "runRight": 1,
        "runLeft": 2,
        "waving": 3,
        "jumping": 4,
        "failed": 5,
        "waiting": 6,
        "running": 7,
        "review": 8,
    }
    for unsafe_url in (
        "https://user:secret@assets.petdex.dev/pet.webp",
        "https://assets.petdex.dev:444/pet.webp",
    ):
        try:
            _valid_petdex_asset_url(unsafe_url)
        except CompanionError:
            pass
        else:
            raise AssertionError(f"unsafe Petdex asset URL passed: {unsafe_url}")
    assert _valid_petdex_manifest_url("https://petdex.dev/api/manifest") == "https://petdex.dev/api/manifest"
    for unsafe_url in (
        "http://petdex.dev/api/manifest",
        "https://petdex.dev.evil.example/api/manifest",
        "https://user@petdex.dev/api/manifest",
        "https://petdex.dev:444/api/manifest",
        "https://petdex.dev/api/manifest#fragment",
    ):
        try:
            _valid_petdex_manifest_url(unsafe_url)
        except CompanionError:
            pass
        else:
            raise AssertionError(f"unsafe Petdex manifest URL passed: {unsafe_url}")
    try:
        _fetch_json("http://petdex.dev/api/manifest", max_bytes=1)
    except CompanionError:
        pass
    else:
        raise AssertionError("unsafe initial Petdex manifest URL reached the HTTP client")
    try:
        _valid_petdex_redirect_url(
            "https://assets.petdex.dev/pet.webp",
            "https://example.com/pet.webp",
        )
    except CompanionError:
        pass
    else:
        raise AssertionError("unsafe Petdex redirect passed")
    with tempfile.TemporaryDirectory(prefix="companion-cache-test-") as cache_text:
        cache_dir = Path(cache_text)
        cache_dir.joinpath("bad.hpet").write_bytes(b"bad")
        cache_dir.joinpath("legacy.hpet").write_bytes(b"legacy")
        cache_dir.joinpath("v2.hpet").write_bytes(b"v2")

        def fake_reader(blob: bytes, *, public_key: Path) -> HpetPackage:
            del public_key
            if blob == b"bad":
                raise HpetError("invalid signature")
            version = 1 if blob == b"legacy" else 2
            return HpetPackage(
                digest=hashlib.sha256(blob).hexdigest(),
                manifest={
                    "slug": "test-pet",
                    "schemaVersion": version,
                    "target": {
                        "preloadVersion": version,
                        "stateCount": 9 if version == 2 else 5,
                        "frameMs": 120 if version == 2 else 180,
                    },
                },
                files={},
            )

        cached_path, cached_package = _cached_v2_hpet_for_slug(
            cache_dir,
            cache_dir / "public.pem",
            "test-pet",
            reader=fake_reader,
        )
        assert cached_path.name == "v2.hpet" and cached_package.manifest["schemaVersion"] == 2
        try:
            _cached_v2_hpet_for_slug(
                cache_dir,
                cache_dir / "public.pem",
                "missing-pet",
                reader=fake_reader,
            )
            raise AssertionError("missing cached pet was accepted")
        except HpetError as exc:
            assert "no verified nine-state" in str(exc)
    with tempfile.TemporaryDirectory(prefix="companion-source-error-test-") as cache_text:
        state = CompanionState.__new__(CompanionState)
        state.cache_dir = Path(cache_text) / "packages"
        state.key_dir = Path(cache_text) / "keys"
        job = CompanionJob("pet-source-error", "install", "missing-pet")
        original_builder = globals()["build_petdex_hpet"]
        original_keys = globals()["ensure_signing_keys"]
        original_cache_lookup = globals()["_cached_v2_hpet_for_slug"]

        def failing_builder(*_args: object, **_kwargs: object) -> tuple[Path, HpetPackage, Path]:
            raise HpetError("build_hpet_petdex: source fetch failed: ETIMEDOUT")

        def fake_keys(_path: Path) -> tuple[Path, Path]:
            return Path(cache_text) / "private.pem", Path(cache_text) / "public.pem"

        def missing_cache(*_args: object, **_kwargs: object) -> tuple[Path, HpetPackage]:
            raise HpetError("no verified nine-state v2 package is cached for missing-pet")

        globals()["build_petdex_hpet"] = failing_builder
        globals()["ensure_signing_keys"] = fake_keys
        globals()["_cached_v2_hpet_for_slug"] = missing_cache
        try:
            asyncio.run(state._build_package(job, {"slug": "missing-pet"}))
        except HpetError as exc:
            message = str(exc)
            assert "source fetch failed: ETIMEDOUT" in message
            assert "no verified nine-state v2 package is cached" in message
        else:
            raise AssertionError("cache miss hid the original Petdex network failure")
        finally:
            globals()["build_petdex_hpet"] = original_builder
            globals()["ensure_signing_keys"] = original_keys
            globals()["_cached_v2_hpet_for_slug"] = original_cache_lookup
    with tempfile.TemporaryDirectory(prefix="companion-recovery-test-") as temp_text:
        async def state_recovery_check() -> None:
            state_dir = Path(temp_text) / "state"
            hooks_path = Path(temp_text) / "hooks.json"
            first = CompanionState(
                loop=asyncio.get_running_loop(),
                device=OfflineDevice(),
                state_dir=state_dir,
                hooks_path=hooks_path,
            )
            job = first._new_job("install", "shinchan")
            job.status = "running"
            job.update(stage="transfer", progress=42, message="transfer in progress")
            second = CompanionState(
                loop=asyncio.get_running_loop(),
                device=OfflineDevice(),
                state_dir=state_dir,
                hooks_path=hooks_path,
            )
            recovered_job = second.get_job(job.job_id)
            assert recovered_job is not None
            assert recovered_job.status == "failed" and recovered_job.stage == "interrupted"
            health = second.health()
            assert health["serviceReady"] is True and health["boardReady"] is False

        asyncio.run(state_recovery_check())
    with tempfile.TemporaryDirectory(prefix="companion-test-") as temp_text:
        hooks_path = Path(temp_text) / ".codex" / "hooks.json"
        hooks_path.parent.mkdir()
        hooks_path.write_text(json.dumps({"hooks": {"Stop": [{"hooks": [{"type": "command", "command": "keep-me"}]}]}}), encoding="utf-8")
        binding = CodexHookBinding(hooks_path)
        assert binding.bind()["bound"] is True
        assert binding.status()["trustStatus"] == "unknown"
        saved = json.loads(hooks_path.read_text(encoding="utf-8"))
        assert any(item.get("command") == "keep-me" for group in saved["hooks"]["Stop"] for item in group["hooks"])
        assert binding.bind()["bound"] is True

        listed_hooks = [
            {
                "eventName": HOOK_WIRE_EVENTS[event],
                "command": binding.command,
                "sourcePath": str(hooks_path),
                "enabled": True,
                "trustStatus": "trusted",
            }
            for event in HOOK_EVENTS
        ]
        trust_value = {"data": [{"cwd": temp_text, "hooks": listed_hooks, "warnings": [], "errors": []}]}
        trusted = parse_codex_hook_trust(
            trust_value,
            hooks_path=hooks_path,
            hook_script=binding.hook_script,
        )
        assert trusted["trusted"] is True
        assert trusted["trustStatus"] == "trusted"
        listed_hooks[0]["trustStatus"] = "modified"
        modified = parse_codex_hook_trust(
            trust_value,
            hooks_path=hooks_path,
            hook_script=binding.hook_script,
        )
        assert modified["trusted"] is False
        assert modified["trustStatus"] == "modified"
        assert modified["untrustedEvents"] == [HOOK_EVENTS[0]]
        assert modified["remediation"] == _hook_trust_remediation()
        listed_hooks[0]["trustStatus"] = "trusted"
        listed_hooks[1]["enabled"] = False
        disabled = parse_codex_hook_trust(
            trust_value,
            hooks_path=hooks_path,
            hook_script=binding.hook_script,
        )
        assert disabled["trustStatus"] == "disabled"
        listed_hooks.pop()
        incomplete = parse_codex_hook_trust(
            trust_value,
            hooks_path=hooks_path,
            hook_script=binding.hook_script,
        )
        assert incomplete["trustStatus"] == "disabled"
        assert HOOK_EVENTS[-1] in incomplete["untrustedEvents"]
        assert binding.unbind()["bound"] is False
        saved = json.loads(hooks_path.read_text(encoding="utf-8"))
        assert any(item.get("command") == "keep-me" for group in saved["hooks"]["Stop"] for item in group["hooks"])
    print("codex_pet_companion self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="VibeBoard Codex Pet Companion web service")
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--no-open", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    try:
        asyncio.run(_standalone(args.port, not args.no_open))
    except KeyboardInterrupt:
        return 130
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except CompanionError as exc:
        print(f"codex_pet_companion: {exc}", file=sys.stderr)
        raise SystemExit(1)
