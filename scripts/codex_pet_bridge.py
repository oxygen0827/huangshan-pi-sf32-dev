#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import hashlib
import json
import os
import re
import signal
import stat
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable, Mapping, Protocol, TextIO

from codex_pet_appserver import CodexAppServerClient, CodexAppServerError, codex_thread_url, public_error
from codex_pet_protocol import (
    DEFAULT_TTL_MS,
    PetEnvelope,
    PetProtocolError,
    SequenceWindow,
    make_ack,
    now_ms,
)
from codex_pet_console import CodexPetConsole
from codex_pet_status import CodexPetStatusService, QUOTA_CHANNEL
from codex_pet_voice import CodexPetVoiceService, GLMASRTranscriber, open_codex_thread
from runtime_transport import BLETransport, BLETransportOptions, DEFAULT_DEVICE_NAME, RuntimeTransportError
from voice_bridge_common import truncate_utf8


DEFAULT_SOCKET = Path(tempfile.gettempdir()) / f"huangshan-codex-pet-{os.getuid()}.sock"
DEFAULT_JOURNAL = Path.home() / ".vibeboard" / "codex_pet_tasks.json"
DEFAULT_DESKTOP_STATE = Path.home() / ".vibeboard" / "codex_pet_desktop_state.json"
DEFAULT_HARDWARE_AUDIT = Path.home() / ".vibeboard" / "codex_pet_hardware_audit.jsonl"
STATE_CHANNEL = "pet.state"
HEARTBEAT_CHANNEL = "pet.heartbeat"
APPROVAL_CHANNEL = "pet.approval"
TASKS_CHANNEL = "pet.tasks"
PET_SELECTION_CHANNEL = "pet.select"
MCP_MESSAGE_CHANNEL = "codex.mcp"
HEARTBEAT_INTERVAL_SECONDS = 10.0
PET_READY_TIMEOUT_SECONDS = 30.0
TRANSPORT_COMMAND_TIMEOUT_SECONDS = 15.0
TRANSPORT_CONNECT_TIMEOUT_SECONDS = 40.0
TRANSPORT_CLOSE_TIMEOUT_SECONDS = 8.0
MAX_PROMPT_CHARS = 8_000
MAX_IPC_LINE_BYTES = 16_384
MAX_HARDWARE_RESULT_BYTES = 12_000
_SAFE_APP_ID = re.compile(r"^[a-z][a-z0-9_]{0,14}$")
_SAFE_PET_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,23}$")
_DEVICE_ACTION = re.compile(
    r"^pet_action action=(approve|deny|prev|next|pet_select) request=([A-Za-z0-9_.:-]{1,24})$"
)


class BridgeError(RuntimeError):
    pass


class BridgeAlreadyRunning(BridgeError):
    pass


class NonFatalTextStream:
    """Keep daemon control paths alive when a terminal or log reader disappears."""

    def __init__(self, stream: TextIO) -> None:
        self.stream = stream

    def write(self, value: str) -> int:
        try:
            written = self.stream.write(value)
        except (BrokenPipeError, OSError, ValueError):
            return len(value)
        return len(value) if written is None else written

    def flush(self) -> None:
        try:
            self.stream.flush()
        except (BrokenPipeError, OSError, ValueError):
            pass

    def __getattr__(self, name: str) -> object:
        return getattr(self.stream, name)


def install_nonfatal_standard_streams() -> None:
    if not isinstance(sys.stdout, NonFatalTextStream):
        sys.stdout = NonFatalTextStream(sys.stdout)  # type: ignore[assignment]
    if not isinstance(sys.stderr, NonFatalTextStream):
        sys.stderr = NonFatalTextStream(sys.stderr)  # type: ignore[assignment]


async def wait_for_shutdown_signal() -> None:
    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    installed: list[signal.Signals] = []
    for signum in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(signum, stop.set)
        except (NotImplementedError, RuntimeError):
            continue
        installed.append(signum)
    try:
        await stop.wait()
    finally:
        for signum in installed:
            with contextlib.suppress(Exception):
                loop.remove_signal_handler(signum)


class DeviceTransport(Protocol):
    async def connect(self) -> None: ...
    async def close(self) -> None: ...
    async def verify_connection(self, *, timeout: float | None = None) -> str: ...
    async def flow_send(self, channel: str, sequence: int, text: str) -> str: ...
    async def next_status_notification(self, *, timeout: float = 0.5) -> str: ...


class CodexAdapter(Protocol):
    async def start(self) -> None: ...
    async def close(self) -> None: ...
    async def thread_start(self, cwd: Path, **kwargs: object) -> str: ...
    async def thread_resume(self, thread_id: str) -> str: ...
    async def turn_start(self, thread_id: str, text: str, *, cwd: Path | None = None) -> str: ...


class MonitorOnlyCodexAdapter:
    """Keeps monitor mode independent from a second Codex app-server."""

    async def start(self) -> None:
        return None

    async def close(self) -> None:
        return None

    async def thread_start(self, cwd: Path, **kwargs: object) -> str:
        raise BridgeError("prompt submission is disabled in monitor mode")

    async def thread_resume(self, thread_id: str) -> str:
        raise BridgeError("prompt submission is disabled in monitor mode")

    async def turn_start(self, thread_id: str, text: str, *, cwd: Path | None = None) -> str:
        raise BridgeError("prompt submission is disabled in monitor mode")


@dataclass
class _TransportCommand:
    method: str
    args: tuple[object, ...]
    kwargs: dict[str, object]
    future: asyncio.Future[object]


class TransportCommandQueue:
    """The only object allowed to invoke the process-owned Runtime transport."""

    def __init__(
        self,
        transport: DeviceTransport,
        *,
        command_timeout: float = TRANSPORT_COMMAND_TIMEOUT_SECONDS,
    ) -> None:
        if command_timeout <= 0:
            raise ValueError("Runtime command timeout must be positive")
        self.transport = transport
        self.command_timeout = command_timeout
        self._commands: asyncio.Queue[_TransportCommand | None] = asyncio.Queue()
        self._worker: asyncio.Task[None] | None = None
        self._transport_failed = False
        self._closing = False

    async def start(self) -> None:
        if self._worker is None:
            self._worker = asyncio.create_task(self._run(), name="codex-pet-runtime-owner")

    async def call(self, method: str, *args: object, **kwargs: object) -> object:
        if self._closing:
            raise BridgeError("Runtime command queue is closing")
        if self._worker is None or self._worker.done():
            raise BridgeError("Runtime command queue is not running")
        if self._transport_failed and method not in {"close", "connect"}:
            raise RuntimeTransportError("Runtime transport requires close/connect recovery")
        future: asyncio.Future[object] = asyncio.get_running_loop().create_future()
        await self._commands.put(_TransportCommand(method, args, dict(kwargs), future))
        return await future

    async def next_status_notification(self, *, timeout: float) -> str:
        """Poll the callback-fed action queue without occupying the GATT command worker."""
        method = getattr(self.transport, "next_status_notification", None)
        if method is None or not callable(method):
            raise BridgeError("Runtime transport has no status notification source")
        value = await method(timeout=timeout)
        if not isinstance(value, str):
            raise BridgeError("Runtime transport returned an invalid status notification")
        return value

    async def close(self) -> None:
        worker = self._worker
        if worker is None:
            return
        self._closing = True
        self._fail_pending("Runtime command queue is closing")
        await self._commands.put(None)
        try:
            await asyncio.wait_for(worker, timeout=max(1.0, self.command_timeout + 1.0))
        except asyncio.TimeoutError:
            worker.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await worker
        finally:
            self._worker = None

    async def _run(self) -> None:
        while True:
            command = await self._commands.get()
            if command is None:
                return
            try:
                method = getattr(self.transport, command.method, None)
                if method is None or not callable(method):
                    raise BridgeError(f"Runtime transport has no method {command.method!r}")
                result = await asyncio.wait_for(
                    method(*command.args, **command.kwargs),
                    timeout=self._timeout_for(command.method),
                )
            except asyncio.CancelledError:
                if not command.future.done():
                    command.future.cancel()
                raise
            except asyncio.TimeoutError:
                exc = RuntimeTransportError(
                    f"Runtime transport method {command.method!r} exceeded its deadline"
                )
                self._transport_failed = True
                self._fail_pending(str(exc))
                if not command.future.done():
                    command.future.set_exception(exc)
            except RuntimeTransportError as exc:
                self._transport_failed = True
                self._fail_pending(str(exc))
                if not command.future.done():
                    command.future.set_exception(exc)
            except BaseException as exc:
                if not command.future.done():
                    command.future.set_exception(exc)
            else:
                if command.method == "connect":
                    self._transport_failed = False
                if not command.future.done():
                    command.future.set_result(result)

    def _timeout_for(self, method: str) -> float:
        if method in {"connect", "verify_connection"}:
            return max(self.command_timeout, TRANSPORT_CONNECT_TIMEOUT_SECONDS)
        if method == "close":
            return min(self.command_timeout, TRANSPORT_CLOSE_TIMEOUT_SECONDS)
        return self.command_timeout

    def _fail_pending(self, message: str) -> None:
        sentinel = False
        while True:
            try:
                pending = self._commands.get_nowait()
            except asyncio.QueueEmpty:
                break
            if pending is None:
                sentinel = True
                continue
            if not pending.future.done():
                pending.future.set_exception(RuntimeTransportError(message))
        if sentinel:
            self._commands.put_nowait(None)


class PetSequencer:
    def __init__(self, start: int | None = None) -> None:
        self._value = (
            int(time.time() * 1000) & 0xFFFFFFFF
            if start is None
            else max(0, start) & 0xFFFFFFFF
        )

    def next(self) -> int:
        if self._value >= 0xFFFFFFFF:
            self._value = 0
        self._value += 1
        return self._value


class TaskJournal:
    VERSION = 1

    def __init__(self, path: Path) -> None:
        self.path = path.expanduser()
        self._records = self._load()
        if self.path.exists():
            os.chmod(self.path, 0o600)

    def get(self, action_id: str) -> dict[str, object] | None:
        value = self._records.get(action_id)
        return dict(value) if isinstance(value, dict) else None

    def latest_thread_id(self) -> str | None:
        submitted = [
            record
            for record in self._records.values()
            if record.get("status") == "submitted" and isinstance(record.get("threadId"), str)
        ]
        if not submitted:
            return None
        latest = max(submitted, key=lambda record: safe_record_int(record.get("updatedAt")))
        return str(latest["threadId"])

    def is_managed_thread(self, thread_id: str | None) -> bool:
        if not thread_id:
            return False
        return any(
            record.get("status") == "submitted" and record.get("threadId") == thread_id
            for record in self._records.values()
        )

    def reserve(self, action_id: str, prompt: str) -> tuple[dict[str, object], bool]:
        existing = self.get(action_id)
        if existing is not None:
            return existing, False
        record: dict[str, object] = {
            "status": "reserved",
            "inputSha256": hashlib.sha256(prompt.encode("utf-8")).hexdigest(),
            "updatedAt": now_ms(),
        }
        self._records[action_id] = record
        self._save()
        return dict(record), True

    def update(self, action_id: str, **fields: object) -> dict[str, object]:
        if action_id not in self._records:
            raise BridgeError(f"task journal has no reservation for {action_id!r}")
        self._records[action_id].update(fields)
        self._records[action_id]["updatedAt"] = now_ms()
        self._save()
        return dict(self._records[action_id])

    def _load(self) -> dict[str, dict[str, object]]:
        if not self.path.exists():
            return {}
        try:
            value = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise BridgeError(f"could not read task journal {self.path}: {exc}") from exc
        if not isinstance(value, dict) or value.get("version") != self.VERSION or not isinstance(value.get("records"), dict):
            raise BridgeError(f"unsupported or malformed task journal: {self.path}")
        records: dict[str, dict[str, object]] = {}
        for action_id, record in value["records"].items():
            if not isinstance(action_id, str) or not isinstance(record, dict):
                raise BridgeError(f"malformed task journal record in {self.path}")
            records[action_id] = dict(record)
        return records

    def _save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = json.dumps(
            {"version": self.VERSION, "records": self._records},
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ) + "\n"
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.tmp")
        try:
            temporary.write_text(payload, encoding="utf-8")
            os.chmod(temporary, 0o600)
            os.replace(temporary, self.path)
        finally:
            with contextlib.suppress(FileNotFoundError):
                temporary.unlink()


class HardwareAuditLog:
    """Append-only metadata log; tool arguments and device responses are never retained."""

    def __init__(self, path: Path = DEFAULT_HARDWARE_AUDIT) -> None:
        self.path = path.expanduser()

    def append(self, op: str, arguments: Mapping[str, object], *, mutating: bool, outcome: str) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        digest = hashlib.sha256(
            json.dumps(dict(arguments), sort_keys=True, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest()
        record = json.dumps(
            {
                "v": 1,
                "at": int(time.time()),
                "op": op,
                "mutating": mutating,
                "outcome": outcome,
                "argsSha256": digest,
            },
            sort_keys=True,
            separators=(",", ":"),
        ) + "\n"
        fd = os.open(self.path, os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o600)
        try:
            os.write(fd, record.encode("utf-8"))
        finally:
            os.close(fd)
        os.chmod(self.path, 0o600)


class DeviceSession:
    def __init__(
        self,
        commands: TransportCommandQueue,
        *,
        heartbeat_interval: float = HEARTBEAT_INTERVAL_SECONDS,
        clock_ms: Callable[[], int] = now_ms,
    ) -> None:
        if heartbeat_interval <= 0:
            raise ValueError("heartbeat_interval must be positive")
        self.commands = commands
        self.heartbeat_interval = heartbeat_interval
        self.clock_ms = clock_ms
        self.sequencer = PetSequencer()
        self.connected = False
        self._transport_open = False
        self.project_name: str | None = None
        self.resume_available = False
        self._state_snapshot: tuple[str, str | None, str | None, str | None, int] | None = None
        self._tasks_snapshot: tuple[str, int] | None = None
        self._approval_snapshot: tuple[str, int] | None = None
        self._pet_selection: str | None = None
        self._replay_pending = False
        self._heartbeat_task: asyncio.Task[None] | None = None
        self._closed = False

    async def start(self, *, run_heartbeat: bool = True) -> None:
        await self.commands.start()
        try:
            await self._connect()
        except Exception:
            if not run_heartbeat:
                raise
            self.connected = False
        if run_heartbeat:
            self._heartbeat_task = asyncio.create_task(self._heartbeat_loop(), name="codex-pet-heartbeat")

    async def close(self) -> None:
        self._closed = True
        task = self._heartbeat_task
        self._heartbeat_task = None
        if task is not None:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task
        if self._transport_open:
            with contextlib.suppress(Exception):
                await self.commands.call("close")
            self._transport_open = False
        self.connected = False
        await self.commands.close()

    async def mark_transport_failure(self) -> None:
        """Force the heartbeat loop to rebuild a half-open BLE session."""
        self.connected = False
        if self._transport_open:
            with contextlib.suppress(Exception):
                await self.commands.call("close")
            self._transport_open = False

    async def publish_state(
        self,
        status: str,
        *,
        task_id: str | None = None,
        subtype: str | None = None,
        detail: str | None = None,
        ttl_ms: int = DEFAULT_TTL_MS,
    ) -> PetEnvelope:
        payload: dict[str, object] = {"status": status}
        if subtype:
            payload["subtype"] = subtype[:24]
        if detail:
            payload["detail"] = detail[:24]
        sequence = self.sequencer.next()
        envelope = PetEnvelope(
            kind="state",
            sequence=sequence,
            message_id=f"s:{sequence}",
            task_id=task_id,
            timestamp_ms=self.clock_ms(),
            ttl_ms=ttl_ms,
            payload=payload,
        )
        self._state_snapshot = (
            status,
            task_id,
            subtype,
            detail,
            envelope.expires_at_ms,
        )
        if self.connected:
            await self._send(STATE_CHANNEL, envelope)
        return envelope

    async def publish_project(self, project: str) -> None:
        self.project_name = truncate_utf8(project, 96) or "workspace"
        if not self.connected:
            return
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", "pet.project", sequence, self.project_name)

    async def publish_resume_available(self) -> None:
        self.resume_available = True
        if not self.connected:
            return
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", "pet.resume", sequence, "ready")

    async def publish_quota(self, payload: str) -> None:
        if not self.connected:
            return
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", QUOTA_CHANNEL, sequence, payload)

    async def publish_approval(self, payload: str) -> None:
        self._approval_snapshot = (payload, self.clock_ms())
        if not self.connected:
            return
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", APPROVAL_CHANNEL, sequence, payload)

    async def publish_tasks(self, payload: str) -> None:
        if len(payload.encode("utf-8")) > 184:
            raise BridgeError("desktop task snapshot exceeds board transport limit")
        self._tasks_snapshot = (payload, self.clock_ms())
        if not self.connected:
            return
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", TASKS_CHANNEL, sequence, payload)

    async def publish_pet_selection(self, slug: str) -> str:
        if _SAFE_PET_SLUG.fullmatch(slug) is None:
            raise BridgeError("invalid pet slug")
        self._pet_selection = slug
        if not self.connected:
            return json.dumps({"pet": slug}, separators=(",", ":"))
        sequence = self.sequencer.next()
        await self.commands.call("flow_send", PET_SELECTION_CHANNEL, sequence, slug)
        return json.dumps({"pet": slug}, separators=(",", ":"))

    async def publish_message(self, text: str) -> str:
        sequence = self.sequencer.next()
        return str(await self.commands.call("flow_send", MCP_MESSAGE_CHANNEL, sequence, text))

    async def next_device_action(self, *, timeout: float = 0.5) -> tuple[str, str] | None:
        value = await self.commands.next_status_notification(timeout=timeout)
        match = _DEVICE_ACTION.fullmatch(value.strip())
        if match is None:
            return None
        return match.group(1), match.group(2)

    async def runtime_call(self, method: str, *args: object, **kwargs: object) -> object:
        return await self.commands.call(method, *args, **kwargs)

    async def heartbeat_once(self) -> PetEnvelope:
        sequence = self.sequencer.next()
        envelope = PetEnvelope(
            kind="heartbeat",
            sequence=sequence,
            message_id=f"hb:{sequence}",
            timestamp_ms=self.clock_ms(),
            ttl_ms=DEFAULT_TTL_MS,
            payload={"status": "connected"},
        )
        await self._send(HEARTBEAT_CHANNEL, envelope)
        if self.project_name:
            await self.publish_project(self.project_name)
        if self.resume_available:
            await self.publish_resume_available()
        if self._replay_pending:
            await self._replay_snapshot()
            self._replay_pending = False
        return envelope

    async def _connect(self) -> None:
        started = time.monotonic()
        print("[codex_pet][ble] connect attempt", flush=True)
        try:
            await self.commands.call("connect")
            self._transport_open = True
            await self.commands.call("verify_connection", timeout=12.0)
            await self._wait_until_pet_ready()
        except BaseException as exc:
            self.connected = False
            with contextlib.suppress(Exception):
                await self.commands.call("close")
            self._transport_open = False
            print(
                f"[codex_pet][ble] connect failed after {int((time.monotonic() - started) * 1000)}ms: "
                f"{type(exc).__name__}",
                flush=True,
            )
            raise
        self.connected = True
        self._replay_pending = True
        print(
            f"[codex_pet][ble] connected and ready after {int((time.monotonic() - started) * 1000)}ms",
            flush=True,
        )

    async def _wait_until_pet_ready(self) -> None:
        deadline = time.monotonic() + PET_READY_TIMEOUT_SECONDS
        last_ui_ticks: int | None = None
        while time.monotonic() < deadline:
            try:
                status = json.loads(str(await self.commands.call("codex_pet")))
                ui_ticks = status.get("uiTicks")
                if (
                    status.get("active") == 1
                    and isinstance(status.get("pets"), int)
                    and status.get("pets", 0) > 0
                    and isinstance(status.get("frames"), int)
                    and status.get("frames", 0) > 0
                    and isinstance(ui_ticks, int)
                    and not isinstance(ui_ticks, bool)
                    and status.get("queuedFlows") == 0
                ):
                    if last_ui_ticks is not None:
                        delta = (ui_ticks - last_ui_ticks) & 0xFFFFFFFF
                        if 0 < delta < 0x80000000:
                            return
                    last_ui_ticks = ui_ticks
                else:
                    last_ui_ticks = None
            except (BridgeError, RuntimeError, ValueError, json.JSONDecodeError):
                last_ui_ticks = None
            await asyncio.sleep(0.5)
        raise BridgeError("Codex Pet board UI did not advance before snapshot replay")

    async def _replay_snapshot(self) -> None:
        current = self.clock_ms()
        if self._state_snapshot is not None:
            status, task_id, subtype, detail, expires_at = self._state_snapshot
            if expires_at > current:
                payload: dict[str, object] = {"status": status}
                if subtype:
                    payload["subtype"] = subtype[:24]
                if detail:
                    payload["detail"] = detail[:24]
                sequence = self.sequencer.next()
                envelope = PetEnvelope(
                    kind="state",
                    sequence=sequence,
                    message_id=f"s:{sequence}",
                    task_id=task_id,
                    timestamp_ms=current,
                    ttl_ms=min(DEFAULT_TTL_MS, expires_at - current),
                    payload=payload,
                )
                await self._send(STATE_CHANNEL, envelope)
        if self._tasks_snapshot is not None:
            payload, cached_at = self._tasks_snapshot
            if current - cached_at < DEFAULT_TTL_MS:
                await self.commands.call("flow_send", TASKS_CHANNEL, self.sequencer.next(), payload)
        if self._approval_snapshot is not None:
            payload, cached_at = self._approval_snapshot
            if current - cached_at < DEFAULT_TTL_MS:
                await self.commands.call("flow_send", APPROVAL_CHANNEL, self.sequencer.next(), payload)
        if self._pet_selection is not None:
            await self.commands.call(
                "flow_send", PET_SELECTION_CHANNEL, self.sequencer.next(), self._pet_selection
            )

    async def _send(self, channel: str, envelope: PetEnvelope) -> None:
        if not self.connected:
            raise BridgeError("device is disconnected")
        await self.commands.call("flow_send", channel, envelope.sequence, envelope.encode())

    async def _heartbeat_loop(self) -> None:
        # The first heartbeat also replays durable snapshots. Send it immediately
        # after startup/reconnect instead of adding one full heartbeat interval.
        delay = 0.0
        while not self._closed:
            if delay > 0:
                await asyncio.sleep(delay)
            try:
                if not self.connected:
                    await self._connect()
                await self.heartbeat_once()
                delay = self.heartbeat_interval
            except asyncio.CancelledError:
                raise
            except Exception:
                self.connected = False
                with contextlib.suppress(Exception):
                    await self.commands.call("close")
                self._transport_open = False
                if self.heartbeat_interval < 1.0:
                    delay = self.heartbeat_interval
                else:
                    delay = min(5.0, max(1.0, delay * 2 if delay >= 1.0 else 1.0))


class CodexPetBridge:
    def __init__(
        self,
        *,
        codex: CodexAdapter,
        device: DeviceSession,
        journal: TaskJournal,
        workspace: Path,
        hook_observer: Callable[..., Awaitable[object]] | None = None,
        hardware_audit: HardwareAuditLog | None = None,
    ) -> None:
        self.codex = codex
        self.device = device
        self.journal = journal
        self.workspace = workspace.expanduser().resolve()
        self.hook_observer = hook_observer
        self.hardware_audit = hardware_audit
        if not self.workspace.is_dir():
            raise BridgeError(f"workspace does not exist: {self.workspace}")
        self.inbound: dict[str, SequenceWindow] = {}
        self.outbound = PetSequencer()

    async def handle(
        self,
        request: PetEnvelope,
        *,
        current_ms: int | None = None,
        source: str = "ipc",
    ) -> PetEnvelope:
        current = now_ms() if current_ms is None else current_ms
        ack_sequence = self.outbound.next()
        if request.is_expired(current):
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current, error="expired")
        payload = dict(request.payload or {})
        action = payload.get("action")
        if source == "ipc" and action in {"hook_event", "hardware_command"}:
            source = "hook" if action == "hook_event" else "mcp"
        window = self.inbound.setdefault(source, SequenceWindow())
        decision = window.accept_unordered(request) if source in {"hook", "mcp"} else window.accept(request)
        if decision.status == "duplicate":
            record = self.journal.get(request.message_id)
            return make_ack(
                request,
                sequence=ack_sequence,
                status="duplicate",
                current_ms=current,
                task_id=_record_string(record, "threadId"),
                payload={"saved": record is not None},
            )
        if decision.status == "out_of_order":
            return make_ack(
                request,
                sequence=ack_sequence,
                status="rejected",
                current_ms=current,
                error=f"out_of_order:{decision.previous_sequence}",
            )
        if request.kind != "action":
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current, error="action_required")
        if action == "ping":
            return make_ack(request, sequence=ack_sequence, status="accepted", current_ms=current)
        if action == "hardware_command":
            return await self._handle_hardware(request, ack_sequence=ack_sequence, current_ms=current)
        if action == "hook_event":
            status = payload.get("status")
            if status not in {"connected", "running", "ready", "blocked", "needs_input"}:
                return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current, error="invalid_hook_state")
            subtype = payload.get("subtype")
            detail = payload.get("detail")
            if subtype not in {None, "approval", "credential", "question", "submit"}:
                subtype = None
            if not isinstance(detail, str):
                detail = None
            observer = self.hook_observer or self.device.publish_state
            await observer(
                str(status),
                task_id=request.task_id,
                subtype=subtype if isinstance(subtype, str) else None,
                detail=detail[:24] if detail else None,
            )
            return make_ack(
                request,
                sequence=ack_sequence,
                status="accepted",
                current_ms=current,
                task_id=request.task_id,
                payload={"observed": True},
            )
        if action != "submit_prompt":
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current, error="unknown_action")
        return await self._submit_prompt(request, ack_sequence=ack_sequence, current_ms=current)

    async def _handle_hardware(
        self,
        request: PetEnvelope,
        *,
        ack_sequence: int,
        current_ms: int,
    ) -> PetEnvelope:
        payload = dict(request.payload or {})
        operation = payload.get("op")
        arguments = payload.get("arguments", {})
        if not isinstance(operation, str) or not isinstance(arguments, dict):
            return make_ack(
                request,
                sequence=ack_sequence,
                status="rejected",
                current_ms=current_ms,
                error="invalid_hardware_request",
            )
        mutating = operation in {
            "set_rgb", "set_brightness", "show_message", "launch_app", "play_cue", "stop_audio",
            "select_pet",
        }
        if self.hardware_audit is not None:
            try:
                self.hardware_audit.append(operation, arguments, mutating=mutating, outcome="started")
            except OSError:
                return make_ack(
                    request,
                    sequence=ack_sequence,
                    status="rejected",
                    current_ms=current_ms,
                    error="hardware_failed",
                )
        try:
            result = await self._execute_hardware(operation, arguments)
            encoded = result if isinstance(result, str) else json.dumps(
                result, ensure_ascii=False, sort_keys=True, separators=(",", ":")
            )
            if len(encoded.encode("utf-8")) > MAX_HARDWARE_RESULT_BYTES:
                raise BridgeError("hardware result exceeds IPC response limit")
        except Exception as exc:
            print(
                f"Codex Pet hardware {operation} failed: "
                f"{type(exc).__name__}: {_public_bridge_error(exc)}",
                file=sys.stderr,
                flush=True,
            )
            if isinstance(exc, (RuntimeTransportError, asyncio.TimeoutError)):
                await self.device.mark_transport_failure()
            if self.hardware_audit is not None:
                with contextlib.suppress(OSError):
                    self.hardware_audit.append(operation, arguments, mutating=mutating, outcome="failed")
            return make_ack(
                request,
                sequence=ack_sequence,
                status="rejected",
                current_ms=current_ms,
                error="hardware_failed",
            )
        if self.hardware_audit is not None:
            with contextlib.suppress(OSError):
                self.hardware_audit.append(operation, arguments, mutating=mutating, outcome="accepted")
        return make_ack(
            request,
            sequence=ack_sequence,
            status="accepted",
            current_ms=current_ms,
            payload={"result": encoded},
        )

    async def _execute_hardware(self, operation: str, arguments: Mapping[str, object]) -> object:
        no_args = {
            "status": "status",
            "capabilities": "capabilities",
            "sensors": "sensors",
            "power": "power",
            "rgb_status": "rgb",
            "pet_status": "codex_pet",
            "display_status": "display",
            "app_status": "app_status",
            "apps": "apps",
            "audio_status": "audio",
        }
        if operation in no_args:
            _require_exact_arguments(arguments, set())
            return await self.device.runtime_call(no_args[operation])
        if operation == "set_rgb":
            _require_exact_arguments(arguments, {"color"})
            color = arguments.get("color")
            if not isinstance(color, str) or color not in {
                "off", "red", "green", "blue", "yellow", "cyan", "magenta", "white"
            }:
                raise BridgeError("unsupported RGB color")
            return await self.device.runtime_call("rgb", color)
        if operation == "set_brightness":
            _require_exact_arguments(arguments, {"brightness"})
            brightness = arguments.get("brightness")
            if not isinstance(brightness, int) or isinstance(brightness, bool) or not 0 <= brightness <= 100:
                raise BridgeError("brightness must be an integer from 0 to 100")
            return await self.device.runtime_call("display", brightness)
        if operation == "show_message":
            _require_exact_arguments(arguments, {"text"})
            text = arguments.get("text")
            if not isinstance(text, str) or not text.strip():
                raise BridgeError("message text is required")
            return await self.device.publish_message(truncate_utf8(text.strip(), 160))
        if operation == "launch_app":
            _require_exact_arguments(arguments, {"app_id"})
            app_id = arguments.get("app_id")
            if not isinstance(app_id, str) or _SAFE_APP_ID.fullmatch(app_id) is None:
                raise BridgeError("invalid app id")
            return await self.device.runtime_call("launch_app", app_id)
        if operation == "select_pet":
            _require_exact_arguments(arguments, {"slug"})
            slug = arguments.get("slug")
            if not isinstance(slug, str) or _SAFE_PET_SLUG.fullmatch(slug) is None:
                raise BridgeError("invalid pet slug")
            return await self.device.publish_pet_selection(slug)
        if operation == "play_cue":
            _require_exact_arguments(arguments, {"cue"})
            cue = arguments.get("cue")
            if cue not in {"listening", "submitted", "needs_input", "done", "error"}:
                raise BridgeError("unsupported Codex Pet cue")
            expected_path = f"/sdcard/apps/codex_pet/assets/{cue}.wav"
            before = _runtime_json_object(
                await self.device.runtime_call("audio"),
                operation="audio status",
            )
            result = await self.device.runtime_call(
                "audio_play", "codex_pet", f"assets/{cue}.wav"
            )
            after = _runtime_json_object(result, operation="audio playback")
            before_sequence = _runtime_json_int(before, "seq")
            after_sequence = _runtime_json_int(after, "seq")
            if (
                after_sequence == before_sequence
                or after.get("path") != expected_path
                or _runtime_json_int(after, "err") != 0
                or not (
                    _runtime_json_int(after, "playing") == 1
                    or _runtime_json_int(after, "ready") == 1
                )
            ):
                raise BridgeError("Codex Pet cue did not start")
            return result
        if operation == "stop_audio":
            _require_exact_arguments(arguments, set())
            result = await self.device.runtime_call("audio_stop")
            status = _runtime_json_object(result, operation="audio stop")
            deadline = asyncio.get_running_loop().time() + 2.0
            while (
                _runtime_json_int(status, "playing") != 0
                and asyncio.get_running_loop().time() < deadline
            ):
                await asyncio.sleep(0.1)
                result = await self.device.runtime_call("audio")
                status = _runtime_json_object(result, operation="audio stop status")
            if _runtime_json_int(status, "playing") != 0:
                raise BridgeError("Codex Pet audio did not stop")
            return result
        raise BridgeError("hardware operation is not allowed")

    async def submit_voice(self, action_id: str, transcript: str, thread_id: str | None) -> PetEnvelope:
        payload: dict[str, object] = {"action": "submit_prompt", "text": transcript}
        if thread_id:
            payload["threadId"] = thread_id
        request = PetEnvelope(
            kind="action",
            sequence=self.outbound.next(),
            message_id=action_id,
            timestamp_ms=now_ms(),
            payload=payload,
        )
        return await self.handle(request, source="voice")

    async def _submit_prompt(self, request: PetEnvelope, *, ack_sequence: int, current_ms: int) -> PetEnvelope:
        payload = dict(request.payload or {})
        prompt = payload.get("text")
        if not isinstance(prompt, str) or not prompt.strip():
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current_ms, error="text_required")
        prompt = prompt.strip()
        if len(prompt) > MAX_PROMPT_CHARS:
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current_ms, error="text_too_long")
        requested_workspace = payload.get("workspace")
        if requested_workspace is not None and Path(str(requested_workspace)).expanduser().resolve() != self.workspace:
            return make_ack(request, sequence=ack_sequence, status="rejected", current_ms=current_ms, error="workspace_not_allowed")

        record, created = self.journal.reserve(request.message_id, prompt)
        if not created:
            return make_ack(
                request,
                sequence=ack_sequence,
                status="duplicate",
                current_ms=current_ms,
                task_id=_record_string(record, "threadId"),
                payload={"saved": True},
            )

        thread_id: str | None = None
        try:
            continue_thread = payload.get("threadId")
            if continue_thread is not None:
                if not isinstance(continue_thread, str) or not continue_thread.strip():
                    raise BridgeError("threadId must be a non-empty string")
                thread_id = await self.codex.thread_resume(continue_thread.strip())
            else:
                thread_id = await self.codex.thread_start(self.workspace)
            self.journal.update(request.message_id, status="thread_allocated", threadId=thread_id)
            turn_id = await self.codex.turn_start(thread_id, prompt, cwd=self.workspace)
            self.journal.update(
                request.message_id,
                status="submitted",
                threadId=thread_id,
                turnId=turn_id,
                deepLink=codex_thread_url(thread_id),
            )
            await self.device.publish_state("running", task_id=thread_id, detail="turn submitted")
        except (BridgeError, CodexAppServerError, OSError, ValueError) as exc:
            self.journal.update(
                request.message_id,
                status="failed",
                threadId=thread_id,
                errorCode=type(exc).__name__,
            )
            with contextlib.suppress(Exception):
                await self.device.publish_state("blocked", task_id=thread_id, subtype="submit", detail="computer action")
            return make_ack(
                request,
                sequence=ack_sequence,
                status="rejected",
                current_ms=current_ms,
                error="submit_failed",
                task_id=thread_id,
            )
        return make_ack(
            request,
            sequence=ack_sequence,
            status="accepted",
            current_ms=current_ms,
            task_id=thread_id,
            payload={"submitted": True},
        )


def _record_string(record: Mapping[str, object] | None, key: str) -> str | None:
    value = record.get(key) if record else None
    return value if isinstance(value, str) and value else None


def _require_exact_arguments(arguments: Mapping[str, object], expected: set[str]) -> None:
    actual = set(arguments)
    if actual != expected:
        raise BridgeError(
            f"hardware arguments must be exactly {sorted(expected)!r}, got {sorted(actual)!r}"
        )


def _runtime_json_object(value: object, *, operation: str) -> dict[str, object]:
    if not isinstance(value, str):
        raise BridgeError(f"{operation} returned a non-text response")
    try:
        decoded = json.loads(value)
    except json.JSONDecodeError as exc:
        raise BridgeError(f"{operation} returned malformed JSON") from exc
    if not isinstance(decoded, dict):
        raise BridgeError(f"{operation} returned a non-object response")
    return decoded


def _runtime_json_int(value: Mapping[str, object], key: str) -> int:
    field = value.get(key)
    if not isinstance(field, int) or isinstance(field, bool):
        raise BridgeError(f"Runtime response omitted integer field {key!r}")
    return field


def safe_record_int(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) else 0


def _public_bridge_error(error: BaseException) -> str:
    if isinstance(error, CodexAppServerError):
        return public_error(error)[:240]
    return str(error)[:240]


class LocalIPCServer:
    def __init__(self, path: Path, handler: Callable[[PetEnvelope], Awaitable[PetEnvelope]]) -> None:
        self.path = path
        self.handler = handler
        self.server: asyncio.AbstractServer | None = None
        self.error_outbound = PetSequencer()

    async def start(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        await self._remove_stale_socket()
        try:
            self.server = await asyncio.start_unix_server(
                self._handle_client,
                path=str(self.path),
                limit=MAX_IPC_LINE_BYTES + 1,
            )
        except OSError as exc:
            raise BridgeAlreadyRunning(f"could not claim Bridge socket {self.path}: {exc}") from exc
        os.chmod(self.path, 0o600)

    async def close(self) -> None:
        if self.server is not None:
            self.server.close()
            await self.server.wait_closed()
            self.server = None
        with contextlib.suppress(FileNotFoundError):
            self.path.unlink()

    async def _remove_stale_socket(self) -> None:
        try:
            mode = self.path.stat().st_mode
        except FileNotFoundError:
            return
        if not stat.S_ISSOCK(mode):
            raise BridgeAlreadyRunning(f"Bridge socket path exists and is not a socket: {self.path}")
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_unix_connection(str(self.path)), timeout=0.2)
        except (ConnectionRefusedError, FileNotFoundError, asyncio.TimeoutError, OSError):
            self.path.unlink()
            return
        writer.close()
        await writer.wait_closed()
        del reader
        raise BridgeAlreadyRunning(f"another Codex Pet Bridge is already listening on {self.path}")

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                if len(line) > MAX_IPC_LINE_BYTES:
                    await self._write_error(writer, "request_too_large")
                    break
                try:
                    request = PetEnvelope.decode(line.decode("utf-8").strip(), max_bytes=MAX_IPC_LINE_BYTES)
                except (UnicodeDecodeError, PetProtocolError, BridgeError) as exc:
                    await self._write_error(writer, str(exc)[:160])
                    continue
                try:
                    response = await self.handler(request)
                except (PetProtocolError, BridgeError) as exc:
                    response = make_ack(
                        request,
                        sequence=self.error_outbound.next(),
                        status="rejected",
                        error=f"bridge_error:{type(exc).__name__}",
                    )
                except Exception as exc:
                    response = make_ack(
                        request,
                        sequence=self.error_outbound.next(),
                        status="rejected",
                        error=f"internal_error:{type(exc).__name__}",
                    )
                encoded = response.encode(max_bytes=MAX_IPC_LINE_BYTES)
                writer.write(encoded.encode("utf-8") + b"\n")
                await writer.drain()
        except ConnectionError:
            pass
        finally:
            writer.close()
            with contextlib.suppress(ConnectionError):
                await writer.wait_closed()

    @staticmethod
    async def _write_error(writer: asyncio.StreamWriter, error: str) -> None:
        payload = json.dumps({"ok": False, "error": error}, separators=(",", ":")).encode("utf-8") + b"\n"
        writer.write(payload)
        await writer.drain()


class CodexPetService:
    def __init__(
        self,
        *,
        bridge: CodexPetBridge,
        ipc: LocalIPCServer,
        codex: CodexAdapter,
        device: DeviceSession,
        voice: CodexPetVoiceService | None = None,
        status: CodexPetStatusService | None = None,
    ) -> None:
        self.bridge = bridge
        self.ipc = ipc
        self.codex = codex
        self.device = device
        self.voice = voice
        self.status = status
        self._started = False
        self._device_action_task: asyncio.Task[None] | None = None

    async def start(self, *, run_heartbeat: bool = True) -> None:
        try:
            await self.codex.start()
            await self.device.start(run_heartbeat=run_heartbeat)
            # Monitor mode only mirrors Desktop state; it does not need a project
            # frame during startup, and a transient BLE write must not kill the daemon.
            if not isinstance(self.codex, MonitorOnlyCodexAdapter):
                await self.device.publish_project(self.bridge.workspace.name)
            if self.status is not None:
                await self.status.start()
                self._device_action_task = asyncio.create_task(
                    self._device_action_loop(), name="codex-pet-device-actions"
                )
            if self.voice is not None:
                if self.voice.current_thread_id:
                    await self.device.publish_resume_available()
                await self.voice.start()
            # Publish the local IPC endpoint only after the device/session ready
            # path has completed. Callers must never observe a socket that can
            # only return hardware_failed while BLE is still starting.
            await self.ipc.start()
        except BaseException:
            await self._stop_device_actions()
            if self.status is not None:
                with contextlib.suppress(Exception):
                    await self.status.stop()
            if self.voice is not None:
                with contextlib.suppress(Exception):
                    await self.voice.close()
            with contextlib.suppress(Exception):
                await self.device.close()
            with contextlib.suppress(Exception):
                await self.codex.close()
            await self.ipc.close()
            raise
        self._started = True

    async def close(self) -> None:
        if not self._started:
            await self.ipc.close()
            return
        await self.ipc.close()
        try:
            await self._stop_device_actions()
            if self.status is not None:
                with contextlib.suppress(Exception):
                    await self.status.stop()
            if self.voice is not None:
                with contextlib.suppress(Exception):
                    await self.voice.close()
        finally:
            try:
                await self.device.close()
            finally:
                await self.codex.close()
                self._started = False

    async def _stop_device_actions(self) -> None:
        task = self._device_action_task
        self._device_action_task = None
        if task is None:
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

    async def _device_action_loop(self) -> None:
        while True:
            try:
                action = await self.device.next_device_action(timeout=0.1)
            except asyncio.CancelledError:
                raise
            except asyncio.TimeoutError:
                continue
            except Exception:
                await asyncio.sleep(0.1)
                continue
            if action is None or self.status is None:
                continue
            decision, request_id = action
            with contextlib.suppress(Exception):
                if decision in {"prev", "next"} and hasattr(self.status, "select"):
                    await self.status.select(-1 if decision == "prev" else 1)
                elif decision in {"approve", "deny"}:
                    await self.status.resolve_approval(request_id, decision == "approve")
                elif decision == "pet_select":
                    print(f"Codex Pet selected on board: {request_id}")


class FakeDeviceTransport:
    def __init__(self) -> None:
        self.connected = False
        self.connect_count = 0
        self.frames: list[tuple[str, int, PetEnvelope | str]] = []
        self.active_calls = 0
        self.max_active_calls = 0
        self.status_notifications: asyncio.Queue[str] = asyncio.Queue()
        self.hardware_calls: list[tuple[str, object]] = []
        self.audio_sequence = 0
        self.audio_playing = False
        self.audio_ready = False
        self.audio_path = ""
        self.audio_stop_delay_reads = 0
        self.audio_stop_pending_reads = 0
        self.pet_ui_ticks = 0

    async def _enter(self) -> None:
        self.active_calls += 1
        self.max_active_calls = max(self.max_active_calls, self.active_calls)
        await asyncio.sleep(0)

    def _exit(self) -> None:
        self.active_calls -= 1

    async def connect(self) -> None:
        await self._enter()
        try:
            self.connected = True
            self.connect_count += 1
        finally:
            self._exit()

    async def close(self) -> None:
        await self._enter()
        try:
            self.connected = False
        finally:
            self._exit()

    async def verify_connection(self, *, timeout: float | None = None) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            return "ok status fake=1"
        finally:
            self._exit()

    async def flow_send(self, channel: str, sequence: int, text: str) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            if channel in {STATE_CHANNEL, HEARTBEAT_CHANNEL}:
                value: PetEnvelope | str = PetEnvelope.decode(text)
                if sequence != value.sequence:
                    raise BridgeError("transport sequence does not match pet/v1 envelope")
            else:
                value = text
            self.frames.append((channel, sequence, value))
            return f"ok flow_send channel={channel} seq={sequence} bytes={len(text.encode('utf-8'))} total=1"
        finally:
            self._exit()

    async def next_status_notification(self, *, timeout: float = 0.5) -> str:
        return await asyncio.wait_for(self.status_notifications.get(), timeout=timeout)

    async def _hardware(self, operation: str, value: object = None) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            self.hardware_calls.append((operation, value))
            return json.dumps(
                {"api": f"fake-{operation}/v1", "value": value, "ok": True},
                sort_keys=True,
                separators=(",", ":"),
            )
        finally:
            self._exit()

    async def status(self) -> str:
        return await self._hardware("status")

    async def capabilities(self) -> str:
        return await self._hardware("capabilities")

    async def sensors(self) -> str:
        return await self._hardware("sensors")

    async def power(self) -> str:
        return await self._hardware("power")

    async def rgb(self, color: str | None = None) -> str:
        return await self._hardware("rgb", color)

    async def codex_pet(self) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            self.hardware_calls.append(("codex-pet", None))
            self.pet_ui_ticks += 1
            return json.dumps({
                "api": "vibeboard-huangshan-codex-pet/v1",
                "active": 1,
                "pets": 2,
                "frames": 4,
                "uiTicks": self.pet_ui_ticks,
                "queuedFlows": 0,
                "ok": True,
            }, sort_keys=True, separators=(",", ":"))
        finally:
            self._exit()

    async def display(self, brightness: int | None = None) -> str:
        return await self._hardware("display", brightness)

    async def app_status(self) -> str:
        return await self._hardware("app-status")

    async def apps(self) -> str:
        return await self._hardware("apps", ["codex_pet"])

    async def launch_app(self, app_id: str) -> str:
        return await self._hardware("launch-app", app_id)

    async def audio(self) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            self.hardware_calls.append(("audio-status", None))
            if self.audio_stop_pending_reads > 0:
                self.audio_stop_pending_reads -= 1
                if self.audio_stop_pending_reads == 0:
                    self.audio_playing = False
                    self.audio_ready = True
            return self._audio_json()
        finally:
            self._exit()

    async def audio_play(self, app_id: str, path: str) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            self.hardware_calls.append(("audio-play", {"app": app_id, "path": path}))
            if not self.audio_playing:
                self.audio_sequence = (self.audio_sequence + 1) & 0xFFFFFFFF
                self.audio_playing = True
                self.audio_ready = False
                self.audio_path = f"/sdcard/apps/{app_id}/{path}"
            return self._audio_json()
        finally:
            self._exit()

    async def audio_stop(self) -> str:
        await self._enter()
        try:
            if not self.connected:
                raise BridgeError("fake device disconnected")
            self.hardware_calls.append(("audio-stop", None))
            self.audio_stop_pending_reads = self.audio_stop_delay_reads
            self.audio_stop_delay_reads = 0
            if self.audio_stop_pending_reads == 0:
                self.audio_playing = False
                self.audio_ready = True
            return self._audio_json()
        finally:
            self._exit()

    def _audio_json(self) -> str:
        return json.dumps(
            {
                "api": "vibeboard-huangshan-audio-playback/v1",
                "playing": int(self.audio_playing),
                "ready": int(self.audio_ready),
                "seq": self.audio_sequence,
                "err": 0,
                "path": self.audio_path,
            },
            sort_keys=True,
            separators=(",", ":"),
        )


class FakeCodexAdapter:
    def __init__(self, *, fail_turn: bool = False) -> None:
        self.started = False
        self.thread_calls = 0
        self.turn_calls = 0
        self.resume_calls = 0
        self.fail_turn = fail_turn

    async def start(self) -> None:
        self.started = True

    async def close(self) -> None:
        self.started = False

    async def thread_start(self, cwd: Path, **kwargs: object) -> str:
        if not self.started:
            raise BridgeError("fake Codex is not started")
        self.thread_calls += 1
        return f"thr_{self.thread_calls}"

    async def thread_resume(self, thread_id: str) -> str:
        if not self.started:
            raise BridgeError("fake Codex is not started")
        self.resume_calls += 1
        return thread_id

    async def turn_start(self, thread_id: str, text: str, *, cwd: Path | None = None) -> str:
        self.turn_calls += 1
        if self.fail_turn:
            raise BridgeError("fake turn failure")
        return f"turn_{self.turn_calls}"


class FakeApprovalStatus:
    def __init__(self) -> None:
        self.started = False
        self.resolutions: list[tuple[str, bool]] = []

    async def start(self) -> None:
        self.started = True

    async def stop(self) -> None:
        self.started = False

    async def resolve_approval(self, request_id: str, approve: bool) -> bool:
        self.resolutions.append((request_id, approve))
        return True


async def _ipc_request(path: Path, envelope: PetEnvelope) -> PetEnvelope:
    reader, writer = await asyncio.open_unix_connection(str(path))
    writer.write(envelope.encode(max_bytes=MAX_IPC_LINE_BYTES).encode("utf-8") + b"\n")
    await writer.drain()
    line = await reader.readline()
    writer.close()
    await writer.wait_closed()
    decoded = line.decode("utf-8")
    try:
        value = json.loads(decoded)
    except json.JSONDecodeError:
        value = None
    if isinstance(value, dict) and value.get("ok") is False:
        raise AssertionError(f"IPC returned an error: {value.get('error')}")
    return PetEnvelope.decode(decoded, max_bytes=MAX_IPC_LINE_BYTES)


def _action(sequence: int, message_id: str, timestamp_ms: int, text: str = "检查项目") -> PetEnvelope:
    return PetEnvelope(
        kind="action",
        sequence=sequence,
        message_id=message_id,
        timestamp_ms=timestamp_ms,
        payload={"action": "submit_prompt", "text": text},
    )


async def self_test_async() -> None:
    clock = 1_900_000_000_000
    assert PetSequencer(0xFFFFFFFF).next() == 1

    class BrokenLogStream:
        def write(self, _value: str) -> int:
            raise BrokenPipeError("log reader closed")

        def flush(self) -> None:
            raise BrokenPipeError("log reader closed")

    broken_log_device = FakeDeviceTransport()
    broken_log_session = DeviceSession(TransportCommandQueue(broken_log_device))
    previous_stdout = sys.stdout
    sys.stdout = NonFatalTextStream(BrokenLogStream())  # type: ignore[arg-type,assignment]
    try:
        await broken_log_session.start(run_heartbeat=False)
        assert broken_log_session.connected
    finally:
        sys.stdout = previous_stdout
        await broken_log_session.close()

    class HangingOnceTransport(FakeDeviceTransport):
        def __init__(self) -> None:
            super().__init__()
            self.hang_once = True

        async def codex_pet(self) -> str:
            if self.hang_once:
                self.hang_once = False
                await asyncio.Event().wait()
            return await super().codex_pet()

    hanging_transport = HangingOnceTransport()
    hanging_queue = TransportCommandQueue(hanging_transport, command_timeout=0.02)
    await hanging_queue.start()
    try:
        hung = asyncio.create_task(hanging_queue.call("codex_pet"))
        await asyncio.sleep(0)
        queued = asyncio.create_task(hanging_queue.call("codex_pet"))
        failures = await asyncio.gather(hung, queued, return_exceptions=True)
        assert all(isinstance(value, RuntimeTransportError) for value in failures)
        await hanging_queue.call("close")
        await hanging_queue.call("connect")
        recovered = json.loads(str(await hanging_queue.call("codex_pet")))
        assert recovered["active"] == 1
    finally:
        await hanging_queue.close()

    with tempfile.TemporaryDirectory(prefix="codex-pet-bridge-") as temporary:
        root = Path(temporary)
        socket_path = root / "bridge.sock"
        journal_path = root / "tasks.json"

        fake_device = FakeDeviceTransport()
        commands = TransportCommandQueue(fake_device)
        device = DeviceSession(commands, heartbeat_interval=0.1, clock_ms=lambda: clock)
        codex = FakeCodexAdapter()
        journal = TaskJournal(journal_path)
        audit_path = root / "hardware-audit.jsonl"
        bridge = CodexPetBridge(
            codex=codex,
            device=device,
            journal=journal,
            workspace=Path.cwd(),
            hardware_audit=HardwareAuditLog(audit_path),
        )
        ipc = LocalIPCServer(socket_path, bridge.handle)
        approval_status = FakeApprovalStatus()
        service = CodexPetService(
            bridge=bridge,
            ipc=ipc,
            codex=codex,
            device=device,
            status=approval_status,  # type: ignore[arg-type]
        )
        await service.start(run_heartbeat=False)

        disconnect_errors: list[dict[str, object]] = []
        loop = asyncio.get_running_loop()
        previous_exception_handler = loop.get_exception_handler()
        loop.set_exception_handler(lambda _loop, context: disconnect_errors.append(dict(context)))
        slow_path = root / "slow.sock"

        async def slow_handler(request: PetEnvelope, **_: object) -> PetEnvelope:
            await asyncio.sleep(0.05)
            return make_ack(request, sequence=1, status="accepted", current_ms=clock)

        slow_ipc = LocalIPCServer(slow_path, slow_handler)
        await slow_ipc.start()
        reader, writer = await asyncio.open_unix_connection(str(slow_path))
        writer.write(_action(1, "voice:disconnect", clock).encode().encode("utf-8") + b"\n")
        await writer.drain()
        writer.transport.abort()
        del reader
        await asyncio.sleep(0.1)
        await slow_ipc.close()
        loop.set_exception_handler(previous_exception_handler)
        assert not disconnect_errors

        async def broken_handler(_request: PetEnvelope, **_: object) -> PetEnvelope:
            raise BrokenPipeError("simulated orphaned log pipe")

        error_path = root / "error.sock"
        error_ipc = LocalIPCServer(error_path, broken_handler)
        await error_ipc.start()
        try:
            error_ack = await _ipc_request(error_path, _action(1, "voice:error", clock))
            assert error_ack.payload == {
                "for": "voice:error",
                "status": "rejected",
                "error": "internal_error:BrokenPipeError",
            }
        finally:
            await error_ipc.close()

        competing_ipc = LocalIPCServer(socket_path, bridge.handle)
        try:
            await competing_ipc.start()
            raise AssertionError("a second Bridge claimed the active IPC socket")
        except BridgeAlreadyRunning:
            pass

        accepted = await _ipc_request(socket_path, _action(1, "voice:1", clock))
        assert accepted.payload == {"for": "voice:1", "status": "accepted", "submitted": True}
        assert accepted.task_id == "thr_1"
        assert len(accepted.encode().encode("utf-8")) <= 192
        assert codex.thread_calls == 1 and codex.turn_calls == 1
        assert journal.get("voice:1")["status"] == "submitted"
        assert "prompt" not in json.dumps(journal.get("voice:1"), ensure_ascii=False).lower()
        assert fake_device.frames[-1][0] == STATE_CHANNEL
        last_state = fake_device.frames[-1][2]
        assert isinstance(last_state, PetEnvelope) and last_state.payload["status"] == "running"

        hook_request = PetEnvelope(
            kind="action",
            sequence=2,
            message_id="hook:2:approval",
            timestamp_ms=clock,
            task_id="thr_1",
            payload={
                "action": "hook_event",
                "status": "needs_input",
                "subtype": "approval",
                "detail": "manual approval",
            },
        )
        hook_ack = await _ipc_request(socket_path, hook_request)
        assert hook_ack.payload == {
            "for": "hook:2:approval",
            "status": "accepted",
            "observed": True,
        }
        assert codex.thread_calls == 1 and codex.turn_calls == 1
        assert journal.get("hook:2:approval") is None
        assert isinstance(fake_device.frames[-1][2], PetEnvelope)
        assert fake_device.frames[-1][2].payload["status"] == "needs_input"

        hardware_request = PetEnvelope(
            kind="action",
            sequence=1,
            message_id="mcp:test:rgb",
            timestamp_ms=clock,
            payload={"action": "hardware_command", "op": "set_rgb", "arguments": {"color": "green"}},
        )
        hardware_ack = await _ipc_request(socket_path, hardware_request)
        assert hardware_ack.payload["status"] == "accepted"
        assert '"value":"green"' in str(hardware_ack.payload["result"])
        forbidden_request = PetEnvelope(
            kind="action",
            sequence=2,
            message_id="mcp:test:gpio",
            timestamp_ms=clock,
            payload={"action": "hardware_command", "op": "raw_gpio", "arguments": {"pin": "secret"}},
        )
        forbidden_ack = await _ipc_request(socket_path, forbidden_request)
        assert forbidden_ack.payload["status"] == "rejected"
        audit_text = audit_path.read_text(encoding="utf-8")
        assert '"op":"set_rgb"' in audit_text and '"op":"raw_gpio"' in audit_text
        assert "green" not in audit_text and "secret" not in audit_text
        assert stat.S_IMODE(audit_path.stat().st_mode) == 0o600

        from codex_pet_mcp import BridgeCaller, MCPServer, PROTOCOL_VERSION

        mcp = MCPServer(BridgeCaller(socket_path))
        initialized = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "protocolVersion": PROTOCOL_VERSION,
                "capabilities": {},
                "clientInfo": {"name": "bridge-integration-test", "version": "1"},
            },
        })
        assert initialized and "result" in initialized
        mcp_result = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 2,
            "method": "tools/call",
            "params": {"name": "huangshan_capabilities", "arguments": {}},
        })
        assert mcp_result and mcp_result["result"]["isError"] is False  # type: ignore[index]
        assert mcp_result["result"]["structuredContent"]["ok"] is True  # type: ignore[index]
        mcp_pet_status = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 21,
            "method": "tools/call",
            "params": {"name": "huangshan_pet_status", "arguments": {}},
        })
        assert mcp_pet_status and mcp_pet_status["result"]["isError"] is False  # type: ignore[index]
        assert mcp_pet_status["result"]["structuredContent"]["ok"] is True  # type: ignore[index]
        assert fake_device.hardware_calls[-1] == ("codex-pet", None)
        mcp_cue = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "huangshan_play_cue", "arguments": {"cue": "done"}},
        })
        assert mcp_cue and mcp_cue["result"]["isError"] is False  # type: ignore[index]
        assert fake_device.hardware_calls[-1] == (
            "audio-play", {"app": "codex_pet", "path": "assets/done.wav"}
        )
        busy_cue = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 31,
            "method": "tools/call",
            "params": {"name": "huangshan_play_cue", "arguments": {"cue": "error"}},
        })
        assert busy_cue and busy_cue["result"]["isError"] is True  # type: ignore[index]
        fake_device.audio_stop_delay_reads = 2
        stop_cue = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 32,
            "method": "tools/call",
            "params": {"name": "huangshan_stop_audio", "arguments": {}},
        })
        assert stop_cue and stop_cue["result"]["isError"] is False  # type: ignore[index]
        mcp_pet = await mcp.handle({
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {"name": "huangshan_select_pet", "arguments": {"slug": "boxcat"}},
        })
        assert mcp_pet and mcp_pet["result"]["isError"] is False  # type: ignore[index]
        assert mcp_pet["result"]["structuredContent"] == {"pet": "boxcat"}  # type: ignore[index]
        assert fake_device.frames[-1][0] == PET_SELECTION_CHANNEL
        assert fake_device.frames[-1][2] == "boxcat"

        await fake_device.status_notifications.put("unrelated status update")
        await fake_device.status_notifications.put("pet_action action=approve request=req.1")
        deadline = asyncio.get_running_loop().time() + 10.0
        while not approval_status.resolutions and asyncio.get_running_loop().time() < deadline:
            await asyncio.sleep(0.01)
        assert approval_status.resolutions == [("req.1", True)]

        duplicate = await _ipc_request(socket_path, _action(3, "voice:1", clock))
        assert duplicate.payload["status"] == "duplicate"
        assert codex.thread_calls == 1 and codex.turn_calls == 1
        out_of_order = await _ipc_request(socket_path, _action(1, "voice:other", clock))
        assert out_of_order.payload["status"] == "rejected"
        assert str(out_of_order.payload["error"]).startswith("out_of_order")

        voice_action_id = "v:7:0123456789abcdefabcd"
        voice_ack = await bridge.submit_voice(voice_action_id, "run the voice task", None)
        assert voice_ack.payload["status"] == "accepted" and voice_ack.task_id == "thr_2"
        voice_duplicate = await bridge.submit_voice(voice_action_id, "run the voice task", None)
        assert voice_duplicate.payload["status"] == "duplicate"
        continued = await bridge.submit_voice("v:8:0123456789abcdefabcd", "continue it", "thr_2")
        assert continued.payload["status"] == "accepted" and continued.task_id == "thr_2"
        assert codex.thread_calls == 2 and codex.turn_calls == 3 and codex.resume_calls == 1

        expired = await bridge.handle(_action(3, "voice:expired", clock - DEFAULT_TTL_MS), current_ms=clock)
        assert expired.payload["error"] == "expired"
        await asyncio.gather(*(device.publish_state("connected") for _ in range(6)))
        assert fake_device.max_active_calls == 1
        await service.close()

        # A fresh Bridge reconnects, but the durable reservation prevents task replay.
        restarted_device_transport = FakeDeviceTransport()
        restarted_device = DeviceSession(TransportCommandQueue(restarted_device_transport), clock_ms=lambda: clock)
        restarted_codex = FakeCodexAdapter()
        restarted_journal = TaskJournal(journal_path)
        restarted_bridge = CodexPetBridge(
            codex=restarted_codex,
            device=restarted_device,
            journal=restarted_journal,
            workspace=Path.cwd(),
        )
        restarted_ipc = LocalIPCServer(socket_path, restarted_bridge.handle)
        restarted_service = CodexPetService(
            bridge=restarted_bridge,
            ipc=restarted_ipc,
            codex=restarted_codex,
            device=restarted_device,
        )
        await restarted_service.start(run_heartbeat=False)
        replay = await _ipc_request(socket_path, _action(1, "voice:1", clock))
        assert replay.payload["status"] == "duplicate"
        voice_replay = await restarted_bridge.submit_voice(voice_action_id, "run the voice task", None)
        assert voice_replay.payload["status"] == "duplicate" and voice_replay.task_id == "thr_2"
        assert restarted_codex.thread_calls == 0 and restarted_codex.turn_calls == 0
        assert restarted_device_transport.connect_count == 1
        await restarted_service.close()

        # A crash after reservation also blocks automatic replay.
        pending_journal_path = root / "pending.json"
        pending = TaskJournal(pending_journal_path)
        pending.reserve("voice:pending", "do not replay")
        pending_device_transport = FakeDeviceTransport()
        pending_device = DeviceSession(TransportCommandQueue(pending_device_transport), clock_ms=lambda: clock)
        pending_codex = FakeCodexAdapter()
        await pending_codex.start()
        await pending_device.start(run_heartbeat=False)
        pending_bridge = CodexPetBridge(
            codex=pending_codex,
            device=pending_device,
            journal=TaskJournal(pending_journal_path),
            workspace=Path.cwd(),
        )
        pending_ack = await pending_bridge.handle(_action(1, "voice:pending", clock), current_ms=clock)
        assert pending_ack.payload["status"] == "duplicate"
        assert pending_codex.thread_calls == 0 and pending_codex.turn_calls == 0
        await pending_device.close()
        await pending_codex.close()

        immediate_transport = FakeDeviceTransport()
        immediate_device = DeviceSession(
            TransportCommandQueue(immediate_transport),
            heartbeat_interval=10.0,
            clock_ms=lambda: clock,
        )
        await immediate_device.start(run_heartbeat=True)
        await asyncio.sleep(0.05)
        assert [frame[0] for frame in immediate_transport.frames] == [HEARTBEAT_CHANNEL]
        await immediate_device.close()

        reconnect_transport = FakeDeviceTransport()
        reconnect_device = DeviceSession(
            TransportCommandQueue(reconnect_transport),
            heartbeat_interval=0.01,
            clock_ms=lambda: clock,
        )
        await reconnect_device.start(run_heartbeat=True)
        await reconnect_device.publish_state("running", task_id="thr-reconnect", detail="Thinking")
        await reconnect_device.publish_tasks(
            '{"v":1,"p":"reconnect","st":"running","d":"Thinking","i":1,"n":1,"ac":1,"a":0}'
        )
        await reconnect_device.publish_approval('{"v":1,"a":1,"r":"a:0123456789abcdefabcd"}')
        await reconnect_device.publish_pet_selection("boba")
        reconnect_transport.frames.clear()
        reconnect_transport.connected = False
        deadline = asyncio.get_running_loop().time() + 2.0
        while asyncio.get_running_loop().time() < deadline:
            replayed_channels = [frame[0] for frame in reconnect_transport.frames]
            replayed = all(
                channel in replayed_channels
                for channel in (
                    HEARTBEAT_CHANNEL,
                    STATE_CHANNEL,
                    TASKS_CHANNEL,
                    APPROVAL_CHANNEL,
                    PET_SELECTION_CHANNEL,
                )
            )
            if reconnect_transport.connect_count >= 2 and reconnect_device.connected and replayed:
                break
            await asyncio.sleep(0.01)
        assert reconnect_transport.connect_count >= 2 and reconnect_device.connected
        replayed_channels = [frame[0] for frame in reconnect_transport.frames]
        assert replayed_channels[:5] == [
            HEARTBEAT_CHANNEL,
            STATE_CHANNEL,
            TASKS_CHANNEL,
            APPROVAL_CHANNEL,
            PET_SELECTION_CHANNEL,
        ]
        await reconnect_device.close()

        class InitiallyOfflineTransport(FakeDeviceTransport):
            def __init__(self) -> None:
                super().__init__()
                self.connect_attempts = 0

            async def connect(self) -> None:
                await self._enter()
                try:
                    self.connect_attempts += 1
                    if self.connect_attempts <= 2:
                        raise BridgeError("fake board is rebooting")
                    self.connected = True
                    self.connect_count += 1
                finally:
                    self._exit()

        offline_transport = InitiallyOfflineTransport()
        offline_device = DeviceSession(
            TransportCommandQueue(offline_transport),
            heartbeat_interval=0.01,
            clock_ms=lambda: clock,
        )
        try:
            await offline_device.start(run_heartbeat=True)
            await offline_device.publish_tasks(
                '{"v":1,"p":"offline","st":"running","d":"Thinking","i":1,"n":1,"ac":1,"a":0}'
            )
            deadline = asyncio.get_running_loop().time() + 2.0
            while asyncio.get_running_loop().time() < deadline:
                channels = [frame[0] for frame in offline_transport.frames]
                if offline_device.connected and TASKS_CHANNEL in channels:
                    break
                await asyncio.sleep(0.01)
            assert offline_transport.connect_attempts >= 3 and offline_device.connected
            assert TASKS_CHANNEL in [frame[0] for frame in offline_transport.frames]
        finally:
            await offline_device.close()

        # Failed turns stay failed and require a new id instead of an implicit retry.
        failed_device = DeviceSession(TransportCommandQueue(FakeDeviceTransport()), clock_ms=lambda: clock)
        failed_codex = FakeCodexAdapter(fail_turn=True)
        await failed_codex.start()
        await failed_device.start(run_heartbeat=False)
        failed_journal = TaskJournal(root / "failed.json")
        failed_bridge = CodexPetBridge(
            codex=failed_codex,
            device=failed_device,
            journal=failed_journal,
            workspace=Path.cwd(),
        )
        failure = await failed_bridge.handle(_action(1, "voice:failure", clock), current_ms=clock)
        assert failure.payload["status"] == "rejected" and failure.payload["error"] == "submit_failed"
        assert not failed_journal.is_managed_thread(failure.task_id)
        again = await failed_bridge.handle(_action(2, "voice:failure", clock), current_ms=clock)
        assert again.payload["status"] == "duplicate" and failed_codex.turn_calls == 1
        await failed_device.close()
        await failed_codex.close()

        blocker = root / "not-a-socket"
        blocker.write_text("owned", encoding="utf-8")
        blocked_ipc = LocalIPCServer(blocker, bridge.handle)
        try:
            await blocked_ipc.start()
            raise AssertionError("non-socket IPC path was replaced")
        except BridgeAlreadyRunning:
            pass
        assert blocker.read_text(encoding="utf-8") == "owned"


async def run_service(args: argparse.Namespace) -> None:
    if args.mode == "monitor" and not args.address:
        raise BridgeError("monitor mode requires a pinned BLE peripheral address")
    options = BLETransportOptions(
        name=args.name,
        address=args.address,
        cache=args.ble_cache,
        no_cache=args.no_cache,
        echo=args.echo,
    )
    transport = BLETransport(options)
    commands = TransportCommandQueue(transport)
    device = DeviceSession(commands)
    journal = TaskJournal(args.journal)
    if args.mode == "monitor":
        from codex_pet_monitor import CodexDesktopMonitor, SubprocessApprovalExecutor

        codex = MonitorOnlyCodexAdapter()
        bridge = CodexPetBridge(
            codex=codex,
            device=device,
            journal=journal,
            workspace=args.workspace,
            hardware_audit=HardwareAuditLog(args.hardware_audit),
        )
        monitor = CodexDesktopMonitor(
            device=device,
            approval_executor=SubprocessApprovalExecutor(args.approval_helper),
            state_path=args.desktop_state,
            managed_task=journal.is_managed_thread,
        )

        async def monitor_handler(request: PetEnvelope, **kwargs: object) -> PetEnvelope:
            payload = dict(request.payload or {})
            if payload.get("action") == "hook_event":
                return await monitor.handle(request, **kwargs)
            return await bridge.handle(request, **kwargs)

        ipc = LocalIPCServer(args.socket, monitor_handler)
        service = CodexPetService(
            bridge=bridge,
            ipc=ipc,
            codex=codex,
            device=device,
            status=monitor,
        )
        await service.start()
        device.project_name = None
        print(f"Codex Pet monitor ready: {args.socket}")
        print("Watching Codex Desktop tasks through global Hooks; voice submission is disabled.")
        try:
            await wait_for_shutdown_signal()
        finally:
            await service.close()
        return

    codex = CodexAppServerClient(codex_bin=args.codex_bin)
    console = CodexPetConsole()

    def console_event(message: Mapping[str, object]) -> None:
        if "id" in message:
            console.handle_server_request(message)
        else:
            console.handle_notification(message)

    status = CodexPetStatusService(
        codex=codex,
        publish_state=device.publish_state,
        publish_quota=device.publish_quota,
        publish_approval=device.publish_approval,
        managed_task=journal.is_managed_thread,
        open_thread=open_codex_thread if args.open_desktop_thread and not args.no_open_thread else None,
        console_event=console_event,
        project_name=args.workspace.name,
    )
    bridge = CodexPetBridge(
        codex=codex,
        device=device,
        journal=journal,
        workspace=args.workspace,
        hook_observer=status.observe_external,
        hardware_audit=HardwareAuditLog(args.hardware_audit),
    )
    ipc = LocalIPCServer(args.socket, bridge.handle)
    transcriber = GLMASRTranscriber(
        model=args.asr_model,
        prompt=args.asr_prompt,
        save_dir=args.voice_capture_dir if args.save_voice_captures else None,
        mock_transcript=args.mock_transcript,
    )
    voice = CodexPetVoiceService(
        runtime_call=device.runtime_call,
        submit=bridge.submit_voice,
        publish_state=device.publish_state,
        transcribe=transcriber,
        initial_thread_id=journal.latest_thread_id(),
        open_thread=None,
    )
    service = CodexPetService(
        bridge=bridge,
        ipc=ipc,
        codex=codex,
        device=device,
        voice=voice,
        status=status,
    )
    await service.start()
    print(f"Codex Pet Bridge ready: {args.socket}")
    print("Codex Pet live console ready: replies and approval requests stay on this Bridge connection.")
    try:
        await wait_for_shutdown_signal()
    finally:
        await service.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Single-owner Huangshan Codex Pet Bridge")
    parser.add_argument("--mode", choices=("monitor", "voice"), default="monitor")
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--journal", type=Path, default=DEFAULT_JOURNAL)
    parser.add_argument("--desktop-state", type=Path, default=DEFAULT_DESKTOP_STATE)
    parser.add_argument("--hardware-audit", type=Path, default=DEFAULT_HARDWARE_AUDIT)
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    parser.add_argument("--codex-bin")
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME)
    parser.add_argument("--address")
    parser.add_argument("--ble-cache", type=Path, default=Path.home() / ".vibeboard" / "codex_pet_ble.json")
    parser.add_argument("--no-cache", action="store_true")
    parser.add_argument("--echo", action="store_true")
    parser.add_argument("--asr-model", default="glm-asr-2512")
    parser.add_argument("--asr-prompt")
    parser.add_argument("--save-voice-captures", action="store_true")
    parser.add_argument("--voice-capture-dir", type=Path, default=Path("captures/codex_pet"))
    parser.add_argument("--mock-transcript", help="Development-only transcript that bypasses GLM-ASR")
    parser.add_argument(
        "--approval-helper",
        type=Path,
        default=Path(__file__).resolve().parent.parent / ".local" / "CodexPetDesktopApproval",
    )
    parser.add_argument("--no-open-thread", action="store_true")
    parser.add_argument(
        "--open-desktop-thread",
        action="store_true",
        help="Also focus the Codex desktop task on terminal completion (not needed for live console).",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    install_nonfatal_standard_streams()
    args = parse_args()
    if args.self_test:
        asyncio.run(self_test_async())
        print("codex_pet_bridge self-test ok")
        return 0
    try:
        asyncio.run(run_service(args))
    except KeyboardInterrupt:
        return 130
    except (BridgeError, CodexAppServerError) as exc:
        print(f"codex_pet_bridge: {_public_bridge_error(exc)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
