#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import os
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Mapping
from unittest.mock import patch

from codex_pet_hook import DEFAULT_SOCKET, hook_envelope, send
from codex_pet_mcp import BridgeCaller, MCPError


VALID_BOARD_STATES = {
    "connected",
    "running",
    "needs_input",
    "blocked",
    "ready",
    "disconnected",
}
ANIMATION_LOAD_GRACE_MS = 15_000


@dataclass
class SoakMetrics:
    started_at_ms: int
    samples: int = 0
    failures: int = 0
    disconnects: int = 0
    reconnects: int = 0
    delivery_failures: int = 0
    exercise_failures: int = 0
    exercise_cycles: int = 0
    state_errors: int = 0
    animation_errors: int = 0
    max_reconnect_ms: int = 0
    total_reconnect_ms: int = 0
    outage_started_at_ms: int | None = None
    rss_min_kib: int | None = None
    rss_max_kib: int | None = None
    rss_epoch_pid: int | None = None
    rss_epoch_min_kib: int | None = None
    rss_epoch_max_kib: int | None = None
    rss_max_growth_kib: int = 0
    sleep_gaps: int = 0
    animation_zero_started_at_ms: int | None = None
    animation_fault_reported: bool = False
    last_ui_ticks: int | None = None
    last_ui_tick_at_ms: int | None = None
    ui_tick_stalls: int = 0
    dropped_flows: int = 0
    max_queued_flows: int = 0

    def observe_status(self, at_ms: int, status: Mapping[str, object]) -> None:
        self.samples += 1
        connected = status.get("connected") == 1
        state = status.get("state")
        if connected and self.outage_started_at_ms is not None:
            duration = max(0, at_ms - self.outage_started_at_ms)
            self.reconnects += 1
            self.total_reconnect_ms += duration
            self.max_reconnect_ms = max(self.max_reconnect_ms, duration)
            self.outage_started_at_ms = None
        elif not connected:
            self._begin_outage(at_ms)
        if (
            state not in VALID_BOARD_STATES
            or (connected and state == "disconnected")
            or (not connected and state != "disconnected")
        ):
            self.state_errors += 1
        custom = status.get("custom")
        frames = status.get("frames")
        if custom == 1 and connected:
            if not isinstance(frames, int) or isinstance(frames, bool):
                self.animation_errors += 1
            elif frames == 0:
                if self.animation_zero_started_at_ms is None:
                    self.animation_zero_started_at_ms = at_ms
                elif (
                    not self.animation_fault_reported
                    and at_ms - self.animation_zero_started_at_ms > ANIMATION_LOAD_GRACE_MS
                ):
                    self.animation_errors += 1
                    self.animation_fault_reported = True
            else:
                self.animation_zero_started_at_ms = None
                self.animation_fault_reported = False
                if frames < 2:
                    self.animation_errors += 1
        else:
            self.animation_zero_started_at_ms = None
            self.animation_fault_reported = False
        ui_ticks = status.get("uiTicks")
        if connected and isinstance(ui_ticks, int) and not isinstance(ui_ticks, bool):
            if (
                self.last_ui_ticks is not None
                and self.last_ui_tick_at_ms is not None
                and at_ms - self.last_ui_tick_at_ms >= 1_000
                and ui_ticks == self.last_ui_ticks
            ):
                self.ui_tick_stalls += 1
            self.last_ui_ticks = ui_ticks
            self.last_ui_tick_at_ms = at_ms
        elif not connected:
            self.last_ui_ticks = None
            self.last_ui_tick_at_ms = None
        dropped_flows = status.get("droppedFlows")
        if isinstance(dropped_flows, int) and not isinstance(dropped_flows, bool):
            self.dropped_flows = max(self.dropped_flows, dropped_flows)
        queued_flows = status.get("queuedFlows")
        if isinstance(queued_flows, int) and not isinstance(queued_flows, bool):
            self.max_queued_flows = max(self.max_queued_flows, queued_flows)

    def observe_failure(self, at_ms: int, _error: str) -> None:
        self.samples += 1
        self.failures += 1
        self._begin_outage(at_ms)

    def observe_rss(self, rss_kib: int | None, pid: int | None = None) -> None:
        if rss_kib is None or rss_kib <= 0:
            return
        self.rss_min_kib = rss_kib if self.rss_min_kib is None else min(self.rss_min_kib, rss_kib)
        self.rss_max_kib = rss_kib if self.rss_max_kib is None else max(self.rss_max_kib, rss_kib)
        if pid is not None and pid != self.rss_epoch_pid:
            self.rss_epoch_pid = pid
            self.rss_epoch_min_kib = rss_kib
            self.rss_epoch_max_kib = rss_kib
        else:
            self.rss_epoch_min_kib = (
                rss_kib if self.rss_epoch_min_kib is None else min(self.rss_epoch_min_kib, rss_kib)
            )
            self.rss_epoch_max_kib = (
                rss_kib if self.rss_epoch_max_kib is None else max(self.rss_epoch_max_kib, rss_kib)
            )
        if self.rss_epoch_min_kib is not None and self.rss_epoch_max_kib is not None:
            self.rss_max_growth_kib = max(
                self.rss_max_growth_kib,
                self.rss_epoch_max_kib - self.rss_epoch_min_kib,
            )

    def record_delivery(self, accepted: bool) -> None:
        if not accepted:
            self.delivery_failures += 1

    def _begin_outage(self, at_ms: int) -> None:
        if self.outage_started_at_ms is None:
            self.outage_started_at_ms = at_ms
            self.disconnects += 1

    def observe_bridge_restart(self, at_ms: int) -> None:
        """Move an outage start past in-flight requests from the old Bridge PID."""
        if self.outage_started_at_ms is not None:
            self.outage_started_at_ms = at_ms

    def summary(self, finished_at_ms: int) -> dict[str, object]:
        return {
            "startedAt": self.started_at_ms,
            "finishedAt": finished_at_ms,
            "durationMs": max(0, finished_at_ms - self.started_at_ms),
            "samples": self.samples,
            "failures": self.failures,
            "disconnects": self.disconnects,
            "reconnects": self.reconnects,
            "openOutage": self.outage_started_at_ms is not None,
            "maxReconnectMs": self.max_reconnect_ms,
            "averageReconnectMs": (
                self.total_reconnect_ms // self.reconnects if self.reconnects else 0
            ),
            "deliveryFailures": self.delivery_failures,
            "exerciseFailures": self.exercise_failures,
            "exerciseCycles": self.exercise_cycles,
            "stateErrors": self.state_errors,
            "animationErrors": self.animation_errors,
            "uiTickStalls": self.ui_tick_stalls,
            "droppedFlows": self.dropped_flows,
            "maxQueuedFlows": self.max_queued_flows,
            "rssMinKiB": self.rss_min_kib,
            "rssMaxKiB": self.rss_max_kib,
            "rssTotalRangeKiB": (
                self.rss_max_kib - self.rss_min_kib
                if self.rss_min_kib is not None and self.rss_max_kib is not None
                else None
            ),
            "rssGrowthKiB": self.rss_max_growth_kib if self.rss_min_kib is not None else None,
            "sleepGaps": self.sleep_gaps,
        }


def soak_passed(
    summary: Mapping[str, object],
    *,
    require_sleep_gap: bool,
    minimum_reconnects: int,
    minimum_exercises: int,
) -> bool:
    return bool(
        summary.get("completed") is True
        and summary.get("openOutage") is False
        and int(summary.get("maxReconnectMs", 0)) <= 30_000
        and int(summary.get("deliveryFailures", 0)) == 0
        and int(summary.get("exerciseFailures", 0)) == 0
        and int(summary.get("stateErrors", 0)) == 0
        and int(summary.get("animationErrors", 0)) == 0
        and int(summary.get("uiTickStalls", 0)) == 0
        and int(summary.get("droppedFlows", 0)) == 0
        and (not require_sleep_gap or int(summary.get("sleepGaps", 0)) >= 1)
        and int(summary.get("reconnects", 0)) >= minimum_reconnects
        and int(summary.get("exerciseCycles", 0)) >= minimum_exercises
    )


def uptime_seconds() -> float:
    clock = getattr(time, "CLOCK_UPTIME_RAW", None)
    if clock is not None:
        return time.clock_gettime(clock)
    return time.monotonic()


def sleep_gap_ms(
    previous_wall: float,
    previous_uptime: float,
    current_wall: float,
    current_uptime: float,
    *,
    threshold_seconds: float,
) -> int | None:
    wall_elapsed = max(0.0, current_wall - previous_wall)
    uptime_elapsed = max(0.0, current_uptime - previous_uptime)
    suspended = max(0.0, wall_elapsed - uptime_elapsed)
    if suspended <= threshold_seconds:
        return None
    return int(suspended * 1000)


class JsonlLog:
    def __init__(self, path: Path) -> None:
        self.path = path.expanduser()
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._file = self.path.open("a", encoding="utf-8")
        os.chmod(self.path, 0o600)

    def write(self, value: Mapping[str, object]) -> None:
        self._file.write(json.dumps(dict(value), ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n")
        self._file.flush()

    def close(self) -> None:
        self._file.close()


def bridge_process_snapshot(workspace: Path, explicit_pid: int | None) -> tuple[int | None, int | None]:
    completed = subprocess.run(
        ["ps", "-axo", "pid=,rss=,command="],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )
    if completed.returncode != 0:
        return None, None
    matches: list[tuple[int, int]] = []
    for line in completed.stdout.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) != 3:
            continue
        try:
            pid = int(parts[0])
            rss = int(parts[1])
        except ValueError:
            continue
        if explicit_pid is not None:
            if pid == explicit_pid:
                return pid, rss
            continue
        command = parts[2]
        if "codex_pet_bridge.py --mode monitor" in command and str(workspace) in command:
            matches.append((pid, rss))
    return (None, None) if len(matches) != 1 else matches[0]


async def deliver_hook(event: str, session_id: str, turn_id: str, workspace: Path) -> bool:
    envelope = hook_envelope(
        {
            "hook_event_name": event,
            "session_id": session_id,
            "turn_id": turn_id,
            "cwd": str(workspace),
        }
    )
    if envelope is None:
        return False
    for attempt in range(3):
        try:
            ack = await send(DEFAULT_SOCKET, envelope)
        except (OSError, RuntimeError, asyncio.TimeoutError):
            if attempt < 2:
                await asyncio.sleep(0.25)
                continue
            return False
        return bool(ack.payload and ack.payload.get("status") in {"accepted", "duplicate"})
    return False


async def wait_board(
    caller: BridgeCaller,
    predicate,
    *,
    timeout: float = 60.0,
    label: str = "board state",
) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    last: dict[str, object] = {}
    while time.monotonic() < deadline:
        try:
            last = json.loads(await caller("pet_status", {}))
        except (MCPError, TimeoutError):
            await asyncio.sleep(3.0)
            continue
        if predicate(last):
            return last
        await asyncio.sleep(3.0)
    raise TimeoutError(f"{label} did not converge: {json.dumps(last, sort_keys=True)}")


async def run_storage_stress(args: argparse.Namespace) -> int:
    caller = BridgeCaller(DEFAULT_SOCKET)
    cues = ("done", "submitted", "needs_input", "error", "listening")
    max_queued = 0
    initial = await wait_board(
        caller,
        lambda value: value.get("connected") == 1
        and value.get("queuedFlows") == 0
        and int(value.get("loaderPhase", -1) or 0) == 0,
        timeout=15.0,
        label="storage stress initial queue drain",
    )
    if int(initial.get("preloadedBytes", 0) or 0) <= 0:
        raise RuntimeError(f"pet assets are not preloaded: {initial}")
    active_pet = str(initial.get("pet") or "")
    if not active_pet:
        raise RuntimeError(f"active pet is missing: {initial}")
    initial_ticks = int(initial.get("uiTicks", 0) or 0)
    initial = await wait_board(
        caller,
        lambda value: int(value.get("uiTicks", 0) or 0) > initial_ticks
        and value.get("queuedFlows") == 0,
        timeout=5.0,
        label="storage stress initial UI heartbeat",
    )
    baseline_dropped = int(initial.get("droppedFlows", 0) or 0)
    for cycle in range(args.storage_stress_cycles):
        target = active_pet
        cue = cues[cycle % len(cues)]
        before = json.loads(await caller("pet_status", {}))
        before_ticks = int(before.get("uiTicks", 0) or 0)
        stopped = json.loads(await caller("stop_audio", {}))
        if stopped.get("playing") != 0:
            raise RuntimeError(f"cycle {cycle + 1} could not stop prior audio: {stopped}")
        results = await asyncio.gather(
            caller("play_cue", {"cue": cue}),
            caller("select_pet", {"slug": target}),
            return_exceptions=True,
        )
        failures = [type(value).__name__ for value in results if isinstance(value, BaseException)]
        if failures:
            raise RuntimeError(f"cycle {cycle + 1} command failure: {failures}")
        audio = json.loads(str(results[0]))
        expected_path = f"/sdcard/apps/codex_pet/assets/{cue}.wav"
        if (
            audio.get("path") != expected_path
            or audio.get("err") != 0
            or int(audio.get("cachedCues", 0) or 0) != 5
            or not (audio.get("playing") == 1 or audio.get("ready") == 1)
        ):
            raise RuntimeError(f"cycle {cycle + 1} cue did not start: {audio}")
        deadline = time.monotonic() + args.storage_stress_timeout
        frames_seen: set[int] = set()
        last: dict[str, object] = {}
        while time.monotonic() < deadline:
            last = json.loads(await caller("pet_status", {}))
            if int(last.get("droppedFlows", 0) or 0) > baseline_dropped:
                raise RuntimeError(f"cycle {cycle + 1} dropped a flow: {last}")
            max_queued = max(max_queued, int(last.get("queuedFlows", 0) or 0))
            if last.get("pet") == target and last.get("custom") == 1:
                frame = last.get("frame")
                if isinstance(frame, int) and not isinstance(frame, bool):
                    frames_seen.add(frame)
            if (
                last.get("connected") == 1
                and last.get("pet") == target
                and int(last.get("frames", 0) or 0) >= 2
                and int(last.get("preloadedBytes", 0) or 0) > 0
                and int(last.get("loaderPhase", -1) or 0) == 0
                and int(last.get("uiTicks", 0) or 0) > before_ticks
                and len(frames_seen) >= 2
            ):
                break
            await asyncio.sleep(0.25)
        else:
            raise TimeoutError(
                f"cycle {cycle + 1} did not animate {target}: "
                f"{json.dumps(last, sort_keys=True)}"
            )
        print(json.dumps({
            "cycle": cycle + 1,
            "cue": cue,
            "audioSeq": audio.get("seq"),
            "pet": target,
            "uiTicks": last.get("uiTicks"),
            "frames": last.get("frames"),
            "preloadedBytes": last.get("preloadedBytes"),
            "maxQueuedFlows": max_queued,
        }, sort_keys=True), flush=True)

    final = await wait_board(
        caller,
        lambda value: value.get("queuedFlows") == 0
        and int(value.get("droppedFlows", 0) or 0) == baseline_dropped,
        timeout=10.0,
        label="storage stress queue drain",
    )
    print(json.dumps({
        "passed": True,
        "cycles": args.storage_stress_cycles,
        "pet": final.get("pet"),
        "uiTicks": final.get("uiTicks"),
        "preloadedBytes": final.get("preloadedBytes"),
        "maxQueuedFlows": max_queued,
        "baselineDroppedFlows": baseline_dropped,
        "newDroppedFlows": int(final.get("droppedFlows", 0) or 0) - baseline_dropped,
    }, sort_keys=True))
    return 0


def task_aggregate_returned_to_baseline(
    value: Mapping[str, object], baseline_active: int, baseline_state: str
) -> bool:
    if value.get("activeTasks", 0) > baseline_active:
        return False
    if baseline_active > 0:
        return value.get("state") in {"running", "ready", "needs_input", "blocked"}
    return value.get("state") in {"ready", "needs_input", "blocked", "connected"} or (
        value.get("state") == baseline_state
    )


async def exercise_cycle(
    caller: BridgeCaller,
    metrics: SoakMetrics,
    workspace: Path,
    run_id: str,
    cycle: int,
) -> dict[str, object]:
    baseline = json.loads(await caller("pet_status", {}))
    baseline_active = int(baseline.get("activeTasks", 0) or 0)
    target_active = max(2, baseline_active + 1)
    baseline_state = str(baseline.get("state", "ready"))
    active_pet = str(baseline.get("pet") or "")
    if not active_pet:
        raise RuntimeError(f"active pet is missing: {baseline}")
    if baseline_active == 0 and baseline_state in {"connected", "disconnected"}:
        baseline_state = "ready"
    first = f"{run_id}-a"
    second = f"{run_id}-b"
    turn = f"turn-{cycle % 1000}"
    deliveries = [
        await deliver_hook("UserPromptSubmit", first, turn, workspace),
        await deliver_hook("UserPromptSubmit", second, turn, workspace),
    ]
    stopped: list[bool] = []

    async def select_and_wait(slug: str) -> dict[str, object]:
        last_error: TimeoutError | None = None
        for _attempt in range(3):
            await caller("select_pet", {"slug": slug})
            try:
                return await wait_board(
                    caller,
                    lambda value: value.get("pet") == slug
                    and value.get("custom") == 1
                    and value.get("frames", 0) > 1,
                    timeout=30.0,
                    label=f"{slug} animation",
                )
            except TimeoutError as exc:
                last_error = exc
        assert last_error is not None
        raise last_error

    try:
        for accepted in deliveries:
            metrics.record_delivery(accepted)
        running = await wait_board(
            caller,
            lambda value: value.get("activeTasks", 0) >= target_active
            and (
                value.get("state") == "running"
                if baseline_active == 0
                else value.get("state") in {"running", "needs_input", "blocked"}
            ),
            label="running task aggregate",
        )
        if running.get("activeTasks", 0) < target_active:
            metrics.state_errors += 1
        sensitive = await deliver_hook("PermissionRequest", first, turn, workspace)
        metrics.record_delivery(sensitive)
        needs = await wait_board(
            caller,
            lambda value: value.get("state") == "needs_input" and value.get("approval") == 0,
            label="approval-needed task aggregate",
        )
        if needs.get("approval") != 0:
            metrics.state_errors += 1
        await select_and_wait(active_pet)
        stopped = [
            await deliver_hook("Stop", first, turn, workspace),
            await deliver_hook("Stop", second, turn, workspace),
        ]
        for accepted in stopped:
            metrics.record_delivery(accepted)
        ready = await wait_board(
            caller,
            lambda value: task_aggregate_returned_to_baseline(value, baseline_active, baseline_state),
            label="stopped task aggregate",
        )
        return {
            "kind": "exercise",
            "cycle": cycle,
            "runningActive": running.get("activeTasks"),
            "baselineActive": baseline_active,
            "baselineState": baseline_state,
            "sensitiveApproval": needs.get("approval"),
            "readyFrames": ready.get("frames"),
            "deliveries": deliveries + [sensitive] + stopped,
        }
    finally:
        if not stopped:
            for session_id in (first, second):
                metrics.record_delivery(await deliver_hook("Stop", session_id, turn, workspace))


async def run_soak(args: argparse.Namespace) -> int:
    started_at = int(time.time() * 1000)
    duration_seconds = args.duration_seconds
    if duration_seconds is None:
        duration_seconds = args.duration_hours * 3600.0
    deadline = time.monotonic() + duration_seconds
    metrics = SoakMetrics(started_at)
    log = JsonlLog(args.output)
    caller = BridgeCaller(DEFAULT_SOCKET)
    run_id = f"soak-{started_at % 10_000_000}"
    next_exercise = float("inf")
    exercise_armed = not args.exercise
    cycle = 0
    last_bridge_pid: int | None = None
    last_sample_wall = time.time()
    last_sample_uptime = uptime_seconds()
    completed = False
    try:
        log.write({"kind": "start", "at": started_at, "durationSeconds": duration_seconds})
        try:
            startup = await wait_board(
                caller,
                lambda value: (
                    value.get("connected") == 1
                    and value.get("active") == 1
                    and value.get("custom") == 1
                    and isinstance(value.get("frames"), int)
                    and value.get("frames", 0) >= 2
                ),
                timeout=60.0,
                label="Codex Pet ready gate",
            )
            startup_at = int(time.time() * 1000)
            metrics.observe_status(startup_at, startup)
            startup_pid, startup_rss = bridge_process_snapshot(args.workspace, args.bridge_pid)
            last_bridge_pid = startup_pid
            metrics.observe_rss(startup_rss, startup_pid)
            log.write({"kind": "startup", "at": startup_at, "status": startup, "bridgeRssKiB": startup_rss})
        except (MCPError, TimeoutError, OSError, json.JSONDecodeError) as exc:
            startup_failure_at = int(time.time() * 1000)
            metrics.observe_failure(startup_failure_at, type(exc).__name__)
            log.write({"kind": "startup-failure", "at": startup_failure_at, "error": type(exc).__name__})
        while time.monotonic() < deadline:
            now_mono = time.monotonic()
            now_wall = time.time()
            now_uptime = uptime_seconds()
            gap_ms = sleep_gap_ms(
                last_sample_wall,
                last_sample_uptime,
                now_wall,
                now_uptime,
                threshold_seconds=args.sample_seconds * 3,
            )
            if gap_ms is not None:
                metrics.sleep_gaps += 1
                log.write({
                    "kind": "sleep-gap",
                    "at": int(now_wall * 1000),
                    "gapMs": gap_ms,
                    "wallGapMs": int(max(0.0, now_wall - last_sample_wall) * 1000),
                    "uptimeGapMs": int(max(0.0, now_uptime - last_sample_uptime) * 1000),
                })
            last_sample_wall = now_wall
            last_sample_uptime = now_uptime
            observed_pid, observed_rss = bridge_process_snapshot(args.workspace, args.bridge_pid)
            if observed_pid is not None and last_bridge_pid is not None and observed_pid != last_bridge_pid:
                restart_at = int(time.time() * 1000)
                metrics.observe_bridge_restart(restart_at)
                log.write({"kind": "bridge-restart", "at": restart_at, "oldPid": last_bridge_pid, "newPid": observed_pid})
            if observed_pid is not None:
                last_bridge_pid = observed_pid
            metrics.observe_rss(observed_rss, observed_pid)
            sample_at_ms = int(time.time() * 1000)
            sample_ok = False
            try:
                status = json.loads(await caller("pet_status", {}))
                metrics.observe_status(sample_at_ms, status)
                sample_ok = True
                if args.exercise and not exercise_armed and metrics.samples - metrics.failures >= 2:
                    exercise_armed = True
                    next_exercise = time.monotonic()
                log.write({"kind": "sample", "at": sample_at_ms, "status": status, "bridgeRssKiB": observed_rss})
            except (MCPError, TimeoutError, OSError, json.JSONDecodeError) as exc:
                failure_at_ms = int(time.time() * 1000)
                metrics.observe_failure(failure_at_ms, type(exc).__name__)
                log.write({"kind": "failure", "at": failure_at_ms, "error": type(exc).__name__})
            if args.exercise and exercise_armed and sample_ok and now_mono >= next_exercise:
                cycle += 1
                try:
                    exercise = await exercise_cycle(caller, metrics, args.workspace, run_id, cycle)
                    metrics.exercise_cycles += 1
                    log.write(exercise)
                except (MCPError, TimeoutError, OSError, json.JSONDecodeError) as exc:
                    metrics.exercise_failures += 1
                    log.write({
                        "kind": "exercise-failure",
                        "at": sample_at_ms,
                        "error": type(exc).__name__,
                        "detail": str(exc)[:600],
                    })
                next_exercise = time.monotonic() + args.exercise_seconds
            await asyncio.sleep(min(args.sample_seconds, max(0.0, deadline - time.monotonic())))
        completed = True
    except KeyboardInterrupt:
        pass
    finally:
        finished_at = int(time.time() * 1000)
        summary = metrics.summary(finished_at)
        summary["kind"] = "summary"
        summary["completed"] = completed
        summary["requiredSleepGap"] = args.require_sleep_gap
        summary["minimumReconnects"] = args.minimum_reconnects
        summary["minimumExercises"] = args.minimum_exercises
        summary["passed"] = soak_passed(
            summary,
            require_sleep_gap=args.require_sleep_gap,
            minimum_reconnects=args.minimum_reconnects,
            minimum_exercises=args.minimum_exercises,
        )
        log.write(summary)
        log.close()
        print(json.dumps(summary, ensure_ascii=False, sort_keys=True))
    return 0 if summary["passed"] else (130 if not completed else 1)


def self_test() -> None:
    metrics = SoakMetrics(started_at_ms=1_000)
    metrics.observe_status(1_000, {"connected": 1, "state": "ready", "custom": 1, "frames": 4})
    metrics.observe_failure(2_000, "MCPError")
    metrics.observe_failure(7_000, "MCPError")
    metrics.observe_status(12_000, {"connected": 1, "state": "ready", "custom": 1, "frames": 4})
    metrics.record_delivery(False)
    metrics.exercise_failures += 1
    summary = metrics.summary(12_000)
    assert summary["samples"] == 4
    assert summary["failures"] == 2 and summary["deliveryFailures"] == 1
    assert summary["exerciseFailures"] == 1
    assert summary["disconnects"] == 1 and summary["reconnects"] == 1
    assert summary["maxReconnectMs"] == 10_000
    assert summary["animationErrors"] == 0

    reboot = SoakMetrics(started_at_ms=20_000)
    reboot.observe_status(
        20_000,
        {"connected": 0, "state": "disconnected", "custom": 1, "frames": 7},
    )
    assert reboot.state_errors == 0

    inconsistent = SoakMetrics(started_at_ms=30_000)
    inconsistent.observe_status(
        30_000,
        {"connected": 1, "state": "disconnected", "custom": 1, "frames": 7},
    )
    assert inconsistent.state_errors == 1

    animation = SoakMetrics(started_at_ms=40_000)
    animation.observe_status(40_000, {"connected": 1, "state": "running", "custom": 1, "frames": 0})
    animation.observe_status(50_000, {"connected": 1, "state": "running", "custom": 1, "frames": 0})
    assert animation.animation_errors == 0
    animation.observe_status(56_001, {"connected": 1, "state": "running", "custom": 1, "frames": 0})
    assert animation.animation_errors == 1

    concurrency = SoakMetrics(started_at_ms=60_000)
    concurrency.observe_status(
        60_000,
        {"connected": 1, "state": "running", "custom": 1, "frames": 6,
         "uiTicks": 10, "queuedFlows": 2, "droppedFlows": 0},
    )
    concurrency.observe_status(
        62_000,
        {"connected": 1, "state": "running", "custom": 1, "frames": 6,
         "uiTicks": 10, "queuedFlows": 3, "droppedFlows": 1},
    )
    concurrency_summary = concurrency.summary(62_000)
    assert concurrency_summary["uiTickStalls"] == 1
    assert concurrency_summary["droppedFlows"] == 1
    assert concurrency_summary["maxQueuedFlows"] == 3

    assert task_aggregate_returned_to_baseline(
        {"activeTasks": 1, "state": "running"}, 1, "running"
    )
    assert task_aggregate_returned_to_baseline(
        {"activeTasks": 1, "state": "running"}, 1, "needs_input"
    )
    assert not task_aggregate_returned_to_baseline(
        {"activeTasks": 2, "state": "running"}, 1, "running"
    )

    restarted = SoakMetrics(started_at_ms=60_000)
    restarted.observe_failure(61_000, "MCPError")
    restarted.observe_bridge_restart(65_000)
    restarted.observe_status(
        67_000,
        {"connected": 1, "state": "ready", "custom": 1, "frames": 4},
    )
    assert restarted.summary(67_000)["maxReconnectMs"] == 2_000

    rss = SoakMetrics(started_at_ms=70_000)
    rss.observe_rss(40_000, 11)
    rss.observe_rss(41_000, 11)
    rss.observe_rss(57_000, 12)
    rss.observe_rss(58_500, 12)
    rss_summary = rss.summary(80_000)
    assert rss_summary["rssGrowthKiB"] == 1_500
    assert rss_summary["rssTotalRangeKiB"] == 18_500

    fake_ps = subprocess.CompletedProcess(
        args=["ps"],
        returncode=0,
        stdout=(
            " 123 2048 /usr/bin/python "
            "/tmp/work/scripts/codex_pet_bridge.py --mode monitor --workspace /tmp/work\n"
        ),
    )
    with patch("subprocess.run", return_value=fake_ps):
        assert bridge_process_snapshot(Path("/tmp/work"), None) == (123, 2048)

    gate = {
        "completed": True,
        "openOutage": False,
        "maxReconnectMs": 25_000,
        "deliveryFailures": 0,
        "exerciseFailures": 0,
        "stateErrors": 0,
        "animationErrors": 0,
        "uiTickStalls": 0,
        "droppedFlows": 0,
        "sleepGaps": 1,
        "reconnects": 2,
        "exerciseCycles": 100,
    }
    assert soak_passed(gate, require_sleep_gap=True, minimum_reconnects=2, minimum_exercises=100)
    assert not soak_passed(
        {**gate, "sleepGaps": 0},
        require_sleep_gap=True,
        minimum_reconnects=2,
        minimum_exercises=100,
    )
    assert not soak_passed(
        {**gate, "reconnects": 1},
        require_sleep_gap=True,
        minimum_reconnects=2,
        minimum_exercises=100,
    )
    assert not soak_passed(
        {**gate, "uiTickStalls": 1},
        require_sleep_gap=True,
        minimum_reconnects=2,
        minimum_exercises=100,
    )
    assert sleep_gap_ms(100.0, 50.0, 140.0, 90.0, threshold_seconds=15.0) is None
    assert sleep_gap_ms(100.0, 50.0, 191.0, 51.0, threshold_seconds=15.0) == 90_000
    print("codex_pet_soak self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Codex Pet 24-hour stability monitor")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--storage-stress-cycles", type=int, default=0)
    parser.add_argument("--storage-stress-timeout", type=float, default=30.0)
    parser.add_argument("--duration-hours", type=float, default=24.0)
    parser.add_argument("--duration-seconds", type=float)
    parser.add_argument("--sample-seconds", type=float, default=5.0)
    parser.add_argument("--exercise", action="store_true")
    parser.add_argument("--exercise-seconds", type=float, default=300.0)
    parser.add_argument("--require-sleep-gap", action="store_true")
    parser.add_argument("--minimum-reconnects", type=int, default=0)
    parser.add_argument("--minimum-exercises", type=int, default=0)
    parser.add_argument("--bridge-pid", type=int)
    parser.add_argument("--workspace", type=Path, default=Path.cwd())
    parser.add_argument(
        "--output",
        type=Path,
        default=Path.home() / ".vibeboard" / f"codex_pet_soak_{int(time.time())}.jsonl",
    )
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.storage_stress_cycles < 0 or args.storage_stress_timeout <= 0:
        parser.error("storage stress cycles must not be negative and its timeout must be positive")
    if args.storage_stress_cycles:
        return asyncio.run(run_storage_stress(args))
    if args.duration_hours <= 0 or (args.duration_seconds is not None and args.duration_seconds <= 0):
        parser.error("duration must be positive")
    if args.sample_seconds <= 0 or args.exercise_seconds <= 0:
        parser.error("sample and exercise intervals must be positive")
    if args.minimum_reconnects < 0 or args.minimum_exercises < 0:
        parser.error("minimum reconnects and exercises must not be negative")
    return asyncio.run(run_soak(args))


if __name__ == "__main__":
    raise SystemExit(main())
