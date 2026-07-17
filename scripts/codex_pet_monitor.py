#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
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
    "running": 10,
    "ready": 20,
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


@dataclass
class PendingApproval:
    request_id: str
    session_id: str
    created_at_ms: int


def _safe_text(value: object, default: str, limit: int) -> str:
    if not isinstance(value, str):
        return default
    text = " ".join(value.split())
    return truncate_utf8(text, limit) or default


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

    def prune(self) -> None:
        now = self.clock_ms()
        expired = []
        for session_id, task in self.tasks.items():
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

    def snapshot(self, *, approval_id: str | None = None) -> str:
        task = self.current()
        if task is None:
            value: dict[str, object] = {
                "v": 1,
                "p": "Codex",
                "st": "connected",
                "d": "No active tasks",
                "i": 0,
                "n": 0,
                "a": 0,
            }
        else:
            ordered = self._ordered_ids()
            value = {
                "v": 1,
                "p": task.project,
                "st": task.status,
                "d": task.detail,
                "i": ordered.index(task.session_id) + 1,
                "n": len(ordered),
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
    ) -> None:
        self.device = device
        self.approval_executor = approval_executor
        self.clock_ms = clock_ms
        self.registry = DesktopTaskRegistry(clock_ms=clock_ms)
        self.pending: dict[str, PendingApproval] = {}
        self.seen: OrderedDict[str, None] = OrderedDict()
        self._ack_sequence = max(1, now_ms() & 0xFFFFFFFF)
        self._refresh_task: asyncio.Task[None] | None = None
        self._publisher_task: asyncio.Task[None] | None = None
        self._publish_event = asyncio.Event()

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
        self.registry.update(
            session_id,
            turn_id=turn_id,
            project=project,
            status=str(status),
            detail=detail,
        )
        if event == "PermissionRequest":
            request_id = "a:" + hashlib.sha256(request.message_id.encode("utf-8")).hexdigest()[:20]
            self.pending[request_id] = PendingApproval(request_id, session_id, self.clock_ms())
        elif event == "Stop":
            self._clear_session_approvals(session_id)
        self._publish_event.set()
        return make_ack(
            request,
            sequence=self._next_ack(),
            status="accepted",
            task_id=session_id,
            payload={"observed": True},
        )

    async def resolve_approval(self, request_id: str, approve: bool) -> bool:
        self._expire_approvals()
        pending = self.pending.get(request_id)
        if pending is None:
            self._publish_event.set()
            return False
        success, detail = await self.approval_executor.resolve(pending.session_id, approve)
        if success:
            self.pending.pop(request_id, None)
            task = self.registry.tasks.get(pending.session_id)
            if task is not None:
                self.registry.update(
                    pending.session_id,
                    turn_id=task.turn_id,
                    project=task.project,
                    status="running",
                    detail=detail,
                )
        else:
            task = self.registry.tasks.get(pending.session_id)
            if task is not None:
                self.registry.update(
                    pending.session_id,
                    turn_id=task.turn_id,
                    project=task.project,
                    status="needs_input",
                    detail=detail,
                )
        self._publish_event.set()
        return success

    async def select(self, delta: int) -> None:
        self.registry.select(delta)
        self._publish_event.set()

    async def publish_selected(self) -> None:
        self._expire_approvals()
        task = self.registry.current()
        pending = self._pending_for(task.session_id if task else None)
        await self.device.publish_tasks(
            self.registry.snapshot(approval_id=pending.request_id if pending else None)
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

    def _expire_approvals(self) -> None:
        now = self.clock_ms()
        for request_id in [
            key for key, value in self.pending.items() if now - value.created_at_ms >= APPROVAL_TTL_MS
        ]:
            self.pending.pop(request_id, None)

    def _next_ack(self) -> int:
        self._ack_sequence = 1 if self._ack_sequence >= 0xFFFFFFFF else self._ack_sequence + 1
        return self._ack_sequence


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
    assert registry.current() and registry.current().session_id == "s2"
    clock[0] += 1
    registry.update("s1", turn_id="t1", project="alpha", status="needs_input", detail="Approval")
    assert registry.current() and registry.current().session_id == "s1"
    assert len(registry.snapshot().encode("utf-8")) <= TASK_SNAPSHOT_MAX_BYTES
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
    monitor = CodexDesktopMonitor(device=device, approval_executor=approval, clock_ms=lambda: clock[0])
    request = PetEnvelope(
        kind="action",
        sequence=7,
        message_id="hook:7:test",
        task_id="session-1",
        timestamp_ms=clock[0],
        payload={
            "action": "hook_event",
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
    monitor = CodexDesktopMonitor(device=device, approval_executor=failed, clock_ms=lambda: clock[0])
    await monitor.handle(request)
    request_id = next(iter(monitor.pending))
    assert not await monitor.resolve_approval(request_id, True)
    assert request_id in monitor.pending
    await monitor.publish_selected()
    assert json.loads(device.tasks[-1])["r"] == request_id
    clock[0] += APPROVAL_TTL_MS
    assert not await monitor.resolve_approval(request_id, True)
    assert request_id not in monitor.pending


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
