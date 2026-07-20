#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import time
from collections import OrderedDict
from dataclasses import dataclass
from typing import Mapping


PROTOCOL_VERSION = "pet/v1"
MAX_WIRE_BYTES = 192
MAX_TTL_MS = 30_000
DEFAULT_TTL_MS = 30_000
VALID_KINDS = frozenset({"action", "ack", "event", "heartbeat", "state"})
VALID_STATES = frozenset(
    {
        "blocked",
        "connected",
        "disconnected",
        "listening",
        "needs_input",
        "ready",
        "running",
        "transcribing",
    }
)
STATE_PRIORITY = {
    "disconnected": 0,
    "connected": 10,
    "ready": 20,
    "running": 40,
    "listening": 50,
    "transcribing": 55,
    "needs_input": 80,
    "blocked": 90,
}
_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.:-]{0,39}$")
_KIND_TO_WIRE = {"action": "a", "ack": "k", "event": "e", "heartbeat": "h", "state": "s"}
_KIND_FROM_WIRE = {wire: name for name, wire in _KIND_TO_WIRE.items()}
_STATUS_TO_WIRE = {
    "accepted": "a",
    "blocked": "b",
    "connected": "c",
    "disconnected": "x",
    "duplicate": "d",
    "listening": "l",
    "needs_input": "n",
    "ready": "y",
    "rejected": "r",
    "running": "u",
    "transcribing": "t",
}
_STATUS_FROM_WIRE = {wire: name for name, wire in _STATUS_TO_WIRE.items()}
_SUBTYPE_TO_WIRE = {"approval": "a", "credential": "c", "question": "q", "submit": "s"}
_SUBTYPE_FROM_WIRE = {wire: name for name, wire in _SUBTYPE_TO_WIRE.items()}
_ACTION_TO_WIRE = {"ping": "p", "submit_prompt": "s"}
_ACTION_FROM_WIRE = {wire: name for name, wire in _ACTION_TO_WIRE.items()}
_PAYLOAD_TO_WIRE = {
    "action": "a",
    "detail": "d",
    "error": "e",
    "for": "f",
    "saved": "sv",
    "status": "st",
    "submitted": "ok",
    "subtype": "u",
    "text": "x",
    "threadId": "th",
    "workspace": "w",
}
_PAYLOAD_FROM_WIRE = {wire: name for name, wire in _PAYLOAD_TO_WIRE.items()}


class PetProtocolError(ValueError):
    pass


def now_ms() -> int:
    return int(time.time() * 1000)


def _require_int(value: object, label: str, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise PetProtocolError(f"{label} must be an integer between {minimum} and {maximum}")
    return value


def _require_id(value: object, label: str, *, optional: bool = False) -> str | None:
    if value is None and optional:
        return None
    if not isinstance(value, str) or not _ID_RE.fullmatch(value):
        raise PetProtocolError(f"{label} must match {_ID_RE.pattern}")
    return value


def _payload(value: object) -> dict[str, object]:
    if not isinstance(value, dict) or any(not isinstance(key, str) for key in value):
        raise PetProtocolError("payload must be an object with string keys")
    return dict(value)


def _wire_payload(payload: Mapping[str, object]) -> dict[str, object]:
    encoded: dict[str, object] = {}
    for key, value in payload.items():
        wire_key = _PAYLOAD_TO_WIRE.get(key, key)
        if wire_key in encoded:
            raise PetProtocolError(f"payload keys collide on wire key {wire_key!r}")
        if key == "status" and isinstance(value, str):
            value = _STATUS_TO_WIRE.get(value, value)
        elif key == "subtype" and isinstance(value, str):
            value = _SUBTYPE_TO_WIRE.get(value, value)
        elif key == "action" and isinstance(value, str):
            value = _ACTION_TO_WIRE.get(value, value)
        encoded[wire_key] = value
    return encoded


def _read_wire_payload(value: object) -> dict[str, object]:
    wire = _payload(value)
    decoded: dict[str, object] = {}
    for key, item in wire.items():
        name = _PAYLOAD_FROM_WIRE.get(key, key)
        if name in decoded:
            raise PetProtocolError(f"wire payload keys collide on field {name!r}")
        if name == "status" and isinstance(item, str):
            item = _STATUS_FROM_WIRE.get(item, item)
        elif name == "subtype" and isinstance(item, str):
            item = _SUBTYPE_FROM_WIRE.get(item, item)
        elif name == "action" and isinstance(item, str):
            item = _ACTION_FROM_WIRE.get(item, item)
        decoded[name] = item
    return decoded


@dataclass(frozen=True)
class PetEnvelope:
    kind: str
    sequence: int
    message_id: str
    timestamp_ms: int
    ttl_ms: int = DEFAULT_TTL_MS
    task_id: str | None = None
    payload: Mapping[str, object] | None = None
    version: str = PROTOCOL_VERSION

    def __post_init__(self) -> None:
        if self.version != PROTOCOL_VERSION:
            raise PetProtocolError(f"unsupported protocol version: {self.version!r}")
        if self.kind not in VALID_KINDS:
            raise PetProtocolError(f"unsupported envelope kind: {self.kind!r}")
        _require_int(self.sequence, "sequence", 1, 0xFFFFFFFF)
        _require_id(self.message_id, "message_id")
        _require_int(self.timestamp_ms, "timestamp_ms", 0, 9_999_999_999_999)
        _require_int(self.ttl_ms, "ttl_ms", 1, MAX_TTL_MS)
        _require_id(self.task_id, "task_id", optional=True)
        _payload({} if self.payload is None else self.payload)

    @property
    def expires_at_ms(self) -> int:
        return self.timestamp_ms + self.ttl_ms

    def is_expired(self, current_ms: int | None = None) -> bool:
        return (now_ms() if current_ms is None else current_ms) >= self.expires_at_ms

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "v": self.version,
            "k": _KIND_TO_WIRE[self.kind],
            "s": self.sequence,
            "i": self.message_id,
            "t": self.timestamp_ms,
            "l": self.ttl_ms,
            "p": _wire_payload(self.payload or {}),
        }
        if self.task_id is not None:
            result["q"] = self.task_id
        return result

    def encode(self, *, max_bytes: int = MAX_WIRE_BYTES) -> str:
        encoded = json.dumps(self.as_dict(), ensure_ascii=False, separators=(",", ":"))
        size = len(encoded.encode("utf-8"))
        if size > max_bytes:
            raise PetProtocolError(f"pet/v1 envelope is {size} bytes; limit is {max_bytes}")
        return encoded

    @classmethod
    def decode(cls, text: str, *, max_bytes: int = MAX_WIRE_BYTES) -> "PetEnvelope":
        if not isinstance(text, str):
            raise PetProtocolError("pet/v1 wire value must be text")
        size = len(text.encode("utf-8"))
        if size > max_bytes:
            raise PetProtocolError(f"pet/v1 envelope is {size} bytes; limit is {max_bytes}")
        try:
            value = json.loads(text)
        except json.JSONDecodeError as exc:
            raise PetProtocolError(f"invalid pet/v1 JSON: {exc.msg}") from exc
        if not isinstance(value, dict):
            raise PetProtocolError("pet/v1 envelope must be an object")
        allowed = {"v", "k", "s", "i", "t", "l", "q", "p"}
        unknown = set(value) - allowed
        if unknown:
            raise PetProtocolError(f"unknown pet/v1 envelope fields: {sorted(unknown)}")
        return cls(
            version=value.get("v"),
            kind=_KIND_FROM_WIRE.get(value.get("k"), value.get("k")),
            sequence=value.get("s"),
            message_id=value.get("i"),
            timestamp_ms=value.get("t"),
            ttl_ms=value.get("l"),
            task_id=value.get("q"),
            payload=_read_wire_payload(value.get("p")),
        )


@dataclass(frozen=True)
class SequenceDecision:
    status: str
    previous_sequence: int


class SequenceWindow:
    def __init__(self, *, retained: int = 128) -> None:
        if retained <= 0:
            raise ValueError("retained must be positive")
        self.retained = retained
        self.last_sequence = 0
        self._seen: OrderedDict[str, int] = OrderedDict()

    def accept(self, envelope: PetEnvelope) -> SequenceDecision:
        previous = self.last_sequence
        known = self._seen.get(envelope.message_id)
        if known is not None:
            return SequenceDecision("duplicate", previous)
        if previous:
            delta = (envelope.sequence - previous) & 0xFFFFFFFF
            if delta == 0 or delta >= 0x80000000:
                return SequenceDecision("out_of_order", previous)
        self.last_sequence = envelope.sequence
        self._seen[envelope.message_id] = envelope.sequence
        while len(self._seen) > self.retained:
            self._seen.popitem(last=False)
        return SequenceDecision("accepted", previous)

    def accept_unordered(self, envelope: PetEnvelope) -> SequenceDecision:
        previous = self.last_sequence
        if envelope.message_id in self._seen:
            return SequenceDecision("duplicate", previous)
        self.last_sequence = envelope.sequence
        self._seen[envelope.message_id] = envelope.sequence
        while len(self._seen) > self.retained:
            self._seen.popitem(last=False)
        return SequenceDecision("accepted", previous)


@dataclass(frozen=True)
class PetState:
    status: str
    task_id: str | None
    subtype: str | None
    detail: str | None
    sequence: int
    expires_at_ms: int


class EventReducer:
    def __init__(self) -> None:
        self._events: dict[str, tuple[PetEnvelope, PetState]] = {}

    def apply(self, envelope: PetEnvelope, *, current_ms: int | None = None) -> bool:
        current = now_ms() if current_ms is None else current_ms
        if envelope.kind not in {"event", "heartbeat", "state"} or envelope.is_expired(current):
            return False
        if envelope.message_id in self._events:
            return False
        payload = dict(envelope.payload or {})
        status = payload.get("status")
        if not isinstance(status, str) or status not in VALID_STATES:
            raise PetProtocolError(f"invalid pet state: {status!r}")
        subtype = payload.get("subtype")
        detail = payload.get("detail")
        if subtype is not None and not isinstance(subtype, str):
            raise PetProtocolError("state subtype must be a string")
        if detail is not None and not isinstance(detail, str):
            raise PetProtocolError("state detail must be a string")
        self._events[envelope.message_id] = (
            envelope,
            PetState(status, envelope.task_id, subtype, detail, envelope.sequence, envelope.expires_at_ms),
        )
        self._prune(current)
        return True

    def current(self, *, current_ms: int | None = None) -> PetState:
        current = now_ms() if current_ms is None else current_ms
        self._prune(current)
        if not self._events:
            return PetState("disconnected", None, None, None, 0, current)
        _, state = max(
            self._events.values(),
            key=lambda item: (STATE_PRIORITY[item[1].status], item[1].sequence),
        )
        return state

    def _prune(self, current_ms: int) -> None:
        expired = [key for key, (envelope, _) in self._events.items() if envelope.is_expired(current_ms)]
        for key in expired:
            del self._events[key]


def make_ack(
    request: PetEnvelope,
    *,
    sequence: int,
    status: str,
    current_ms: int | None = None,
    error: str | None = None,
    payload: Mapping[str, object] | None = None,
    task_id: str | None = None,
) -> PetEnvelope:
    if status not in {"accepted", "duplicate", "rejected"}:
        raise PetProtocolError(f"invalid ACK status: {status!r}")
    ack_payload = {"for": request.message_id, "status": status}
    if error:
        ack_payload["error"] = error[:40]
    if payload:
        ack_payload.update(payload)
    ack_id = f"ack:{request.message_id}"
    if len(ack_id) > 40:
        digest = hashlib.sha256(request.message_id.encode("utf-8")).hexdigest()[:16]
        ack_id = f"ack:{request.message_id[:18]}:{digest}"
    return PetEnvelope(
        kind="ack",
        sequence=sequence,
        message_id=ack_id,
        task_id=request.task_id if task_id is None else task_id,
        timestamp_ms=now_ms() if current_ms is None else current_ms,
        ttl_ms=min(request.ttl_ms, 10_000),
        payload=ack_payload,
    )


def run_self_test() -> None:
    base = 1_900_000_000_000
    envelope = PetEnvelope(
        kind="action",
        sequence=7,
        message_id="voice:7",
        task_id="task:1",
        timestamp_ms=base,
        ttl_ms=30_000,
        payload={"action": "submit", "text": "检查项目"},
    )
    assert PetEnvelope.decode(envelope.encode()) == envelope
    assert not envelope.is_expired(base + 29_999)
    assert envelope.is_expired(base + 30_000)
    try:
        PetEnvelope.decode(envelope.encode() + " " * MAX_WIRE_BYTES)
        raise AssertionError("oversized envelope was accepted")
    except PetProtocolError:
        pass
    try:
        PetEnvelope("action", 0, "bad", base, payload={})
        raise AssertionError("zero sequence was accepted")
    except PetProtocolError:
        pass

    window = SequenceWindow(retained=2)
    assert window.accept(envelope).status == "accepted"
    assert window.accept(envelope).status == "duplicate"
    older = PetEnvelope("action", 6, "voice:6", base, payload={"action": "noop"})
    assert window.accept(older).status == "out_of_order"
    assert window.accept_unordered(older).status == "accepted"
    assert window.accept_unordered(older).status == "duplicate"

    wrap_window = SequenceWindow()
    near_wrap = PetEnvelope("action", 0xFFFFFFFE, "wrap:before", base, payload={"action": "noop"})
    at_wrap = PetEnvelope("action", 0xFFFFFFFF, "wrap:last", base, payload={"action": "noop"})
    after_wrap = PetEnvelope("action", 1, "wrap:first", base, payload={"action": "noop"})
    stale_wrap = PetEnvelope("action", 0xFFFFFFFD, "wrap:stale", base, payload={"action": "noop"})
    assert wrap_window.accept(near_wrap).status == "accepted"
    assert wrap_window.accept(at_wrap).status == "accepted"
    assert wrap_window.accept(after_wrap).status == "accepted"
    assert wrap_window.accept(stale_wrap).status == "out_of_order"

    reducer = EventReducer()
    heartbeat = PetEnvelope("heartbeat", 1, "hb:1", base, 30_000, payload={"status": "connected"})
    running = PetEnvelope("event", 2, "evt:run", base, 10_000, "task:1", {"status": "running"})
    blocked = PetEnvelope("event", 3, "evt:block", base, 5_000, "task:2", {"status": "blocked"})
    assert reducer.apply(heartbeat, current_ms=base)
    assert reducer.apply(running, current_ms=base)
    assert reducer.apply(blocked, current_ms=base)
    assert not reducer.apply(blocked, current_ms=base)
    assert reducer.current(current_ms=base).status == "blocked"
    assert reducer.current(current_ms=base + 5_000).status == "running"
    assert reducer.current(current_ms=base + 10_000).status == "connected"
    assert reducer.current(current_ms=base + 30_000).status == "disconnected"

    ack = make_ack(envelope, sequence=8, status="accepted", current_ms=base)
    assert PetEnvelope.decode(ack.encode()).payload == {"for": "voice:7", "status": "accepted"}
    assert len(heartbeat.encode().encode("utf-8")) <= MAX_WIRE_BYTES
    worst_state = PetEnvelope(
        "state",
        0xFFFFFFFF,
        "s:4294967295",
        9_999_999_999_999,
        MAX_TTL_MS,
        "019f614c-9d13-7040-80a9-559a7fca2f55",
        {"status": "needs_input", "subtype": "credential", "detail": "x" * 24},
    )
    assert len(worst_state.encode().encode("utf-8")) <= MAX_WIRE_BYTES


def main() -> int:
    parser = argparse.ArgumentParser(description="Huangshan Codex Pet pet/v1 protocol")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("use --self-test")
    run_self_test()
    print("codex_pet_protocol self-test ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
