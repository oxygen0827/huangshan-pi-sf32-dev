#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import hashlib
import json
import os
import re
import shlex
import time
from dataclasses import dataclass
from typing import Awaitable, Callable, Mapping, Protocol

from codex_pet_appserver import (
    AppServerUsageProvider,
    CodexAppServerError,
    CodexAppServerProtocolError,
    RateLimitSnapshot,
    UsageProvider,
    UsageReading,
    codex_thread_url,
)
from codex_pet_protocol import DEFAULT_TTL_MS, now_ms


QUOTA_CHANNEL = "pet.quota"
QUOTA_MAX_BYTES = 184
STATUS_REFRESH_SECONDS = 5.0
QUOTA_REFRESH_SECONDS = 60.0
APPROVAL_TTL_SECONDS = 60.0
APPROVAL_MAX_BYTES = 184
_APPROVAL_ID = re.compile(r"^[A-Za-z0-9_.:-]{1,24}$")
_BOARD_APPROVAL_METHODS = {
    "item/commandExecution/requestApproval": "command",
    "item/fileChange/requestApproval": "file",
}

_STATE_PRIORITY = {
    "disconnected": 0,
    "connected": 10,
    "ready": 20,
    "running": 40,
    "listening": 50,
    "transcribing": 55,
    "needs_input": 80,
    "blocked": 90,
}
_REQUEST_METHODS = {
    "item/commandExecution/requestApproval": ("needs_input", "approval", "command approval"),
    "item/fileChange/requestApproval": ("needs_input", "approval", "file approval"),
    "item/permissions/requestApproval": ("needs_input", "approval", "permission approval"),
    "item/tool/requestUserInput": ("needs_input", "question", "computer input"),
    "mcpServer/elicitation/request": ("needs_input", "question", "MCP question"),
}


class CodexEventReader(Protocol):
    async def next_notification(self, *, timeout: float | None = None) -> dict[str, object]: ...
    async def next_server_request(self, *, timeout: float | None = None) -> dict[str, object]: ...
    async def respond(self, request_id: object, result: object) -> None: ...


PublishState = Callable[..., Awaitable[object]]
PublishQuota = Callable[[str], Awaitable[object]]
PublishApproval = Callable[[str], Awaitable[object]]
ManagedTask = Callable[[str | None], bool]
OpenThread = Callable[[str], Awaitable[None]]
ConsoleEvent = Callable[[Mapping[str, object]], None]


@dataclass(frozen=True)
class TaskStatus:
    task_id: str | None
    status: str
    subtype: str | None
    detail: str | None
    turn_id: str | None
    updated_at: int
    expires_at: int


@dataclass(frozen=True)
class PendingApproval:
    key: str
    request_id: object
    method: str
    task_id: str | None
    summary: str
    expires_at: float
    approve_decision: str = "accept"
    deny_decision: str = "decline"


class TaskStateReducer:
    """Reduce interleaved app-server events to one safe board status."""

    def __init__(self) -> None:
        self._states: dict[str, TaskStatus] = {}

    def update(
        self,
        task_id: str | None,
        status: str,
        *,
        subtype: str | None = None,
        detail: str | None = None,
        turn_id: str | None = None,
        now: int | None = None,
        ttl_ms: int = DEFAULT_TTL_MS,
    ) -> TaskStatus:
        if status not in _STATE_PRIORITY:
            raise CodexAppServerProtocolError(f"unsupported Codex Pet status: {status!r}")
        current = now_ms() if now is None else now
        key = task_id or "__bridge__"
        value = TaskStatus(
            task_id=task_id,
            status=status,
            subtype=subtype[:24] if isinstance(subtype, str) else None,
            detail=detail[:24] if isinstance(detail, str) else None,
            turn_id=turn_id if isinstance(turn_id, str) and turn_id else None,
            updated_at=current,
            expires_at=current + max(1, min(DEFAULT_TTL_MS, ttl_ms)),
        )
        self._states[key] = value
        self._prune(current)
        return value

    def current(self, *, now: int | None = None) -> TaskStatus:
        current = now_ms() if now is None else now
        self._prune(current)
        if not self._states:
            return TaskStatus(None, "ready", None, None, None, current, current + DEFAULT_TTL_MS)
        return max(
            self._states.values(),
            key=lambda value: (_STATE_PRIORITY[value.status], value.updated_at),
        )

    def remove(self, task_id: str | None) -> None:
        self._states.pop(task_id or "__bridge__", None)

    def _prune(self, current: int) -> None:
        for key, value in list(self._states.items()):
            if value.expires_at <= current:
                del self._states[key]


def _mapping(value: object, label: str) -> Mapping[str, object]:
    if not isinstance(value, dict):
        raise CodexAppServerProtocolError(f"{label} must be an object")
    return value


def _string(value: object) -> str | None:
    return value if isinstance(value, str) and value else None


def _event_ids(params: Mapping[str, object]) -> tuple[str | None, str | None]:
    task_id = _string(params.get("threadId"))
    turn_id = _string(params.get("turnId"))
    turn = params.get("turn")
    if isinstance(turn, dict):
        task_id = task_id or _string(turn.get("threadId"))
        turn_id = turn_id or _string(turn.get("id"))
    return task_id, turn_id


def _notification_status(method: str, params: Mapping[str, object]) -> tuple[str, str | None, str | None] | None:
    task_id, turn_id = _event_ids(params)
    if method in {"turn/started", "turn/inProgress", "item/started", "item/commandExecution/started"}:
        return "running", task_id, turn_id
    if method in {"turn/completed", "turn/completion"}:
        turn = params.get("turn")
        status = _string(turn.get("status")) if isinstance(turn, dict) else _string(params.get("status"))
        normalized = (status or "completed").lower()
        return ("blocked" if normalized in {"failed", "error", "interrupted", "cancelled"} else "ready"), task_id, turn_id
    if method in {"turn/failed", "turn/interrupted", "turn/cancelled"}:
        return "blocked", task_id, turn_id
    if method.startswith("item/") and method.endswith("/completed"):
        item = params.get("item")
        status = _string(item.get("status")) if isinstance(item, dict) else None
        return ("blocked" if status and status.lower() in {"failed", "error"} else "running"), task_id, turn_id
    return None


def _request_detail(method: str, params: Mapping[str, object]) -> tuple[str, str, str, str | None] | None:
    mapped = _REQUEST_METHODS.get(method)
    if mapped is None:
        return None
    status, subtype, detail = mapped
    if method == "item/tool/requestUserInput":
        questions = params.get("questions")
        if isinstance(questions, list) and questions:
            first = questions[0]
            if isinstance(first, dict) and first.get("isSecret") is True:
                subtype, detail = "credential", "computer credential"
    task_id, turn_id = _event_ids(params)
    return status, subtype, detail, task_id or turn_id


def _approval_key(request_id: object) -> str:
    candidate = str(request_id)
    if _APPROVAL_ID.fullmatch(candidate):
        return candidate
    digest = hashlib.sha256(candidate.encode("utf-8", "replace")).hexdigest()[:20]
    return f"r:{digest}"


def _approval_summary(method: str, params: Mapping[str, object]) -> str:
    if method != "item/commandExecution/requestApproval":
        return "file change"
    command = params.get("command")
    if not isinstance(command, str) or not command.strip():
        return "command"
    try:
        words = shlex.split(command, posix=True)
    except ValueError:
        words = []
    executable = os.path.basename(words[0]) if words else "command"
    executable = "".join(char for char in executable if char.isalnum() or char in "._-")[:16]
    return f"{executable or 'command'} command"


def _clip_utf8(value: str, max_bytes: int) -> str:
    encoded = value.encode("utf-8")[:max_bytes]
    return encoded.decode("utf-8", "ignore")


def _approval_payload(pending: PendingApproval, project_name: str, *, status: str | None = None) -> str:
    value: dict[str, object] = {"v": 1, "id": pending.key}
    if status is None:
        value.update({
            "project": _clip_utf8(project_name or "workspace", 32),
            "task": _clip_utf8(pending.task_id or "task", 20),
            "tool": _BOARD_APPROVAL_METHODS[pending.method],
            "summary": _clip_utf8(pending.summary, 32),
            "expires": int(time.time() + max(0.0, pending.expires_at - time.monotonic())),
        })
    else:
        value["status"] = status
    encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > APPROVAL_MAX_BYTES:
        value.pop("task", None)
        encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > APPROVAL_MAX_BYTES:
        raise ValueError("pet.approval payload exceeds board flow limit")
    return encoded


def quota_payload(reading: UsageReading, previous: RateLimitSnapshot | None = None) -> str:
    """Serialize quota without credentials or provider error bodies."""
    snapshot = reading.snapshot or previous
    status = "live" if reading.status == "live" and reading.snapshot is not None else (
        "stale" if previous is not None else "unavailable"
    )
    stale = status != "live"
    value: dict[str, object] = {
        "v": 1,
        "status": status,
        "src": reading.source[:20],
        "at": snapshot.fetched_at if snapshot else int(time.time()),
        "stale": stale,
    }
    if snapshot is not None:
        for prefix, window in (("primary", snapshot.primary), ("secondary", snapshot.secondary)):
            if window is not None:
                short = "p" if prefix == "primary" else "s"
                value[f"{short}U"] = window.used_percent
                if window.window_minutes is not None:
                    value[f"{short}W"] = window.window_minutes
                if window.resets_at is not None:
                    value[f"{short}R"] = window.resets_at
    if status != "live":
        error = (reading.error or "unavailable").lower()
        value["error"] = "auth" if "auth" in error or "login" in error else "unavailable"
    encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > QUOTA_MAX_BYTES:
        value.pop("sR", None)
        value.pop("pR", None)
        encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"))
    if len(encoded.encode("utf-8")) > QUOTA_MAX_BYTES:
        raise ValueError(f"pet.quota payload exceeds {QUOTA_MAX_BYTES} bytes")
    return encoded


class CodexPetStatusService:
    def __init__(
        self,
        *,
        codex: CodexEventReader,
        publish_state: PublishState,
        publish_quota: PublishQuota,
        publish_approval: PublishApproval | None = None,
        managed_task: ManagedTask | None = None,
        open_thread: OpenThread | None = None,
        console_event: ConsoleEvent | None = None,
        project_name: str = "workspace",
        usage_provider: UsageProvider | None = None,
        status_refresh_seconds: float = STATUS_REFRESH_SECONDS,
        quota_refresh_seconds: float = QUOTA_REFRESH_SECONDS,
        approval_ttl_seconds: float = APPROVAL_TTL_SECONDS,
    ) -> None:
        if status_refresh_seconds <= 0 or quota_refresh_seconds <= 0 or approval_ttl_seconds <= 0:
            raise ValueError("status refresh intervals must be positive")
        self.codex = codex
        self.publish_state = publish_state
        self.publish_quota = publish_quota
        self.publish_approval = publish_approval
        self.managed_task = managed_task or (lambda _task_id: False)
        self.open_thread = open_thread
        self.console_event = console_event
        self.project_name = project_name
        self.usage_provider = usage_provider or AppServerUsageProvider(codex)  # type: ignore[arg-type]
        self.status_refresh_seconds = status_refresh_seconds
        self.quota_refresh_seconds = quota_refresh_seconds
        self.approval_ttl_seconds = approval_ttl_seconds
        self.reducer = TaskStateReducer()
        self.last_quota: RateLimitSnapshot | None = None
        self.pending_requests: dict[str, str] = {}
        self.pending_approvals: dict[str, PendingApproval] = {}
        self._approval_lock = asyncio.Lock()
        self._opened_turns: set[tuple[str, str]] = set()
        self._tasks: list[asyncio.Task[None]] = []
        self._stop = asyncio.Event()
        self._last_state_signature: tuple[object, ...] | None = None

    async def start(self) -> None:
        if self._tasks:
            return
        self._stop.clear()
        self.reducer.update(None, "ready", detail="bridge ready")
        await self._publish_state(force=True)
        await self._refresh_quota()
        self._tasks = [
            asyncio.create_task(self._notification_loop(), name="codex-pet-status-events"),
            asyncio.create_task(self._server_request_loop(), name="codex-pet-status-requests"),
            asyncio.create_task(self._refresh_loop(), name="codex-pet-status-refresh"),
            asyncio.create_task(self._approval_timeout_loop(), name="codex-pet-approval-timeout"),
        ]

    async def stop(self) -> None:
        self._stop.set()
        for key in list(self.pending_approvals):
            with contextlib.suppress(Exception):
                await self._resolve_approval(key, approve=False, outcome="declined")
        tasks, self._tasks = self._tasks, []
        for task in tasks:
            task.cancel()
        for task in tasks:
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await task

    async def _notification_loop(self) -> None:
        while not self._stop.is_set():
            try:
                message = await self.codex.next_notification(timeout=0.5)
                await self.handle_notification(message)
            except (CodexAppServerError, asyncio.TimeoutError):
                continue
            except Exception:
                # Device publishing is best-effort; reconnect/refresh will restore state.
                continue

    async def _server_request_loop(self) -> None:
        while not self._stop.is_set():
            try:
                message = await self.codex.next_server_request(timeout=0.5)
                await self.handle_server_request(message)
            except (CodexAppServerError, asyncio.TimeoutError):
                continue
            except Exception:
                continue

    async def _refresh_loop(self) -> None:
        status_elapsed = 0.0
        quota_elapsed = 0.0
        interval = min(self.status_refresh_seconds, self.quota_refresh_seconds, 1.0)
        while not self._stop.is_set():
            await asyncio.sleep(interval)
            status_elapsed += interval
            quota_elapsed += interval
            if status_elapsed >= self.status_refresh_seconds:
                status_elapsed = 0.0
                with contextlib.suppress(Exception):
                    await self._publish_state()
            if quota_elapsed >= self.quota_refresh_seconds:
                quota_elapsed = 0.0
                with contextlib.suppress(Exception):
                    await self._refresh_quota()

    async def _approval_timeout_loop(self) -> None:
        while not self._stop.is_set():
            await asyncio.sleep(min(0.25, self.approval_ttl_seconds))
            current = time.monotonic()
            expired = [
                key for key, pending in self.pending_approvals.items()
                if pending.expires_at <= current
            ]
            for key in expired:
                with contextlib.suppress(Exception):
                    await self._resolve_approval(key, approve=False, outcome="expired")

    async def handle_notification(self, message: Mapping[str, object]) -> None:
        method = _string(message.get("method"))
        if method is None:
            return
        params = _mapping(message.get("params", {}), "notification params")
        if self.console_event is not None:
            with contextlib.suppress(Exception):
                self.console_event(message)
        mapped = _notification_status(method, params)
        if mapped is None:
            return
        status, task_id, turn_id = mapped
        self.reducer.update(task_id, status, turn_id=turn_id, detail=method.split("/")[-1])
        await self._publish_state()
        if status in {"ready", "blocked"}:
            await self._open_terminal_thread(task_id, turn_id or method)

    async def _open_terminal_thread(self, task_id: str | None, terminal_id: str) -> None:
        if self.open_thread is None or not task_id or not self.managed_task(task_id):
            return
        key = (task_id, terminal_id)
        if key in self._opened_turns:
            return
        try:
            await self.open_thread(codex_thread_url(task_id))
        except Exception:
            return
        self._opened_turns.add(key)

    async def handle_server_request(self, message: Mapping[str, object]) -> None:
        method = _string(message.get("method"))
        if method is None:
            return
        params = _mapping(message.get("params", {}), "server request params")
        if self.console_event is not None:
            with contextlib.suppress(Exception):
                self.console_event(message)
        mapped = _request_detail(method, params)
        if mapped is None:
            return
        status, subtype, detail, task_id = mapped
        request_id = str(message.get("id", "request"))[:32]
        self.pending_requests[request_id] = method
        self.reducer.update(task_id, status, subtype=subtype, detail=detail)
        await self._publish_state()
        await self._register_approval(message, method, params, task_id)

    async def _register_approval(
        self,
        message: Mapping[str, object],
        method: str,
        params: Mapping[str, object],
        task_id: str | None,
    ) -> None:
        if self.publish_approval is None:
            self._console_approval("skipped", task_id, "publisher unavailable")
            return
        if method not in _BOARD_APPROVAL_METHODS:
            self._console_approval("skipped", task_id, "unsupported request")
            return
        if not self.managed_task(task_id):
            self._console_approval("skipped", task_id, "unmanaged task")
            return
        decisions = params.get("availableDecisions")
        approve_decision = "accept"
        deny_decision = "decline"
        if isinstance(decisions, list):
            named_decisions = {value for value in decisions if isinstance(value, str)}
            approve_decision = "accept" if "accept" in named_decisions else ""
            deny_decision = "decline" if "decline" in named_decisions else (
                "cancel" if "cancel" in named_decisions else ""
            )
            if not approve_decision or not deny_decision:
                self._console_approval("skipped", task_id, "unsupported decisions")
                return
        request_id = message.get("id")
        if request_id is None:
            self._console_approval("skipped", task_id, "missing request id")
            return
        key = _approval_key(request_id)
        pending = PendingApproval(
            key=key,
            request_id=request_id,
            method=method,
            task_id=task_id,
            summary=_approval_summary(method, params),
            expires_at=time.monotonic() + self.approval_ttl_seconds,
            approve_decision=approve_decision,
            deny_decision=deny_decision,
        )
        async with self._approval_lock:
            if key in self.pending_approvals:
                return
            self.pending_approvals[key] = pending
        try:
            await self.publish_approval(_approval_payload(pending, self.project_name))
        except Exception:
            async with self._approval_lock:
                self.pending_approvals.pop(key, None)
            self._console_approval("error", task_id)
            raise
        self._console_approval("registered", task_id)

    def _console_approval(self, status: str, task_id: str | None, reason: str | None = None) -> None:
        if self.console_event is None:
            return
        params: dict[str, object] = {"threadId": task_id}
        if reason:
            params["reason"] = reason
        with contextlib.suppress(Exception):
            self.console_event({"method": f"pet/approval/{status}", "params": params})

    async def resolve_approval(self, key: str, approve: bool) -> bool:
        return await self._resolve_approval(key, approve=approve, outcome="accepted" if approve else "declined")

    async def _resolve_approval(self, key: str, *, approve: bool, outcome: str) -> bool:
        async with self._approval_lock:
            pending = self.pending_approvals.pop(key, None)
        if pending is None:
            return False
        expired = pending.expires_at <= time.monotonic()
        if expired:
            approve = False
            outcome = "expired"
        try:
            decision = pending.approve_decision if approve else pending.deny_decision
            await self.codex.respond(pending.request_id, {"decision": decision})
        except Exception:
            self.reducer.remove(pending.task_id)
            self.reducer.update(pending.task_id, "blocked", subtype="approval", detail="computer approval")
            with contextlib.suppress(Exception):
                await self._publish_state(force=True)
            return False
        self.pending_requests.pop(str(pending.request_id)[:32], None)
        self.reducer.remove(pending.task_id)
        self.reducer.update(pending.task_id, "running", detail="approval handled")
        await self._publish_state(force=True)
        if self.publish_approval is not None:
            await self.publish_approval(_approval_payload(pending, self.project_name, status=outcome))
        return True

    async def observe_external(
        self,
        status: str,
        *,
        task_id: str | None = None,
        subtype: str | None = None,
        detail: str | None = None,
        ttl_ms: int = DEFAULT_TTL_MS,
    ) -> None:
        """Merge trusted Bridge/Hook state into the same reducer as app-server events."""
        self.reducer.update(
            task_id,
            status,
            subtype=subtype,
            detail=detail,
            ttl_ms=ttl_ms,
        )
        await self._publish_state()

    async def _publish_state(self, *, force: bool = False) -> None:
        value = self.reducer.current()
        signature = (value.task_id, value.status, value.subtype, value.detail, value.turn_id)
        if not force and signature == self._last_state_signature:
            # Refreshing the TTL is intentional: a long-running turn must not expire on the board.
            pass
        self._last_state_signature = signature
        await self.publish_state(
            value.status,
            task_id=value.task_id,
            subtype=value.subtype,
            detail=value.detail,
            ttl_ms=DEFAULT_TTL_MS,
        )

    async def _refresh_quota(self) -> None:
        try:
            reading = await self.usage_provider.fetch()
        except Exception:
            reading = UsageReading("codex-app-server", "unavailable", None, "unavailable")
        if reading.snapshot is not None:
            self.last_quota = reading.snapshot
        await self.publish_quota(quota_payload(reading, self.last_quota))


class _FakeEvents:
    def __init__(self) -> None:
        self.notifications: asyncio.Queue[dict[str, object]] = asyncio.Queue()
        self.requests: asyncio.Queue[dict[str, object]] = asyncio.Queue()
        self.responses: list[tuple[object, object]] = []

    async def next_notification(self, *, timeout: float | None = None) -> dict[str, object]:
        return await asyncio.wait_for(self.notifications.get(), timeout=timeout)

    async def next_server_request(self, *, timeout: float | None = None) -> dict[str, object]:
        return await asyncio.wait_for(self.requests.get(), timeout=timeout)

    async def respond(self, request_id: object, result: object) -> None:
        self.responses.append((request_id, result))


class _FakeUsage:
    def __init__(self, reading: UsageReading) -> None:
        self.reading = reading

    async def fetch(self) -> UsageReading:
        return self.reading


async def _self_test_async() -> None:
    base = 1_900_000_000_000
    reducer = TaskStateReducer()
    reducer.update("thr_1", "running", now=base)
    reducer.update("thr_2", "needs_input", subtype="credential", now=base + 1)
    assert reducer.current(now=base + 2).task_id == "thr_2"
    assert reducer.current(now=base + DEFAULT_TTL_MS + 2).status == "ready"

    snapshot = RateLimitSnapshot(None, None, "codex", "plus", 123)
    reading = UsageReading("codex-app-server", "live", snapshot, None)
    encoded = quota_payload(reading)
    assert len(encoded.encode("utf-8")) <= QUOTA_MAX_BYTES and '"status":"live"' in encoded
    stale = quota_payload(UsageReading("codex-app-server", "unavailable", None, "auth required"), snapshot)
    assert '"status":"stale"' in stale and '"error":"auth"' in stale

    events = _FakeEvents()
    states: list[dict[str, object]] = []
    quotas: list[str] = []
    opened: list[str] = []

    async def publish_state(status: str, **kwargs: object) -> None:
        states.append({"status": status, **kwargs})

    async def publish_quota(text: str) -> None:
        quotas.append(text)

    async def open_thread(url: str) -> None:
        opened.append(url)

    service = CodexPetStatusService(
        codex=events,
        publish_state=publish_state,
        publish_quota=publish_quota,
        managed_task=lambda task_id: task_id == "thr_1",
        open_thread=open_thread,
        usage_provider=_FakeUsage(reading),
        status_refresh_seconds=0.02,
        quota_refresh_seconds=0.05,
    )
    await service.start()
    assert states[0].get("status") == "ready"
    await service.handle_notification(
        {"method": "turn/started", "params": {"threadId": "thr_1", "turnId": "turn_1"}}
    )
    assert opened == []
    await service.handle_notification(
        {
            "method": "turn/completed",
            "params": {"threadId": "thr_1", "turn": {"id": "turn_1", "status": "completed"}},
        }
    )
    assert opened == ["codex://threads/thr_1"]
    await events.notifications.put({"method": "turn/started", "params": {"threadId": "thr_1", "turnId": "turn_1"}})
    await events.requests.put({
        "id": 9,
        "method": "item/tool/requestUserInput",
        "params": {"threadId": "thr_1", "questions": [{"isSecret": True}]},
    })
    await asyncio.sleep(0.04)
    assert any(value.get("status") == "running" for value in states)
    assert any(value.get("subtype") == "credential" for value in states)
    assert quotas and '"status":"live"' in quotas[0]
    await service.observe_external("needs_input", subtype="approval", detail="manual approval")
    observed = len(states)
    await service._publish_state()
    assert all(value.get("status") == "needs_input" for value in states[observed - 1 :])
    await service.observe_external("ready", detail="turn ready")
    assert states[-1].get("status") == "needs_input"
    await service.stop()

    approval_events = _FakeEvents()
    approval_frames: list[str] = []

    async def publish_approval(text: str) -> None:
        approval_frames.append(text)

    approvals = CodexPetStatusService(
        codex=approval_events,
        publish_state=publish_state,
        publish_quota=publish_quota,
        publish_approval=publish_approval,
        managed_task=lambda task_id: task_id == "thr_owned",
        project_name="黄山派 pet workspace",
        usage_provider=_FakeUsage(reading),
        approval_ttl_seconds=0.01,
    )
    secret = "api_key_should_not_reach_board"
    await approvals.handle_server_request({
        "id": "approval-request-id-that-needs-hashing",
        "method": "item/commandExecution/requestApproval",
        "params": {
            "threadId": "thr_owned",
            "command": f"git -c credential={secret} status",
            "availableDecisions": ["accept", "decline"],
        },
    })
    assert len(approval_frames) == 1 and len(approval_frames[0].encode("utf-8")) <= APPROVAL_MAX_BYTES
    assert secret not in approval_frames[0] and '"summary":"git command"' in approval_frames[0]
    approval_id = json.loads(approval_frames[0])["id"]
    assert await approvals.resolve_approval(approval_id, True) is True
    assert await approvals.resolve_approval(approval_id, False) is False
    assert approval_events.responses[-1][1] == {"decision": "accept"}
    assert json.loads(approval_frames[-1])["status"] == "accepted"

    await approvals.handle_server_request({
        "id": 22,
        "method": "item/fileChange/requestApproval",
        "params": {"threadId": "thr_other", "availableDecisions": ["accept", "decline"]},
    })
    assert len(approval_frames) == 2
    await approvals.handle_server_request({
        "id": 23,
        "method": "item/permissions/requestApproval",
        "params": {"threadId": "thr_owned", "availableDecisions": ["accept", "decline"]},
    })
    assert len(approval_frames) == 2

    await approvals.handle_server_request({
        "id": 24,
        "method": "item/fileChange/requestApproval",
        "params": {"threadId": "thr_owned", "availableDecisions": ["accept", "decline"]},
    })
    await asyncio.sleep(0.02)
    assert await approvals.resolve_approval("24", True) is True
    assert approval_events.responses[-1] == (24, {"decision": "decline"})
    assert json.loads(approval_frames[-1])["status"] == "expired"

    await approvals.handle_server_request({
        "id": 25,
        "method": "item/commandExecution/requestApproval",
        "params": {"threadId": "thr_owned", "availableDecisions": ["accept", "cancel"]},
    })
    assert await approvals.resolve_approval("25", False) is True
    assert approval_events.responses[-1] == (25, {"decision": "cancel"})

    fail_publish = False

    async def transient_state(_status: str, **_kwargs: object) -> None:
        if fail_publish:
            raise RuntimeError("device disconnected")

    async def transient_quota(_text: str) -> None:
        if fail_publish:
            raise RuntimeError("device disconnected")

    transient = CodexPetStatusService(
        codex=_FakeEvents(),
        publish_state=transient_state,
        publish_quota=transient_quota,
        usage_provider=_FakeUsage(reading),
        status_refresh_seconds=0.01,
        quota_refresh_seconds=0.01,
    )
    await transient.start()
    fail_publish = True
    await asyncio.sleep(0.03)
    await transient.stop()


def main() -> int:
    parser = argparse.ArgumentParser(description="Codex Pet app-server event and quota reducer")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("use --self-test")
    asyncio.run(_self_test_async())
    print("codex_pet_status self-test ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
