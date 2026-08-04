#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import os
import re
import time
from dataclasses import dataclass
from datetime import date, datetime, timedelta
from pathlib import Path
from typing import Awaitable, Callable, Mapping


USAGE_CHANNEL = "pet.usage"
USAGE_SUMMARY_CHANNEL = "pet.usage.summary"
USAGE_SNAPSHOT_MAX_BYTES = 184
USAGE_REFRESH_SECONDS = 1.0
USAGE_SUMMARY_REFRESH_SECONDS = 60.0
DEFAULT_SESSIONS_DIR = Path.home() / ".codex" / "sessions"
_SUMMARY_RECORD_RE = re.compile(
    r'"type"\s*:\s*"(?:session_meta|turn_context|token_count)"'
)


@dataclass(frozen=True)
class TokenCounts:
    input_tokens: int = 0
    cached_input_tokens: int = 0
    cache_write_input_tokens: int = 0
    output_tokens: int = 0
    reasoning_output_tokens: int = 0
    total_tokens: int = 0

    @classmethod
    def from_value(cls, value: object) -> "TokenCounts":
        source = value if isinstance(value, Mapping) else {}

        def count(name: str) -> int:
            item = source.get(name, 0)
            return item if isinstance(item, int) and not isinstance(item, bool) and item >= 0 else 0

        input_tokens = count("input_tokens")
        output_tokens = count("output_tokens")
        total_tokens = count("total_tokens") or input_tokens + output_tokens
        return cls(
            input_tokens=input_tokens,
            cached_input_tokens=min(input_tokens, count("cached_input_tokens")),
            cache_write_input_tokens=min(input_tokens, count("cache_write_input_tokens")),
            output_tokens=output_tokens,
            reasoning_output_tokens=min(output_tokens, count("reasoning_output_tokens")),
            total_tokens=total_tokens,
        )

    def delta(self, baseline: "TokenCounts") -> "TokenCounts":
        return TokenCounts(
            input_tokens=max(0, self.input_tokens - baseline.input_tokens),
            cached_input_tokens=max(0, self.cached_input_tokens - baseline.cached_input_tokens),
            cache_write_input_tokens=max(
                0, self.cache_write_input_tokens - baseline.cache_write_input_tokens
            ),
            output_tokens=max(0, self.output_tokens - baseline.output_tokens),
            reasoning_output_tokens=max(
                0, self.reasoning_output_tokens - baseline.reasoning_output_tokens
            ),
            total_tokens=max(0, self.total_tokens - baseline.total_tokens),
        )

    @property
    def uncached_input_tokens(self) -> int:
        return max(0, self.input_tokens - self.cached_input_tokens)


@dataclass(frozen=True)
class ModelPrice:
    input_per_million: float
    cached_input_per_million: float
    output_per_million: float

    def estimate_microusd(self, counts: TokenCounts) -> int:
        # One token at a USD-per-million rate has the same numeric value in micro-USD.
        return max(
            0,
            round(
                counts.uncached_input_tokens * self.input_per_million
                + counts.cached_input_tokens * self.cached_input_per_million
                + counts.output_tokens * self.output_per_million
            ),
        )


# Standard processing prices, USD per one million tokens. Keep exact model keys so
# an unknown relay alias never receives a plausible-looking but incorrect price.
MODEL_PRICES: dict[str, ModelPrice] = {
    "gpt-5.6-sol": ModelPrice(5.00, 0.50, 30.00),
    "gpt-5.6-terra": ModelPrice(2.50, 0.25, 15.00),
    "gpt-5.6-luna": ModelPrice(1.00, 0.10, 6.00),
    "gpt-5.4": ModelPrice(2.50, 0.25, 15.00),
}


def price_for_model(
    model: str,
    provider: str = "",
    environ: Mapping[str, str] | None = None,
) -> ModelPrice | None:
    env = os.environ if environ is None else environ
    custom_keys = (
        "CODEX_PET_PRICE_INPUT_PER_M",
        "CODEX_PET_PRICE_CACHED_INPUT_PER_M",
        "CODEX_PET_PRICE_OUTPUT_PER_M",
    )
    supplied = [env.get(key, "").strip() for key in custom_keys]
    if any(supplied):
        if not all(supplied):
            return None
        configured_model = env.get("CODEX_PET_PRICE_MODEL", "").strip()
        if configured_model and configured_model != model:
            return None
        try:
            values = [float(value) for value in supplied]
        except ValueError:
            return None
        if any(value < 0 for value in values):
            return None
        return ModelPrice(*values)
    if provider.strip().casefold() != "openai":
        return None
    return MODEL_PRICES.get(model)


@dataclass(frozen=True)
class UsageSnapshot:
    session_id: str
    model: str
    provider: str
    total: TokenCounts
    turn: TokenCounts
    context_tokens: int
    context_window: int

    def encode(self, *, environ: Mapping[str, str] | None = None) -> str:
        payload: dict[str, object] = {
            "v": 1,
            "s": "l",
            "m": _compact_model(self.model),
            "a": self.total.total_tokens,
            "x": self.context_tokens,
            "w": self.context_window,
            "i": self.total.uncached_input_tokens,
            "c": self.total.cached_input_tokens,
            "o": self.total.output_tokens,
            "t": self.turn.total_tokens,
        }
        price = price_for_model(self.model, self.provider, environ)
        if price is not None:
            payload["d"] = price.estimate_microusd(self.total)
            payload["e"] = price.estimate_microusd(self.turn)
        encoded = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > USAGE_SNAPSHOT_MAX_BYTES:
            payload.pop("m", None)
            encoded = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > USAGE_SNAPSHOT_MAX_BYTES:
            raise ValueError("usage snapshot exceeds board transport limit")
        return encoded


@dataclass
class UsageDay:
    day: date
    tokens: int = 0
    cost_microusd: int = 0
    cost_complete: bool = True


@dataclass(frozen=True)
class UsageSummary:
    days: tuple[UsageDay, ...]

    @property
    def today(self) -> UsageDay:
        return self.days[-1]

    @property
    def trend_unit(self) -> str:
        return "cost" if all(day.cost_complete for day in self.days) else "tokens"

    def encode(self) -> str:
        cost_trend = self.trend_unit == "cost"
        trend = [day.cost_microusd if cost_trend else day.tokens for day in self.days]
        maximum = max(trend, default=0)
        compact_trend = [
            (value * 1000 + maximum // 2) // maximum if maximum else 0
            for value in trend
        ]
        payload: dict[str, object] = {
            "v": 1,
            "s": "l",
            "t": self.today.tokens,
            "c": 1 if self.today.cost_complete else 0,
            "u": "c" if cost_trend else "t",
            "w": compact_trend,
        }
        if self.today.cost_complete:
            payload["d"] = self.today.cost_microusd
        encoded = json.dumps(payload, ensure_ascii=True, separators=(",", ":"))
        if len(encoded.encode("utf-8")) > USAGE_SNAPSHOT_MAX_BYTES:
            raise ValueError("usage summary exceeds board transport limit")
        return encoded

    def public_value(self) -> dict[str, object]:
        return {
            "todayTokens": self.today.tokens,
            "todayCostMicrousd": self.today.cost_microusd if self.today.cost_complete else None,
            "costComplete": self.today.cost_complete,
            "trendUnit": self.trend_unit,
            "days": [
                {
                    "date": day.day.isoformat(),
                    "tokens": day.tokens,
                    "costMicrousd": day.cost_microusd if day.cost_complete else None,
                    "costComplete": day.cost_complete,
                }
                for day in self.days
            ],
        }


def _compact_model(value: str) -> str:
    return "".join(character for character in value.strip() if 32 <= ord(character) < 127)[:24]


def _positive_int(value: object) -> int:
    return value if isinstance(value, int) and not isinstance(value, bool) and value > 0 else 0


class CodexSessionUsageReader:
    """Incrementally reduces one Codex rollout JSONL into a small usage snapshot."""

    def __init__(self, sessions_dir: Path = DEFAULT_SESSIONS_DIR) -> None:
        self.sessions_dir = sessions_dir.expanduser()
        self.session_id = ""
        self.path: Path | None = None
        self.offset = 0
        self.remainder = b""
        self.model = ""
        self.provider = ""
        self.total = TokenCounts()
        self.turn_baseline = TokenCounts()
        self.context_tokens = 0
        self.context_window = 0

    def read(self, session_id: str | None) -> UsageSnapshot | None:
        path = self._resolve(session_id)
        if path is None:
            self._reset("", None)
            return None
        resolved_id = session_id or _session_id_from_name(path.name)
        if path != self.path or resolved_id != self.session_id:
            self._reset(resolved_id, path)
        try:
            size = path.stat().st_size
            if size < self.offset:
                self._reset(resolved_id, path)
            with path.open("rb") as handle:
                handle.seek(self.offset)
                chunk = handle.read()
                self.offset = handle.tell()
        except OSError:
            return None
        if chunk:
            lines = (self.remainder + chunk).split(b"\n")
            self.remainder = lines.pop()
            for line in lines:
                self._consume(line)
        if self.total.total_tokens <= 0:
            return None
        return UsageSnapshot(
            session_id=self.session_id,
            model=self.model,
            provider=self.provider,
            total=self.total,
            turn=self.total.delta(self.turn_baseline),
            context_tokens=self.context_tokens,
            context_window=self.context_window,
        )

    def _resolve(self, session_id: str | None) -> Path | None:
        if not self.sessions_dir.is_dir():
            return None
        if session_id:
            if session_id == self.session_id and self.path is not None and self.path.is_file():
                return self.path
            matches = list(self.sessions_dir.rglob(f"*{session_id}.jsonl"))
            if matches:
                return max(matches, key=lambda item: item.stat().st_mtime_ns)
            return None
        candidates = list(self.sessions_dir.rglob("rollout-*.jsonl"))
        if not candidates:
            return None
        return max(candidates, key=lambda item: item.stat().st_mtime_ns)

    def _reset(self, session_id: str, path: Path | None) -> None:
        self.session_id = session_id
        self.path = path
        self.offset = 0
        self.remainder = b""
        self.model = ""
        self.provider = ""
        self.total = TokenCounts()
        self.turn_baseline = TokenCounts()
        self.context_tokens = 0
        self.context_window = 0

    def _consume(self, line: bytes) -> None:
        if not line:
            return
        try:
            record = json.loads(line)
        except (UnicodeDecodeError, json.JSONDecodeError):
            return
        if not isinstance(record, Mapping):
            return
        payload = record.get("payload")
        if not isinstance(payload, Mapping):
            return
        record_type = record.get("type")
        payload_type = payload.get("type")
        if record_type == "session_meta":
            value = payload.get("id")
            if isinstance(value, str) and value:
                self.session_id = value
            provider = payload.get("model_provider")
            if isinstance(provider, str) and provider:
                self.provider = provider
            return
        if record_type == "turn_context":
            value = payload.get("model")
            if isinstance(value, str) and value:
                self.model = value
            return
        if record_type != "event_msg":
            return
        if payload_type == "task_started":
            self.turn_baseline = self.total
            window = _positive_int(payload.get("model_context_window"))
            if window:
                self.context_window = window
            return
        if payload_type != "token_count":
            return
        info = payload.get("info")
        if not isinstance(info, Mapping):
            return
        self.total = TokenCounts.from_value(info.get("total_token_usage"))
        last = TokenCounts.from_value(info.get("last_token_usage"))
        self.context_tokens = last.input_tokens
        window = _positive_int(info.get("model_context_window"))
        if window:
            self.context_window = window


def _record_timestamp(value: object) -> datetime | None:
    if not isinstance(value, str) or not value:
        return None
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None
    return parsed if parsed.tzinfo is not None else parsed.astimezone()


class GlobalUsageReader:
    """Reduce all local session token deltas into seven day buckets."""

    def __init__(
        self,
        sessions_dir: Path = DEFAULT_SESSIONS_DIR,
        *,
        local_now: Callable[[], datetime] = lambda: datetime.now().astimezone(),
        environ: Mapping[str, str] | None = None,
    ) -> None:
        self.sessions_dir = sessions_dir.expanduser()
        self.local_now = local_now
        self.environ = environ

    def read(self) -> UsageSummary:
        now = self.local_now()
        today = now.date()
        first_day = today - timedelta(days=6)
        buckets = {
            first_day + timedelta(days=index): UsageDay(first_day + timedelta(days=index))
            for index in range(7)
        }
        if self.sessions_dir.is_dir():
            for path in self.sessions_dir.rglob("rollout-*.jsonl"):
                try:
                    modified_day = datetime.fromtimestamp(path.stat().st_mtime, tz=now.tzinfo).date()
                except OSError:
                    continue
                if modified_day >= first_day:
                    self._consume_file(path, buckets, now.tzinfo)
        return UsageSummary(tuple(buckets[key] for key in sorted(buckets)))

    def _consume_file(
        self,
        path: Path,
        buckets: dict[date, UsageDay],
        local_tz: object,
    ) -> None:
        provider = ""
        model = ""
        previous = TokenCounts()
        try:
            handle = path.open("r", encoding="utf-8", errors="replace")
        except OSError:
            return
        with handle:
            for line in handle:
                # Reject prompt, response, command, and file records before JSON decoding.
                if _SUMMARY_RECORD_RE.search(line) is None:
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, Mapping):
                    continue
                payload = record.get("payload")
                if not isinstance(payload, Mapping):
                    continue
                record_type = record.get("type")
                if record_type == "session_meta":
                    value = payload.get("model_provider")
                    if isinstance(value, str):
                        provider = value
                    continue
                if record_type == "turn_context":
                    value = payload.get("model")
                    if isinstance(value, str):
                        model = value
                    continue
                if record_type != "event_msg" or payload.get("type") != "token_count":
                    continue
                info = payload.get("info")
                if not isinstance(info, Mapping):
                    continue
                current = TokenCounts.from_value(info.get("total_token_usage"))
                delta = current.delta(previous)
                previous = current
                if delta.total_tokens <= 0:
                    continue
                timestamp = _record_timestamp(record.get("timestamp"))
                if timestamp is None:
                    continue
                local_day = timestamp.astimezone(local_tz).date()  # type: ignore[arg-type]
                bucket = buckets.get(local_day)
                if bucket is None:
                    continue
                bucket.tokens += delta.total_tokens
                price = price_for_model(model, provider, self.environ)
                if price is None:
                    bucket.cost_complete = False
                else:
                    bucket.cost_microusd += price.estimate_microusd(delta)


def _session_id_from_name(name: str) -> str:
    stem = name[:-6] if name.endswith(".jsonl") else name
    return stem[-36:] if len(stem) >= 36 else stem


class CodexPetUsageService:
    def __init__(
        self,
        *,
        publish_usage: Callable[[str], Awaitable[None]],
        selected_session: Callable[[], str | None],
        publish_summary: Callable[[str], Awaitable[None]] | None = None,
        sessions_dir: Path = DEFAULT_SESSIONS_DIR,
        refresh_seconds: float = USAGE_REFRESH_SECONDS,
        summary_refresh_seconds: float = USAGE_SUMMARY_REFRESH_SECONDS,
    ) -> None:
        if refresh_seconds <= 0:
            raise ValueError("usage refresh interval must be positive")
        self.publish_usage = publish_usage
        self.publish_summary = publish_summary
        self.selected_session = selected_session
        self.refresh_seconds = refresh_seconds
        self.reader = CodexSessionUsageReader(sessions_dir)
        self.summary_reader = GlobalUsageReader(sessions_dir)
        self._task: asyncio.Task[None] | None = None
        self._last_payload = ""
        self._last_summary_payload = ""
        self._summary: UsageSummary | None = None
        self._next_summary_at = 0.0
        self.summary_refresh_seconds = max(refresh_seconds, summary_refresh_seconds)

    async def start(self) -> None:
        if self._task is not None:
            return
        await self.refresh()
        self._task = asyncio.create_task(self._run(), name="codex-pet-usage")

    async def stop(self) -> None:
        task = self._task
        self._task = None
        if task is None:
            return
        task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await task

    async def refresh(self) -> None:
        snapshot = self.reader.read(self.selected_session())
        payload = snapshot.encode() if snapshot is not None else '{"v":1,"s":"u"}'
        usage_changed = payload != self._last_payload
        if usage_changed:
            await self.publish_usage(payload)
            self._last_payload = payload
        if usage_changed and snapshot is not None:
            cost = price_for_model(snapshot.model, snapshot.provider)
            cost_text = (
                str(cost.estimate_microusd(snapshot.total)) if cost is not None else "unavailable"
            )
            print(
                f"[codex_pet][usage] session={snapshot.session_id} "
                f"provider={snapshot.provider or 'unknown'} model={snapshot.model or 'unknown'} "
                f"total={snapshot.total.total_tokens} "
                f"context={snapshot.context_tokens}/{snapshot.context_window} "
                f"costMicrousd={cost_text}",
                flush=True,
            )
        now = time.monotonic()
        if self._summary is None or now >= self._next_summary_at:
            summary = self.summary_reader.read()
            summary_payload = summary.encode()
            self._summary = summary
            self._next_summary_at = now + self.summary_refresh_seconds
            if self.publish_summary is not None and summary_payload != self._last_summary_payload:
                await self.publish_summary(summary_payload)
                self._last_summary_payload = summary_payload

    def public_summary(self) -> dict[str, object]:
        return (self._summary or self.summary_reader.read()).public_value()

    async def _run(self) -> None:
        while True:
            await asyncio.sleep(self.refresh_seconds)
            try:
                await self.refresh()
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                print(f"[codex_pet][usage] refresh failed: {type(exc).__name__}", flush=True)


def self_test() -> None:
    from tempfile import TemporaryDirectory

    counts = TokenCounts(1_250, 1_000, 0, 20, 5, 1_270)
    price = ModelPrice(5.0, 0.5, 30.0)
    assert price.estimate_microusd(counts) == 2_350
    assert counts.uncached_input_tokens == 250
    custom = {
        "CODEX_PET_PRICE_MODEL": "relay/model",
        "CODEX_PET_PRICE_INPUT_PER_M": "2",
        "CODEX_PET_PRICE_CACHED_INPUT_PER_M": "0.2",
        "CODEX_PET_PRICE_OUTPUT_PER_M": "8",
    }
    assert price_for_model("relay/model", "relay", custom) == ModelPrice(2.0, 0.2, 8.0)
    assert price_for_model("other/model", "relay", custom) is None
    assert price_for_model("gpt-5.6-sol", "relay") is None
    assert price_for_model("gpt-5.6-sol", "OpenAI") == MODEL_PRICES["gpt-5.6-sol"]

    with TemporaryDirectory() as temporary:
        root = Path(temporary)
        session_id = "019fc29f-3e06-7e60-9de1-cc7dbdf76f1b"
        path = root / f"rollout-2026-08-03T00-00-00-{session_id}.jsonl"
        records = [
            {"type": "session_meta", "payload": {"id": session_id, "model_provider": "OpenAI"}},
            {"type": "turn_context", "payload": {"model": "gpt-5.6-sol"}},
            {"type": "event_msg", "payload": {"type": "task_started", "model_context_window": 258400}},
            {
                "type": "event_msg",
                "payload": {
                    "type": "token_count",
                    "info": {
                        "total_token_usage": {
                            "input_tokens": 1_250,
                            "cached_input_tokens": 1_000,
                            "output_tokens": 20,
                            "reasoning_output_tokens": 5,
                            "total_tokens": 1_270,
                        },
                        "last_token_usage": {"input_tokens": 900, "output_tokens": 20},
                        "model_context_window": 258400,
                    },
                },
            },
        ]
        for index, record in enumerate(records):
            record["timestamp"] = f"2026-08-03T04:00:0{index}Z"
        path.write_text("".join(json.dumps(item) + "\n" for item in records), encoding="utf-8")
        reader = CodexSessionUsageReader(root)
        snapshot = reader.read(session_id)
        assert snapshot is not None
        assert snapshot.context_tokens == 900 and snapshot.turn.total_tokens == 1_270
        encoded = snapshot.encode()
        assert len(encoded.encode("utf-8")) <= USAGE_SNAPSHOT_MAX_BYTES
        parsed = json.loads(encoded)
        assert parsed["d"] == 2_350 and parsed["e"] == 2_350
        with path.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps({"type": "event_msg", "payload": {"type": "task_started"}}) + "\n")
            handle.write(
                json.dumps(
                    {
                        "type": "event_msg",
                        "payload": {
                            "type": "token_count",
                            "info": {
                                "total_token_usage": {
                                    "input_tokens": 1_500,
                                    "cached_input_tokens": 1_100,
                                    "output_tokens": 30,
                                    "total_tokens": 1_530,
                                },
                                "last_token_usage": {"input_tokens": 950, "output_tokens": 10},
                                "model_context_window": 258400,
                            },
                        },
                    }
                )
                + "\n"
            )
        snapshot = reader.read(session_id)
        assert snapshot is not None and snapshot.turn.total_tokens == 260

        second = root / "rollout-2026-08-02T00-00-00-unknown.jsonl"
        second.write_text(
            "\n".join(
                json.dumps(item)
                for item in (
                    {
                        "timestamp": "2026-08-02T03:00:00Z",
                        "type": "session_meta",
                        "payload": {"model_provider": "relay"},
                    },
                    {
                        "timestamp": "2026-08-02T03:00:01Z",
                        "type": "turn_context",
                        "payload": {"model": "unknown"},
                    },
                    {
                        "timestamp": "2026-08-02T03:00:02Z",
                        "type": "event_msg",
                        "payload": {
                            "type": "token_count",
                            "info": {
                                "total_token_usage": {
                                    "input_tokens": 40,
                                    "output_tokens": 2,
                                    "total_tokens": 42,
                                }
                            },
                        },
                    },
                )
            )
            + "\n",
            encoding="utf-8",
        )
        local_now = lambda: datetime.fromisoformat("2026-08-03T12:00:00+08:00")
        summary = GlobalUsageReader(root, local_now=local_now).read()
        assert summary.today.tokens == 1_270 and summary.today.cost_complete
        assert summary.trend_unit == "tokens"
        assert summary.days[-2].tokens == 42 and not summary.days[-2].cost_complete
        assert len(summary.encode().encode("utf-8")) <= USAGE_SNAPSHOT_MAX_BYTES
        second.write_text("", encoding="utf-8")
        after_truncate = GlobalUsageReader(root, local_now=local_now).read()
        assert after_truncate.days[-2].tokens == 0 and after_truncate.trend_unit == "cost"

        largest = UsageSummary(
            tuple(
                UsageDay(
                    local_now().date() - timedelta(days=6 - index),
                    tokens=(1 << 63) + index,
                    cost_microusd=(1 << 63) + index,
                )
                for index in range(7)
            )
        )
        assert len(largest.encode().encode("utf-8")) <= USAGE_SNAPSHOT_MAX_BYTES


def main() -> int:
    parser = argparse.ArgumentParser(description="Reduce Codex JSONL token usage for Codex Pet")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--session")
    parser.add_argument("--sessions-dir", type=Path, default=DEFAULT_SESSIONS_DIR)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        print("codex_pet_usage self-test ok")
        return 0
    snapshot = CodexSessionUsageReader(args.sessions_dir).read(args.session)
    print(snapshot.encode() if snapshot is not None else '{"v":1,"s":"u"}')
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
