#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import functools
import hashlib
import json
import math
import os
import re
import tempfile
import threading
import time
from collections import deque
from datetime import date, datetime, time as datetime_time, timedelta
from pathlib import Path
from typing import Awaitable, Callable, Iterable, Mapping
from zoneinfo import ZoneInfo

from codex_pet_protocol import PetEnvelope


PROGRESS_CHANNEL = "pet.progress"
ACHIEVEMENT_CHANNEL = "pet.achievement"
CUE_CHANNEL = "pet.cue"
PROGRESS_SNAPSHOT_MAX_BYTES = 184
PROFILE_VERSION = 1
PROFILE_RETENTION_DAYS = 90
MAX_NICKNAME_CHARS = 24
SAMPLE_SECONDS = 5.0
MAX_SAMPLE_GAP_SECONDS = 15.0
ACTIVE_FLUSH_SECONDS = 30
TIME_XP_SECONDS = 5 * 60
TIME_XP_DAILY_CAP = 12
TASK_XP = 20
PROUD_SECONDS = 10.0
CELEBRATING_SECONDS = 8.0
_SAFE_SLUG = re.compile(r"^[a-z0-9][a-z0-9-]{0,23}$")
_SAFE_CUE = {"done", "needs_input", "error"}

BADGES: dict[str, tuple[str, str]] = {
    "first-task": ("First task", "Completed the first Codex task"),
    "five-task-day": ("Five together", "Completed five Codex tasks in one day"),
    "hour-together": ("Hour together", "Ran Codex tasks for one hour in one day"),
    "three-day-streak": ("Three days", "Completed tasks on three consecutive days"),
    "seven-day-streak": ("Seven days", "Completed tasks on seven consecutive days"),
}


def validate_nickname(value: object) -> str:
    if not isinstance(value, str):
        raise ValueError("nickname must be a string")
    nickname = " ".join(value.split())
    if len(nickname) > MAX_NICKNAME_CHARS:
        raise ValueError(f"nickname must be at most {MAX_NICKNAME_CHARS} characters")
    return nickname


def default_preferences() -> dict[str, object]:
    return {
        "sound": {
            "enabled": True,
            "volume": 8,
            "quietHours": {
                "enabled": True,
                "start": "22:00",
                "end": "08:00",
            },
        }
    }


def _valid_time(value: object) -> str:
    if not isinstance(value, str) or re.fullmatch(r"(?:[01]\d|2[0-3]):[0-5]\d", value) is None:
        raise ValueError("quiet-hour times must use 24-hour HH:MM")
    return value


def validate_preferences(value: object) -> dict[str, object]:
    if not isinstance(value, Mapping) or set(value) != {"sound"}:
        raise ValueError("preferences must contain exactly sound")
    sound = value.get("sound")
    if not isinstance(sound, Mapping) or set(sound) != {"enabled", "volume", "quietHours"}:
        raise ValueError("sound preferences have invalid fields")
    enabled = sound.get("enabled")
    volume = sound.get("volume")
    quiet = sound.get("quietHours")
    if not isinstance(enabled, bool):
        raise ValueError("sound.enabled must be boolean")
    if not isinstance(volume, int) or isinstance(volume, bool) or not 0 <= volume <= 15:
        raise ValueError("sound.volume must be an integer from 0 to 15")
    if not isinstance(quiet, Mapping) or set(quiet) != {"enabled", "start", "end"}:
        raise ValueError("sound.quietHours has invalid fields")
    quiet_enabled = quiet.get("enabled")
    if not isinstance(quiet_enabled, bool):
        raise ValueError("sound.quietHours.enabled must be boolean")
    return {
        "sound": {
            "enabled": enabled,
            "volume": volume,
            "quietHours": {
                "enabled": quiet_enabled,
                "start": _valid_time(quiet.get("start")),
                "end": _valid_time(quiet.get("end")),
            },
        }
    }


def sound_allowed(preferences: Mapping[str, object], now: datetime | None = None) -> bool:
    sound = preferences.get("sound")
    if not isinstance(sound, Mapping) or sound.get("enabled") is not True:
        return False
    quiet = sound.get("quietHours")
    if not isinstance(quiet, Mapping) or quiet.get("enabled") is not True:
        return True
    current = now or datetime.now().astimezone()
    minute = current.hour * 60 + current.minute
    start_text = str(quiet.get("start") or "22:00")
    end_text = str(quiet.get("end") or "08:00")
    start_hour, start_minute = (int(part) for part in start_text.split(":"))
    end_hour, end_minute = (int(part) for part in end_text.split(":"))
    start = start_hour * 60 + start_minute
    end = end_hour * 60 + end_minute
    if start == end:
        return False
    in_quiet = start <= minute < end if start < end else minute >= start or minute < end
    return not in_quiet


def level_for_xp(xp: int) -> tuple[int, int]:
    safe_xp = max(0, xp)
    transitions = max(0, (math.isqrt(30_625 + 200 * safe_xp) - 175) // 50)
    while (25 * (transitions + 1) * (transitions + 1) + 175 * (transitions + 1)) // 2 <= safe_xp:
        transitions += 1
    while transitions > 0 and (25 * transitions * transitions + 175 * transitions) // 2 > safe_xp:
        transitions -= 1
    next_transition = transitions + 1
    next_threshold = (25 * next_transition * next_transition + 175 * next_transition) // 2
    return transitions + 1, next_threshold


def _nonnegative_int(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) and value >= 0 else 0


def _atomic_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n"
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, path)
    finally:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)


def _empty_state() -> dict[str, object]:
    return {"version": PROFILE_VERSION, "profiles": {}, "preferences": default_preferences()}


def _locked(method: Callable[..., object]) -> Callable[..., object]:
    @functools.wraps(method)
    def wrapped(self: "PetProfileStore", *args: object, **kwargs: object) -> object:
        with self._lock:
            return method(self, *args, **kwargs)

    return wrapped


class PetProfileStore:
    def __init__(self, path: Path) -> None:
        self.path = path.expanduser()
        self.backup_path = self.path.with_suffix(self.path.suffix + ".bak")
        self.health_warning = ""
        self._lock = threading.RLock()
        self.state = self._load()

    def _decode(self, path: Path) -> dict[str, object]:
        value = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(value, dict) or value.get("version") != PROFILE_VERSION:
            raise ValueError("unsupported pet profile")
        if not isinstance(value.get("profiles"), dict):
            raise ValueError("pet profiles must be an object")
        value["preferences"] = validate_preferences(value.get("preferences", default_preferences()))
        return value

    def _quarantine(self, path: Path) -> None:
        if not path.exists():
            return
        suffix = f".corrupt-{int(time.time())}"
        target = path.with_name(path.name + suffix)
        with contextlib.suppress(OSError):
            os.replace(path, target)

    def _load(self) -> dict[str, object]:
        if self.path.exists():
            try:
                value = self._decode(self.path)
                os.chmod(self.path, 0o600)
                _atomic_json(self.backup_path, value)
                return value
            except (OSError, ValueError, json.JSONDecodeError):
                self.health_warning = "primary pet profile was corrupt"
                self._quarantine(self.path)
        if self.backup_path.exists():
            try:
                value = self._decode(self.backup_path)
                self.health_warning = "pet profile restored from backup"
                _atomic_json(self.path, value)
                _atomic_json(self.backup_path, value)
                return value
            except (OSError, ValueError, json.JSONDecodeError):
                self.health_warning = "pet profile and backup were corrupt"
                self._quarantine(self.backup_path)
        value = _empty_state()
        _atomic_json(self.path, value)
        _atomic_json(self.backup_path, value)
        return value

    def _profiles(self) -> dict[str, object]:
        profiles = self.state.setdefault("profiles", {})
        if not isinstance(profiles, dict):
            profiles = {}
            self.state["profiles"] = profiles
        return profiles

    def _profile(self, slug: str) -> dict[str, object]:
        safe_slug = slug if _SAFE_SLUG.fullmatch(slug) else "rocky"
        profiles = self._profiles()
        profile = profiles.get(safe_slug)
        if not isinstance(profile, dict):
            profile = {
                "xp": 0,
                "lifetimeTasks": 0,
                "days": {},
                "badges": {},
                "completed": {},
            }
            profiles[safe_slug] = profile
        return profile

    @staticmethod
    def _day_key(now: datetime | None = None) -> str:
        return (now or datetime.now().astimezone()).date().isoformat()

    def _day(self, profile: dict[str, object], key: str) -> dict[str, int]:
        days = profile.setdefault("days", {})
        if not isinstance(days, dict):
            days = {}
            profile["days"] = days
        value = days.get(key)
        if not isinstance(value, dict):
            value = {"tasks": 0, "activeSeconds": 0, "timeXp": 0}
            days[key] = value
        for field in ("tasks", "activeSeconds", "timeXp"):
            item = value.get(field, 0)
            value[field] = item if isinstance(item, int) and item >= 0 else 0
        return value  # type: ignore[return-value]

    def _prune(self, profile: dict[str, object], today: date) -> None:
        cutoff = (today - timedelta(days=PROFILE_RETENTION_DAYS - 1)).isoformat()
        days = profile.get("days")
        if isinstance(days, dict):
            profile["days"] = {key: value for key, value in days.items() if key >= cutoff}
        completed = profile.get("completed")
        cutoff_epoch = int(time.time()) - PROFILE_RETENTION_DAYS * 24 * 60 * 60
        if isinstance(completed, dict):
            profile["completed"] = {
                key: value for key, value in completed.items()
                if isinstance(value, int) and value >= cutoff_epoch
            }

    @_locked
    def save(self) -> None:
        _atomic_json(self.path, self.state)
        _atomic_json(self.backup_path, self.state)

    @_locked
    def preferences(self) -> dict[str, object]:
        return validate_preferences(self.state.get("preferences", default_preferences()))

    @_locked
    def set_preferences(self, value: object) -> dict[str, object]:
        preferences = validate_preferences(value)
        self.state["preferences"] = preferences
        self.save()
        return preferences

    @_locked
    def set_nickname(self, slug: str, value: object) -> str:
        nickname = validate_nickname(value)
        profile = self._profile(slug)
        if nickname:
            profile["nickname"] = nickname
        else:
            profile.pop("nickname", None)
        self.save()
        return nickname

    def _streak(self, profile: Mapping[str, object], today: date) -> int:
        days = profile.get("days")
        if not isinstance(days, Mapping):
            return 0
        cursor = today
        today_value = days.get(today.isoformat())
        if not isinstance(today_value, Mapping) or _nonnegative_int(today_value.get("tasks")) <= 0:
            cursor -= timedelta(days=1)
        streak = 0
        while True:
            value = days.get(cursor.isoformat())
            if not isinstance(value, Mapping) or _nonnegative_int(value.get("tasks")) <= 0:
                break
            streak += 1
            cursor -= timedelta(days=1)
        return streak

    def _unlock(self, profile: dict[str, object], now: datetime) -> list[str]:
        badges = profile.setdefault("badges", {})
        if not isinstance(badges, dict):
            badges = {}
            profile["badges"] = badges
        day = self._day(profile, now.date().isoformat())
        streak = self._streak(profile, now.date())
        candidates = {
            "first-task": _nonnegative_int(profile.get("lifetimeTasks")) >= 1,
            "five-task-day": day["tasks"] >= 5,
            "hour-together": day["activeSeconds"] >= 60 * 60,
            "three-day-streak": streak >= 3,
            "seven-day-streak": streak >= 7,
        }
        unlocked: list[str] = []
        for badge, earned in candidates.items():
            if earned and badge not in badges:
                badges[badge] = int(now.timestamp())
                unlocked.append(badge)
        return unlocked

    @_locked
    def record_completion(
        self,
        slug: str,
        event_key: str,
        *,
        now: datetime | None = None,
    ) -> tuple[bool, list[str], bool]:
        current = now or datetime.now().astimezone()
        profile = self._profile(slug)
        completed = profile.setdefault("completed", {})
        if not isinstance(completed, dict):
            completed = {}
            profile["completed"] = completed
        digest = hashlib.sha256(event_key.encode("utf-8", "replace")).hexdigest()[:24]
        if digest in completed:
            return False, [], False
        old_level = level_for_xp(_nonnegative_int(profile.get("xp")))[0]
        completed[digest] = int(current.timestamp())
        profile["lifetimeTasks"] = _nonnegative_int(profile.get("lifetimeTasks")) + 1
        profile["xp"] = _nonnegative_int(profile.get("xp")) + TASK_XP
        self._day(profile, current.date().isoformat())["tasks"] += 1
        unlocked = self._unlock(profile, current)
        self._prune(profile, current.date())
        new_level = level_for_xp(int(profile["xp"]))[0]
        self.save()
        return True, unlocked, new_level > old_level

    @_locked
    def add_active_seconds(
        self,
        slug: str,
        seconds: int,
        *,
        now: datetime | None = None,
    ) -> tuple[list[str], bool]:
        if seconds <= 0:
            return [], False
        current = now or datetime.now().astimezone()
        profile = self._profile(slug)
        day = self._day(profile, current.date().isoformat())
        old_level = level_for_xp(_nonnegative_int(profile.get("xp")))[0]
        day["activeSeconds"] += seconds
        earned = min(TIME_XP_DAILY_CAP, day["activeSeconds"] // TIME_XP_SECONDS)
        delta = max(0, earned - day["timeXp"])
        if delta:
            day["timeXp"] += delta
            profile["xp"] = _nonnegative_int(profile.get("xp")) + delta
        unlocked = self._unlock(profile, current)
        self._prune(profile, current.date())
        new_level = level_for_xp(_nonnegative_int(profile.get("xp")))[0]
        self.save()
        return unlocked, new_level > old_level

    @_locked
    def snapshot(self, slug: str, *, now: datetime | None = None) -> dict[str, object]:
        current = now or datetime.now().astimezone()
        safe_slug = slug if _SAFE_SLUG.fullmatch(slug) else "rocky"
        profile = self._profile(safe_slug)
        day = self._day(profile, current.date().isoformat())
        xp = _nonnegative_int(profile.get("xp"))
        level, next_threshold = level_for_xp(xp)
        badges = profile.get("badges") if isinstance(profile.get("badges"), dict) else {}
        streak = self._streak(profile, current.date())
        achievement_progress = {
            "first-task": (_nonnegative_int(profile.get("lifetimeTasks")), 1, "tasks"),
            "five-task-day": (day["tasks"], 5, "tasks"),
            "hour-together": (day["activeSeconds"], 60 * 60, "seconds"),
            "three-day-streak": (streak, 3, "days"),
            "seven-day-streak": (streak, 7, "days"),
        }
        try:
            nickname = validate_nickname(profile.get("nickname", ""))
        except ValueError:
            nickname = ""
        return {
            "pet": safe_slug,
            "nickname": nickname,
            "level": level,
            "xp": xp,
            "nextXp": next_threshold,
            "todayTasks": day["tasks"],
            "todayActiveSeconds": day["activeSeconds"],
            "streak": streak,
            "lifetimeTasks": _nonnegative_int(profile.get("lifetimeTasks")),
            "badges": [
                {"id": badge, "name": BADGES[badge][0], "unlockedAt": unlocked_at}
                for badge, unlocked_at in sorted(badges.items(), key=lambda item: item[1])
                if badge in BADGES and isinstance(unlocked_at, int)
            ],
            "achievements": [
                {
                    "id": badge,
                    "name": name,
                    "description": description,
                    "current": achievement_progress[badge][0],
                    "target": achievement_progress[badge][1],
                    "unit": achievement_progress[badge][2],
                    "unlockedAt": badges.get(badge) if isinstance(badges.get(badge), int) else None,
                }
                for badge, (name, description) in BADGES.items()
            ],
        }

    @_locked
    def all_profiles(self, *, now: datetime | None = None) -> dict[str, object]:
        result: dict[str, object] = {}
        for slug in sorted(self._profiles()):
            result[slug] = self.snapshot(slug, now=now)
        return result


def active_pet_slug(state_dir: Path) -> str:
    try:
        value = json.loads((state_dir.expanduser() / "active.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "rocky"
    slug = value.get("slug") if isinstance(value, Mapping) else None
    return slug if isinstance(slug, str) and _SAFE_SLUG.fullmatch(slug) else "rocky"


class CodexPetProgressService:
    def __init__(
        self,
        *,
        store: PetProfileStore,
        active_pet: Callable[[], str],
        task_statuses: Callable[[], Iterable[str]],
        has_pending_approval: Callable[[], bool],
        publish_progress: Callable[[str], Awaitable[None]],
        publish_achievement: Callable[[str], Awaitable[None]],
        publish_cue: Callable[[str], Awaitable[None]],
        sample_seconds: float = SAMPLE_SECONDS,
        monotonic: Callable[[], float] = time.monotonic,
        local_now: Callable[[], datetime] = lambda: datetime.now().astimezone(),
    ) -> None:
        self.store = store
        self.active_pet = active_pet
        self.task_statuses = task_statuses
        self.has_pending_approval = has_pending_approval
        self.publish_progress = publish_progress
        self.publish_achievement = publish_achievement
        self.publish_cue = publish_cue
        self.sample_seconds = sample_seconds
        self.monotonic = monotonic
        self.local_now = local_now
        self._task: asyncio.Task[None] | None = None
        self._wake = asyncio.Event()
        self._pending_active: dict[tuple[str, str], float] = {}
        self._last_sample = monotonic()
        self._last_progress = ""
        self._achievements: deque[tuple[str, str]] = deque()
        self._cues: deque[tuple[str, str, bool]] = deque()
        self._proud_until = 0.0
        self._celebrating_until = 0.0

    def _statuses(self) -> list[str]:
        return [value for value in self.task_statuses() if isinstance(value, str)]

    def _mood(self) -> str:
        statuses = self._statuses()
        if self.has_pending_approval() or "needs_input" in statuses:
            return "attentive"
        if "blocked" in statuses:
            return "concerned"
        if "running" in statuses:
            return "focused"
        now = self.monotonic()
        if now < self._proud_until:
            return "proud"
        if now < self._celebrating_until:
            return "celebrating"
        return "content" if self.store.snapshot(self.active_pet(), now=self.local_now())["todayTasks"] else "calm"

    def _progress_payload(self) -> str:
        value = self.store.snapshot(self.active_pet(), now=self.local_now())
        payload = {
            "v": 1,
            "p": value["pet"],
            "l": value["level"],
            "x": value["xp"],
            "n": value["nextXp"],
            "m": self._mood(),
            "d": value["todayTasks"],
            "a": value["todayActiveSeconds"],
            "s": value["streak"],
        }
        encoded = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > PROGRESS_SNAPSHOT_MAX_BYTES:
            raise ValueError("pet.progress payload exceeds board flow limit")
        return encoded

    def observe_hook(self, request: PetEnvelope, payload: Mapping[str, object]) -> None:
        event = payload.get("event")
        slug = self.active_pet()
        if event == "Stop":
            turn_id = payload.get("turnId")
            event_key = f"{request.task_id}:{turn_id}" if isinstance(turn_id, str) and turn_id else request.message_id
            recorded, badges, level_up = self.store.record_completion(
                slug,
                event_key,
                now=self.local_now(),
            )
            if recorded:
                self._celebrating_until = self.monotonic() + CELEBRATING_SECONDS
                self._cues.append(("done", request.message_id, False))
            self._queue_badges(slug, badges, level_up)
        elif payload.get("status") == "blocked":
            self._cues.append(("error", request.message_id, False))
        elif event == "PermissionRequest":
            self._cues.append(("needs_input", request.message_id, False))
        self._wake.set()

    def _queue_badges(self, slug: str, badges: Iterable[str], level_up: bool) -> None:
        queued = False
        for badge in badges:
            if badge in BADGES:
                self._achievements.append((badge, slug))
                queued = True
        if queued or level_up:
            self._proud_until = self.monotonic() + PROUD_SECONDS

    def queue_audio_test(self) -> None:
        self._cues.append(("done", f"test:{time.time_ns()}", True))
        self._wake.set()

    def preferences(self) -> dict[str, object]:
        return self.store.preferences()

    def set_preferences(self, value: object) -> dict[str, object]:
        preferences = self.store.set_preferences(value)
        self._wake.set()
        return preferences

    def set_active_nickname(self, value: object) -> str:
        nickname = self.store.set_nickname(self.active_pet(), value)
        self._wake.set()
        return nickname

    def public_state(self) -> dict[str, object]:
        return {
            "active": {**self.store.snapshot(self.active_pet(), now=self.local_now()), "mood": self._mood()},
            "profiles": self.store.all_profiles(now=self.local_now()),
            "preferences": self.preferences(),
            "healthWarning": self.store.health_warning or None,
        }

    async def start(self) -> None:
        if self._task is not None:
            return
        self._last_sample = self.monotonic()
        self._task = asyncio.create_task(self._run(), name="codex-pet-progress")
        self._wake.set()

    async def stop(self) -> None:
        task, self._task = self._task, None
        if task is None:
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task
        self._sample_active(force=True)

    def _sample_active(self, *, force: bool = False) -> None:
        now = self.monotonic()
        elapsed = max(0.0, now - self._last_sample)
        self._last_sample = now
        if "running" in self._statuses() and elapsed <= MAX_SAMPLE_GAP_SECONDS:
            current = self.local_now()
            key = (self.active_pet(), current.date().isoformat())
            self._pending_active[key] = self._pending_active.get(key, 0.0) + elapsed
        for key, pending in list(self._pending_active.items()):
            if pending < ACTIVE_FLUSH_SECONDS and not (force and pending >= 1):
                continue
            seconds = int(pending)
            self._pending_active[key] = pending - seconds
            if self._pending_active[key] < 1:
                self._pending_active.pop(key, None)
            slug, day_text = key
            current = self.local_now()
            bucket_now = datetime.combine(
                date.fromisoformat(day_text), datetime_time(hour=12), tzinfo=current.tzinfo
            )
            badges, level_up = self.store.add_active_seconds(
                slug,
                seconds,
                now=bucket_now,
            )
            self._queue_badges(slug, badges, level_up)

    def _cue_payload(self, cue: str, event_id: str, *, bypass_quiet: bool) -> str | None:
        if cue not in _SAFE_CUE:
            return None
        preferences = self.preferences()
        if not bypass_quiet and not sound_allowed(preferences, self.local_now()):
            return None
        sound = preferences["sound"]
        assert isinstance(sound, Mapping)
        short_id = hashlib.sha256(event_id.encode("utf-8", "replace")).hexdigest()[:16]
        encoded = json.dumps(
            {"v": 1, "id": short_id, "c": cue, "n": int(sound["volume"])},
            separators=(",", ":"),
        )
        return encoded if len(encoded.encode("utf-8")) <= PROGRESS_SNAPSHOT_MAX_BYTES else None

    async def _publish_pending(self) -> None:
        progress = self._progress_payload()
        if progress != self._last_progress:
            await self.publish_progress(progress)
            self._last_progress = progress
        while self._achievements:
            badge, slug = self._achievements.popleft()
            name = BADGES[badge][0]
            payload = json.dumps(
                {"v": 1, "id": badge, "p": slug, "n": name},
                ensure_ascii=True,
                separators=(",", ":"),
            )
            if len(payload.encode("utf-8")) <= PROGRESS_SNAPSHOT_MAX_BYTES:
                await self.publish_achievement(payload)
        while self._cues:
            cue, event_id, bypass_quiet = self._cues.popleft()
            payload = self._cue_payload(cue, event_id, bypass_quiet=bypass_quiet)
            if payload is not None:
                await self.publish_cue(payload)

    async def _run(self) -> None:
        while True:
            try:
                await asyncio.wait_for(self._wake.wait(), timeout=self.sample_seconds)
            except asyncio.TimeoutError:
                pass
            self._wake.clear()
            self._sample_active()
            try:
                await self._publish_pending()
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                print(f"[codex_pet][progress] publish failed: {type(exc).__name__}", flush=True)


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="codex-pet-progress-") as temporary:
        path = Path(temporary) / "pet-profile.json"
        store = PetProfileStore(path)
        noon = datetime.fromisoformat("2026-08-03T12:00:00+08:00")
        for index in range(5):
            recorded, _, _ = store.record_completion("boba", f"session:turn-{index}", now=noon)
            assert recorded
        recorded, _, _ = store.record_completion("boba", "session:turn-4", now=noon)
        assert not recorded
        badges, _ = store.add_active_seconds("boba", 60 * 60, now=noon)
        snapshot = store.snapshot("boba", now=noon)
        assert snapshot["xp"] == 112 and snapshot["level"] == 2
        assert snapshot["todayTasks"] == 5 and snapshot["todayActiveSeconds"] == 3600
        badge_ids = {item["id"] for item in snapshot["badges"]}
        assert {"first-task", "five-task-day", "hour-together"} <= badge_ids
        assert "hour-together" in badges
        achievements = {item["id"]: item for item in snapshot["achievements"]}
        assert achievements["five-task-day"]["current"] == 5
        assert achievements["five-task-day"]["unlockedAt"] is not None
        assert achievements["seven-day-streak"]["target"] == 7
        assert store.set_nickname("boba", "  小 波   巴  ") == "小 波 巴"
        assert store.snapshot("boba", now=noon)["nickname"] == "小 波 巴"
        assert store.set_nickname("boba", "") == ""
        assert store.snapshot("boba", now=noon)["nickname"] == ""
        try:
            store.set_nickname("boba", "x" * (MAX_NICKNAME_CHARS + 1))
        except ValueError:
            pass
        else:
            raise AssertionError("oversized pet nickname was accepted")
        badges, _ = store.add_active_seconds("boba", 60 * 60, now=noon)
        assert store.snapshot("boba", now=noon)["xp"] == 112 and not badges
        assert store.snapshot("rocky", now=noon)["xp"] == 0

        streak_pet = "rocky"
        for offset in (2, 1, 0):
            current = noon - timedelta(days=offset)
            store.record_completion(streak_pet, f"streak:{offset}", now=current)
        assert store.snapshot(streak_pet, now=noon)["streak"] == 3
        assert "three-day-streak" in {
            item["id"] for item in store.snapshot(streak_pet, now=noon)["badges"]
        }
        for offset in (6, 5, 4, 3):
            current = noon - timedelta(days=offset)
            store.record_completion(streak_pet, f"streak:{offset}", now=current)
        store.record_completion(streak_pet, "streak:seven-unlock", now=noon)
        assert store.snapshot(streak_pet, now=noon)["streak"] == 7
        assert "seven-day-streak" in {
            item["id"] for item in store.snapshot(streak_pet, now=noon)["badges"]
        }

        new_york = ZoneInfo("America/New_York")
        for day_number in (7, 8, 9):
            dst_day = datetime(2026, 3, day_number, 12, tzinfo=new_york)
            store.record_completion("dst-pet", f"dst:{day_number}", now=dst_day)
        dst_now = datetime(2026, 3, 9, 12, tzinfo=new_york)
        assert store.snapshot("dst-pet", now=dst_now)["streak"] == 3

        preferences = store.set_preferences(default_preferences())
        assert sound_allowed(preferences, noon)
        assert not sound_allowed(preferences, datetime.fromisoformat("2026-08-03T23:00:00+08:00"))
        assert level_for_xp(99) == (1, 100)
        assert level_for_xp(100) == (2, 225)
        assert path.stat().st_mode & 0o777 == 0o600
        assert store.backup_path.stat().st_mode & 0o777 == 0o600

        path.write_text("not json", encoding="utf-8")
        recovered = PetProfileStore(path)
        assert recovered.snapshot("boba", now=noon)["todayTasks"] == 5
        assert recovered.health_warning == "pet profile restored from backup"

    with tempfile.TemporaryDirectory(prefix="codex-pet-progress-hooks-") as temporary:
        root = Path(temporary)
        clock = [0.0]
        current = [datetime.fromisoformat("2026-08-03T23:59:50+08:00")]
        statuses = ["running", "running"]

        async def discard(_payload: str) -> None:
            return None

        service = CodexPetProgressService(
            store=PetProfileStore(root / "hooks.json"),
            active_pet=lambda: "rocky",
            task_statuses=lambda: statuses,
            has_pending_approval=lambda: False,
            publish_progress=discard,
            publish_achievement=discard,
            publish_cue=discard,
            monotonic=lambda: clock[0],
            local_now=lambda: current[0],
        )

        def request(sequence: int, message_id: str, turn_id: str | None, event: str = "Stop") -> None:
            payload: dict[str, object] = {"event": event, "status": "ready"}
            if turn_id is not None:
                payload["turnId"] = turn_id
            envelope = PetEnvelope(
                kind="action",
                sequence=sequence,
                message_id=message_id,
                task_id="session-one",
                timestamp_ms=1,
                payload=payload,
            )
            service.observe_hook(envelope, payload)

        request(1, "hook:one", "turn-one")
        request(2, "hook:two", "turn-one")
        request(3, "hook:sub", "turn-sub", "SubagentStop")
        request(4, "hook:no-turn", None)
        request(5, "hook:no-turn", None)
        snapshot = service.store.snapshot("rocky", now=current[0])
        assert snapshot["todayTasks"] == 2 and snapshot["xp"] == 40
        assert len(service._cues) == 2

        clock[0] = 5.0
        service._sample_active()
        current[0] = datetime.fromisoformat("2026-08-04T00:00:05+08:00")
        clock[0] = 10.0
        service._sample_active(force=True)
        assert service.store.snapshot(
            "rocky", now=datetime.fromisoformat("2026-08-03T23:59:59+08:00")
        )["todayActiveSeconds"] == 5
        assert service.store.snapshot("rocky", now=current[0])["todayActiveSeconds"] == 5
        clock[0] = 30.0
        service._sample_active(force=True)
        assert service.store.snapshot("rocky", now=current[0])["todayActiveSeconds"] == 5

        corrupt = root / "both-corrupt.json"
        corrupt.write_text("bad", encoding="utf-8")
        corrupt.with_suffix(".json.bak").write_text("bad", encoding="utf-8")
        empty = PetProfileStore(corrupt)
        assert empty.health_warning == "pet profile and backup were corrupt"
        assert empty.snapshot("rocky", now=current[0])["xp"] == 0
        assert corrupt.is_file() and empty.backup_path.is_file()
        assert list(root.glob("both-corrupt.json.corrupt-*"))
        assert list(root.glob("both-corrupt.json.bak.corrupt-*"))

    print("codex_pet_progress self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Codex Pet local progress and preferences")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("no action selected")


if __name__ == "__main__":
    raise SystemExit(main())
