#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import hashlib
import json
import os
import sys
import tempfile
import time
from pathlib import Path
from typing import Mapping

from codex_pet_protocol import PetEnvelope
from voice_bridge_common import truncate_utf8


DEFAULT_SOCKET = Path(
    os.environ.get(
        "CODEX_PET_SOCKET",
        str(Path(tempfile.gettempdir()) / f"huangshan-codex-pet-{os.getuid()}.sock"),
    )
)
_EVENTS = {
    "SessionStart": ("connected", None, "Session opened"),
    "UserPromptSubmit": ("running", "submit", "Thinking"),
    "PreToolUse": ("running", None, "Using a tool"),
    "PermissionRequest": ("needs_input", "approval", "Approval required"),
    "PostToolUse": ("running", None, "Processing result"),
    "Stop": ("ready", None, "Task complete"),
    "SubagentStop": ("ready", None, "Subtask complete"),
}
HOOK_DELIVERY_TIMEOUT_SECONDS = 1.5


def hook_envelope(value: Mapping[str, object], *, sequence: int | None = None) -> PetEnvelope | None:
    event = value.get("hook_event_name")
    if not isinstance(event, str) or event not in _EVENTS:
        return None
    status, subtype, detail = _EVENTS[event]
    session = value.get("session_id")
    if not isinstance(session, str) or not session or len(session) > 40:
        return None
    turn_id = value.get("turn_id")
    if not isinstance(turn_id, str) or not turn_id:
        turn_id = None
    cwd = value.get("cwd")
    project = Path(cwd).name if isinstance(cwd, str) and cwd else "Codex"
    project = truncate_utf8(" ".join(project.split()), 56) or "Codex"
    tool = value.get("tool_name")
    if not isinstance(tool, str):
        tool = ""
    tool = truncate_utf8(" ".join(tool.split()), 24)
    if event == "PreToolUse" and tool:
        detail = f"Using {tool}"
    elif event == "PostToolUse" and tool:
        detail = f"Finished {tool}"
    digest = hashlib.sha256(f"{session}:{turn_id}:{event}".encode("utf-8")).hexdigest()[:12]
    seq = int(time.time_ns() // 1000) & 0xFFFFFFFF if sequence is None else sequence
    payload: dict[str, object] = {
        "action": "hook_event",
        "status": status,
        "subtype": subtype,
        "detail": truncate_utf8(detail, 64),
        "project": project,
        "event": event,
    }
    if turn_id:
        payload["turnId"] = truncate_utf8(turn_id, 64)
    if tool:
        payload["tool"] = tool
    return PetEnvelope(
        kind="action",
        sequence=max(1, seq),
        message_id=f"hook:{seq}:{digest}",
        task_id=session,
        timestamp_ms=int(time.time() * 1000),
        payload=payload,
    )


async def send(path: Path, envelope: PetEnvelope) -> PetEnvelope:
    async def exchange() -> PetEnvelope:
        reader, writer = await asyncio.open_unix_connection(str(path))
        try:
            writer.write(envelope.encode(max_bytes=16_384).encode("utf-8") + b"\n")
            await writer.drain()
            line = await reader.readline()
            if not line:
                raise RuntimeError("Bridge returned no hook ACK")
            return PetEnvelope.decode(line.decode("utf-8"), max_bytes=16_384)
        finally:
            writer.close()
            await writer.wait_closed()

    return await asyncio.wait_for(exchange(), timeout=HOOK_DELIVERY_TIMEOUT_SECONDS)


def ack_accepted(envelope: PetEnvelope) -> bool:
    payload = envelope.payload or {}
    return payload.get("status") == "accepted" and payload.get("observed") is True


def self_test() -> None:
    envelope = hook_envelope(
        {
            "hook_event_name": "PermissionRequest",
            "session_id": "session-1",
            "turn_id": "turn_1",
            "cwd": "/work/sample-project",
            "tool_name": "Bash",
            "tool_input": {"command": "secret command"},
            "prompt": "secret prompt",
        },
        sequence=7,
    )
    assert envelope is not None
    assert envelope.payload == {
        "action": "hook_event",
        "status": "needs_input",
        "subtype": "approval",
        "detail": "Approval required",
        "project": "sample-project",
        "event": "PermissionRequest",
        "turnId": "turn_1",
        "tool": "Bash",
    }
    assert envelope.task_id == "session-1"
    assert "secret command" not in envelope.encode(max_bytes=16_384)
    assert "secret prompt" not in envelope.encode(max_bytes=16_384)
    ack = PetEnvelope(
        kind="ack",
        sequence=9,
        message_id="ack:9",
        timestamp_ms=1,
        payload={"status": "accepted", "observed": True},
    )
    assert ack_accepted(ack)
    assert hook_envelope({"hook_event_name": "Unknown"}) is None
    assert hook_envelope({"hook_event_name": "Stop", "session_id": "session-1", "transcript_path": "/secret/path"}, sequence=8)
    assert hook_envelope({"hook_event_name": "Stop"}, sequence=8) is None
    print("codex_pet_hook self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Safe Codex lifecycle hook bridge for Codex Pet")
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--strict", action="store_true", help="Exit non-zero when diagnostic hook delivery fails")
    parser.add_argument("--print-ack", action="store_true", help="Print the safe pet/v1 ACK for diagnostics")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    try:
        raw = sys.stdin.read()
        value = json.loads(raw)
        if not isinstance(value, dict):
            return 0
        envelope = hook_envelope(value)
        if envelope is None:
            return 0
        ack = asyncio.run(send(args.socket, envelope))
        if args.print_ack:
            print(ack.encode(max_bytes=16_384))
        if not ack_accepted(ack):
            if args.strict:
                print("Codex Pet Bridge rejected the hook event", file=sys.stderr)
                return 2
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        # Hooks must never block or fail the Codex turn when the optional Bridge is offline.
        if args.strict:
            print(f"Codex Pet hook delivery failed: {type(exc).__name__}", file=sys.stderr)
            return 2
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
