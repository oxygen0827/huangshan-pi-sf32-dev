#!/usr/bin/env python3
"""Signed firmware feed and USB recovery orchestration for Companion.

The current production board still uses the verified USB recovery path.  This
module deliberately keeps the transport behind one manager so the future
ping-pong DFU transport can replace ``_flash`` without changing the web API.
"""
from __future__ import annotations

import json
import os
import shutil
import ssl
import stat
import subprocess
import tempfile
import threading
import time
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path
from pathlib import PurePosixPath
from typing import Any, Callable

from firmware_release import FirmwareReleaseError, validate_release


MAX_FEED_BYTES = 256 * 1024
MAX_RELEASE_BYTES = 32 * 1024 * 1024
HTTPS_READ_ATTEMPTS = 3
HTTPS_READ_RETRY_SECONDS = 0.35


class FirmwareManagerError(RuntimeError):
    pass


def _valid_https_url(value: object, *, label: str) -> str:
    text = str(value or "")
    parsed = urllib.parse.urlsplit(text)
    try:
        port = parsed.port
    except ValueError as exc:
        raise FirmwareManagerError(f"{label} contains an invalid URL") from exc
    if (
        parsed.scheme != "https"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or port not in (None, 443)
        or bool(parsed.fragment)
    ):
        raise FirmwareManagerError(f"{label} must use a credential-free https:// URL")
    return text


class _HTTPSRedirectHandler(urllib.request.HTTPRedirectHandler):
    def redirect_request(
        self,
        request: urllib.request.Request,
        file_pointer: Any,
        code: int,
        message: str,
        headers: Any,
        new_url: str,
    ) -> urllib.request.Request | None:
        safe_url = _valid_https_url(
            urllib.parse.urljoin(request.full_url, new_url),
            label="firmware redirect",
        )
        return super().redirect_request(request, file_pointer, code, message, headers, safe_url)


def _retryable_https_read_error(error: OSError) -> bool:
    """Retry transient transport failures, but never hide certificate errors."""
    if isinstance(error, urllib.error.HTTPError):
        return False
    reason = getattr(error, "reason", error)
    if isinstance(reason, ssl.SSLCertVerificationError):
        return False
    if isinstance(reason, ssl.SSLError):
        return "CERTIFICATE_VERIFY_FAILED" not in str(reason).upper()
    return True


def _open_https(request: urllib.request.Request, *, timeout: float):
    """Open an idempotent firmware GET with bounded transport recovery."""
    for attempt in range(HTTPS_READ_ATTEMPTS):
        try:
            return urllib.request.build_opener(_HTTPSRedirectHandler()).open(request, timeout=timeout)
        except OSError as exc:
            if attempt + 1 >= HTTPS_READ_ATTEMPTS or not _retryable_https_read_error(exc):
                raise
            time.sleep(HTTPS_READ_RETRY_SECONDS * (attempt + 1))
    raise AssertionError("unreachable")


def _safe_extract(package: zipfile.ZipFile, destination: Path) -> None:
    """Extract a release only after validating every archive member path."""
    root = destination.resolve()
    for member in package.infolist():
        name = member.filename
        parts = PurePosixPath(name).parts
        if not name or name.startswith("/") or ".." in parts:
            raise FirmwareManagerError(f"firmware archive contains unsafe path: {name!r}")
        mode = member.external_attr >> 16
        if stat.S_ISLNK(mode):
            raise FirmwareManagerError(f"firmware archive contains a symlink: {name!r}")
        target = (destination / Path(*parts)).resolve()
        if target != root and root not in target.parents:
            raise FirmwareManagerError(f"firmware archive escapes staging directory: {name!r}")
    destination.mkdir(parents=True, exist_ok=True)
    package.extractall(destination)


def _version_key(value: str) -> tuple[int, ...]:
    parts = []
    for token in value.split("."):
        number = "".join(ch for ch in token if ch.isdigit())
        parts.append(int(number or 0))
    return tuple(parts or [0])


def _flash_failure_message(lines: list[str]) -> str:
    """Keep the actionable post-flash error instead of mislabeling every failure."""
    tail = " | ".join(lines[-4:]).strip()
    if tail:
        return f"firmware installation validation failed: {tail[:800]}"
    return "firmware installation validation failed; no tool output was received"


class FirmwareManager:
    def __init__(self, state_dir: Path, *, root: Path) -> None:
        self.state_dir = state_dir.expanduser() / "firmware"
        self.state_dir.mkdir(parents=True, exist_ok=True)
        self.root = root
        self.feed_url = os.environ.get("VIBEBOARD_FIRMWARE_MANIFEST_URL", "").strip()
        self.public_key = Path(os.environ["VIBEBOARD_FIRMWARE_PUBLIC_KEY"]).expanduser() if os.environ.get("VIBEBOARD_FIRMWARE_PUBLIC_KEY") else None
        self.release_dir = Path(os.environ["VIBEBOARD_FIRMWARE_RELEASE_DIR"]).expanduser() if os.environ.get("VIBEBOARD_FIRMWARE_RELEASE_DIR") else None
        self.port = os.environ.get("VIBEBOARD_FIRMWARE_PORT", "").strip()
        self._operation_lock = threading.Lock()

    @property
    def configured(self) -> bool:
        return bool(self.public_key and self.public_key.is_file() and (self.feed_url or self.release_dir))

    def _state_path(self, name: str) -> Path:
        return self.state_dir / name

    def _read_state(self, name: str) -> dict[str, Any]:
        try:
            value = json.loads(self._state_path(name).read_text(encoding="utf-8"))
            return value if isinstance(value, dict) else {}
        except (OSError, ValueError):
            return {}

    def _write_state(self, name: str, value: dict[str, Any]) -> None:
        target = self._state_path(name)
        temporary = target.with_suffix(target.suffix + ".tmp")
        temporary.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n", encoding="utf-8")
        os.replace(temporary, target)

    def status(self) -> dict[str, object]:
        last_good = self._read_state("last-success.json")
        last_attempt = self._read_state("last-attempt.json")
        value: dict[str, object] = {
            "updateMode": "verified_usb_recovery",
            "rollbackMode": "automatic_health_checked",
            "wirelessDfu": False,
            "configured": self.configured,
            "feedConfigured": bool(self.feed_url),
            "pinnedKeyConfigured": bool(self.public_key and self.public_key.is_file()),
            "portConfigured": bool(self.port),
            "portSelection": "configured" if self.port else "automatic_single_uart",
            "lastGood": last_good or None,
            "lastAttempt": last_attempt or None,
        }
        if not self.configured:
            value["reason"] = "Set VIBEBOARD_FIRMWARE_MANIFEST_URL and VIBEBOARD_FIRMWARE_PUBLIC_KEY"
            value["usbRecovery"] = {"ready": False, "reason": "official firmware source is not configured"}
        else:
            value["usbRecovery"] = self.usb_recovery_status()
        return value

    def usb_recovery_status(self) -> dict[str, object]:
        """Report whether the local Companion can unambiguously select a USB UART.

        Enumeration does not open the serial port, so it cannot reset the board.
        The actual flash operation selects it again immediately before writing.
        """
        try:
            from flash import choose_port

            port = choose_port(self.port or None, allow_usbmodem=False)
        except SystemExit as exc:
            return {"ready": False, "reason": str(exc)[:220]}
        return {"ready": True, "port": port.path, "role": port.role}

    def _read_feed(self) -> dict[str, Any]:
        if not self.feed_url:
            return {"schemaVersion": 1, "releases": []}
        request = urllib.request.Request(
            _valid_https_url(self.feed_url, label="firmware feed"),
            headers={"accept": "application/json"},
        )
        try:
            with _open_https(request, timeout=10) as response:
                data = response.read(MAX_FEED_BYTES + 1)
        except OSError as exc:
            raise FirmwareManagerError(f"firmware feed unavailable: {exc}") from exc
        if len(data) > MAX_FEED_BYTES:
            raise FirmwareManagerError("firmware feed is too large")
        try:
            value = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, ValueError) as exc:
            raise FirmwareManagerError("firmware feed is not valid JSON") from exc
        if not isinstance(value, dict) or value.get("schemaVersion") != 1 or not isinstance(value.get("releases"), list):
            raise FirmwareManagerError("unsupported firmware feed")
        return value

    def available(self) -> dict[str, object]:
        feed = self._read_feed()
        releases = [row for row in feed["releases"] if isinstance(row, dict) and isinstance(row.get("version"), str)]
        releases.sort(key=lambda row: _version_key(str(row["version"])), reverse=True)
        baseline_rows = [row for row in releases if row.get("baseline") is True]
        baseline = baseline_rows[0] if len(baseline_rows) == 1 else None
        last = self._read_state("last-success.json").get("version")
        latest = releases[0] if releases else None
        return {
            "board": feed.get("board"),
            "current": last,
            "latest": latest,
            "baseline": baseline,
            "releases": releases[:10],
        }

    @staticmethod
    def _baseline_version(available: dict[str, object]) -> str:
        row = available.get("baseline")
        version = row.get("version") if isinstance(row, dict) else None
        if not isinstance(version, str) or not version:
            raise FirmwareManagerError("official firmware feed has no unique baseline release")
        return version

    def _release_from_feed(self, version: str | None) -> tuple[str, Path]:
        if self.release_dir:
            if not self.public_key or not self.public_key.is_file():
                raise FirmwareManagerError("pinned firmware public key is missing")
            manifest = validate_release(self.release_dir, self.public_key)
            if version and manifest["version"] != version:
                raise FirmwareManagerError("requested firmware version is not the configured release")
            return str(manifest["version"]), self.release_dir
        rows = self.available()["releases"]
        candidates = [row for row in rows if not version or row.get("version") == version]
        if not candidates:
            raise FirmwareManagerError("requested firmware release is not in the signed feed")
        row = candidates[0]
        url = _valid_https_url(row.get("url"), label="firmware release URL")
        expected_sha = str(row.get("sha256") or "")
        staging = Path(tempfile.mkdtemp(prefix="vibeboard-release-", dir=self.state_dir))
        archive = staging / "release.zip"
        try:
            with _open_https(urllib.request.Request(url), timeout=30) as response, archive.open("wb") as handle:
                total = 0
                while True:
                    block = response.read(128 * 1024)
                    if not block:
                        break
                    total += len(block)
                    if total > MAX_RELEASE_BYTES:
                        raise FirmwareManagerError("firmware release is too large")
                    handle.write(block)
            if expected_sha:
                import hashlib
                digest = hashlib.sha256(archive.read_bytes()).hexdigest()
                if digest != expected_sha:
                    raise FirmwareManagerError("firmware release archive digest mismatch")
            with zipfile.ZipFile(archive) as package:
                _safe_extract(package, staging / "release")
            release = staging / "release"
            # Feeds may wrap the release in one directory; locate the signed root
            # without accepting arbitrary paths outside the staging directory.
            if not (release / "firmware-release.json").is_file():
                roots = list(release.glob("*/firmware-release.json"))
                if len(roots) != 1:
                    raise FirmwareManagerError("downloaded release has no unique signed root")
                release = roots[0].parent
            if not self.public_key:
                raise FirmwareManagerError("pinned firmware public key is missing")
            manifest = validate_release(release, self.public_key)
            if version and manifest["version"] != version:
                raise FirmwareManagerError("downloaded firmware version does not match feed")
            destination = self.state_dir / "releases" / str(manifest["version"])
            destination.parent.mkdir(parents=True, exist_ok=True)
            if destination.exists():
                shutil.rmtree(destination)
            shutil.copytree(release, destination)
            return str(manifest["version"]), destination
        except (OSError, zipfile.BadZipFile, FirmwareReleaseError) as exc:
            raise FirmwareManagerError(f"firmware release verification failed: {exc}") from exc
        finally:
            shutil.rmtree(staging, ignore_errors=True)

    def _flash(self, release: Path, version: str, progress: Callable[[str, int, str], None]) -> None:
        port = self.port
        if not port:
            try:
                from flash import choose_port
                port = choose_port(None, allow_usbmodem=False).path
            except SystemExit as exc:
                raise FirmwareManagerError(str(exc)) from exc
        command = [
            os.fspath(Path(os.environ.get("VIBEBOARD_PYTHON", os.sys.executable))),
            "apply", "--release", str(release), "--port", port,
            "--public-key", str(self.public_key), "--confirm", "UPDATE_FIRMWARE",
        ]
        if getattr(os.sys, "frozen", False):
            command.insert(1, "--firmware-release")
        else:
            command.insert(1, str(self.root / "scripts" / "firmware_release.py"))
        progress("flash", 45, "Verified image; flashing over USB")
        process = subprocess.Popen(command, cwd=self.root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        assert process.stdout is not None
        output: list[str] = []
        for line in process.stdout:
            line = line.strip()
            if line:
                output.append(line)
                progress("flash", 62, line[:220])
        if process.wait() != 0:
            raise FirmwareManagerError(_flash_failure_message(output))
        progress("health", 92, "Boot log confirmed; checking Runtime health")

    def update(self, version: str | None, progress: Callable[[str, int, str], None]) -> dict[str, object]:
        if not self._operation_lock.acquire(blocking=False):
            raise FirmwareManagerError("another firmware operation is already in progress")
        try:
            return self._update_locked(version, progress)
        finally:
            self._operation_lock.release()

    def install_baseline(
        self,
        progress: Callable[[str, int, str], None],
        *,
        expected_version: str | None = None,
    ) -> dict[str, object]:
        """Explicitly migrate a legacy board that has no reportable release version."""
        if not self._operation_lock.acquire(blocking=False):
            raise FirmwareManagerError("another firmware operation is already in progress")
        try:
            if not self.configured:
                raise FirmwareManagerError("firmware update is not configured for this Companion build")
            available = self.available()
            version = self._baseline_version(available)
            if expected_version is not None and version != expected_version:
                raise FirmwareManagerError("official baseline firmware changed; review the new release before installing")
            version_value, release = self._release_from_feed(version)
            if version_value != version:
                raise FirmwareManagerError("downloaded baseline version does not match the official feed")
            progress("verify", 24, f"Verified signed baseline firmware {version_value}")
            self._write_state("last-attempt.json", {
                "version": version_value,
                "baseline": True,
                "startedAt": int(time.time()),
            })
            self._flash(release, version_value, progress)
            self._write_state("last-success.json", {
                "version": version_value,
                "release": str(release),
                "baseline": True,
                "updatedAt": int(time.time()),
            })
            progress("complete", 100, f"Baseline firmware {version_value} is healthy")
            return {"version": version_value, "baseline": True, "rolledBack": False}
        finally:
            self._operation_lock.release()

    def _update_locked(self, version: str | None, progress: Callable[[str, int, str], None]) -> dict[str, object]:
        if not self.configured:
            raise FirmwareManagerError("firmware update is not configured for this Companion build")
        version_value, release = self._release_from_feed(version)
        previous = self._read_state("last-success.json")
        self._write_state("last-attempt.json", {
            "version": version_value,
            "previous": previous.get("version"),
            "previousRelease": previous.get("release"),
            "startedAt": int(time.time()),
        })
        try:
            progress("verify", 24, "Pinned signature and flash layout verified")
            self._flash(release, version_value, progress)
            self._write_state("last-success.json", {
                "version": version_value,
                "release": str(release),
                "previous": previous.get("version"),
                "previousRelease": previous.get("release"),
                "updatedAt": int(time.time()),
            })
            progress("complete", 100, f"Firmware {version_value} is healthy")
            return {"version": version_value, "rolledBack": False}
        except Exception:
            if previous.get("version") and previous.get("release"):
                try:
                    progress("rollback", 70, "Health check failed; restoring last-good firmware")
                    self._flash(Path(str(previous["release"])), str(previous["version"]), progress)
                except Exception as rollback_error:
                    raise FirmwareManagerError(f"update failed and automatic rollback failed: {rollback_error}")
            raise

    def rollback(self, progress: Callable[[str, int, str], None]) -> dict[str, object]:
        if not self._operation_lock.acquire(blocking=False):
            raise FirmwareManagerError("another firmware operation is already in progress")
        try:
            return self._rollback_locked(progress)
        finally:
            self._operation_lock.release()

    def _rollback_locked(self, progress: Callable[[str, int, str], None]) -> dict[str, object]:
        last_good = self._read_state("last-success.json")
        candidate = last_good.get("previous")
        release = last_good.get("previousRelease")
        if not candidate or not release:
            raise FirmwareManagerError("no verified previous firmware release is available")
        self._flash(Path(str(release)), str(candidate), progress)
        self._write_state("last-success.json", {
            "version": candidate,
            "release": release,
            "updatedAt": int(time.time()),
        })
        progress("complete", 100, f"Rolled back to firmware {candidate}")
        return {"version": candidate, "rolledBack": True}


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="companion-firmware-test-") as directory:
        root = Path(directory)
        manager = FirmwareManager(root, root=root)
        status = manager.status()
        assert status["configured"] is False
        assert "reason" in status
        assert _version_key("1.10.0") > _version_key("1.9.9")
        assert _valid_https_url("https://downloads.example.com/release.zip", label="test")
        for unsafe_url in (
            "http://downloads.example.com/release.zip",
            "https://user@downloads.example.com/release.zip",
            "https://downloads.example.com:444/release.zip",
            "https://downloads.example.com/release.zip#fragment",
        ):
            try:
                _valid_https_url(unsafe_url, label="test")
            except FirmwareManagerError:
                pass
            else:
                raise AssertionError(f"unsafe HTTPS URL accepted: {unsafe_url}")
        assert _retryable_https_read_error(urllib.error.URLError(ssl.SSLEOFError("transient EOF")))
        assert not _retryable_https_read_error(
            urllib.error.URLError(ssl.SSLCertVerificationError("certificate verify failed"))
        )
        archive_path = root / "unsafe.zip"
        with zipfile.ZipFile(archive_path, "w") as archive:
            archive.writestr("../escape", b"must not extract")
        with zipfile.ZipFile(archive_path) as archive:
            try:
                _safe_extract(archive, root / "staging")
            except FirmwareManagerError:
                pass
            else:
                raise AssertionError("archive traversal was accepted")
        assert not (root / "escape").exists()
        manager._read_feed = lambda: {
            "schemaVersion": 1,
            "releases": [
                {"version": "1.0.1"},
                {"version": "1.0.0", "baseline": True},
            ],
        }
        assert manager.available()["baseline"] == {"version": "1.0.0", "baseline": True}
        manager._read_feed = lambda: {
            "schemaVersion": 1,
            "releases": [
                {"version": "1.0.0", "baseline": True},
                {"version": "0.9.0", "baseline": True},
            ],
        }
        assert manager.available()["baseline"] is None
        assert FirmwareManager._baseline_version({"baseline": {"version": "1.0.0"}}) == "1.0.0"
        try:
            FirmwareManager._baseline_version({"baseline": None})
        except FirmwareManagerError:
            pass
        else:
            raise AssertionError("missing baseline release was accepted")
        assert manager._operation_lock.acquire(blocking=False)
        try:
            try:
                manager.update(None, lambda *_: None)
            except FirmwareManagerError as exc:
                assert "already in progress" in str(exc)
            else:
                raise AssertionError("concurrent firmware update was accepted")
        finally:
            manager._operation_lock.release()
    assert _flash_failure_message([]) == "firmware installation validation failed; no tool output was received"
    assert _flash_failure_message(["written", "Runtime health check failed: status timeout"]).endswith(
        "Runtime health check failed: status timeout"
    )
    print("companion_firmware self-test ok")
