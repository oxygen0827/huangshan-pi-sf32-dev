#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import json
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Protocol, Union

try:
    import serial
except ImportError:  # pragma: no cover - serial transport is optional for BLE-only users
    serial = None

from runtime_package import build_install_commands

SERVICE_UUID = "454d5452-0100-0000-5453-4e4954524256"
COMMAND_UUID = "454d5452-0200-0000-5453-4e4954524256"
STATUS_UUID = "454d5452-0300-0000-5453-4e4954524256"
VOICE_STREAM_UUID = "454d5452-0400-0000-5453-4e4954524256"
DEFAULT_DEVICE_NAME = "VibeBoard"
DEFAULT_BLE_CACHE_PATH = Path.home() / ".vibeboard" / "huangshan_ble.json"

CAPABILITIES_API = "vibeboard-huangshan-capabilities/v1"
SENSORS_API = "vibeboard-huangshan-sensors/v1"
POWER_API = "vibeboard-huangshan-power/v1"
DISPLAY_API = "vibeboard-huangshan-display/v1"
GPIO_API = "vibeboard-huangshan-gpio/v1"
TOUCH_API = "vibeboard-huangshan-touch/v1"
RGB_API = "vibeboard-huangshan-rgb/v1"
VOICE_API = "vibeboard-huangshan-voice-bridge/v1"
AUDIO_API = "vibeboard-huangshan-audio-playback/v1"
APP_MANAGER_API = "vibeboard-huangshan-app-manager/v1"

SERIAL_APP_PAGE_LIMIT = 5
# Keep BLE app pages below the status characteristic buffer/MTU comfort zone.
BLE_APP_PAGE_LIMIT = 1
RUNTIME_DATA_CHUNK_BYTES = 160
VOICE_CHUNK_BYTES = 200
SERIAL_MSH_VOICE_CHUNK_BYTES = 30
INSTALL_CHUNK_BYTES = 48
SERIAL_JSON_CHUNK_BYTES = SERIAL_MSH_VOICE_CHUNK_BYTES
MAX_INSTALL_CHUNK_BYTES = 240


class JSONResponsePending(RuntimeError):
    pass


class JSONResponseTruncated(JSONResponsePending):
    pass


class RuntimeTransportError(RuntimeError):
    pass


def transport_fail(message: str, exit_code: int | None = None) -> None:
    raise RuntimeTransportError(message)


def _validate_audio_target(app_id: str, path: str) -> None:
    if not app_id or not app_id[0].islower() or len(app_id) > 15 or any(
        not (char.islower() or char.isdigit() or char == "_") for char in app_id
    ):
        transport_fail(f"Unsafe Runtime audio app id: {app_id!r}")
    if (
        not path.startswith(("assets/", "lib/"))
        or not path.lower().endswith(".wav")
        or ".." in path
        or "//" in path
        or any(not (char.isalnum() or char in "_./-") for char in path)
    ):
        transport_fail(f"Unsafe Runtime audio path: {path!r}")


def _validate_audio_volume(volume: int) -> None:
    if not isinstance(volume, int) or isinstance(volume, bool) or not 0 <= volume <= 15:
        transport_fail(f"Runtime audio volume must be 0..15, got {volume!r}")


def _assert_raises(expected: type[BaseException], func, *args, **kwargs) -> None:
    try:
        func(*args, **kwargs)
    except expected:
        return
    except BaseException as exc:
        raise AssertionError(f"expected {expected.__name__}, got {type(exc).__name__}: {exc}") from exc
    raise AssertionError(f"expected {expected.__name__}")


async def _assert_raises_async(expected: type[BaseException], func, *args, **kwargs) -> None:
    try:
        await func(*args, **kwargs)
    except expected:
        return
    except BaseException as exc:
        raise AssertionError(f"expected {expected.__name__}, got {type(exc).__name__}: {exc}") from exc
    raise AssertionError(f"expected {expected.__name__}")


class SyncRuntimeTransport(Protocol):
    def status(self) -> str: ...
    def capabilities(self) -> str: ...
    def sensors(self) -> str: ...
    def power(self) -> str: ...
    def display(self, brightness: int | None = None) -> str: ...
    def gpio(self) -> str: ...
    def touch(self) -> str: ...
    def rgb(self, color: str | None = None) -> str: ...
    def voice(self) -> str: ...
    def audio(self) -> str: ...
    def audio_play(self, app_id: str, path: str) -> str: ...
    def audio_stop(self) -> str: ...
    def audio_volume(self, volume: int) -> str: ...
    def voice_status(self, expected_seq: int | None = None) -> tuple[str, dict[str, str]]: ...
    def voice_start(self, duration_ms: int, expected_seq: int | None = None) -> tuple[str, dict[str, str]]: ...
    def voice_stop(self) -> str: ...
    def voice_read(self, sequence: int, offset: int, max_bytes: int) -> "VoiceReadChunk": ...
    def voice_clear(self) -> str: ...
    def capture_voice(
        self,
        duration_ms: int,
        *,
        chunk_bytes: int = VOICE_CHUNK_BYTES,
        ready_timeout: float = 8.0,
        poll_interval: float = 0.25,
        progress: Callable[[int, int], None] | None = None,
    ) -> tuple[bytes, dict[str, str]]: ...
    def send_voice_reply(
        self,
        reply: str,
        *,
        channel: str = "pc.voice",
        sequence: int | None = None,
    ) -> tuple[str, int]: ...
    def flow_status(self) -> str: ...
    def flow_clear(self) -> str: ...
    def flow_send(self, channel: str, sequence: int, text: str) -> str: ...
    def app_status(self) -> str: ...
    def apps(self) -> str: ...
    def launch_app(self, app_id: str) -> str: ...
    def stop_app(self) -> str: ...
    def delete_app(self, app_id: str) -> str: ...
    def staging_clear(self) -> str: ...
    def abort_install(self, app_id: str) -> str: ...
    def install_package(
        self,
        package_id: str,
        files: dict[str, bytes],
        *,
        chunk_bytes: int = INSTALL_CHUNK_BYTES,
        progress: Callable[[str, int, int], None] | None = None,
        commit: bool = True,
    ) -> str: ...


class AsyncRuntimeTransport(Protocol):
    async def status(self) -> str: ...
    async def verify_connection(self, *, timeout: float | None = None) -> str: ...
    async def hold_connection(self, seconds: float, *, keepalive_period: float = 5.0) -> None: ...
    async def capabilities(self) -> str: ...
    async def sensors(self) -> str: ...
    async def power(self) -> str: ...
    async def display(self, brightness: int | None = None) -> str: ...
    async def gpio(self) -> str: ...
    async def touch(self) -> str: ...
    async def rgb(self, color: str | None = None) -> str: ...
    async def voice(self) -> str: ...
    async def audio(self) -> str: ...
    async def audio_play(self, app_id: str, path: str) -> str: ...
    async def audio_stop(self) -> str: ...
    async def audio_volume(self, volume: int) -> str: ...
    async def voice_status(self, expected_seq: int | None = None) -> tuple[str, dict[str, str]]: ...
    async def voice_start(self, duration_ms: int, expected_seq: int | None = None) -> tuple[str, dict[str, str]]: ...
    async def voice_stop(self) -> str: ...
    async def voice_read(self, sequence: int, offset: int, max_bytes: int) -> "VoiceReadChunk": ...
    async def voice_clear(self) -> str: ...
    async def capture_voice(
        self,
        duration_ms: int,
        *,
        chunk_bytes: int = VOICE_CHUNK_BYTES,
        ready_timeout: float = 8.0,
        poll_interval: float = 0.25,
        progress: Callable[[int, int], None] | None = None,
    ) -> tuple[bytes, dict[str, str]]: ...
    async def send_voice_reply(
        self,
        reply: str,
        *,
        channel: str = "pc.voice",
        sequence: int | None = None,
    ) -> tuple[str, int]: ...
    async def flow_status(self) -> str: ...
    async def flow_clear(self) -> str: ...
    async def flow_send(self, channel: str, sequence: int, text: str) -> str: ...
    async def app_status(self) -> str: ...
    async def apps(self) -> str: ...
    async def launch_app(self, app_id: str) -> str: ...
    async def stop_app(self) -> str: ...
    async def delete_app(self, app_id: str) -> str: ...
    async def staging_clear(self) -> str: ...
    async def abort_install(self, app_id: str) -> str: ...
    async def install_package(
        self,
        package_id: str,
        files: dict[str, bytes],
        *,
        chunk_bytes: int = INSTALL_CHUNK_BYTES,
        progress: Callable[[str, int, int], None] | None = None,
        commit: bool = True,
    ) -> str: ...


RuntimeTransport = Union[SyncRuntimeTransport, AsyncRuntimeTransport]


def decode_status(data: bytes | bytearray | str | None) -> str:
    if data is None:
        return ""
    if isinstance(data, str):
        return data
    return bytes(data).decode("utf-8", "replace").replace("\x00", "").strip()


def json_response_is_truncated(value: dict[str, object]) -> bool:
    if value.get("error") == "truncated":
        return True
    truncated = value.get("truncated")
    return truncated is True or truncated == 1 or truncated == "1"


def status_is_transport_failure(status: str) -> bool:
    text = status.strip().lower()
    if text.startswith("read_status_failed:"):
        return True
    if text in {"disconnected", "not connected"}:
        return True
    if "not connected" in text and ("ble" in text or "peripheral" in text):
        return True
    return False


def extract_json_line(text: str, expected_api: str | None = None, *, allow_truncated: bool = False) -> str:
    decoder = json.JSONDecoder()
    best: dict[str, object] | None = None
    saw_truncated = False
    search_start = 0
    while True:
        start = text.find("{", search_start)
        if start < 0:
            break
        try:
            value, end = decoder.raw_decode(text[start:])
        except json.JSONDecodeError:
            search_start = start + 1
            continue
        if isinstance(value, dict):
            if expected_api and value.get("api") != expected_api:
                search_start = start + max(end, 1)
                continue
            if not allow_truncated and json_response_is_truncated(value):
                saw_truncated = True
                search_start = start + max(end, 1)
                continue
            if "api" in value:
                best = value
            elif best is None:
                best = value
        search_start = start + max(end, 1)
    if best is not None:
        return json.dumps(best, ensure_ascii=False, separators=(",", ":"))
    if saw_truncated:
        raise JSONResponseTruncated("truncated JSON response")
    raise JSONResponsePending(text[-2000:])


def extract_json_chunk(text: str, kind: str) -> tuple[int, int, bytes] | None:
    for line in reversed(text.splitlines()):
        value = line.strip()
        marker = "ok json_read "
        marker_index = value.find(marker)
        if marker_index < 0:
            continue
        value = value[marker_index:]
        parts = value.split()
        values: dict[str, str] = {}
        for token in parts[2:]:
            if "=" not in token:
                continue
            key, token_value = token.split("=", 1)
            values[key] = token_value
        if values.get("kind") != kind:
            continue
        try:
            offset = int(values.get("offset", "0"))
            total = int(values.get("total", "0"))
            hex_text = ""
            for ch in values.get("hex", ""):
                if ch not in "0123456789abcdefABCDEF":
                    break
                hex_text += ch
            payload = bytes.fromhex(hex_text)
        except ValueError:
            continue
        return offset, total, payload
    return None


def combine_app_pages(pages: list[str]) -> str:
    merged: dict[str, object] | None = None
    apps: list[object] = []
    seen_app_ids: set[str] = set()
    expected_offset = 0
    expected_count: int | None = None
    for page_text in pages:
        data = json.loads(page_text)
        if not isinstance(data, dict):
            transport_fail("Runtime app page JSON was not an object")
        if data.get("api") != APP_MANAGER_API:
            transport_fail(f"Runtime app page API mismatch: {data.get('api')!r}")
        page_offset = safe_int(str(data.get("offset", expected_offset)), expected_offset)
        if page_offset != expected_offset:
            transport_fail(f"Runtime app page offset mismatch: expected {expected_offset}, got {page_offset}")
        page_apps = data.get("apps", [])
        if not isinstance(page_apps, list):
            transport_fail("Runtime app page apps field was not a list")
        page_included = safe_int(str(data.get("included", len(page_apps))), len(page_apps))
        if page_included != len(page_apps):
            transport_fail(f"Runtime app page included mismatch: included={page_included}, apps={len(page_apps)}")
        page_count = safe_int(str(data.get("count", len(apps) + len(page_apps))), len(apps) + len(page_apps))
        if expected_count is None:
            expected_count = page_count
        elif page_count != expected_count:
            transport_fail(f"Runtime app page count changed: {expected_count} -> {page_count}")
        for app in page_apps:
            if not isinstance(app, dict):
                transport_fail("Runtime app page entry was not an object")
            app_id = str(app.get("id", ""))
            if not app_id:
                transport_fail("Runtime app page entry missing id")
            if app_id in seen_app_ids:
                transport_fail(f"Runtime app page duplicate app id: {app_id}")
            seen_app_ids.add(app_id)
        if merged is None:
            merged = {key: value for key, value in data.items() if key not in {"apps", "offset", "limit", "included", "truncated"}}
        apps.extend(page_apps)
        expected_offset += page_included
    if merged is None:
        merged = {"api": APP_MANAGER_API, "apps": [], "count": 0}
    count = expected_count if expected_count is not None else len(apps)
    if len(apps) < count:
        transport_fail(f"Runtime app pages incomplete: got {len(apps)} of {count}")
    if len(apps) > count:
        transport_fail(f"Runtime app pages exceeded declared count: got {len(apps)} of {count}")
    merged["apps"] = apps
    merged["count"] = count
    merged["included"] = len(apps)
    merged["truncated"] = 0
    return json.dumps(merged, ensure_ascii=False, separators=(",", ":"))


def merge_status_text(previous: str, current: str) -> str:
    prev = previous.strip()
    curr = current.strip()
    if not curr:
        return prev
    if not prev:
        return curr
    if curr == prev:
        return prev
    if curr.startswith(prev):
        return curr
    if prev.startswith(curr):
        return prev
    overlap = min(len(prev), len(curr))
    for size in range(overlap, 0, -1):
        if prev.endswith(curr[:size]):
            return prev + curr[size:]
    return curr


def install_ack_matches(status: str, command: str) -> bool:
    parts = command.strip().split()
    if not parts:
        return False
    name = parts[0]
    if name == "vb_runtime_install_begin" and len(parts) >= 2:
        app_id = parts[1]
        return f"install_begin app={app_id} rc=" in status or f"install_begin {app_id} rc=" in status
    if name == "vb_runtime_install_file" and len(parts) >= 5:
        app_id, path, offset = parts[1], parts[2], parts[3]
        return (
            f"install_file app={app_id} path={path} offset={offset} rc=" in status
            or f"install_file {app_id}/{path} rc=" in status
        )
    if name == "vb_runtime_install_abort" and len(parts) >= 2:
        app_id = parts[1]
        return f"install_abort app={app_id} rc=" in status
    if name == "vb_runtime_install_end" and len(parts) >= 2:
        app_id = parts[1]
        return f"install_end app={app_id} " in status or f"install_end {app_id} rc=" in status
    return False


def app_launch_matches(status: str, app_id: str) -> bool:
    return (status.startswith(("ok launch ", "err launch ")) or " launch app=" in status) and f"app={app_id}" in status


def app_stop_matches(status: str) -> bool:
    return status.startswith(("ok stop ", "err stop ")) or " stop rc=" in status


def app_delete_matches(status: str, app_id: str) -> bool:
    return (status.startswith(("ok delete ", "err delete ")) or " delete app=" in status) and f"app={app_id}" in status


def staging_clear_matches(status: str) -> bool:
    return status.startswith(("ok staging_clear ", "err staging_clear ")) or " staging_clear removed=" in status


def flow_status_matches(status: str) -> bool:
    return status.startswith("ok flow api=") or "[vb_runtime][flow] total=" in status


def flow_clear_matches(status: str) -> bool:
    value = latest_line(
        status,
        lambda line: line.startswith(("ok flow_clear", "err flow_clear")) or line == "cleared",
    )
    return value.startswith(("ok flow_clear", "err flow_clear")) or value == "cleared"


def flow_send_matches(status: str, channel: str, sequence: int) -> bool:
    value = normalize_flow_send_text(status)
    return (
        value.startswith(("ok flow_send ", "err flow_send ", "recv "))
        and f"channel={channel}" in value
        and f"seq={sequence}" in value
    )


def strip_log_prefix(value: str) -> str:
    text = value.strip()
    for marker in ("[vb_runtime][flow] ", "[vb_runtime][voice] ", "[vb_runtime] "):
        index = text.find(marker)
        if index >= 0:
            return text[index + len(marker):].strip()
    for prompt in ("msh />", "msh >"):
        index = text.rfind(prompt)
        if index >= 0:
            text = text[index + len(prompt):].strip()
    if "] " in text:
        return text.rsplit("] ", 1)[1].strip()
    return text


def latest_line(text: str, matcher: Callable[[str], bool]) -> str:
    for line in reversed(text.splitlines()):
        value = strip_log_prefix(line)
        if matcher(value):
            return value
    value = strip_log_prefix(text)
    if matcher(value):
        return value
    return text.strip()


def normalize_voice_status_text(text: str) -> str:
    return latest_line(text, lambda value: value.startswith("ok voice api="))


def normalize_voice_start_text(text: str) -> str:
    return latest_line(
        text,
        lambda value: value.startswith(("ok voice_start ", "err voice_start ", "ok voice api=")),
    )


def normalize_voice_read_text(text: str) -> str:
    return latest_line(text, lambda value: value.startswith(("ok voice_data ", "err voice_read ")))


def normalize_voice_stop_text(text: str) -> str:
    return latest_line(text, lambda value: value.startswith(("ok voice_stop ", "err voice_stop ")))


def normalize_flow_send_text(text: str) -> str:
    return latest_line(
        text,
        lambda value: value.startswith(("ok flow_send ", "err flow_send ", "recv ")),
    )


def voice_status_matches(status: str, expected_seq: int | None = None) -> bool:
    value = normalize_voice_status_text(status)
    if not value.startswith("ok voice api="):
        return False
    if expected_seq is not None and f"seq={expected_seq}" not in value:
        return False
    return True


def voice_start_matches(status: str, expected_seq: int | None, duration_ms: int) -> bool:
    value = normalize_voice_start_text(status)
    if value.startswith("err voice_start "):
        return True
    if value.startswith("ok voice_start "):
        if expected_seq is not None and f"seq={expected_seq}" not in value:
            return False
        if f"ms={duration_ms}" not in value and f"requested_ms={duration_ms}" not in value:
            return False
        return True
    if value.startswith("ok voice api="):
        if expected_seq is not None and f"seq={expected_seq}" not in value:
            return False
        return True
    return False


def voice_read_matches(status: str, expected_seq: int, offset: int) -> bool:
    value = normalize_voice_read_text(status)
    if value.startswith("ok voice_data "):
        return f"seq={expected_seq}" in value and f"offset={offset}" in value
    if value.startswith("err voice_read "):
        return f"offset={offset}" in value
    return False


def voice_stop_matches(status: str) -> bool:
    return normalize_voice_stop_text(status).startswith(("ok voice_stop ", "err voice_stop "))


def voice_clear_matches(status: str) -> bool:
    value = latest_line(
        text=status,
        matcher=lambda line: line.startswith(("ok voice_clear", "err voice_clear")) or line == "cleared",
    )
    return value.startswith(("ok voice_clear", "err voice_clear")) or value == "cleared"


@dataclass
class VoiceReadChunk:
    sequence: int
    offset: int
    data: bytes
    status: str
    values: dict[str, str]


def safe_int(value: str | None, default: int = 0) -> int:
    try:
        return int(value or default)
    except (TypeError, ValueError):
        return default


def parse_voice_read_chunk(status: str, expected_seq: int, expected_offset: int) -> VoiceReadChunk:
    value = normalize_voice_read_text(status)
    if value.startswith("err " ):
        transport_fail(f"voice_read failed: {value}")
    if not value.startswith("ok voice_data " ):
        transport_fail(f"voice_read response missing data: {status[-2000:]}")
    values = parse_key_values(value)
    sequence = safe_int(values.get("seq"), -1)
    offset = safe_int(values.get("offset"), -1)
    expected_bytes = safe_int(values.get("bytes"), 0)
    if sequence != expected_seq:
        transport_fail(f"voice_read sequence mismatch: expected {expected_seq}, got {sequence}")
    if offset != expected_offset:
        transport_fail(f"voice_read offset mismatch: expected {expected_offset}, got {offset}")
    hex_payload = values.get("hex")
    if hex_payload is None:
        transport_fail(f"voice_read missing hex payload: {value}")
    try:
        payload = bytes.fromhex(hex_payload)
    except ValueError as exc:
        transport_fail(f"voice_read invalid hex payload: {exc}")
    if len(payload) != expected_bytes:
        transport_fail(f"voice_read length mismatch: expected {expected_bytes}, got {len(payload)}")
    return VoiceReadChunk(sequence=sequence, offset=offset, data=payload, status=value, values=values)


def normalize_flow_send_ack(status: str, channel: str, sequence: int, expected_bytes: int) -> str:
    value = normalize_flow_send_text(status)
    if value.startswith("err " ):
        transport_fail(f"failed to send flow frame: {value}")
    if value.startswith("ok flow_send " ):
        values = parse_key_values(value)
        if values.get("channel") != channel or values.get("seq") != str(sequence):
            transport_fail(f"flow send ack mismatch: {value}")
        actual_bytes = safe_int(values.get("bytes"), -1)
        if actual_bytes != expected_bytes:
            transport_fail(f"flow send byte count mismatch: expected {expected_bytes}, got {actual_bytes}")
        return value
    if value.startswith("recv " ):
        values = parse_key_values(value)
        if values.get("channel") != channel:
            transport_fail(f"flow send channel mismatch: {value}")
        if values.get("seq") != str(sequence):
            transport_fail(f"flow send sequence mismatch: {value}")
        actual_bytes = safe_int(values.get("bytes"), -1)
        if actual_bytes != expected_bytes:
            transport_fail(f"flow send byte count mismatch: expected {expected_bytes}, got {actual_bytes}")
        total = values.get("total", "0")
        return f"ok flow_send channel={channel} seq={sequence} bytes={actual_bytes} total={total}"
    transport_fail(f"flow send ack missing: {status[-2000:]}")


def validate_flow_roundtrip_output(
    output: str,
    channel: str,
    sequence: int,
    payload: str,
    expected_total: int = 1,
    expected_retained: int | None = None,
) -> str:
    expected_bytes = len(payload.encode("utf-8"))
    normalize_flow_send_ack(output, channel, sequence, expected_bytes)
    retained = expected_total if expected_retained is None else expected_retained

    status = latest_line(output, lambda value: value.startswith("ok flow api="))
    if status.startswith("ok flow api="):
        values = parse_key_values(status)
        checks = {
            "total": str(expected_total),
            "retained": str(retained),
            "seq": str(sequence),
            "channel": channel,
            "bytes": str(expected_bytes),
        }
        for key, expected in checks.items():
            if values.get(key) != expected:
                transport_fail(
                    f"flow status {key} mismatch: expected {expected}, "
                    f"got {values.get(key)!r}; status={status}"
                )
        return status

    total_line = latest_line(output, lambda value: value.startswith("total="))
    if not total_line.startswith("total="):
        transport_fail(f"flow status missing total line: {output[-2000:]}")
    totals = parse_key_values(total_line)
    if totals.get("total") != str(expected_total):
        transport_fail(
            f"flow status total mismatch: expected {expected_total}, "
            f"got {totals.get('total')!r}; status={total_line}"
        )
    if totals.get("retained") != str(retained):
        transport_fail(
            f"flow status retained mismatch: expected {retained}, "
            f"got {totals.get('retained')!r}; status={total_line}"
        )

    item_line = latest_line(
        output,
        lambda value: value.startswith("item=")
        and f"seq={sequence}" in value
        and f"channel={channel}" in value,
    )
    if item_line.startswith("item="):
        values = parse_key_values(item_line)
        if values.get("bytes") != str(expected_bytes):
            transport_fail(
                f"flow status bytes mismatch: expected {expected_bytes}, "
                f"got {values.get('bytes')!r}; status={item_line}"
            )
        if "payload=" in item_line and payload not in item_line:
            transport_fail(f"flow status payload mismatch: expected payload text not found; status={item_line}")
        return item_line

    return normalize_flow_send_text(output)


def parse_key_values(text: str) -> dict[str, str]:
    values: dict[str, str] = {}
    for token in text.split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        values[key] = value
    return values


def load_ble_cache(path: Path) -> dict[str, str]:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        return {}


def save_ble_cache(path: Path, data: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def ensure_bleak():
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError as exc:
        transport_fail(
            "Python package 'bleak' is required for BLE transport.\n"
            "Install it in a project-local virtual environment, for example:\n"
            "  python3 -m venv .venv-ble\n"
            "  .venv-ble/bin/python -m pip install bleak\n"
            f"Import error: {exc}",
            2,
        )
    return BleakClient, BleakScanner


@dataclass(frozen=True)
class BLEScanResult:
    address: str
    name: str


async def scan_ble_devices(name: str, timeout: float) -> list[BLEScanResult]:
    _, BleakScanner = ensure_bleak()
    devices = await BleakScanner.discover(timeout=timeout, return_adv=False)
    return [
        BLEScanResult(
            address=str(getattr(device, "address", "") or ""),
            name=str(getattr(device, "name", "") or ""),
        )
        for device in devices
    ]


@dataclass
class SerialTransportOptions:
    port: str
    baud: int = 1_000_000
    command_wait: float = 0.22
    final_wait: float = 2.0
    ready_timeout: float = 24.0
    echo: bool = False


class SerialTransport:
    def __init__(self, options: SerialTransportOptions) -> None:
        if serial is None:
            transport_fail(
                "Python package 'pyserial' is required for serial transport. "
                "Install it with: python3 -m pip install pyserial"
            )
        self.options = options
        self._serial = None

    @staticmethod
    def status_has_runtime_ready(text: str) -> bool:
        if "[vb_runtime] running=1" in text:
            return True
        return (
            "[vb_runtime] api=vibeboard-huangshan-runtime/v1" in text
            and "[vb_runtime] fs=ready" in text
            and "[vb_runtime] app_manager=" in text
        )

    def __enter__(self) -> "SerialTransport":
        self.connect()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def connect(self) -> None:
        ser = serial.Serial(
            port=None,
            baudrate=self.options.baud,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0.05,
            write_timeout=2,
        )
        ser.dtr = False
        ser.rts = False
        ser.port = self.options.port
        ser.open()
        ser.dtr = False
        ser.rts = False
        self._serial = ser
        self.wait_until_ready()

    def close(self) -> None:
        if self._serial is not None:
            self._serial.close()
        self._serial = None

    def read_text(self, seconds: float) -> str:
        end = time.time() + seconds
        chunks = bytearray()
        while time.time() < end:
            data = self._serial.read(4096)
            if data:
                chunks.extend(data)
        return bytes((b & 0x7F) for b in chunks).decode("utf-8", "replace")

    def write_line_slow(self, line: str) -> None:
        data = line.encode("utf-8") + b"\r\n"
        for index in range(0, len(data), 24):
            self._serial.write(data[index:index + 24])
            self._serial.flush()
            time.sleep(0.012)

    def command(self, command: str, *, wait: float | None = None, echo: bool | None = None) -> str:
        do_echo = self.options.echo if echo is None else echo
        command_wait = self.options.command_wait if wait is None else wait
        if do_echo:
            print(f"> {command}")
        self.write_line_slow(command)
        text = self.read_text(command_wait)
        if do_echo and text.strip():
            print(text.rstrip())
        return text

    def wait_for_runtime(self, seconds: float) -> str:
        start = time.time()
        seen = ""
        while time.time() - start < seconds:
            seen += self.read_text(0.25)
            self._serial.write(b"\r\nvb_runtime_status\r\n")
            self._serial.flush()
            status = self.read_text(0.5)
            seen += status
            if self.status_has_runtime_ready(status):
                return seen
        return seen

    def wait_until_ready(self) -> None:
        ready = self.wait_for_runtime(self.options.ready_timeout)
        if not self.status_has_runtime_ready(ready):
            transport_fail("Runtime did not become ready on serial. Last output:\n" + ready[-2000:])
        self.read_text(0.35)

    def reset_and_capture_boot(self, seconds: float) -> str:
        if self._serial is None:
            transport_fail("Serial transport is not connected")
        self._serial.reset_input_buffer()
        self._serial.reset_output_buffer()
        self._serial.rts = True
        time.sleep(0.25)
        self._serial.rts = False
        time.sleep(0.25)
        return self.read_text(seconds)

    def read_matching(self, command: str, matcher: Callable[[str], bool], *, timeout: float = 4.0, wait: float | None = None) -> str:
        command_wait = self.options.command_wait if wait is None else wait
        started = time.time()
        text = self.command(command, wait=command_wait)
        while True:
            if matcher(text):
                return text
            if time.time() - started >= timeout:
                transport_fail("Did not receive expected serial response. Last output:\n" + text[-2000:])
            chunk = self.read_text(min(command_wait, 0.25))
            if chunk:
                text += chunk

    def read_json_inline(self, command: str, expected_api: str | None = None, *, timeout: float = 4.0, wait: float | None = None) -> str:
        command_wait = self.options.final_wait if wait is None else wait
        started = time.time()
        output = self.command(command, wait=command_wait)
        while True:
            try:
                return extract_json_line(output, expected_api)
            except JSONResponseTruncated as exc:
                transport_fail(f"Runtime JSON response for {command!r} was truncated and has no safe json_read fallback: {exc}")
            except JSONResponsePending:
                if time.time() - started >= timeout:
                    transport_fail("Did not receive JSON response. Last output:\n" + output[-2000:])
                chunk = self.read_text(min(command_wait, 0.25))
                if chunk:
                    output += chunk

    def read_json(self, command: str, kind: str, expected_api: str | None = None, *, timeout: float = 4.0, wait: float | None = None) -> str:
        command_wait = self.options.final_wait if wait is None else wait
        started = time.time()
        output = self.command(command, wait=command_wait)
        while True:
            try:
                return extract_json_line(output, expected_api)
            except JSONResponseTruncated:
                break
            except JSONResponsePending:
                if time.time() - started >= timeout:
                    break
                chunk = self.read_text(min(command_wait, 0.25))
                if chunk:
                    output += chunk

        buffer = bytearray()
        offset = 0
        total = None
        fallback_deadline = time.time() + max(timeout, 4.0)
        while True:
            chunk_output = self.command(f"json_read {kind} {offset} {SERIAL_JSON_CHUNK_BYTES}", wait=command_wait)
            chunk = extract_json_chunk(chunk_output, kind)
            if chunk is None:
                if time.time() >= fallback_deadline:
                    transport_fail("Did not receive JSON response. Last output:\n" + (output + chunk_output)[-2000:])
                output += chunk_output
                continue
            chunk_offset, chunk_total, payload = chunk
            if chunk_offset != offset:
                transport_fail(f"Unexpected serial json_read offset for {kind}: expected {offset}, got {chunk_offset}")
            if total is None:
                total = chunk_total
                buffer = bytearray(total)
            elif total != chunk_total:
                transport_fail(f"Serial json_read total changed for {kind}: {total} -> {chunk_total}")
            end_offset = chunk_offset + len(payload)
            if end_offset > len(buffer):
                transport_fail(f"Serial json_read overflow for {kind}: {end_offset}>{len(buffer)}")
            if len(payload) == 0 and chunk_total > chunk_offset:
                transport_fail(f"Serial json_read returned empty chunk for {kind} at offset {chunk_offset} before total {chunk_total}")
            buffer[chunk_offset:end_offset] = payload
            offset = end_offset
            if total is not None and offset >= total:
                return extract_json_line(buffer.decode("utf-8", "replace"), expected_api)

    def status(self) -> str:
        return self.read_matching(
            "vb_runtime_status",
            lambda text: self.status_has_runtime_ready(text) and ("[vb_runtime] active=" in text or "[vb_runtime] active app:" in text),
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def capabilities(self) -> str:
        return self.read_json("vb_runtime_capabilities", "capabilities", CAPABILITIES_API)

    def sensors(self) -> str:
        return self.read_json("vb_runtime_sensors", "sensors", SENSORS_API, timeout=max(12.0, self.options.final_wait + 4.0), wait=max(12.0, self.options.final_wait))

    def power(self) -> str:
        return self.read_json("vb_runtime_power", "power", POWER_API)

    def display(self, brightness: int | None = None) -> str:
        command = "vb_runtime_display" if brightness is None else f"vb_runtime_display {brightness}"
        return self.read_json(command, "display", DISPLAY_API)

    def gpio(self) -> str:
        return self.read_json("vb_runtime_gpio", "gpio", GPIO_API)

    def touch(self) -> str:
        return self.read_json("vb_runtime_touch", "touch", TOUCH_API)

    def rgb(self, color: str | None = None) -> str:
        command = "vb_runtime_rgb" if color is None else f"vb_runtime_rgb {color}"
        return self.read_json(command, "rgb", RGB_API)

    def voice(self) -> str:
        return self.read_json("vb_runtime_voice", "voice", VOICE_API)

    def audio(self) -> str:
        return self.read_json("vb_runtime_audio status", "audio", AUDIO_API)

    def audio_play(self, app_id: str, path: str) -> str:
        _validate_audio_target(app_id, path)
        return self.read_json(f"vb_runtime_audio play /sdcard/apps/{app_id}/{path}", "audio", AUDIO_API)

    def audio_stop(self) -> str:
        return self.read_json("vb_runtime_audio stop", "audio", AUDIO_API)

    def audio_volume(self, volume: int) -> str:
        _validate_audio_volume(volume)
        return self.read_json(f"vb_runtime_audio volume {volume}", "audio", AUDIO_API)

    def voice_status(self, expected_seq: int | None = None) -> tuple[str, dict[str, str]]:
        text = self.read_matching(
            "vb_runtime_voice_status",
            lambda status: voice_status_matches(status, expected_seq),
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.command_wait, 0.15),
        )
        status = normalize_voice_status_text(text)
        return status, parse_key_values(status)

    def voice_start(self, duration_ms: int, expected_seq: int | None = None) -> tuple[str, dict[str, str]]:
        text = self.read_matching(
            f"vb_runtime_voice_start {duration_ms}",
            lambda status: voice_start_matches(status, expected_seq, duration_ms),
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.command_wait, 0.2),
        )
        status = normalize_voice_start_text(text)
        if status.startswith("err " ):
            transport_fail(f"voice_start failed: {status}")
        return status, parse_key_values(status)

    def voice_read(self, sequence: int, offset: int, max_bytes: int) -> VoiceReadChunk:
        if max_bytes <= 0:
            transport_fail(f"voice_read max_bytes must be positive, got {max_bytes}")
        text = self.read_matching(
            f"vb_runtime_voice_read {offset} {max_bytes}",
            lambda status: voice_read_matches(status, sequence, offset),
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.command_wait, 0.15),
        )
        return parse_voice_read_chunk(text, sequence, offset)

    def voice_stop(self) -> str:
        return self.read_matching(
            "vb_runtime_voice_stop",
            voice_stop_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.command_wait, 0.15),
        )

    def voice_clear(self) -> str:
        return self.read_matching(
            "vb_runtime_voice_clear",
            voice_clear_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.command_wait, 0.15),
        )

    def capture_voice(
        self,
        duration_ms: int,
        *,
        chunk_bytes: int = VOICE_CHUNK_BYTES,
        ready_timeout: float = 8.0,
        poll_interval: float = 0.25,
        progress: Callable[[int, int], None] | None = None,
    ) -> tuple[bytes, dict[str, str]]:
        _, pre_info = self.voice_status()
        if pre_info.get("built") == "0":
            transport_fail("board firmware reports voice capture built=0; rebuild with CONFIG_AUDIO=y and Audio Manager support")
        expected_seq = safe_int(pre_info.get("seq"), 0) + 1
        self.voice_start(duration_ms, expected_seq)

        deadline = time.time() + max(ready_timeout, duration_ms / 1000.0 + 2.0)
        last_status = ""
        info: dict[str, str] = {}
        while time.time() < deadline:
            status, info = self.voice_status(expected_seq)
            last_status = status
            if info.get("built") == "0":
                transport_fail("board firmware reports voice capture built=0; rebuild with CONFIG_AUDIO=y and Audio Manager support")
            if info.get("recording") == "0" and info.get("ready") == "1":
                break
            if info.get("recording") == "0" and info.get("err") not in (None, "0", "1"):
                transport_fail(f"voice capture failed: {status}")
            time.sleep(poll_interval)
        else:
            transport_fail(f"voice capture did not become ready before timeout: {last_status}")

        total = safe_int(info.get("bytes"), 0)
        if total <= 0:
            transport_fail(f"board returned no voice data: {last_status}")

        pcm = bytearray()
        offset = 0
        read_size = max(16, min(chunk_bytes, VOICE_CHUNK_BYTES))
        try:
            while offset < total:
                chunk = self.voice_read(expected_seq, offset, min(read_size, total - offset))
                if len(chunk.data) == 0:
                    transport_fail(f"voice_read returned empty chunk before complete: offset={offset} total={total}")
                pcm.extend(chunk.data)
                offset += len(chunk.data)
                if progress:
                    progress(offset, total)
        finally:
            self.voice_clear()
        if len(pcm) != total:
            transport_fail(f"voice capture length mismatch: expected {total}, got {len(pcm)}")
        return bytes(pcm), info

    def send_voice_reply(
        self,
        reply: str,
        *,
        channel: str = "pc.voice",
        sequence: int | None = None,
    ) -> tuple[str, int]:
        reply_sequence = sequence if sequence is not None else int(time.time()) & 0xFFFFFFFF
        status = self.flow_send(channel, reply_sequence, reply)
        ack = normalize_flow_send_ack(status, channel, reply_sequence, len(reply.encode("utf-8")))
        return ack, reply_sequence

    def flow_status(self) -> str:
        return self.read_matching(
            "vb_runtime_flow_status",
            flow_status_matches,
            timeout=max(4.0, self.options.final_wait + 2.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def flow_clear(self) -> str:
        return self.read_matching(
            "vb_runtime_flow_clear",
            flow_clear_matches,
            timeout=max(4.0, self.options.final_wait + 2.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def flow_send(self, channel: str, sequence: int, text: str) -> str:
        hex_payload = text.encode("utf-8").hex() if text else "-"
        return self.read_matching(
            f"vb_runtime_flow_send {channel} {sequence} {hex_payload}",
            lambda current: flow_send_matches(current, channel, sequence),
            timeout=max(4.0, self.options.final_wait + 2.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def app_status(self) -> str:
        return self.read_json("vb_runtime_app", "app", APP_MANAGER_API)

    def app_page(self, offset: int = 0, limit: int = SERIAL_APP_PAGE_LIMIT) -> str:
        return self.read_json_inline(
            f"vb_runtime_apps_page {offset} {limit}",
            APP_MANAGER_API,
            timeout=max(4.0, self.options.final_wait + 3.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def apps(self) -> str:
        try:
            pages: list[str] = []
            offset = 0
            limit = SERIAL_APP_PAGE_LIMIT
            while True:
                page = self.app_page(offset, limit)
                pages.append(page)
                data = json.loads(page)
                count = int(data.get("count", 0) or 0)
                included = int(data.get("included", 0) or 0)
                offset += included
                if included <= 0 or offset >= count:
                    break
            return combine_app_pages(pages)
        except Exception:
            return self.read_json("vb_runtime_apps", "apps", APP_MANAGER_API, timeout=max(6.0, self.options.final_wait + 4.0), wait=max(self.options.final_wait, 0.6))

    def launch_app(self, app_id: str) -> str:
        return self.read_matching(
            f"vb_runtime_launch {app_id}",
            lambda text: app_launch_matches(text, app_id),
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def stop_app(self) -> str:
        return self.read_matching(
            "vb_runtime_stop",
            app_stop_matches,
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def delete_app(self, app_id: str) -> str:
        return self.read_matching(
            f"vb_runtime_delete {app_id}",
            lambda text: app_delete_matches(text, app_id),
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def staging_clear(self) -> str:
        return self.read_matching(
            "vb_runtime_staging_clear",
            staging_clear_matches,
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.final_wait, 0.4),
        )

    def abort_install(self, app_id: str) -> str:
        abort_command = f"vb_runtime_install_abort {app_id}"
        response = self.read_matching(
            abort_command,
            lambda text, command=abort_command: install_ack_matches(text, command),
            timeout=max(6.0, self.options.final_wait + 4.0),
            wait=max(self.options.command_wait, 0.2),
        )
        if "err install_abort" in response or " rc=-" in response:
            transport_fail("Serial install abort failed. Last output:\n" + response[-2000:])
        return response

    def install_package(
        self,
        package_id: str,
        files: dict[str, bytes],
        *,
        chunk_bytes: int = INSTALL_CHUNK_BYTES,
        progress: Callable[[str, int, int], None] | None = None,
        commit: bool = True,
    ) -> str:
        commands = build_install_commands(package_id, files, max(16, min(chunk_bytes, MAX_INSTALL_CHUNK_BYTES)))
        if not commit:
            commands = commands[:-1]
        output = ""
        install_started = False
        try:
            for index, command in enumerate(commands, start=1):
                if command.startswith("vb_runtime_install_begin "):
                    install_started = True
                if progress:
                    progress(command, index, len(commands))
                response = self.read_matching(
                    command,
                    lambda text, command=command: install_ack_matches(text, command),
                    timeout=max(6.0, self.options.final_wait + 4.0),
                    wait=self.options.command_wait,
                )
                output += response
                if "err install_" in response or " rc=-" in response:
                    transport_fail("Serial install command failed. Last output:\n" + response[-2000:])
            if not commit:
                return output
            status_output = self.command("vb_runtime_status", wait=self.options.final_wait)
            output += status_output
            if (
                f"active={package_id}" not in output
                and f"active app: {package_id}" not in output
            ):
                transport_fail("Serial install did not confirm active app. Last output:\n" + output[-2000:])
            return output
        except BaseException:
            if install_started:
                try:
                    self.abort_install(package_id)
                except BaseException as abort_exc:
                    print(f"warning: install abort failed for {package_id}: {abort_exc}")
            raise


@dataclass
class BLETransportOptions:
    name: str = DEFAULT_DEVICE_NAME
    address: str | None = None
    cache: Path = DEFAULT_BLE_CACHE_PATH
    no_cache: bool = False
    scan_timeout: float = 12.0
    connect_timeout: float = 12.0
    connect_settle: float = 0.35
    service_timeout: float = 6.0
    response_wait: float = 0.12
    final_wait: float = 0.8
    disconnect_pause: float = 0.8
    write_with_response: bool = True
    subscribe_status_notifications: bool = True
    echo: bool = False


class BLETransport:
    def __init__(self, options: BLETransportOptions | None = None) -> None:
        self.options = options or BLETransportOptions()
        self._client_context = None
        self._client = None
        self._connection_label = ""
        self._status_notify_started = False
        self._voice_notify_started = False

    async def __aenter__(self) -> "BLETransport":
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        await self.close()

    async def connect(self) -> None:
        BleakClient, _ = ensure_bleak()
        device = self._cached_device() or await self._scan_device()
        self._connection_label = self._describe_device(device)
        try:
            self._client_context = BleakClient(device, timeout=self.options.connect_timeout, services=[SERVICE_UUID])
            self._client = await self._client_context.__aenter__()
        except Exception as exc:
            if self.options.address:
                transport_fail(f"Pinned BLE peripheral {self.options.address} failed: {exc}")
            if self.options.no_cache or not isinstance(device, str):
                raise
            if self.options.echo:
                print(f"cached BLE peripheral failed: {exc}; scanning again...")
            device = await self._scan_device()
            self._connection_label = self._describe_device(device)
            self._client_context = BleakClient(device, timeout=self.options.connect_timeout, services=[SERVICE_UUID])
            self._client = await self._client_context.__aenter__()
        if not self._client.is_connected:
            transport_fail("BLE connection failed")
        if self.options.connect_settle > 0:
            await asyncio.sleep(self.options.connect_settle)
        await self._wait_for_services()
        if self.options.subscribe_status_notifications:
            try:
                await self._client.start_notify(STATUS_UUID, lambda _sender, _data: None)
                self._status_notify_started = True
            except Exception:
                pass

    async def close(self) -> None:
        if self._client is not None:
            if self._voice_notify_started:
                try:
                    await self._client.stop_notify(VOICE_STREAM_UUID)
                except Exception:
                    pass
                self._voice_notify_started = False
            if self._status_notify_started:
                try:
                    await self._client.stop_notify(STATUS_UUID)
                except Exception:
                    pass
                self._status_notify_started = False
        if self.options.disconnect_pause > 0:
            await asyncio.sleep(self.options.disconnect_pause)
        if self._client_context is not None:
            await self._client_context.__aexit__(None, None, None)
        self._client_context = None
        self._client = None

    @property
    def connection_label(self) -> str:
        return self._connection_label

    @staticmethod
    def _describe_device(device) -> str:
        if isinstance(device, str):
            return device
        return f"{getattr(device, 'address', '')} {getattr(device, 'name', '')}".strip()

    def _cached_device(self) -> str | None:
        if self.options.address:
            return self.options.address
        if self.options.no_cache:
            return None
        cache = load_ble_cache(self.options.cache)
        cached_name = cache.get("name")
        cached_address = cache.get("address")
        if not cached_address:
            return None
        if cached_name != self.options.name:
            if self.options.echo:
                print(
                    f"ignoring cached BLE peripheral for {cached_name or 'unknown'}; "
                    f"requested {self.options.name!r}"
                )
            return None
        if cached_address and self.options.echo:
            print(f"reusing cached BLE peripheral: {cached_address}")
        return cached_address

    async def _scan_device(self):
        _, BleakScanner = ensure_bleak()
        if self.options.echo:
            print(f"scanning for BLE device named {self.options.name!r}...")
        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: (d.name or adv.local_name or "") == self.options.name,
            timeout=self.options.scan_timeout,
        )
        if not device:
            transport_fail(f"Could not find BLE device named {self.options.name!r}")
        save_ble_cache(self.options.cache, {"name": self.options.name, "address": getattr(device, "address", "")})
        return device

    async def _services_ready(self) -> bool:
        try:
            services = self._client.services
            return bool(
                services.get_service(SERVICE_UUID)
                and services.get_characteristic(COMMAND_UUID)
                and services.get_characteristic(STATUS_UUID)
            )
        except Exception:
            return False

    async def _wait_for_services(self) -> None:
        deadline = time.monotonic() + self.options.service_timeout
        while time.monotonic() < deadline:
            if await self._services_ready():
                return
            await asyncio.sleep(0.15)
        transport_fail("BLE service discovery did not complete")

    async def read_status(self) -> str:
        try:
            return decode_status(await self._client.read_gatt_char(STATUS_UUID))
        except Exception as exc:
            return f"read_status_failed: {exc}"

    async def start_voice_stream(self, callback: Callable[[bytes], None]) -> None:
        if self._voice_notify_started:
            return
        services = self._client.services
        if not services.get_characteristic(VOICE_STREAM_UUID):
            transport_fail("Board firmware has no BLE voice stream characteristic")

        def notify(_sender, data: bytearray) -> None:
            callback(bytes(data))

        await self._client.start_notify(VOICE_STREAM_UUID, notify)
        self._voice_notify_started = True

    async def read_status_retrying(self, response_wait: float) -> str:
        status = await self.read_status()
        if "Service Discovery has not been performed yet" not in status:
            return status

        deadline = time.monotonic() + max(1.5, response_wait * 6)
        while time.monotonic() < deadline:
            await asyncio.sleep(min(0.2, max(0.05, response_wait)))
            if not await self._services_ready():
                continue
            status = await self.read_status()
            if "Service Discovery has not been performed yet" not in status:
                return status
        return status

    async def _wait_for_services_retrying(self, response_wait: float) -> None:
        deadline = time.monotonic() + max(1.5, response_wait * 8)
        while time.monotonic() < deadline:
            if await self._services_ready():
                return
            await asyncio.sleep(min(0.2, max(0.05, response_wait)))
        transport_fail("BLE service discovery did not complete")

    async def command(self, command: str, *, response_wait: float | None = None, echo: bool | None = None) -> str:
        wait = self.options.response_wait if response_wait is None else response_wait
        do_echo = self.options.echo if echo is None else echo
        if do_echo:
            print(f"> {command}")
        payload = (command + "\n").encode("utf-8")
        last_error: Exception | None = None
        deadline = time.monotonic() + max(2.0, wait * 10)
        while time.monotonic() < deadline:
            try:
                await self._wait_for_services_retrying(wait)
                await self._client.write_gatt_char(
                    COMMAND_UUID,
                    payload,
                    response=self.options.write_with_response,
                )
                await asyncio.sleep(wait)
                status = await self.read_status_retrying(wait)
                if status_is_transport_failure(status):
                    transport_fail(status)
                if do_echo and status:
                    print(status)
                return status
            except Exception as exc:  # pragma: no cover - depends on platform BLE stack
                last_error = exc
                if "Service Discovery has not been performed yet" not in str(exc):
                    raise
                await asyncio.sleep(min(0.2, max(0.05, wait)))
        transport_fail(f"BLE command failed: {last_error}" if last_error else "BLE command timed out")

    async def read_matching(
        self,
        command: str,
        matcher: Callable[[str], bool],
        *,
        timeout: float = 4.0,
        response_wait: float | None = None,
    ) -> str:
        wait = response_wait or self.options.response_wait
        started = time.monotonic()
        status = await self.command(command, response_wait=wait)
        while True:
            if matcher(status):
                return status
            if time.monotonic() - started >= timeout:
                transport_fail("Did not receive expected BLE status response. Last output:\n" + status[-2000:])
            await asyncio.sleep(min(wait, 0.2))
            status = await self.read_status_retrying(wait)
            if status_is_transport_failure(status):
                transport_fail(status)

    async def read_json_inline(self, command: str, expected_api: str, *, timeout: float = 4.0) -> str:
        started = time.monotonic()
        status = await self.command(command, response_wait=max(self.options.response_wait, 0.25))
        merged = status
        while True:
            try:
                return extract_json_line(merged, expected_api)
            except JSONResponseTruncated as exc:
                transport_fail(f"Runtime JSON response for {command!r} was truncated and has no safe json_read fallback: {exc}")
            except JSONResponsePending:
                if time.monotonic() - started >= timeout:
                    transport_fail("Did not receive expected BLE JSON response. Last output:\n" + merged[-2000:])
                await asyncio.sleep(min(self.options.response_wait, 0.2))
                status = await self.read_status_retrying(self.options.response_wait)
                if status_is_transport_failure(status):
                    transport_fail(status)
                merged = merge_status_text(merged, status)

    async def read_json(self, command: str, kind: str, expected_api: str, *, timeout: float = 4.0) -> str:
        started = time.monotonic()
        status = await self.command(command, response_wait=max(self.options.response_wait, 0.25))
        merged = status
        while True:
            try:
                return extract_json_line(merged, expected_api)
            except JSONResponseTruncated:
                break
            except JSONResponsePending:
                if time.monotonic() - started >= timeout:
                    break
                await asyncio.sleep(min(self.options.response_wait, 0.2))
                status = await self.read_status_retrying(self.options.response_wait)
                if status_is_transport_failure(status):
                    transport_fail(status)
                merged = merge_status_text(merged, status)
        return await self.read_json_chunks(kind, expected_api, timeout=max(timeout, 4.0))

    async def read_json_chunks(self, kind: str, expected_api: str, *, timeout: float = 4.0) -> str:
        deadline = time.monotonic() + timeout
        buffer = bytearray()
        offset = 0
        total = None
        while True:
            status = await self.command(f"json_read {kind} {offset} {RUNTIME_DATA_CHUNK_BYTES}")
            chunk = extract_json_chunk(status, kind)
            if chunk is None:
                if time.monotonic() >= deadline:
                    transport_fail("Did not receive expected JSON response. Last output:\n" + status[-2000:])
                await asyncio.sleep(min(self.options.response_wait, 0.2))
                continue
            chunk_offset, chunk_total, payload = chunk
            if chunk_offset != offset:
                transport_fail(f"Unexpected BLE json_read offset for {kind}: expected {offset}, got {chunk_offset}")
            if total is None:
                total = chunk_total
                buffer = bytearray(total)
            elif total != chunk_total:
                transport_fail(f"BLE json_read total changed for {kind}: {total} -> {chunk_total}")
            end_offset = chunk_offset + len(payload)
            if end_offset > len(buffer):
                transport_fail(f"BLE json_read overflow for {kind}: {end_offset}>{len(buffer)}")
            if len(payload) == 0 and chunk_total > chunk_offset:
                transport_fail(f"BLE json_read returned empty chunk for {kind} at offset {chunk_offset} before total {chunk_total}")
            buffer[chunk_offset:end_offset] = payload
            offset = end_offset
            if total is not None and offset >= total:
                return extract_json_line(buffer.decode("utf-8", "replace"), expected_api)
            if time.monotonic() >= deadline:
                transport_fail("Did not receive expected JSON response. Last output:\n" + buffer.decode("utf-8", "replace")[-2000:])

    async def status(self) -> str:
        return await self.command("status")

    async def verify_connection(self, *, timeout: float | None = None) -> str:
        return await self.read_matching(
            "status",
            lambda status: status.startswith("ok status "),
            timeout=timeout or max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.12),
        )

    async def hold_connection(self, seconds: float, *, keepalive_period: float = 5.0) -> None:
        if seconds == 0:
            return
        started = time.monotonic()
        deadline = None if seconds < 0 else started + seconds
        while deadline is None or time.monotonic() < deadline:
            sleep_for = keepalive_period
            if deadline is not None:
                sleep_for = min(sleep_for, max(0.0, deadline - time.monotonic()))
            if sleep_for > 0:
                await asyncio.sleep(sleep_for)
            if deadline is not None and time.monotonic() >= deadline:
                break
            await self.verify_connection()

    async def capabilities(self) -> str:
        return await self.read_json("capabilities", "capabilities", CAPABILITIES_API)

    async def sensors(self) -> str:
        return await self.read_json("sensors", "sensors", SENSORS_API, timeout=max(12.0, self.options.final_wait + 4.0))

    async def power(self) -> str:
        return await self.read_json("power", "power", POWER_API)

    async def display(self, brightness: int | None = None) -> str:
        command = "display" if brightness is None else f"display {brightness}"
        return await self.read_json(command, "display", DISPLAY_API)

    async def gpio(self) -> str:
        return await self.read_json("gpio", "gpio", GPIO_API)

    async def touch(self) -> str:
        return await self.read_json("touch", "touch", TOUCH_API)

    async def rgb(self, color: str | None = None) -> str:
        command = "rgb" if color is None else f"rgb {color}"
        return await self.read_json(command, "rgb", RGB_API)

    async def voice(self) -> str:
        return await self.read_json("voice", "voice", VOICE_API)

    async def audio(self) -> str:
        return await self.read_json("playback", "audio", AUDIO_API)

    async def audio_play(self, app_id: str, path: str) -> str:
        _validate_audio_target(app_id, path)
        return await self.read_json(f"playback_play {app_id} {path}", "audio", AUDIO_API)

    async def audio_stop(self) -> str:
        return await self.read_json("playback_stop", "audio", AUDIO_API)

    async def audio_volume(self, volume: int) -> str:
        _validate_audio_volume(volume)
        return await self.read_json(f"playback_volume {volume}", "audio", AUDIO_API)

    async def voice_status(self, expected_seq: int | None = None) -> tuple[str, dict[str, str]]:
        status = await self.read_matching(
            "voice_status",
            lambda current: voice_status_matches(current, expected_seq),
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.03),
        )
        status = normalize_voice_status_text(status)
        return status, parse_key_values(status)

    async def voice_start(self, duration_ms: int, expected_seq: int | None = None) -> tuple[str, dict[str, str]]:
        status = await self.read_matching(
            f"voice_start {duration_ms}",
            lambda current: voice_start_matches(current, expected_seq, duration_ms),
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.2),
        )
        status = normalize_voice_start_text(status)
        if status.startswith("err " ):
            transport_fail(f"voice_start failed: {status}")
        return status, parse_key_values(status)

    async def voice_read(self, sequence: int, offset: int, max_bytes: int) -> VoiceReadChunk:
        if max_bytes <= 0:
            transport_fail(f"voice_read max_bytes must be positive, got {max_bytes}")
        status = await self.read_matching(
            f"voice_read {offset} {max_bytes}",
            lambda current: voice_read_matches(current, sequence, offset),
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.03),
        )
        return parse_voice_read_chunk(status, sequence, offset)

    async def voice_stop(self) -> str:
        return await self.read_matching(
            "voice_stop",
            voice_stop_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.15),
        )

    async def voice_clear(self) -> str:
        return await self.read_matching(
            "voice_clear",
            voice_clear_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.15),
        )

    async def capture_voice(
        self,
        duration_ms: int,
        *,
        chunk_bytes: int = VOICE_CHUNK_BYTES,
        ready_timeout: float = 8.0,
        poll_interval: float = 0.25,
        progress: Callable[[int, int], None] | None = None,
    ) -> tuple[bytes, dict[str, str]]:
        _, pre_info = await self.voice_status()
        if pre_info.get("built") == "0":
            transport_fail("board firmware reports voice capture built=0; rebuild with CONFIG_AUDIO=y and Audio Manager support")
        expected_seq = safe_int(pre_info.get("seq"), 0) + 1
        await self.voice_start(duration_ms, expected_seq)

        deadline = time.monotonic() + max(ready_timeout, duration_ms / 1000.0 + 2.0)
        last_status = ""
        info: dict[str, str] = {}
        while time.monotonic() < deadline:
            status, info = await self.voice_status(expected_seq)
            last_status = status
            if info.get("built") == "0":
                transport_fail("board firmware reports voice capture built=0; rebuild with CONFIG_AUDIO=y and Audio Manager support")
            if info.get("recording") == "0" and info.get("ready") == "1":
                break
            if info.get("recording") == "0" and info.get("err") not in (None, "0", "1"):
                transport_fail(f"voice capture failed: {status}")
            await asyncio.sleep(poll_interval)
        else:
            transport_fail(f"voice capture did not become ready before timeout: {last_status}")

        total = safe_int(info.get("bytes"), 0)
        if total <= 0:
            transport_fail(f"board returned no voice data: {last_status}")

        pcm = bytearray()
        offset = 0
        read_size = max(16, min(chunk_bytes, VOICE_CHUNK_BYTES))
        try:
            while offset < total:
                chunk = await self.voice_read(expected_seq, offset, min(read_size, total - offset))
                if len(chunk.data) == 0:
                    transport_fail(f"voice_read returned empty chunk before complete: offset={offset} total={total}")
                pcm.extend(chunk.data)
                offset += len(chunk.data)
                if progress:
                    progress(offset, total)
        finally:
            await self.voice_clear()
        if len(pcm) != total:
            transport_fail(f"voice capture length mismatch: expected {total}, got {len(pcm)}")
        return bytes(pcm), info

    async def send_voice_reply(
        self,
        reply: str,
        *,
        channel: str = "pc.voice",
        sequence: int | None = None,
    ) -> tuple[str, int]:
        reply_sequence = sequence if sequence is not None else int(time.time()) & 0xFFFFFFFF
        status = await self.flow_send(channel, reply_sequence, reply)
        ack = normalize_flow_send_ack(status, channel, reply_sequence, len(reply.encode("utf-8")))
        return ack, reply_sequence

    async def flow_status(self) -> str:
        return await self.read_matching(
            "flow_status",
            flow_status_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.12),
        )

    async def flow_clear(self) -> str:
        return await self.read_matching(
            "flow_clear",
            flow_clear_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.12),
        )

    async def flow_send(self, channel: str, sequence: int, text: str) -> str:
        hex_payload = text.encode("utf-8").hex() if text else "-"
        return await self.read_matching(
            f"flow_send {channel} {sequence} {hex_payload}",
            lambda current: flow_send_matches(current, channel, sequence),
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.12),
        )

    async def app_status(self) -> str:
        return await self.read_json("app", "app", APP_MANAGER_API)

    async def app_page(self, offset: int = 0, limit: int = BLE_APP_PAGE_LIMIT) -> str:
        return await self.read_json_inline(f"apps_page {offset} {limit}", APP_MANAGER_API, timeout=max(4.0, self.options.final_wait + 3.0))

    async def apps(self) -> str:
        try:
            pages: list[str] = []
            offset = 0
            limit = BLE_APP_PAGE_LIMIT
            while True:
                page = await self.app_page(offset, limit)
                pages.append(page)
                data = json.loads(page)
                count = int(data.get("count", 0) or 0)
                included = int(data.get("included", 0) or 0)
                offset += included
                if included <= 0 or offset >= count:
                    break
            return combine_app_pages(pages)
        except Exception:
            return await self.read_json("apps", "apps", APP_MANAGER_API, timeout=max(6.0, self.options.final_wait + 4.0))

    async def launch_app(self, app_id: str) -> str:
        return await self.read_matching(
            f"launch {app_id}",
            lambda status: app_launch_matches(status, app_id),
            timeout=max(4.0, self.options.final_wait + 3.0),
        )

    async def stop_app(self) -> str:
        return await self.read_matching("stop", app_stop_matches, timeout=max(4.0, self.options.final_wait + 3.0))

    async def delete_app(self, app_id: str) -> str:
        return await self.read_matching(
            f"delete {app_id}",
            lambda status: app_delete_matches(status, app_id),
            timeout=max(4.0, self.options.final_wait + 3.0),
        )

    async def staging_clear(self) -> str:
        return await self.read_matching(
            "staging_clear",
            staging_clear_matches,
            timeout=max(4.0, self.options.final_wait + 3.0),
        )

    async def abort_install(self, app_id: str) -> str:
        abort_command = f"vb_runtime_install_abort {app_id}"
        status = await self.read_matching(
            abort_command,
            lambda current, command=abort_command: install_ack_matches(current, command),
            timeout=max(4.0, self.options.final_wait + 3.0),
            response_wait=max(self.options.response_wait, 0.12),
        )
        if status.startswith("err ") or " rc=-" in status:
            transport_fail(f"BLE install abort failed: {status}")
        return status

    async def install_package(
        self,
        package_id: str,
        files: dict[str, bytes],
        *,
        chunk_bytes: int = INSTALL_CHUNK_BYTES,
        progress: Callable[[str, int, int], None] | None = None,
        commit: bool = True,
    ) -> str:
        commands = build_install_commands(package_id, files, max(16, min(chunk_bytes, MAX_INSTALL_CHUNK_BYTES)))
        if not commit:
            commands = commands[:-1]
        last_status = ""
        install_started = False
        try:
            for index, command in enumerate(commands, start=1):
                if command.startswith("vb_runtime_install_begin "):
                    install_started = True
                if progress:
                    progress(command, index, len(commands))
                last_status = await self.read_matching(
                    command,
                    lambda status, command=command: install_ack_matches(status, command),
                    timeout=max(4.0, self.options.final_wait + 3.0),
                    response_wait=max(self.options.response_wait, 0.12),
                )
                if last_status.startswith("err ") or " rc=-" in last_status:
                    transport_fail(f"BLE install command failed: {last_status}")
            if not commit:
                return last_status
            if f"active={package_id}" not in last_status:
                last_status = await self.read_matching(
                    "status",
                    lambda status: f"active={package_id}" in status,
                    timeout=max(4.0, self.options.final_wait + 3.0),
                    response_wait=max(self.options.final_wait, 0.25),
                )
            return last_status
        except BaseException:
            if install_started:
                try:
                    await self.abort_install(package_id)
                except BaseException as abort_exc:
                    print(f"warning: install abort failed for {package_id}: {abort_exc}")
            raise


def run_self_test() -> None:
    assert RUNTIME_DATA_CHUNK_BYTES == 160
    assert VOICE_CHUNK_BYTES > RUNTIME_DATA_CHUNK_BYTES
    assert 0 < SERIAL_MSH_VOICE_CHUNK_BYTES <= VOICE_CHUNK_BYTES
    assert INSTALL_CHUNK_BYTES == 48
    assert SERIAL_JSON_CHUNK_BYTES == SERIAL_MSH_VOICE_CHUNK_BYTES
    assert MAX_INSTALL_CHUNK_BYTES == 240
    assert SERIAL_APP_PAGE_LIMIT >= BLE_APP_PAGE_LIMIT > 0
    assert hasattr(BLETransport, "verify_connection")
    assert hasattr(BLETransport, "hold_connection")
    assert BLEScanResult(address="addr", name="VibeBoard").name == "VibeBoard"
    serial_commands = {
        "status": "vb_runtime_status",
        "capabilities": "vb_runtime_capabilities",
        "sensors": "vb_runtime_sensors",
        "power": "vb_runtime_power",
        "display": "vb_runtime_display",
        "gpio": "vb_runtime_gpio",
        "touch": "vb_runtime_touch",
        "rgb": "vb_runtime_rgb",
        "voice": "vb_runtime_voice",
        "audio": "vb_runtime_audio",
        "voice_status": "vb_runtime_voice_status",
        "voice_start": "vb_runtime_voice_start",
        "voice_read": "vb_runtime_voice_read",
        "voice_clear": "vb_runtime_voice_clear",
        "flow_status": "vb_runtime_flow_status",
        "flow_clear": "vb_runtime_flow_clear",
        "flow_send": "vb_runtime_flow_send",
        "app_status": "vb_runtime_app",
        "app_page": "vb_runtime_apps_page",
        "apps_fallback": "vb_runtime_apps",
        "launch": "vb_runtime_launch",
        "stop": "vb_runtime_stop",
        "delete": "vb_runtime_delete",
        "staging_clear": "vb_runtime_staging_clear",
    }
    ble_commands = {
        "status": "status",
        "capabilities": "capabilities",
        "sensors": "sensors",
        "power": "power",
        "display": "display",
        "gpio": "gpio",
        "touch": "touch",
        "rgb": "rgb",
        "voice": "voice",
        "audio": "playback",
        "voice_status": "voice_status",
        "voice_start": "voice_start",
        "voice_read": "voice_read",
        "voice_clear": "voice_clear",
        "flow_status": "flow_status",
        "flow_clear": "flow_clear",
        "flow_send": "flow_send",
        "app_status": "app",
        "app_page": "apps_page",
        "apps_fallback": "apps",
        "launch": "launch",
        "stop": "stop",
        "delete": "delete",
        "staging_clear": "staging_clear",
    }
    install_commands = (
        "vb_runtime_install_begin",
        "vb_runtime_install_file",
        "vb_runtime_install_abort",
        "vb_runtime_install_end",
    )
    assert serial_commands["flow_clear"] == "vb_runtime_flow_clear"
    assert ble_commands["flow_clear"] == "flow_clear"
    assert serial_commands["app_page"] == "vb_runtime_apps_page"
    assert ble_commands["app_page"] == "apps_page"
    assert all(command.startswith("vb_runtime_install_") for command in install_commands)
    assert set(serial_commands) == set(ble_commands)
    _validate_audio_target("audio_stage", "assets/chime.wav")
    _validate_audio_volume(15)
    _assert_raises(RuntimeTransportError, _validate_audio_target, "Bad", "assets/chime.wav")
    _assert_raises(RuntimeTransportError, _validate_audio_target, "audio_stage", "../chime.wav")
    _assert_raises(RuntimeTransportError, _validate_audio_volume, 16)
    capabilities = '{"api":"noise"}\n{"api":"vibeboard-huangshan-capabilities/v1","ok":true}'
    assert extract_json_line(capabilities, CAPABILITIES_API) == '{"api":"vibeboard-huangshan-capabilities/v1","ok":true}'
    truncated_apps = '{"api":"vibeboard-huangshan-app-manager/v1","error":"truncated","count":17}'
    _assert_raises(JSONResponsePending, extract_json_line, truncated_apps, APP_MANAGER_API)
    assert extract_json_line(truncated_apps, APP_MANAGER_API, allow_truncated=True) == truncated_apps
    assert status_is_transport_failure("read_status_failed: disconnected")
    assert status_is_transport_failure("disconnected")
    assert not status_is_transport_failure("ok status api=v1 active=ios_demo")
    broken_prefix = 'noise {broken { still broken {"api":"vibeboard-huangshan-app-manager/v1","active":"flow_stage"}'
    assert json.loads(extract_json_line(broken_prefix, APP_MANAGER_API))["active"] == "flow_stage"

    combined = combine_app_pages([
        '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":0,"limit":1,"apps":[{"id":"a"}],"count":2,"included":1,"truncated":0}',
        '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":1,"limit":1,"apps":[{"id":"b"}],"count":2,"included":1,"truncated":0}',
    ])
    assert json.loads(combined)["included"] == 2
    assert [app["id"] for app in json.loads(combined)["apps"]] == ["a", "b"]
    _assert_raises(
        RuntimeTransportError,
        combine_app_pages,
        [
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":0,"limit":1,"apps":[{"id":"a"}],"count":2,"included":1,"truncated":0}',
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":0,"limit":1,"apps":[{"id":"b"}],"count":2,"included":1,"truncated":0}',
        ],
    )
    _assert_raises(
        RuntimeTransportError,
        combine_app_pages,
        [
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":0,"limit":1,"apps":[{"id":"a"}],"count":2,"included":1,"truncated":0}',
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":1,"limit":1,"apps":[{"id":"a"}],"count":2,"included":1,"truncated":0}',
        ],
    )
    _assert_raises(
        RuntimeTransportError,
        combine_app_pages,
        [
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":0,"limit":1,"apps":[{"id":"a"}],"count":2,"included":1,"truncated":0}',
            '{"api":"vibeboard-huangshan-app-manager/v1","active":"a","state":"idle","offset":1,"limit":1,"apps":[],"count":2,"included":0,"truncated":1}',
        ],
    )

    chunk_line = "msh />ok json_read kind=apps offset=0 total=5 hex=68656c6c6f\x00msh />"
    assert extract_json_chunk(chunk_line, "apps") == (0, 5, b"hello")
    assert extract_json_chunk(chunk_line, "app") is None

    begin = "vb_runtime_install_begin demo"
    file_cmd = "vb_runtime_install_file demo main.lua 0 7072696e74"
    end = "vb_runtime_install_end demo"
    assert install_ack_matches("ok install_begin app=demo rc=0", begin)
    assert install_ack_matches("ok install_file app=demo path=main.lua offset=0 rc=0", file_cmd)
    abort = "vb_runtime_install_abort demo"
    assert install_ack_matches("ok install_end app=demo rc=0 active=demo", end)
    assert install_ack_matches("ok install_abort app=demo rc=0", abort)

    assert app_launch_matches("ok launch app=demo rc=0", "demo")
    assert app_stop_matches("ok stop rc=0")
    assert app_delete_matches("ok delete app=demo rc=0", "demo")
    assert staging_clear_matches("ok staging_clear removed=1 rc=0")
    assert flow_send_matches("ok flow_send channel=pc.voice seq=7 rc=0", "pc.voice", 7)
    assert flow_send_matches("[vb_runtime][flow] recv total=1 seq=7 channel=pc.voice bytes=2 text=ok", "pc.voice", 7)
    assert flow_clear_matches("msh />ok flow_clear total=0")
    assert flow_clear_matches("noise\n[vb_runtime][flow] cleared")
    assert not flow_clear_matches("vb_runtime_flow_clear")
    assert not flow_send_matches("vb_runtime_flow_send pc.voice 7 6f6b", "pc.voice", 7)
    assert parse_key_values("api=x active=demo rc=0") == {"api": "x", "active": "demo", "rc": "0"}
    assert voice_status_matches("[vb_runtime] ok voice api=v1 seq=3 ready=1", 3)
    assert voice_status_matches("[vb_runtime][voice] ok voice api=v1 built=1 ready=0 recording=0 seq=3 bytes=0 bits=\x00msh />msh />[vb_runtime][lua] timer Runtime Tick 2", 3)
    assert voice_start_matches("ok voice_start seq=4 ms=800 rc=0", 4, 800)
    assert voice_start_matches("[vb_runtime] ok voice api=v1 seq=4 requested_ms=800", 4, 800)
    assert voice_stop_matches("ok voice_stop seq=4 bytes=16000 rc=0")
    assert voice_stop_matches("noise\nerr voice_stop seq=4 bytes=0 rc=-22")
    assert not voice_stop_matches("voice_stop")
    assert voice_clear_matches("[vb_runtime][voice] cleared")
    assert voice_clear_matches("ok voice_clear")
    assert not voice_clear_matches("vb_runtime_voice_clear")
    voice_chunk = parse_voice_read_chunk("ok voice_data seq=4 offset=0 bytes=3 hex=010203", 4, 0)
    assert voice_chunk.data == b"\x01\x02\x03"
    assert normalize_flow_send_ack("ok flow_send channel=pc.voice seq=7 bytes=2 total=2", "pc.voice", 7, 2) == "ok flow_send channel=pc.voice seq=7 bytes=2 total=2"
    assert normalize_flow_send_ack("[vb_runtime][flow] recv channel=pc.voice seq=7 bytes=2 total=2", "pc.voice", 7, 2) == "ok flow_send channel=pc.voice seq=7 bytes=2 total=2"
    _assert_raises(RuntimeTransportError, normalize_flow_send_ack, "flow_send pc.voice 7 6f6b", "pc.voice", 7, 2)
    _assert_raises(RuntimeTransportError, normalize_flow_send_ack, "ok flow_send channel=pc.voice seq=7 bytes=3", "pc.voice", 7, 2)
    validate_flow_roundtrip_output(
        "ok flow_clear total=0\nok flow_send channel=pc.voice seq=7 bytes=2 total=1\n"
        "ok flow api=vibeboard-huangshan-flow/v1 total=1 retained=1 seq=7 channel=pc.voice bytes=2",
        "pc.voice",
        7,
        "ok",
    )
    validate_flow_roundtrip_output(
        "[vb_runtime][flow] cleared\n"
        "[vb_runtime][flow] recv total=1 seq=7 channel=pc.voice bytes=2 text=ok\n"
        "[vb_runtime][flow] total=1 retained=1 capacity=4\n"
        "[vb_runtime][flow] item=0 seq=7 channel=pc.voice bytes=2 payload=ok",
        "pc.voice",
        7,
        "ok",
    )
    _assert_raises(
        RuntimeTransportError,
        validate_flow_roundtrip_output,
        "ok flow_send channel=pc.voice seq=7 bytes=3 total=1\n"
        "ok flow api=vibeboard-huangshan-flow/v1 total=1 retained=1 seq=7 channel=pc.voice bytes=3",
        "pc.voice",
        7,
        "ok",
    )

    cache_path = Path("/tmp/vibeboard-runtime-transport-cache-test.json")
    try:
        save_ble_cache(cache_path, {"name": "OtherBoard", "address": "old-address"})
        cached = BLETransport(BLETransportOptions(name="VibeBoard", cache=cache_path))._cached_device()
        assert cached is None
        save_ble_cache(cache_path, {"name": "VibeBoard", "address": "current-address"})
        cached = BLETransport(BLETransportOptions(name="VibeBoard", cache=cache_path))._cached_device()
        assert cached == "current-address"
        pinned = BLETransport(
            BLETransportOptions(
                name="VibeBoard",
                address="pinned-address",
                cache=cache_path,
                no_cache=True,
            )
        )._cached_device()
        assert pinned == "pinned-address"
    finally:
        try:
            cache_path.unlink()
        except FileNotFoundError:
            pass

    class FakeSerialTransport(SerialTransport):
        def __init__(self) -> None:
            self.options = SerialTransportOptions(port="fake")
            self.commands: list[str] = []
            self.voice_seq = 4
            self.voice_bytes = 3

        def read_matching(self, command: str, matcher: Callable[[str], bool], *, timeout: float = 4.0, wait: float | None = None) -> str:
            self.commands.append(command)
            if command.startswith("vb_runtime_install_begin "):
                response = "ok install_begin app=demo rc=0"
            elif command.startswith("vb_runtime_install_file "):
                parts = command.split()
                response = f"ok install_file app={parts[1]} path={parts[2]} offset={parts[3]} rc=0"
            elif command.startswith("vb_runtime_install_end "):
                response = "ok install_end app=demo rc=0 active=demo"
            elif command.startswith("vb_runtime_install_abort "):
                response = "ok install_abort app=demo rc=0"
            elif command.startswith("vb_runtime_flow_clear"):
                response = "ok flow_clear total=0"
            elif command.startswith("vb_runtime_flow_send "):
                parts = command.split()
                response = f"ok flow_send channel={parts[1]} seq={parts[2]} bytes={len(bytes.fromhex(parts[3] if parts[3] != '-' else ''))} total=1"
            elif command.startswith("vb_runtime_voice_status"):
                response = f"ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq={self.voice_seq} bytes={self.voice_bytes} rate=16000 bits=16 channels=1 dropped=0 err=0"
            elif command.startswith("vb_runtime_voice_start"):
                parts = command.split()
                self.voice_seq += 1
                response = f"ok voice_start seq={self.voice_seq} bytes=0 ms={parts[1]} rc=0 built=1"
            elif command == "vb_runtime_voice_stop":
                response = f"ok voice_stop seq={self.voice_seq} bytes={self.voice_bytes} rc=0"
            elif command.startswith("vb_runtime_voice_read "):
                parts = command.split()
                response = f"ok voice_data seq={self.voice_seq} offset={parts[1]} bytes=3 hex=010203"
            elif command == "vb_runtime_voice_clear":
                response = "ok voice_clear"
            elif command.startswith("vb_runtime_apps_page "):
                parts = command.split()
                response = f'{{"api":"{APP_MANAGER_API}","active":"demo","state":"running","offset":{parts[1]},"limit":{parts[2]},"apps":[],"count":0,"included":0,"truncated":0}}'
            elif command.startswith("vb_runtime_launch "):
                response = "ok launch app=demo rc=0"
            elif command.startswith("vb_runtime_stop"):
                response = "ok stop rc=0"
            elif command.startswith("vb_runtime_delete "):
                response = "ok delete app=demo rc=0"
            else:
                response = "ok status api=v1 active=demo"
            assert matcher(response)
            return response + "\n"

        def command(self, command: str, *, wait: float | None = None, echo: bool | None = None) -> str:
            self.commands.append(command)
            return "ok status api=v1 active=demo\n"

        def read_json_inline(self, command: str, expected_api: str | None = None, *, timeout: float = 4.0, wait: float | None = None) -> str:
            self.commands.append(command)
            if command.startswith("vb_runtime_apps_page "):
                parts = command.split()
                return f'{{"api":"{APP_MANAGER_API}","active":"demo","state":"running","offset":{parts[1]},"limit":{parts[2]},"apps":[],"count":0,"included":0,"truncated":0}}'
            raise AssertionError(f"unexpected serial JSON inline command: {command}")

    class FakeBLETransport(BLETransport):
        def __init__(self) -> None:
            self.options = BLETransportOptions(cache=Path("/tmp/vibeboard-test-cache.json"))
            self.commands: list[str] = []
            self.voice_seq = 4
            self.voice_bytes = 3

        async def read_json_inline(self, command: str, expected_api: str, *, timeout: float = 4.0) -> str:
            self.commands.append(command)
            if command.startswith("apps_page "):
                parts = command.split()
                return f'{{"api":"{APP_MANAGER_API}","active":"demo","state":"running","offset":{parts[1]},"limit":{parts[2]},"apps":[],"count":0,"included":0,"truncated":0}}'
            raise AssertionError(f"unexpected BLE JSON inline command: {command}")

        async def read_matching(
            self,
            command: str,
            matcher: Callable[[str], bool],
            *,
            timeout: float = 4.0,
            response_wait: float | None = None,
        ) -> str:
            self.commands.append(command)
            if command.startswith("vb_runtime_install_begin "):
                response = "ok install_begin app=demo rc=0"
            elif command.startswith("vb_runtime_install_file "):
                parts = command.split()
                response = f"ok install_file app={parts[1]} path={parts[2]} offset={parts[3]} rc=0"
            elif command.startswith("vb_runtime_install_end "):
                response = "ok install_end app=demo rc=0 active=demo"
            elif command == "status":
                response = "ok status api=v1 active=demo"
            elif command.startswith("vb_runtime_install_abort "):
                response = "ok install_abort app=demo rc=0"
            elif command == "flow_clear":
                response = "ok flow_clear total=0"
            elif command.startswith("flow_send "):
                parts = command.split()
                response = f"ok flow_send channel={parts[1]} seq={parts[2]} bytes={len(bytes.fromhex(parts[3] if parts[3] != '-' else ''))} total=1"
            elif command == "voice_status":
                response = f"ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq={self.voice_seq} bytes={self.voice_bytes} rate=16000 bits=16 channels=1 dropped=0 err=0"
            elif command.startswith("voice_start"):
                parts = command.split()
                self.voice_seq += 1
                response = f"ok voice_start seq={self.voice_seq} bytes=0 ms={parts[1]} rc=0 built=1"
            elif command == "voice_stop":
                response = f"ok voice_stop seq={self.voice_seq} bytes={self.voice_bytes} rc=0"
            elif command.startswith("voice_read "):
                parts = command.split()
                response = f"ok voice_data seq={self.voice_seq} offset={parts[1]} bytes=3 hex=010203"
            elif command == "voice_clear":
                response = "ok voice_clear"
            elif command.startswith("apps_page "):
                parts = command.split()
                response = f'{{"api":"{APP_MANAGER_API}","active":"demo","state":"running","offset":{parts[1]},"limit":{parts[2]},"apps":[],"count":0,"included":0,"truncated":0}}'
            elif command.startswith("launch "):
                response = "ok launch app=demo rc=0"
            elif command == "stop":
                response = "ok stop rc=0"
            elif command.startswith("delete "):
                response = "ok delete app=demo rc=0"
            else:
                response = "ok"
            assert matcher(response)
            return response

    fake_serial = FakeSerialTransport()
    fake_serial.flow_clear()
    fake_serial.flow_send("pc.voice", 7, "ok")
    fake_serial.voice_status(4)
    fake_serial.voice_stop()
    fake_serial.voice_read(4, 0, 3)
    fake_serial.app_page(0, 5)
    fake_serial.launch_app("demo")
    fake_serial.stop_app()
    fake_serial.delete_app("demo")
    assert fake_serial.commands == [
        "vb_runtime_flow_clear",
        "vb_runtime_flow_send pc.voice 7 6f6b",
        "vb_runtime_voice_status",
        "vb_runtime_voice_stop",
        "vb_runtime_voice_read 0 3",
        "vb_runtime_apps_page 0 5",
        "vb_runtime_launch demo",
        "vb_runtime_stop",
        "vb_runtime_delete demo",
    ]

    async def check_ble_command_matrix() -> None:
        fake_ble = FakeBLETransport()
        await fake_ble.flow_clear()
        await fake_ble.flow_send("pc.voice", 7, "ok")
        await fake_ble.voice_status(4)
        await fake_ble.voice_stop()
        await fake_ble.voice_read(4, 0, 3)
        await fake_ble.app_page(0, 1)
        await fake_ble.launch_app("demo")
        await fake_ble.stop_app()
        await fake_ble.delete_app("demo")
        assert fake_ble.commands == [
            "flow_clear",
            "flow_send pc.voice 7 6f6b",
            "voice_status",
            "voice_stop",
            "voice_read 0 3",
            "apps_page 0 1",
            "launch demo",
            "stop",
            "delete demo",
        ]

    asyncio.run(check_ble_command_matrix())

    fake_serial = FakeSerialTransport()
    pcm, voice_info = fake_serial.capture_voice(600, chunk_bytes=3)
    assert pcm == b"\x01\x02\x03"
    assert voice_info["bytes"] == "3"
    assert fake_serial.commands[-1] == "vb_runtime_voice_clear"

    class EmptyVoiceSerialTransport(FakeSerialTransport):
        def __init__(self) -> None:
            super().__init__()
            self.voice_seq = 0

        def read_matching(self, command: str, matcher: Callable[[str], bool], *, timeout: float = 4.0, wait: float | None = None) -> str:
            self.commands.append(command)
            if command.startswith("vb_runtime_voice_status"):
                response = f"ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq={self.voice_seq} bytes=3 rate=16000 bits=16 channels=1 dropped=0 err=0"
            elif command.startswith("vb_runtime_voice_start"):
                parts = command.split()
                self.voice_seq += 1
                response = f"ok voice_start seq={self.voice_seq} bytes=0 ms={parts[1]} rc=0 built=1"
            elif command.startswith("vb_runtime_voice_read"):
                response = f"ok voice_data seq={self.voice_seq} offset=0 bytes=0 hex="
            else:
                response = super().read_matching(command, matcher, timeout=timeout, wait=wait)
                return response
            assert matcher(response)
            return response + "\n"

    empty_serial = EmptyVoiceSerialTransport()
    _assert_raises(RuntimeTransportError, empty_serial.capture_voice, 600, chunk_bytes=3)
    assert empty_serial.commands[-1] == "vb_runtime_voice_clear"

    fake_serial = FakeSerialTransport()
    serial_staged = fake_serial.install_package("demo", {"main.lua": b"print(1)"}, commit=False)
    assert "install_file" in serial_staged
    assert not any(command.startswith("vb_runtime_install_end ") for command in fake_serial.commands)
    assert not any(command == "vb_runtime_status" for command in fake_serial.commands)

    fake_serial = FakeSerialTransport()
    fake_serial.install_package("demo", {"main.lua": b"print(1)"}, commit=True)
    assert any(command.startswith("vb_runtime_install_end ") for command in fake_serial.commands)
    assert fake_serial.commands[-1] == "vb_runtime_status"

    async def check_ble_staged_install() -> None:
        fake_ble = FakeBLETransport()
        pcm, voice_info = await fake_ble.capture_voice(600, chunk_bytes=3)
        assert pcm == b"\x01\x02\x03"
        assert voice_info["bytes"] == "3"
        assert fake_ble.commands[-1] == "voice_clear"

        class EmptyVoiceBLETransport(FakeBLETransport):
            def __init__(self) -> None:
                super().__init__()
                self.voice_seq = 0

            async def read_matching(
                self,
                command: str,
                matcher: Callable[[str], bool],
                *,
                timeout: float = 4.0,
                response_wait: float | None = None,
            ) -> str:
                self.commands.append(command)
                if command == "voice_status":
                    response = f"ok voice api=vibeboard-huangshan-voice-bridge/v1 built=1 ready=1 recording=0 seq={self.voice_seq} bytes=3 rate=16000 bits=16 channels=1 dropped=0 err=0"
                elif command.startswith("voice_start"):
                    parts = command.split()
                    self.voice_seq += 1
                    response = f"ok voice_start seq={self.voice_seq} bytes=0 ms={parts[1]} rc=0 built=1"
                elif command.startswith("voice_read"):
                    response = f"ok voice_data seq={self.voice_seq} offset=0 bytes=0 hex="
                else:
                    response = await super().read_matching(command, matcher, timeout=timeout, response_wait=response_wait)
                    return response
                assert matcher(response)
                return response

        empty_ble = EmptyVoiceBLETransport()
        await _assert_raises_async(RuntimeTransportError, empty_ble.capture_voice, 600, chunk_bytes=3)
        assert empty_ble.commands[-1] == "voice_clear"

        fake_ble = FakeBLETransport()
        ble_staged = await fake_ble.install_package("demo", {"main.lua": b"print(1)"}, commit=False)
        assert "install_file" in ble_staged
        assert not any(command.startswith("vb_runtime_install_end ") for command in fake_ble.commands)
        assert "status" not in fake_ble.commands

        fake_ble = FakeBLETransport()
        await fake_ble.install_package("demo", {"main.lua": b"print(1)"}, commit=True)
        assert any(command.startswith("vb_runtime_install_end ") for command in fake_ble.commands)

    asyncio.run(check_ble_staged_install())

    merged = merge_status_text("abc\ndef", "def\nghi")
    assert merged == "abc\ndef\nghi"
    print("runtime_transport self-test ok")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Runtime transport helpers.")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
    else:
        parser.print_help()
