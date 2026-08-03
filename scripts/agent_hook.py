#!/usr/bin/env python3
"""Unified multi-agent lifecycle hook entry for VibeBoard Codex Pet.

Reads one hook event as JSON on stdin, normalizes it through the selected
AgentAdapter, and delivers it to the Bridge over the local pet/v1 socket. Safe
to run when the optional Bridge is offline: it never blocks or fails the agent
turn. Existing Codex users keep pointing their ~/.codex/hooks.json at
codex_pet_hook.py; this entry is how Claude Code (and future agents) attach.

    python3 agent_hook.py --agent claude_code               # deliver one event
    python3 agent_hook.py --agent claude_code --print-config # settings snippet
    python3 agent_hook.py --self-test
"""
from __future__ import annotations

import argparse
import asyncio
import json
import sys
from pathlib import Path

from agent_adapters import (
    ADAPTERS,
    DEFAULT_SOCKET,
    ack_accepted,
    get_adapter,
    render_hook_config,
    send,
)
from agent_adapters import self_test as adapters_self_test


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="VibeBoard multi-agent Codex Pet hook")
    parser.add_argument(
        "--agent",
        choices=sorted(ADAPTERS),
        default="claude_code",
        help="source agent whose hook payload arrives on stdin",
    )
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument(
        "--print-config",
        action="store_true",
        help="print a ready-to-merge hooks config for --agent and exit",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit non-zero when diagnostic hook delivery fails",
    )
    parser.add_argument("--print-ack", action="store_true", help="print the pet/v1 ACK")
    args = parser.parse_args(argv)

    if args.self_test:
        adapters_self_test()
        return 0

    if args.print_config:
        script = Path(__file__).resolve()
        config = render_hook_config(
            args.agent, python=sys.executable or "python3", script=script
        )
        print(json.dumps(config, indent=2, ensure_ascii=False))
        return 0

    adapter = get_adapter(args.agent)
    try:
        value = json.loads(sys.stdin.read())
        if not isinstance(value, dict):
            return 0
        envelope = adapter.build_envelope(value)
        if envelope is None:
            return 0
        ack = asyncio.run(send(args.socket, envelope))
        if args.print_ack:
            print(ack.encode(max_bytes=16_384))
        if not ack_accepted(ack) and args.strict:
            print("Codex Pet Bridge rejected the hook event", file=sys.stderr)
            return 2
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        # Hooks must never block or fail the agent turn when the Bridge is offline.
        if args.strict:
            print(f"Codex Pet hook delivery failed: {type(exc).__name__}", file=sys.stderr)
            return 2
        return 0
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
