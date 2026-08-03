#!/usr/bin/env python3
"""Multi-agent hook adapter layer for VibeBoard Codex Pet.

Different AI coding agents (Codex, Claude Code, ...) emit lifecycle hook events
with slightly different vocabularies. This module normalizes them into the one
agent-neutral pet/v1 ``hook_event`` envelope that the Bridge/Monitor pipeline
already consumes, so adding a new agent means implementing one AgentAdapter and
registering it -- no Bridge, Monitor, or protocol changes.

The Codex adapter delegates to ``codex_pet_hook.hook_envelope`` (the existing,
audited, byte-for-byte source of truth). Other adapters translate their event
vocabulary into that same builder, which strips everything except the fields it
constructs itself, so agent-specific message/prompt/tool_input never leak to
the board.
"""
from __future__ import annotations

from pathlib import Path
from typing import Mapping, Sequence

from codex_pet_hook import DEFAULT_SOCKET, ack_accepted, hook_envelope, send
from codex_pet_protocol import PetEnvelope

__all__ = [
    "DEFAULT_SOCKET",
    "ack_accepted",
    "send",
    "AgentAdapter",
    "CodexAdapter",
    "ClaudeCodeAdapter",
    "ADAPTERS",
    "get_adapter",
    "render_hook_config",
    "self_test",
]

# Claude Code lifecycle event name -> Codex-equivalent event understood by
# codex_pet_hook.hook_envelope(). Events not listed here (and non-permission
# Notifications) are intentionally ignored: they carry no board-relevant state.
_CLAUDE_TO_CODEX_EVENT = {
    "SessionStart": "SessionStart",
    "UserPromptSubmit": "UserPromptSubmit",
    "PreToolUse": "PreToolUse",
    "PostToolUse": "PostToolUse",
    "Stop": "Stop",
    "SubagentStop": "SubagentStop",
}
# Claude Code has no dedicated PermissionRequest event; permission prompts
# arrive as a Notification whose message mentions permission. Only those map to
# Codex PermissionRequest semantics (yellow needs_input). We inspect the message
# text solely to classify it -- the text itself is never forwarded to the board.
_PERMISSION_HINTS = ("permission", "approve", "approval", "needs your", "waiting for your")


class AgentAdapter:
    """Base adapter: translate one agent's raw hook payload into a pet/v1
    ``hook_event`` envelope, and describe how its hooks are installed."""

    id: str = ""
    display_name: str = ""
    config_target: str = ""  # human-facing config file location
    # (event name, uses tool-name matcher) in canonical install order.
    hook_events: tuple[tuple[str, bool], ...] = ()

    def build_envelope(
        self, raw: Mapping[str, object], *, sequence: int | None = None
    ) -> PetEnvelope | None:
        raise NotImplementedError


class CodexAdapter(AgentAdapter):
    """Codex Desktop (ChatGPT.app). Delegates to the existing audited builder."""

    id = "codex"
    display_name = "Codex (ChatGPT.app)"
    config_target = "~/.codex/hooks.json"
    hook_events = (
        ("SessionStart", False),
        ("PermissionRequest", True),
        ("UserPromptSubmit", False),
        ("PreToolUse", True),
        ("PostToolUse", True),
        ("Stop", False),
    )

    def build_envelope(self, raw, *, sequence=None):
        return hook_envelope(raw, sequence=sequence)


class ClaudeCodeAdapter(AgentAdapter):
    """Claude Code. Notify-only: permission prompts show needs_input on the
    board but no Allow/Deny buttons (v1). Translates Claude Code's event
    vocabulary into the shared Codex builder, forwarding only safe fields."""

    id = "claude_code"
    display_name = "Claude Code"
    config_target = "~/.claude/settings.json"
    hook_events = (
        ("SessionStart", False),
        ("UserPromptSubmit", False),
        ("PreToolUse", True),
        ("PostToolUse", True),
        ("Notification", False),
        ("Stop", False),
        ("SubagentStop", False),
    )

    def build_envelope(self, raw, *, sequence=None):
        event = raw.get("hook_event_name")
        if not isinstance(event, str):
            return None
        codex_event = self._translate_event(event, raw)
        if codex_event is None:
            return None
        # Re-shape into codex_pet_hook's input contract, forwarding ONLY the
        # fields it already sanitizes (basename of cwd, short tool name). Message,
        # prompt, tool_input and transcript are deliberately dropped here.
        translated = {
            "hook_event_name": codex_event,
            "session_id": raw.get("session_id"),
            "turn_id": None,  # Claude Code has no turn id
            "cwd": raw.get("cwd"),
            "tool_name": raw.get("tool_name"),
        }
        return hook_envelope(translated, sequence=sequence)

    @staticmethod
    def _translate_event(event: str, raw: Mapping[str, object]) -> str | None:
        mapped = _CLAUDE_TO_CODEX_EVENT.get(event)
        if mapped is not None:
            return mapped
        if event == "Notification":
            message = raw.get("message")
            text = message.lower() if isinstance(message, str) else ""
            if any(hint in text for hint in _PERMISSION_HINTS):
                return "PermissionRequest"
        return None
ADAPTERS: dict[str, AgentAdapter] = {
    adapter.id: adapter for adapter in (CodexAdapter(), ClaudeCodeAdapter())
}


def get_adapter(agent_id: str) -> AgentAdapter:
    try:
        return ADAPTERS[agent_id]
    except KeyError:
        known = ", ".join(sorted(ADAPTERS))
        raise KeyError(f"unknown agent {agent_id!r}; known agents: {known}") from None


def render_hook_config(
    agent_id: str, *, python: str, script: Path, timeout: int = 5
) -> dict:
    """Build a ready-to-merge hooks config fragment for the given agent.

    Codex keeps its canonical ~/.codex/hooks.json pointing at codex_pet_hook.py
    (unchanged for existing users); other agents point at the unified entry with
    an explicit --agent flag.
    """
    adapter = get_adapter(agent_id)
    if agent_id == "codex":
        command = f"{python} {script}"
    else:
        command = f"{python} {script} --agent {agent_id}"
    hooks: dict[str, list] = {}
    for event, matched in adapter.hook_events:
        leaf = {"type": "command", "command": command, "timeout": timeout}
        entry = {"matcher": "*", "hooks": [leaf]} if matched else {"hooks": [leaf]}
        hooks[event] = [entry]
    return {"hooks": hooks}


def self_test() -> None:
    # 1. Codex path stays identical to codex_pet_hook (single source of truth).
    codex = get_adapter("codex")
    env = codex.build_envelope(
        {
            "hook_event_name": "PermissionRequest",
            "session_id": "s1",
            "turn_id": "t1",
            "cwd": "/work/proj",
            "tool_name": "Bash",
            "tool_input": {"command": "SECRETCMD"},
            "prompt": "SECRETPROMPT",
        },
        sequence=7,
    )
    assert env is not None and env.task_id == "s1"
    assert env.payload["status"] == "needs_input"
    assert env.payload["event"] == "PermissionRequest"
    assert env.payload["project"] == "proj"
    encoded = env.encode(max_bytes=16_384)
    assert "SECRETCMD" not in encoded and "SECRETPROMPT" not in encoded

    cc = get_adapter("claude_code")

    # 2. Tool use -> running; project from cwd; secrets stripped; no turn id.
    env = cc.build_envelope(
        {
            "hook_event_name": "PreToolUse",
            "session_id": "cc-1",
            "cwd": "/home/me/app",
            "tool_name": "Bash",
            "tool_input": {"command": "rm -rf SECRETPATH"},
        },
        sequence=3,
    )
    assert env is not None and env.task_id == "cc-1"
    assert env.payload["status"] == "running" and env.payload["project"] == "app"
    assert env.payload.get("turnId") is None
    encoded = env.encode(max_bytes=16_384)
    assert "SECRETPATH" not in encoded and "rm -rf" not in encoded

    # 3. Permission Notification -> needs_input/approval, but NO board buttons
    #    (no approvalClass) and the message text is never forwarded.
    env = cc.build_envelope(
        {
            "hook_event_name": "Notification",
            "session_id": "cc-1",
            "cwd": "/home/me/app",
            "message": "Claude needs your permission to run SECRETMSG",
        },
        sequence=4,
    )
    assert env is not None and env.payload["status"] == "needs_input"
    assert env.payload["subtype"] == "approval"
    assert "approvalClass" not in env.payload
    assert "SECRETMSG" not in env.encode(max_bytes=16_384)

    # 4. Idle Notification and unknown events are ignored.
    assert (
        cc.build_envelope(
            {
                "hook_event_name": "Notification",
                "session_id": "cc-1",
                "cwd": "/home/me/app",
                "message": "Task finished, see you later",
            },
            sequence=5,
        )
        is None
    )
    assert cc.build_envelope({"hook_event_name": "PreCompact", "session_id": "cc-1"}) is None
    assert cc.build_envelope({"hook_event_name": "SessionEnd", "session_id": "cc-1"}) is None

    # 5. Stop -> ready; SubagentStop -> ready.
    for stop_event in ("Stop", "SubagentStop"):
        env = cc.build_envelope(
            {"hook_event_name": stop_event, "session_id": "cc-1", "cwd": "/home/me/app"},
            sequence=6,
        )
        assert env is not None and env.payload["status"] == "ready"

    # 6. Config rendering: matcher on tool events, --agent flag for non-codex.
    cfg = render_hook_config("claude_code", python="/usr/bin/python3", script=Path("/x/agent_hook.py"))
    assert set(cfg["hooks"]) >= {"SessionStart", "PreToolUse", "Notification", "Stop"}
    assert cfg["hooks"]["PreToolUse"][0]["matcher"] == "*"
    assert "matcher" not in cfg["hooks"]["Stop"][0]
    assert "--agent claude_code" in cfg["hooks"]["Stop"][0]["hooks"][0]["command"]
    codex_cfg = render_hook_config("codex", python="/usr/bin/python3", script=Path("/x/codex_pet_hook.py"))
    assert "--agent" not in codex_cfg["hooks"]["Stop"][0]["hooks"][0]["command"]

    # 7. Unknown agent is rejected.
    try:
        get_adapter("nope")
    except KeyError:
        pass
    else:  # pragma: no cover
        raise AssertionError("unknown agent should raise")

    print("agent_adapters self-test ok")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="VibeBoard multi-agent hook adapters")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()


