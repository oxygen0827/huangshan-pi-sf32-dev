#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import tempfile
import time
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path
from typing import Awaitable, Callable, Mapping, Protocol

from codex_pet_protocol import PetEnvelope, make_ack, now_ms
from voice_bridge_common import truncate_utf8


TASKS_CHANNEL = "pet.tasks"
TASK_SNAPSHOT_MAX_BYTES = 184
APPROVAL_TTL_MS = 120_000
TASK_RETENTION_MS = 2 * 60 * 60 * 1000
READY_RETENTION_MS = 30 * 60 * 1000
STALLED_TASK_MS = 8 * 60 * 1000
REFRESH_SECONDS = 5.0
_PRIORITY = {
    "connected": 0,
    "ready": 10,
    "running": 20,
    "blocked": 30,
    "needs_input": 40,
}


class MonitorDevice(Protocol):
    async def publish_state(
        self,
        status: str,
        *,
        task_id: str | None = None,
        subtype: str | None = None,
        detail: str | None = None,
        ttl_ms: int = 30_000,
    ) -> PetEnvelope: ...

    async def publish_project(self, project: str) -> None: ...
    async def publish_tasks(self, payload: str) -> None: ...
    async def publish_approval(self, payload: str) -> None: ...


class ApprovalExecutor(Protocol):
    async def resolve(self, session_id: str, approve: bool) -> tuple[bool, str]: ...


@dataclass
class DesktopTask:
    session_id: str
    turn_id: str | None
    project: str
    status: str
    detail: str
    updated_at_ms: int
    event_at_ms: int = 0
    completed_turn_id: str | None = None


@dataclass
class PendingApproval:
    request_id: str
    session_id: str
    created_at_ms: int


@dataclass
class RestoredDesktopState:
    seen_hook_event: bool
    pending: dict[str, PendingApproval]
    seen: OrderedDict[str, None]


def _safe_text(value: object, default: str, limit: int) -> str:
    if not isinstance(value, str):
        return default
    text = " ".join(value.split())
    return truncate_utf8(text, limit) or default


def _is_generic_approval_detail(value: str) -> bool:
    return value.strip().casefold() in {"approval required", "approval needed"}


def _is_soak_session(session_id: str) -> bool:
    return session_id.startswith("soak-")


class DesktopTaskRegistry:
    def __init__(self, *, clock_ms: Callable[[], int] = now_ms) -> None:
        self.clock_ms = clock_ms
        self.tasks: dict[str, DesktopTask] = {}
        self.selected_session: str | None = None

    def update(
        self,
        session_id: str,
        *,
        turn_id: str | None,
        project: str,
        status: str,
        detail: str,
    ) -> DesktopTask:
        if status not in _PRIORITY:
            raise ValueError(f"unsupported desktop task status: {status}")
        now = self.clock_ms()
        existing = self.tasks.get(session_id)
        task = DesktopTask(
            session_id=session_id,
            turn_id=turn_id or (existing.turn_id if existing else None),
            project=_safe_text(project, existing.project if existing else "Codex", 56),
            status=status,
            detail=_safe_text(detail, "Waiting for Codex", 64),
            updated_at_ms=now,
            event_at_ms=existing.event_at_ms if existing else now,
            completed_turn_id=existing.completed_turn_id if existing else None,
        )
        self.tasks[session_id] = task
        current = self.tasks.get(self.selected_session or "")
        if current is None or session_id == self.selected_session:
            self.selected_session = session_id
        elif _PRIORITY[status] > _PRIORITY[current.status]:
            self.selected_session = session_id
        elif _PRIORITY[status] == _PRIORITY[current.status] and now > current.updated_at_ms:
            self.selected_session = session_id
        self.prune()
        return task

    def apply_hook(
        self,
        session_id: str,
        *,
        turn_id: str | None,
        project: str,
        status: str,
        detail: str,
        event: str | None,
        event_at_ms: int,
    ) -> tuple[DesktopTask, bool]:
        existing = self.tasks.get(session_id)
        if existing is not None:
            if event_at_ms < existing.event_at_ms:
                return existing, False
            if event == "SessionStart" and existing.status != "connected":
                return existing, False
            if (
                existing.status in {"running", "needs_input", "blocked"}
                and turn_id is not None
                and existing.turn_id is not None
                and turn_id != existing.turn_id
                and event != "UserPromptSubmit"
            ):
                return existing, False
            if (
                existing.completed_turn_id is not None
                and turn_id == existing.completed_turn_id
                and event != "Stop"
            ):
                return existing, False
        task = self.update(
            session_id,
            turn_id=turn_id,
            project=project,
            status=status,
            detail=detail,
        )
        task.event_at_ms = event_at_ms
        if event == "Stop":
            task.completed_turn_id = turn_id or task.turn_id
        elif event == "UserPromptSubmit" and turn_id != task.completed_turn_id:
            task.completed_turn_id = None
        return task, True

    def prune(self) -> None:
        now = self.clock_ms()
        expired = []
        for session_id, task in self.tasks.items():
            if _is_soak_session(session_id) and task.status in {"ready", "connected"}:
                expired.append(session_id)
                continue
            if task.status == "running" and now - task.updated_at_ms >= STALLED_TASK_MS:
                task.status = "blocked"
                task.detail = "No progress - check computer"
            retention = READY_RETENTION_MS if task.status in {"ready", "connected"} else TASK_RETENTION_MS
            if now - task.updated_at_ms > retention:
                expired.append(session_id)
        for session_id in expired:
            self.tasks.pop(session_id, None)
        if self.selected_session not in self.tasks:
            self.selected_session = self._ordered_ids()[0] if self.tasks else None

    def remove(self, session_id: str) -> None:
        self.tasks.pop(session_id, None)
        if self.selected_session == session_id:
            self.selected_session = self._ordered_ids()[0] if self.tasks else None

    def select(self, delta: int) -> DesktopTask | None:
        self.prune()
        ordered = self._ordered_ids()
        if not ordered:
            self.selected_session = None
            return None
        try:
            index = ordered.index(self.selected_session or "")
        except ValueError:
            index = 0
        self.selected_session = ordered[(index + delta) % len(ordered)]
        return self.tasks[self.selected_session]

    def current(self) -> DesktopTask | None:
        self.prune()
        if self.selected_session and self.selected_session in self.tasks:
            return self.tasks[self.selected_session]
        ordered = self._ordered_ids()
        if not ordered:
            return None
        self.selected_session = ordered[0]
        return self.tasks[self.selected_session]

    def snapshot(
        self,
        *,
        approval_id: str | None = None,
        empty_detail: str = "No active tasks",
    ) -> str:
        task = self.current()
        active_count = sum(
            item.status in {"running", "needs_input", "blocked"}
            for item in self.tasks.values()
        )
        if task is None:
            value: dict[str, object] = {
                "v": 1,
                "p": "Codex",
                "st": "connected",
                "d": _safe_text(empty_detail, "No active tasks", 64),
                "i": 0,
                "n": 0,
                "ac": 0,
                "a": 0,
            }
        else:
            ordered = self._ordered_ids()
            snapshot_status = task.status
            snapshot_detail = task.detail
            if (
                approval_id is None
                and task.status == "needs_input"
                and _is_generic_approval_detail(task.detail)
            ):
                # PermissionRequest can be informational when desktop auto-approval is active.
                snapshot_status = "running"
                snapshot_detail = "Approval handled"
            value = {
                "v": 1,
                "p": task.project,
                "st": snapshot_status,
                "d": snapshot_detail,
                "i": ordered.index(task.session_id) + 1,
                "n": len(ordered),
                "ac": active_count,
                "a": int(approval_id is not None),
            }
            if approval_id is not None:
                value["r"] = approval_id
        encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > TASK_SNAPSHOT_MAX_BYTES:
            value["d"] = truncate_utf8(str(value["d"]), 28)
            encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > TASK_SNAPSHOT_MAX_BYTES:
            value["p"] = truncate_utf8(str(value["p"]), 24)
            encoded = json.dumps(value, ensure_ascii=False, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > TASK_SNAPSHOT_MAX_BYTES:
            raise ValueError("desktop task snapshot exceeds board transport limit")
        return encoded

    def _ordered_ids(self) -> list[str]:
        return [
            task.session_id
            for task in sorted(
                self.tasks.values(),
                key=lambda item: (_PRIORITY[item.status], item.updated_at_ms),
                reverse=True,
            )
        ]


class DesktopTaskStateStore:
    VERSION = 1

    def __init__(self, path: Path) -> None:
        self.path = path.expanduser()

    def load(self, registry: DesktopTaskRegistry) -> RestoredDesktopState:
        empty = RestoredDesktopState(False, {}, OrderedDict())
        if not self.path.exists():
            return empty
        try:
            value = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError):
            return empty
        if not isinstance(value, dict) or value.get("version") != self.VERSION:
            return empty
        records = value.get("tasks")
        if not isinstance(records, list) or len(records) > 128:
            return empty
        current = registry.clock_ms()
        for record in records:
            if not isinstance(record, dict):
                continue
            session_id = record.get("sessionId")
            status = record.get("status")
            updated_at = record.get("updatedAt")
            if (
                not isinstance(session_id, str)
                or not session_id
                or len(session_id) > 40
                or status not in _PRIORITY
                or isinstance(updated_at, bool)
                or not isinstance(updated_at, int)
                or updated_at < 0
            ):
                continue
            turn_id = record.get("turnId")
            if not isinstance(turn_id, str) or not turn_id:
                turn_id = None
            registry.tasks[session_id] = DesktopTask(
                session_id=session_id,
                turn_id=turn_id,
                project=_safe_text(record.get("project"), "Codex", 56),
                status=str(status),
                detail=_safe_text(record.get("detail"), "Codex updated", 64),
                updated_at_ms=min(updated_at, current),
                event_at_ms=min(
                    record.get("eventAt")
                    if isinstance(record.get("eventAt"), int) and not isinstance(record.get("eventAt"), bool)
                    else updated_at,
                    current,
                ),
                completed_turn_id=(
                    record.get("completedTurnId")
                    if isinstance(record.get("completedTurnId"), str) and record.get("completedTurnId")
                    else None
                ),
            )
        selected = value.get("selectedSession")
        if isinstance(selected, str) and selected in registry.tasks:
            registry.selected_session = selected
        registry.prune()
        pending: dict[str, PendingApproval] = {}
        approval_records = value.get("pendingApprovals", [])
        if isinstance(approval_records, list) and len(approval_records) <= 128:
            for record in approval_records:
                if not isinstance(record, dict):
                    continue
                request_id = record.get("requestId")
                session_id = record.get("sessionId")
                created_at = record.get("createdAt")
                task = registry.tasks.get(session_id) if isinstance(session_id, str) else None
                if (
                    not isinstance(request_id, str)
                    or len(request_id) != 22
                    or not request_id.startswith("a:")
                    or any(ch not in "0123456789abcdef" for ch in request_id[2:])
                    or task is None
                    or task.status != "needs_input"
                    or isinstance(created_at, bool)
                    or not isinstance(created_at, int)
                    or created_at < 0
                ):
                    continue
                pending[request_id] = PendingApproval(request_id, task.session_id, min(created_at, current))
        seen = OrderedDict()
        seen_values = value.get("seenMessageIds", [])
        if isinstance(seen_values, list):
            for message_id in seen_values[-256:]:
                if isinstance(message_id, str) and 0 < len(message_id) <= 40:
                    seen[message_id] = None
        return RestoredDesktopState(bool(value.get("seenHookEvent")), pending, seen)

    def save(
        self,
        registry: DesktopTaskRegistry,
        *,
        seen_hook_event: bool,
        pending: Mapping[str, PendingApproval],
        seen: Mapping[str, None],
    ) -> None:
        registry.prune()
        records = [
            {
                "sessionId": task.session_id,
                "turnId": task.turn_id,
                "project": task.project,
                "status": task.status,
                "detail": task.detail,
                "updatedAt": task.updated_at_ms,
                "eventAt": task.event_at_ms,
                "completedTurnId": task.completed_turn_id,
            }
            for task in registry.tasks.values()
        ]
        payload = json.dumps(
            {
                "version": self.VERSION,
                "selectedSession": registry.selected_session,
                "seenHookEvent": seen_hook_event,
                "pendingApprovals": [
                    {
                        "requestId": value.request_id,
                        "sessionId": value.session_id,
                        "createdAt": value.created_at_ms,
                    }
                    for value in pending.values()
                ],
                "seenMessageIds": list(seen)[-256:],
                "tasks": records,
            },
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        ) + "\n"
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(f".{self.path.name}.{os.getpid()}.tmp")
        try:
            temporary.write_text(payload, encoding="utf-8")
            os.chmod(temporary, 0o600)
            os.replace(temporary, self.path)
            os.chmod(self.path, 0o600)
        finally:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass


class SubprocessApprovalExecutor:
    def __init__(self, helper: Path) -> None:
        self.helper = helper.expanduser().resolve()

    async def resolve(self, session_id: str, approve: bool) -> tuple[bool, str]:
        if not self.helper.is_file():
            return False, "Desktop approval helper unavailable"
        process = await asyncio.create_subprocess_exec(
            str(self.helper),
            "--approve" if approve else "--deny",
            "--session",
            session_id,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        try:
            stdout, _ = await asyncio.wait_for(process.communicate(), timeout=8.0)
        except asyncio.TimeoutError:
            process.kill()
            await process.wait()
            return False, "Desktop approval timed out"
        try:
            value = json.loads(stdout.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            return False, "Desktop approval helper failed closed"
        if process.returncode == 0 and isinstance(value, dict) and value.get("ok") is True:
            return True, "Allowed on desktop" if approve else "Denied on desktop"
        reason = value.get("reason") if isinstance(value, dict) else None
        return False, _safe_text(reason, "Use the computer to approve", 64)


class CodexDesktopMonitor:
    def __init__(
        self,
        *,
        device: MonitorDevice,
        approval_executor: ApprovalExecutor,
        clock_ms: Callable[[], int] = now_ms,
        state_path: Path | None = None,
        managed_task: Callable[[str], bool] | None = None,
    ) -> None:
        self.device = device
        self.approval_executor = approval_executor
        self.clock_ms = clock_ms
        self.managed_task = managed_task or (lambda _session_id: False)
        self.registry = DesktopTaskRegistry(clock_ms=clock_ms)
        self.state_store = DesktopTaskStateStore(state_path) if state_path is not None else None
        restored = self.state_store.load(self.registry) if self.state_store else RestoredDesktopState(False, {}, OrderedDict())
        self.pending = {
            request_id: pending
            for request_id, pending in restored.pending.items()
            if self.managed_task(pending.session_id)
        }
        self.seen = restored.seen
        self._ack_sequence = max(1, now_ms() & 0xFFFFFFFF)
        self._refresh_task: asyncio.Task[None] | None = None
        self._publisher_task: asyncio.Task[None] | None = None
        self._publish_event = asyncio.Event()
        self.seen_hook_event = restored.seen_hook_event

    async def start(self) -> None:
        self._publisher_task = asyncio.create_task(self._publish_loop(), name="codex-pet-desktop-publisher")
        self._refresh_task = asyncio.create_task(self._refresh_loop(), name="codex-pet-desktop-refresh")
        self._publish_event.set()

    async def stop(self) -> None:
        tasks = [task for task in (self._refresh_task, self._publisher_task) if task is not None]
        self._refresh_task = None
        self._publisher_task = None
        for task in tasks:
            task.cancel()
        for task in tasks:
            try:
                await task
            except asyncio.CancelledError:
                pass

    async def handle(self, request: PetEnvelope, **_: object) -> PetEnvelope:
        payload = dict(request.payload or {})
        if request.kind != "action" or payload.get("action") != "hook_event":
            return make_ack(request, sequence=self._next_ack(), status="rejected", error="hook_event_required")
        if request.is_expired(self.clock_ms()):
            return make_ack(request, sequence=self._next_ack(), status="rejected", error="expired")
        if request.message_id in self.seen:
            return make_ack(
                request,
                sequence=self._next_ack(),
                status="duplicate",
                task_id=request.task_id,
                payload={"observed": True},
            )
        self.seen[request.message_id] = None
        while len(self.seen) > 256:
            self.seen.popitem(last=False)
        session_id = request.task_id
        status = payload.get("status")
        if not isinstance(session_id, str) or status not in _PRIORITY:
            return make_ack(request, sequence=self._next_ack(), status="rejected", error="invalid_hook_event")
        project = _safe_text(payload.get("project"), "Codex", 56)
        detail = _safe_text(payload.get("detail"), "Codex updated", 64)
        turn_id = payload.get("turnId") if isinstance(payload.get("turnId"), str) else None
        event = payload.get("event")
        _, applied = self.registry.apply_hook(
            session_id,
            turn_id=turn_id,
            project=project,
            status=str(status),
            detail=detail,
            event=event if isinstance(event, str) else None,
            event_at_ms=min(request.timestamp_ms, self.clock_ms()),
        )
        self.seen_hook_event = True
        if applied:
            print(f"Codex task: {project} | {status} | {detail}")
        if (
            applied
            and event == "PermissionRequest"
            and payload.get("approvalClass") == "managed_action"
            and self.managed_task(session_id)
        ):
            request_id = "a:" + hashlib.sha256(request.message_id.encode("utf-8")).hexdigest()[:20]
            self.pending[request_id] = PendingApproval(request_id, session_id, self.clock_ms())
        elif applied and event in {"UserPromptSubmit", "PreToolUse", "PostToolUse", "Stop"}:
            # A later lifecycle event proves that desktop-side approval is no longer pending.
            self._clear_session_approvals(session_id)
            if _is_soak_session(session_id):
                if event == "Stop":
                    self.registry.remove(session_id)
        self._persist_state()
        self._publish_event.set()
        return make_ack(
            request,
            sequence=self._next_ack(),
            status="accepted",
            task_id=session_id,
            payload={"observed": True},
        )

    async def resolve_approval(self, request_id: str, approve: bool) -> bool:
        await self._expire_approvals()
        pending = self.pending.get(request_id)
        if pending is None:
            self._persist_state()
            self._publish_event.set()
            return False
        self.pending.pop(request_id, None)
        self._persist_state()
        success, detail = await self.approval_executor.resolve(pending.session_id, approve)
        task = self.registry.tasks.get(pending.session_id)
        if success:
            if task is not None:
                self.registry.update(
                    pending.session_id,
                    turn_id=task.turn_id,
                    project=task.project,
                    status="running" if approve else "blocked",
                    detail=detail,
                )
        else:
            if task is not None:
                self.registry.update(
                    pending.session_id,
                    turn_id=task.turn_id,
                    project=task.project,
                    status="needs_input",
                    detail=detail,
                )
        self._persist_state()
        self._publish_event.set()
        return success

    async def select(self, delta: int) -> None:
        self.registry.select(delta)
        self._persist_state()
        self._publish_event.set()

    async def publish_selected(self) -> None:
        await self._expire_approvals()
        self._persist_state()
        task = self.registry.current()
        pending = self._pending_for(task.session_id if task else None)
        await self.device.publish_tasks(
            self.registry.snapshot(
                approval_id=pending.request_id if pending else None,
                empty_detail="No active tasks" if self.seen_hook_event else "Waiting for Codex events",
            )
        )

    async def _publish_loop(self) -> None:
        while True:
            await self._publish_event.wait()
            self._publish_event.clear()
            await asyncio.sleep(0.03)
            try:
                await self.publish_selected()
            except asyncio.CancelledError:
                raise
            except Exception:
                await asyncio.sleep(0.25)
                self._publish_event.set()

    async def _refresh_loop(self) -> None:
        while True:
            await asyncio.sleep(REFRESH_SECONDS)
            self._publish_event.set()

    def _pending_for(self, session_id: str | None) -> PendingApproval | None:
        if session_id is None:
            return None
        values = [value for value in self.pending.values() if value.session_id == session_id]
        return max(values, key=lambda item: item.created_at_ms) if values else None

    def _clear_session_approvals(self, session_id: str) -> None:
        for request_id in [key for key, value in self.pending.items() if value.session_id == session_id]:
            self.pending.pop(request_id, None)

    async def _expire_approvals(self) -> None:
        now = self.clock_ms()
        expired = [
            key for key, value in self.pending.items() if now - value.created_at_ms >= APPROVAL_TTL_MS
        ]
        values = [self.pending.pop(request_id) for request_id in expired]
        if values:
            self._persist_state()
        for pending in values:
            success, detail = await self.approval_executor.resolve(pending.session_id, False)
            task = self.registry.tasks.get(pending.session_id)
            if task is not None:
                self.registry.update(
                    pending.session_id,
                    turn_id=task.turn_id,
                    project=task.project,
                    status="blocked" if success else "needs_input",
                    detail=detail if success else "Approval expired - use computer",
                )
        if values:
            self._persist_state()

    def _next_ack(self) -> int:
        self._ack_sequence = 1 if self._ack_sequence >= 0xFFFFFFFF else self._ack_sequence + 1
        return self._ack_sequence

    def _persist_state(self) -> None:
        if self.state_store is not None:
            self.state_store.save(
                self.registry,
                seen_hook_event=self.seen_hook_event,
                pending=self.pending,
                seen=self.seen,
            )


class _FakeDevice:
    def __init__(self) -> None:
        self.tasks: list[str] = []
        self.approvals: list[str] = []

    async def publish_state(self, status: str, **kwargs: object) -> PetEnvelope:
        return PetEnvelope(
            kind="state",
            sequence=len(self.tasks) + 1,
            message_id=f"s:{len(self.tasks) + 1}",
            timestamp_ms=1,
            payload={"status": status},
        )

    async def publish_project(self, project: str) -> None:
        return None

    async def publish_tasks(self, payload: str) -> None:
        self.tasks.append(payload)

    async def publish_approval(self, payload: str) -> None:
        self.approvals.append(payload)


class _FakeApproval:
    def __init__(self, result: bool = True) -> None:
        self.result = result
        self.calls: list[tuple[str, bool]] = []

    async def resolve(self, session_id: str, approve: bool) -> tuple[bool, str]:
        self.calls.append((session_id, approve))
        return self.result, "resolved" if self.result else "Use the computer to approve"


async def self_test_async() -> None:
    clock = [1_000]
    registry = DesktopTaskRegistry(clock_ms=lambda: clock[0])
    registry.update("s1", turn_id="t1", project="alpha", status="running", detail="Thinking")
    clock[0] += 1
    registry.update("s2", turn_id="t2", project="beta", status="ready", detail="Done")
    assert registry.current() and registry.current().session_id == "s1"
    snapshot = json.loads(registry.snapshot())
    assert snapshot["st"] == "running" and snapshot["ac"] == 1
    clock[0] += 1
    registry.update("s1", turn_id="t1", project="alpha", status="needs_input", detail="Approval")
    assert registry.current() and registry.current().session_id == "s1"
    snapshot = json.loads(registry.snapshot())
    assert snapshot["ac"] == 1 and snapshot["n"] == 2
    assert len(json.dumps(snapshot, separators=(",", ":")).encode("utf-8")) <= TASK_SNAPSHOT_MAX_BYTES
    assert registry.select(1) and registry.current().session_id == "s2"
    clock[0] += STALLED_TASK_MS
    registry.prune()
    assert registry.tasks["s1"].status == "needs_input"
    registry.update("s3", turn_id="t3", project="gamma", status="running", detail="Thinking")
    clock[0] += STALLED_TASK_MS
    registry.prune()
    assert registry.tasks["s3"].status == "blocked"

    device = _FakeDevice()
    approval = _FakeApproval()
    monitor = CodexDesktopMonitor(
        device=device,
        approval_executor=approval,
        clock_ms=lambda: clock[0],
        managed_task=lambda session_id: session_id == "session-1",
    )
    await monitor.publish_selected()
    assert json.loads(device.tasks[-1])["d"] == "Waiting for Codex events"
    request = PetEnvelope(
        kind="action",
        sequence=7,
        message_id="hook:7:test",
        task_id="session-1",
        timestamp_ms=clock[0],
        payload={
            "action": "hook_event",
            "approvalClass": "managed_action",
            "status": "needs_input",
            "subtype": "approval",
            "detail": "Approval required",
            "project": "project-one",
            "turnId": "turn-1",
            "event": "PermissionRequest",
        },
    )
    ack = await monitor.handle(request)
    assert ack.payload and ack.payload.get("status") == "accepted"
    duplicate = await monitor.handle(request)
    assert duplicate.payload and duplicate.payload.get("status") == "duplicate"
    request_id = next(iter(monitor.pending))
    assert await monitor.resolve_approval(request_id, False)
    assert approval.calls == [("session-1", False)] and request_id not in monitor.pending
    await monitor.publish_selected()
    assert json.loads(device.tasks[-1])["a"] == 0

    failed = _FakeApproval(False)
    monitor = CodexDesktopMonitor(
        device=device,
        approval_executor=failed,
        clock_ms=lambda: clock[0],
        managed_task=lambda session_id: session_id == "session-1",
    )
    await monitor.handle(request)
    request_id = next(iter(monitor.pending))
    assert not await monitor.resolve_approval(request_id, True)
    assert request_id not in monitor.pending
    assert not await monitor.resolve_approval(request_id, True)
    assert failed.calls == [("session-1", True)]
    await monitor.publish_selected()
    assert json.loads(device.tasks[-1])["a"] == 0

    timeout_executor = _FakeApproval()
    timeout_monitor = CodexDesktopMonitor(
        device=device,
        approval_executor=timeout_executor,
        clock_ms=lambda: clock[0],
        managed_task=lambda session_id: session_id == "session-1",
    )
    timeout_request = PetEnvelope(
        kind="action",
        sequence=8,
        message_id="hook:8:timeout",
        task_id="session-1",
        timestamp_ms=clock[0],
        payload=request.payload,
    )
    await timeout_monitor.handle(timeout_request)
    timeout_id = next(iter(timeout_monitor.pending))
    clock[0] += APPROVAL_TTL_MS
    await timeout_monitor.publish_selected()
    assert timeout_id not in timeout_monitor.pending
    assert timeout_executor.calls == [("session-1", False)]

    unmanaged = CodexDesktopMonitor(
        device=device,
        approval_executor=_FakeApproval(),
        clock_ms=lambda: clock[0],
    )
    unmanaged_request = PetEnvelope(
        kind="action",
        sequence=9,
        message_id="hook:9:unmanaged",
        task_id="session-unmanaged",
        timestamp_ms=clock[0],
        payload=request.payload,
    )
    await unmanaged.handle(unmanaged_request)
    await unmanaged.publish_selected()
    assert not unmanaged.pending and json.loads(device.tasks[-1])["a"] == 0

    sensitive_monitor = CodexDesktopMonitor(
        device=device,
        approval_executor=_FakeApproval(),
        clock_ms=lambda: clock[0],
        managed_task=lambda session_id: session_id == "session-sensitive",
    )
    sensitive_request = PetEnvelope(
        kind="action",
        sequence=10,
        message_id="hook:10:sensitive",
        task_id="session-sensitive",
        timestamp_ms=clock[0],
        payload={
            "action": "hook_event",
            "status": "needs_input",
            "detail": "Approval required",
            "project": "project-one",
            "turnId": "turn-sensitive",
            "event": "PermissionRequest",
        },
    )
    await sensitive_monitor.handle(sensitive_request)
    await sensitive_monitor.publish_selected()
    automatic_snapshot = json.loads(device.tasks[-1])
    assert not sensitive_monitor.pending and automatic_snapshot["a"] == 0
    assert automatic_snapshot["st"] == "running" and automatic_snapshot["d"] == "Approval handled"

    resumed_device = _FakeDevice()
    resumed_monitor = CodexDesktopMonitor(
        device=resumed_device,
        approval_executor=_FakeApproval(),
        clock_ms=lambda: clock[0],
        managed_task=lambda session_id: session_id == "session-resumed",
    )
    pending_request = PetEnvelope(
        kind="action",
        sequence=11,
        message_id="hook:11:approval",
        task_id="session-resumed",
        timestamp_ms=clock[0],
        payload={
            **request.payload,
            "project": "resumed-project",
            "turnId": "turn-resumed",
        },
    )
    await resumed_monitor.handle(pending_request)
    assert resumed_monitor.pending
    resumed_request = PetEnvelope(
        kind="action",
        sequence=12,
        message_id="hook:12:pretool",
        task_id="session-resumed",
        timestamp_ms=clock[0] + 1,
        payload={
            "action": "hook_event",
            "status": "running",
            "detail": "Using a tool",
            "project": "resumed-project",
            "turnId": "turn-resumed",
            "event": "PreToolUse",
        },
    )
    await resumed_monitor.handle(resumed_request)
    await resumed_monitor.publish_selected()
    resumed_snapshot = json.loads(resumed_device.tasks[-1])
    assert not resumed_monitor.pending and resumed_snapshot["a"] == 0
    assert resumed_snapshot["st"] == "running" and resumed_snapshot["d"] == "Using a tool"

    with tempfile.TemporaryDirectory(prefix="codex-pet-monitor-") as temporary:
        state_path = Path(temporary) / "desktop-state.json"
        device = _FakeDevice()
        monitor = CodexDesktopMonitor(
            device=device,
            approval_executor=_FakeApproval(),
            clock_ms=lambda: clock[0],
            state_path=state_path,
            managed_task=lambda session_id: session_id == "session-restart",
        )
        running = PetEnvelope(
            kind="action",
            sequence=8,
            message_id="hook:8:running",
            task_id="session-restart",
            timestamp_ms=clock[0],
            payload={
                "action": "hook_event",
                "status": "running",
                "detail": "Thinking",
                "project": "restart-project",
                "turnId": "turn-restart",
                "event": "UserPromptSubmit",
            },
        )
        assert (await monitor.handle(running)).payload["status"] == "accepted"
        approval_request = PetEnvelope(
            kind="action",
            sequence=9,
            message_id="hook:9:approval",
            task_id="session-restart",
            timestamp_ms=clock[0],
            payload={
                "action": "hook_event",
                "approvalClass": "managed_action",
                "status": "needs_input",
                "subtype": "approval",
                "detail": "Approval required",
                "project": "restart-project",
                "turnId": "turn-restart",
                "event": "PermissionRequest",
            },
        )
        assert (await monitor.handle(approval_request)).payload["status"] == "accepted"
        restarted_device = _FakeDevice()
        restarted = CodexDesktopMonitor(
            device=restarted_device,
            approval_executor=_FakeApproval(),
            clock_ms=lambda: clock[0],
            state_path=state_path,
            managed_task=lambda session_id: session_id == "session-restart",
        )
        await restarted.publish_selected()
        restored = json.loads(restarted_device.tasks[-1])
        assert restored["n"] == 1 and restored["ac"] == 1
        assert restored["st"] == "needs_input" and restored["p"] == "restart-project"
        assert restored["a"] == 1 and restored["r"].startswith("a:")
        duplicate = await restarted.handle(approval_request)
        assert duplicate.payload["status"] == "duplicate"

        clock[0] += APPROVAL_TTL_MS
        restart_timeout_executor = _FakeApproval()
        expired_restart_device = _FakeDevice()
        expired_restart = CodexDesktopMonitor(
            device=expired_restart_device,
            approval_executor=restart_timeout_executor,
            clock_ms=lambda: clock[0],
            state_path=state_path,
            managed_task=lambda session_id: session_id == "session-restart",
        )
        await expired_restart.publish_selected()
        assert restart_timeout_executor.calls == [("session-restart", False)]
        assert json.loads(expired_restart_device.tasks[-1])["a"] == 0

        stale_path = Path(temporary) / "stale-state.json"
        stale_device = _FakeDevice()
        stale_monitor = CodexDesktopMonitor(
            device=stale_device,
            approval_executor=_FakeApproval(),
            clock_ms=lambda: clock[0],
            state_path=stale_path,
        )
        stale_running = PetEnvelope(
            kind="action",
            sequence=10,
            message_id="hook:10:stale",
            task_id="session-stale",
            timestamp_ms=clock[0],
            payload={
                "action": "hook_event",
                "status": "running",
                "detail": "Thinking",
                "project": "stale-project",
                "turnId": "turn-stale",
                "event": "UserPromptSubmit",
            },
        )
        await stale_monitor.handle(stale_running)
        clock[0] += STALLED_TASK_MS
        stalled_device = _FakeDevice()
        stalled = CodexDesktopMonitor(
            device=stalled_device,
            approval_executor=_FakeApproval(),
            clock_ms=lambda: clock[0],
            state_path=stale_path,
        )
        await stalled.publish_selected()
        assert json.loads(stalled_device.tasks[-1])["st"] == "blocked"
        clock[0] += TASK_RETENTION_MS
        expired_device = _FakeDevice()
        expired = CodexDesktopMonitor(
            device=expired_device,
            approval_executor=_FakeApproval(),
            clock_ms=lambda: clock[0],
            state_path=stale_path,
        )
        await expired.publish_selected()
        expired_snapshot = json.loads(expired_device.tasks[-1])
        assert expired_snapshot["n"] == 0 and expired_snapshot["ac"] == 0

    ordered_device = _FakeDevice()
    ordered_monitor = CodexDesktopMonitor(
        device=ordered_device,
        approval_executor=_FakeApproval(),
        clock_ms=lambda: clock[0],
    )

    async def ordered_hook(sequence: int, event: str, status: str, turn_id: str) -> None:
        request = PetEnvelope(
            kind="action",
            sequence=sequence,
            message_id=f"hook:{sequence}:{event}",
            task_id="session-order",
            timestamp_ms=clock[0] + sequence,
            payload={
                "action": "hook_event",
                "status": status,
                "detail": event,
                "project": "order-project",
                "turnId": turn_id,
                "event": event,
            },
        )
        assert (await ordered_monitor.handle(request)).payload["status"] == "accepted"

    await ordered_hook(20, "UserPromptSubmit", "running", "turn-1")
    await ordered_hook(21, "Stop", "ready", "turn-1")
    await ordered_hook(22, "PostToolUse", "running", "turn-1")
    await ordered_monitor.publish_selected()
    assert json.loads(ordered_device.tasks[-1])["st"] == "ready"
    await ordered_hook(23, "UserPromptSubmit", "running", "turn-2")
    await ordered_hook(24, "Stop", "ready", "turn-1")
    await ordered_monitor.publish_selected()
    assert json.loads(ordered_device.tasks[-1])["st"] == "running"

    soak_device = _FakeDevice()
    soak_monitor = CodexDesktopMonitor(
        device=soak_device,
        approval_executor=_FakeApproval(),
        clock_ms=lambda: clock[0],
    )

    async def soak_hook(sequence: int, event: str, status: str) -> None:
        request = PetEnvelope(
            kind="action",
            sequence=sequence,
            message_id=f"hook:{sequence}:{event}:soak",
            task_id="soak-self-test-a",
            timestamp_ms=clock[0] + sequence,
            payload={
                "action": "hook_event",
                "status": status,
                "detail": event,
                "project": "soak-project",
                "turnId": "turn-soak",
                "event": event,
            },
        )
        assert (await soak_monitor.handle(request)).payload["status"] == "accepted"

    await soak_hook(30, "UserPromptSubmit", "running")
    await soak_monitor.publish_selected()
    assert json.loads(soak_device.tasks[-1])["n"] == 1
    await soak_hook(31, "Stop", "ready")
    await soak_monitor.publish_selected()
    soak_snapshot = json.loads(soak_device.tasks[-1])
    assert soak_snapshot["n"] == 0 and soak_snapshot["ac"] == 0

    restored_soak = DesktopTaskRegistry(clock_ms=lambda: clock[0])
    restored_soak.tasks["soak-stale-a"] = DesktopTask(
        "soak-stale-a", "turn-stale", "soak", "ready", "Done", clock[0]
    )
    restored_soak.selected_session = "soak-stale-a"
    restored_soak.prune()
    assert not restored_soak.tasks and restored_soak.selected_session is None


def main() -> int:
    parser = argparse.ArgumentParser(description="Codex Desktop task monitor for Huangshan Codex Pet")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        asyncio.run(self_test_async())
        print("codex_pet_monitor self-test ok")
        return 0
    parser.error("run this module through codex_pet_bridge.py --mode monitor")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
