#!/usr/bin/env python3
"""Persistent Companion jobs and bounded package-cache ownership."""
from __future__ import annotations

import json
import os
import re
import tempfile
import threading
import time
from pathlib import Path
from typing import Mapping


SAFE_DIGEST = re.compile(r"^[0-9a-f]{64}$")
TERMINAL_JOB_STATES = frozenset({"done", "failed"})


def _number(value: object, default: float = 0.0) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return default
    return result if result == result else default


def _atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(value, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(name, 0o600)
        os.replace(name, path)
    finally:
        if os.path.exists(name):
            os.unlink(name)


def _read_json(path: Path, default: object) -> object:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError, json.JSONDecodeError):
        return default


class JobJournal:
    """Small atomic journal for the user-visible Companion job lifecycle."""

    def __init__(self, path: Path, *, max_age_seconds: int = 24 * 60 * 60) -> None:
        self.path = path.expanduser()
        self.max_age_seconds = max(3600, int(max_age_seconds))
        self._lock = threading.RLock()
        self._rows: dict[str, dict[str, object]] = {}
        self._last_write_at = 0.0
        self._write_interval = 0.25
        self._load()
        self._recover_interrupted()
        self._prune()

    def _load(self) -> None:
        value = _read_json(self.path, {})
        rows = value.get("jobs") if isinstance(value, dict) else None
        if not isinstance(rows, list):
            return
        for row in rows:
            if not isinstance(row, dict):
                continue
            job_id = row.get("jobId")
            if isinstance(job_id, str) and job_id:
                self._rows[job_id] = dict(row)

    def _write(self) -> None:
        rows = sorted(self._rows.values(), key=lambda row: _number(row.get("createdAt", 0)))
        _atomic_json(self.path, {"schemaVersion": 1, "jobs": rows})
        self._last_write_at = time.monotonic()

    def _recover_interrupted(self) -> None:
        changed = False
        for row in self._rows.values():
            if row.get("status") in TERMINAL_JOB_STATES:
                continue
            row["status"] = "failed"
            row["stage"] = "interrupted"
            row["message"] = "Companion restarted before this job completed"
            row["updatedAt"] = time.time()
            log = row.setdefault("log", [])
            if isinstance(log, list):
                log.append("ERROR CompanionRestarted: job interrupted by service restart")
                del log[:-160]
            changed = True
        if changed:
            self._write()

    def _prune(self, now: float | None = None) -> None:
        cutoff = (time.time() if now is None else now) - self.max_age_seconds
        stale = [
            job_id
            for job_id, row in self._rows.items()
            if row.get("status") in TERMINAL_JOB_STATES
            and _number(row.get("updatedAt", row.get("createdAt", 0))) < cutoff
        ]
        if stale:
            for job_id in stale:
                self._rows.pop(job_id, None)
            self._write()

    def upsert(self, row: Mapping[str, object], *, force: bool = False) -> None:
        job_id = row.get("jobId")
        if not isinstance(job_id, str) or not job_id:
            return
        with self._lock:
            self._rows[job_id] = dict(row)
            self._prune()
            if force or time.monotonic() - self._last_write_at >= self._write_interval:
                self._write()

    def get(self, job_id: str) -> dict[str, object] | None:
        with self._lock:
            row = self._rows.get(job_id)
            return dict(row) if row is not None else None

    def rows(self, *, limit: int = 100) -> list[dict[str, object]]:
        with self._lock:
            values = sorted(self._rows.values(), key=lambda row: _number(row.get("createdAt", 0)), reverse=True)
            return [dict(row) for row in values[: max(1, min(int(limit), 200))]]

    def summary(self) -> dict[str, int]:
        with self._lock:
            counts = {"queued": 0, "running": 0, "done": 0, "failed": 0}
            for row in self._rows.values():
                status = str(row.get("status") or "failed")
                counts[status] = counts.get(status, 0) + 1
            return counts


class PackageCache:
    """Bounded cache that never evicts the active or explicitly protected package."""

    def __init__(self, directory: Path, *, max_entries: int = 24, max_bytes: int = 128 * 1024 * 1024) -> None:
        self.directory = directory.expanduser()
        self.max_entries = max(2, int(max_entries))
        self.max_bytes = max(16 * 1024 * 1024, int(max_bytes))
        self._lock = threading.RLock()
        self.directory.mkdir(parents=True, exist_ok=True)

    def _files(self) -> list[Path]:
        try:
            return [
                path for path in self.directory.glob("*.hpet")
                if SAFE_DIGEST.fullmatch(path.stem) and path.is_file()
            ]
        except OSError:
            return []

    def status(self) -> dict[str, object]:
        with self._lock:
            files = self._files()
            valid_files: list[Path] = []
            total = 0
            for path in files:
                try:
                    total += path.stat().st_size
                    valid_files.append(path)
                except OSError:
                    continue
            return {
                "entries": len(valid_files),
                "bytes": total,
                "maxEntries": self.max_entries,
                "maxBytes": self.max_bytes,
                "withinLimits": len(valid_files) <= self.max_entries and total <= self.max_bytes,
            }

    def prune(self, *, active_digest: str | None = None, preserve: tuple[str, ...] = ()) -> dict[str, object]:
        protected = {value for value in (active_digest, *preserve) if isinstance(value, str) and SAFE_DIGEST.fullmatch(value)}
        removed: list[str] = []
        with self._lock:
            files = []
            for path in self._files():
                try:
                    files.append((path, path.stat().st_mtime, path.stat().st_size))
                except OSError:
                    continue
            files.sort(key=lambda item: item[1], reverse=True)
            total = sum(item[2] for item in files)
            for path, _, _ in reversed(files):
                if len(files) <= self.max_entries and total <= self.max_bytes:
                    break
                if path.stem in protected:
                    continue
                try:
                    size = path.stat().st_size
                    path.unlink()
                except OSError:
                    continue
                files = [item for item in files if item[0] != path]
                total -= size
                removed.append(path.name)
            result = self.status()
            result["removed"] = removed
            result["protected"] = sorted(protected)
            return result


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="companion-state-test-") as directory:
        root = Path(directory)
        journal = JobJournal(root / "jobs.json", max_age_seconds=3600)
        journal.upsert({"jobId": "job-one", "status": "running", "createdAt": time.time()})
        current = journal.get("job-one")
        assert current is not None and current["status"] == "running"
        restarted = JobJournal(root / "jobs.json", max_age_seconds=3600)
        recovered = restarted.get("job-one")
        assert recovered is not None and recovered["stage"] == "interrupted"
        (root / "corrupt.json").write_text(
            '{"jobs":[{"jobId":"job-bad","status":"done","createdAt":"invalid","updatedAt":null}]}',
            encoding="utf-8",
        )
        corrupt = JobJournal(root / "corrupt.json")
        assert isinstance(corrupt.summary(), dict)

        cache = PackageCache(root / "packages", max_entries=2, max_bytes=16 * 1024 * 1024)
        digests = [f"{index:064x}" for index in range(3)]
        for index, digest in enumerate(digests):
            (cache.directory / f"{digest}.hpet").write_bytes(b"x" * (index + 1))
            os.utime(cache.directory / f"{digest}.hpet", (index, index))
        result = cache.prune(active_digest=digests[0], preserve=(digests[1],))
        assert result["entries"] == 2 and digests[0] in result["protected"] and digests[2] in result["removed"][0]
    print("companion_state self-test ok")


if __name__ == "__main__":
    run_self_test()
