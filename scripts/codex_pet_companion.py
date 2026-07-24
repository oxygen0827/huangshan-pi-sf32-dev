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
from typing import Any, Mapping, Protocol

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


ROOT_DIR = Path(__file__).resolve().parents[1]
WEB_PATH = ROOT_DIR / "scripts" / "codex_pet_web.html"
PETDEX_MANIFEST_URL = "https://petdex.dev/api/manifest"
PETDEX_CONFIG_PATH = ROOT_DIR / "scripts" / "petdex_pets.json"
DEFAULT_STATE_DIR = Path.home() / ".vibeboard" / "companion"
DEFAULT_HOOKS_PATH = Path.home() / ".codex" / "hooks.json"
SESSION_TTL_SECONDS = 15 * 60
CATALOG_TTL_SECONDS = 10 * 60
MAX_JSON_BYTES = 64 * 1024
MAX_MANIFEST_BYTES = 2 * 1024 * 1024
MAX_SPRITESHEET_BYTES = 16 * 1024 * 1024
SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,23}$")
SAFE_DIGEST = re.compile(r"^[0-9a-f]{64}$")
HOOK_EVENTS = ("SessionStart", "PermissionRequest", "UserPromptSubmit", "PreToolUse", "PostToolUse", "Stop")


class CompanionError(RuntimeError):
    pass


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


def _fetch_json(url: str, *, max_bytes: int, timeout: float = 20.0) -> object:
    request = urllib.request.Request(url, headers={"User-Agent": "VibeBoard-Companion/1.0"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        declared = int(response.headers.get("content-length") or 0)
        if declared > max_bytes:
            raise CompanionError("Petdex manifest is too large")
        data = response.read(max_bytes + 1)
    if len(data) > max_bytes:
        raise CompanionError("Petdex manifest is too large")
    return json.loads(data.decode("utf-8"))


def _fetch_bytes(url: str, *, max_bytes: int, timeout: float = 25.0) -> tuple[bytes, str]:
    request = urllib.request.Request(url, headers={"User-Agent": "VibeBoard-Companion/1.0"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
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
    if parsed.scheme != "https" or parsed.hostname != "assets.petdex.dev":
        raise CompanionError("Petdex entry contains a non-allowlisted asset URL")
    return text


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
        "stateRows": {"idle": 0, "ready": 1, "running": 2, "blocked": 3, "needs": 4},
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


class CodexHookBinding:
    def __init__(self, hooks_path: Path = DEFAULT_HOOKS_PATH, *, python_path: Path | None = None) -> None:
        self.hooks_path = hooks_path.expanduser()
        self.python_path = (python_path or Path(sys.executable)).resolve()
        self.hook_script = (ROOT_DIR / "scripts" / "codex_pet_hook.py").resolve()

    @property
    def command(self) -> str:
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
        return {
            "detected": self.hooks_path.parent.exists(),
            "bound": len(events) == len(HOOK_EVENTS),
            "events": events,
            "hooksPath": str(self.hooks_path),
        }

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
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)

    def update(self, *, stage: str | None = None, progress: int | None = None, message: str | None = None) -> None:
        with self._lock:
            if stage is not None:
                self.stage = stage
            if progress is not None:
                self.progress = max(0, min(100, int(progress)))
            if message is not None:
                self.message = message[:240]

    def append(self, value: str) -> None:
        with self._lock:
            self.log.append(value[:500])
            self.log[:] = self.log[-160:]

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
    ) -> None:
        self.loop = loop
        self.device = device
        self.state_dir = state_dir.expanduser()
        self.cache_dir = self.state_dir / "packages"
        self.key_dir = self.state_dir / "keys"
        self.catalog = PetdexCatalog(cache_path=self.state_dir / "petdex-manifest.json")
        self.hooks = CodexHookBinding(hooks_path)
        self.ble_cache = ble_cache
        self.jobs: dict[str, CompanionJob] = {}
        self._jobs_lock = threading.Lock()
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
            "board": {
                "connected": bool(self.device.connected),
                "name": cache.get("name") or "VibeBoard",
                "identity": (cache.get("address") or label).split(" ", 1)[0][-8:],
                "runtimeApi": capabilities.get("rt"),
                "bleInstall": (capabilities.get("ins") or {}).get("ble") == 1
                if isinstance(capabilities.get("ins"), dict) else False,
            },
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

    def _new_job(self, kind: str, slug: str) -> CompanionJob:
        job = CompanionJob(f"pet-{secrets.token_hex(8)}", kind, slug)
        with self._jobs_lock:
            self.jobs[job.job_id] = job
            cutoff = time.time() - 3600
            self.jobs = {key: value for key, value in self.jobs.items() if value.created_at >= cutoff}
        return job

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
        package_path, package, public_key = await asyncio.to_thread(
            build_petdex_hpet,
            pet,
            cache_dir=self.cache_dir,
            key_dir=self.key_dir,
        )
        job.digest = package.digest
        job.download_url = f"/api/packages/{package.digest}.hpet"
        job.update(stage="verify", progress=30, message="Signature and animation states verified")
        job.append(f"verified hpet digest={package.digest} bytes={package_path.stat().st_size}")
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

        job.append(f"compose runtime files={len(files)} bytes={sum(map(len, files.values()))}")
        try:
            result = await self.device.install_codex_pet(files, package.slug, progress=progress)
        except Exception as install_error:
            if isinstance(previous_digest, str) and SAFE_DIGEST.fullmatch(previous_digest) and previous_digest != package.digest:
                previous_path = self.cache_dir / f"{previous_digest}.hpet"
                if previous_path.is_file():
                    job.append(f"restoring previous pet digest={previous_digest}")
                    try:
                        await self.device.reconnect()
                        public_key = ensure_signing_keys(self.key_dir)[1]
                        previous_package = await asyncio.to_thread(read_hpet, previous_path.read_bytes(), public_key=public_key)
                        _, previous_files = await asyncio.to_thread(compose_codex_pet_runtime, previous_package)
                        await self.device.install_codex_pet(previous_files, previous_package.slug)
                        job.append("previous pet restored")
                    except Exception as rollback_error:
                        job.append(f"rollback also failed: {type(rollback_error).__name__}: {rollback_error}")
            raise install_error
        job.update(stage="verify", progress=96, message="Verifying board animation")
        _atomic_json(self.state_dir / "active.json", {"slug": package.slug, "digest": package.digest, "installedAt": int(time.time())})
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
        temp = asset_path.with_suffix(asset_path.suffix + ".tmp")
        temp.write_bytes(data)
        os.replace(temp, asset_path)
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
            if path.startswith("/v1/jobs/"):
                job = self.state.get_job(urllib.parse.unquote(path.removeprefix("/v1/jobs/")))
                if job is None:
                    self._error(404, "job not found")
                    return
                self._json(200, job.to_dict())
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
    assert normalized["stateRows"] == {"idle": 0, "ready": 1, "running": 2, "blocked": 3, "needs": 4}
    with tempfile.TemporaryDirectory(prefix="companion-test-") as temp_text:
        hooks_path = Path(temp_text) / ".codex" / "hooks.json"
        hooks_path.parent.mkdir()
        hooks_path.write_text(json.dumps({"hooks": {"Stop": [{"hooks": [{"type": "command", "command": "keep-me"}]}]}}), encoding="utf-8")
        binding = CodexHookBinding(hooks_path)
        assert binding.bind()["bound"] is True
        saved = json.loads(hooks_path.read_text(encoding="utf-8"))
        assert any(item.get("command") == "keep-me" for group in saved["hooks"]["Stop"] for item in group["hooks"])
        assert binding.bind()["bound"] is True
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
