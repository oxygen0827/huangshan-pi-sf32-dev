#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Awaitable, Callable, Mapping

from codex_pet_protocol import PetEnvelope, now_ms


PROTOCOL_VERSION = "2025-06-18"
SUPPORTED_PROTOCOLS = {PROTOCOL_VERSION, "2025-03-26", "2024-11-05"}
DEFAULT_SOCKET = Path(tempfile.gettempdir()) / f"huangshan-codex-pet-{os.getuid()}.sock"
MAX_IPC_LINE_BYTES = 16_384
MAX_JSONRPC_LINE_BYTES = 64 * 1024


class MCPError(RuntimeError):
    pass


def _schema(properties: Mapping[str, object] | None = None, required: list[str] | None = None) -> dict[str, object]:
    return {
        "type": "object",
        "properties": dict(properties or {}),
        "required": list(required or []),
        "additionalProperties": False,
    }


def _annotations(title: str, *, read_only: bool, idempotent: bool = True) -> dict[str, object]:
    return {
        "title": title,
        "readOnlyHint": read_only,
        "destructiveHint": False,
        "idempotentHint": idempotent,
        "openWorldHint": False,
    }


TOOLS: list[dict[str, object]] = [
    {
        "name": "huangshan_status",
        "description": "Read the encrypted Huangshan Pi Runtime connection status.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read board status", read_only=True),
    },
    {
        "name": "huangshan_capabilities",
        "description": "Read the board's machine-readable Runtime capabilities.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read board capabilities", read_only=True),
    },
    {
        "name": "huangshan_sensors",
        "description": "Read the current safe sensor snapshot exposed by Runtime.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read sensor snapshot", read_only=True),
    },
    {
        "name": "huangshan_power",
        "description": "Read battery and power state from the board.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read power state", read_only=True),
    },
    {
        "name": "huangshan_rgb",
        "description": "Read the current RGB indicator state.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read RGB state", read_only=True),
    },
    {
        "name": "huangshan_pet_status",
        "description": "Read the exact task, alert, animation, and selected-pet state shown by Codex Pet.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read Codex Pet state", read_only=True),
    },
    {
        "name": "huangshan_display",
        "description": "Read the current display state and brightness.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read display state", read_only=True),
    },
    {
        "name": "huangshan_app_status",
        "description": "Read the active Runtime App status.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read active App", read_only=True),
    },
    {
        "name": "huangshan_apps",
        "description": "List installed Runtime Apps.",
        "inputSchema": _schema(),
        "annotations": _annotations("List installed Apps", read_only=True),
    },
    {
        "name": "huangshan_audio",
        "description": "Read optional external-speaker playback state and codec arbitration state.",
        "inputSchema": _schema(),
        "annotations": _annotations("Read audio state", read_only=True),
    },
    {
        "name": "huangshan_set_rgb",
        "description": "Set the RGB indicator to one validated named color.",
        "inputSchema": _schema(
            {
                "color": {
                    "type": "string",
                    "enum": ["off", "red", "green", "blue", "yellow", "cyan", "magenta", "white"],
                }
            },
            ["color"],
        ),
        "annotations": _annotations("Set RGB color", read_only=False),
    },
    {
        "name": "huangshan_set_brightness",
        "description": "Set display brightness from 0 through 100.",
        "inputSchema": _schema(
            {"brightness": {"type": "integer", "minimum": 0, "maximum": 100}},
            ["brightness"],
        ),
        "annotations": _annotations("Set display brightness", read_only=False),
    },
    {
        "name": "huangshan_show_message",
        "description": "Show a short UTF-8 message in the Codex Pet App.",
        "inputSchema": _schema(
            {"text": {"type": "string", "minLength": 1, "maxLength": 160}},
            ["text"],
        ),
        "annotations": _annotations("Show Pet message", read_only=False, idempotent=False),
    },
    {
        "name": "huangshan_launch_app",
        "description": "Launch one already-installed Runtime App by validated App id.",
        "inputSchema": _schema(
            {"app_id": {"type": "string", "pattern": "^[a-z][a-z0-9_]{0,14}$"}},
            ["app_id"],
        ),
        "annotations": _annotations("Launch installed App", read_only=False, idempotent=False),
    },
    {
        "name": "huangshan_select_pet",
        "description": "Select the animated companion shown on the Huangshan Codex Pet display.",
        "inputSchema": _schema(
            {"slug": {"type": "string", "enum": ["boba", "boxcat", "shinchan"]}},
            ["slug"],
        ),
        "annotations": _annotations("Select board pet", read_only=False),
    },
    {
        "name": "huangshan_play_cue",
        "description": "Play one bundled Codex Pet cue when an external speaker is attached.",
        "inputSchema": _schema(
            {"cue": {"type": "string", "enum": ["listening", "submitted", "needs_input", "done", "error"]}},
            ["cue"],
        ),
        "annotations": _annotations("Play Pet cue", read_only=False, idempotent=False),
    },
    {
        "name": "huangshan_stop_audio",
        "description": "Stop optional speaker playback without affecting a microphone capture.",
        "inputSchema": _schema(),
        "annotations": _annotations("Stop speaker playback", read_only=False),
    },
]

TOOL_OPERATIONS = {
    "huangshan_status": "status",
    "huangshan_capabilities": "capabilities",
    "huangshan_sensors": "sensors",
    "huangshan_power": "power",
    "huangshan_rgb": "rgb_status",
    "huangshan_pet_status": "pet_status",
    "huangshan_display": "display_status",
    "huangshan_app_status": "app_status",
    "huangshan_apps": "apps",
    "huangshan_audio": "audio_status",
    "huangshan_set_rgb": "set_rgb",
    "huangshan_set_brightness": "set_brightness",
    "huangshan_show_message": "show_message",
    "huangshan_launch_app": "launch_app",
    "huangshan_select_pet": "select_pet",
    "huangshan_play_cue": "play_cue",
    "huangshan_stop_audio": "stop_audio",
}


class BridgeCaller:
    def __init__(self, socket_path: Path) -> None:
        self.socket_path = socket_path
        self._sequence = now_ms() & 0xFFFFFFFF

    def _next_sequence(self) -> int:
        self._sequence = (self._sequence + 1) & 0xFFFFFFFF
        if self._sequence == 0:
            self._sequence = 1
        return self._sequence

    async def __call__(self, operation: str, arguments: Mapping[str, object]) -> str:
        request = PetEnvelope(
            kind="action",
            sequence=self._next_sequence(),
            message_id=f"mcp:{uuid.uuid4().hex[:20]}",
            timestamp_ms=now_ms(),
            payload={"action": "hardware_command", "op": operation, "arguments": dict(arguments)},
        )
        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_unix_connection(str(self.socket_path)), timeout=3.0
            )
        except (OSError, asyncio.TimeoutError) as exc:
            raise MCPError("Codex Pet Bridge is unavailable") from exc
        try:
            encoded = request.encode(max_bytes=MAX_IPC_LINE_BYTES).encode("utf-8") + b"\n"
            writer.write(encoded)
            await writer.drain()
            line = await asyncio.wait_for(reader.readline(), timeout=20.0)
        finally:
            writer.close()
            await writer.wait_closed()
        if not line or len(line) > MAX_IPC_LINE_BYTES:
            raise MCPError("Codex Pet Bridge returned an invalid response")
        try:
            response = PetEnvelope.decode(line.decode("utf-8").strip(), max_bytes=MAX_IPC_LINE_BYTES)
        except Exception as exc:
            raise MCPError("Codex Pet Bridge returned an invalid response") from exc
        payload = dict(response.payload or {})
        if payload.get("status") != "accepted":
            raise MCPError("The board rejected the hardware request")
        result = payload.get("result")
        if not isinstance(result, str):
            raise MCPError("Codex Pet Bridge omitted the hardware result")
        return result


ToolCaller = Callable[[str, Mapping[str, object]], Awaitable[str]]


class MCPServer:
    def __init__(self, caller: ToolCaller) -> None:
        self.caller = caller
        self.initialized = False

    async def handle(self, message: object) -> dict[str, object] | None:
        if not isinstance(message, dict) or message.get("jsonrpc") != "2.0":
            return _error(None, -32600, "Invalid Request")
        has_id = "id" in message
        request_id = message.get("id")
        method = message.get("method")
        if not isinstance(method, str):
            return _error(request_id, -32600, "Invalid Request")
        if not has_id:
            return None
        params = message.get("params", {})
        if not isinstance(params, dict):
            return _error(request_id, -32602, "Invalid params")
        if method == "initialize":
            requested = params.get("protocolVersion")
            if (
                not isinstance(requested, str)
                or not isinstance(params.get("capabilities"), dict)
                or not isinstance(params.get("clientInfo"), dict)
            ):
                return _error(request_id, -32602, "Invalid params")
            negotiated = requested if requested in SUPPORTED_PROTOCOLS else PROTOCOL_VERSION
            self.initialized = True
            return _result(
                request_id,
                {
                    "protocolVersion": negotiated,
                    "capabilities": {"tools": {"listChanged": False}},
                    "serverInfo": {"name": "huangshan-codex-pet", "version": "1.0.0"},
                },
            )
        if method == "ping":
            return _result(request_id, {})
        if not self.initialized:
            return _error(request_id, -32002, "Server is not initialized")
        if method == "tools/list":
            if set(params) - {"cursor"} or ("cursor" in params and not isinstance(params["cursor"], str)):
                return _error(request_id, -32602, "Invalid params")
            return _result(request_id, {"tools": TOOLS})
        if method == "tools/call":
            return await self._call_tool(request_id, params)
        return _error(request_id, -32601, "Method not found")

    async def _call_tool(self, request_id: object, params: Mapping[str, object]) -> dict[str, object]:
        if set(params) - {"name", "arguments"}:
            return _error(request_id, -32602, "Invalid params")
        name = params.get("name")
        arguments = params.get("arguments", {})
        operation = TOOL_OPERATIONS.get(name) if isinstance(name, str) else None
        if operation is None or not isinstance(arguments, dict):
            return _error(request_id, -32602, "Unknown tool or invalid arguments")
        try:
            result = await self.caller(operation, arguments)
        except MCPError as exc:
            return _result(request_id, {"content": [{"type": "text", "text": str(exc)}], "isError": True})
        content: dict[str, object] = {"content": [{"type": "text", "text": result}], "isError": False}
        try:
            structured = json.loads(result)
        except json.JSONDecodeError:
            pass
        else:
            if isinstance(structured, dict):
                content["structuredContent"] = structured
        return _result(request_id, content)


def _result(request_id: object, result: object) -> dict[str, object]:
    return {"jsonrpc": "2.0", "id": request_id, "result": result}


def _error(request_id: object, code: int, message: str) -> dict[str, object]:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


async def run_stdio(server: MCPServer) -> None:
    while True:
        line = await asyncio.to_thread(sys.stdin.buffer.readline, MAX_JSONRPC_LINE_BYTES + 1)
        if not line:
            return
        if len(line) > MAX_JSONRPC_LINE_BYTES:
            response = _error(None, -32700, "Parse error")
        else:
            try:
                message = json.loads(line)
            except (UnicodeDecodeError, json.JSONDecodeError):
                response = _error(None, -32700, "Parse error")
            else:
                response = await server.handle(message)
        if response is not None:
            sys.stdout.write(json.dumps(response, ensure_ascii=False, separators=(",", ":")) + "\n")
            sys.stdout.flush()


async def self_test_async() -> None:
    calls: list[tuple[str, Mapping[str, object]]] = []

    async def fake_call(operation: str, arguments: Mapping[str, object]) -> str:
        calls.append((operation, dict(arguments)))
        if operation == "power":
            raise MCPError("test rejection")
        return json.dumps({"op": operation, "arguments": dict(arguments)}, separators=(",", ":"))

    server = MCPServer(fake_call)
    pre_init = await server.handle({"jsonrpc": "2.0", "id": 0, "method": "tools/list", "params": {}})
    assert pre_init and pre_init["error"]["code"] == -32002  # type: ignore[index]
    initialized = await server.handle({
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {"protocolVersion": PROTOCOL_VERSION, "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}},
    })
    assert initialized and initialized["result"]["protocolVersion"] == PROTOCOL_VERSION  # type: ignore[index]
    assert await server.handle({"jsonrpc": "2.0", "method": "notifications/initialized"}) is None
    listed = await server.handle({"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}})
    tools = listed["result"]["tools"]  # type: ignore[index]
    assert len(tools) == len(TOOL_OPERATIONS)
    assert all("annotations" in tool and tool["inputSchema"].get("additionalProperties") is False for tool in tools)
    names = {tool["name"] for tool in tools}
    assert {"raw_gpio", "read_file", "flash_firmware", "capture_microphone"}.isdisjoint(names)
    called = await server.handle({
        "jsonrpc": "2.0", "id": 3, "method": "tools/call",
        "params": {"name": "huangshan_set_rgb", "arguments": {"color": "green"}},
    })
    assert called and called["result"]["isError"] is False  # type: ignore[index]
    assert calls[-1] == ("set_rgb", {"color": "green"})
    pet_status = await server.handle({
        "jsonrpc": "2.0", "id": 30, "method": "tools/call",
        "params": {"name": "huangshan_pet_status", "arguments": {}},
    })
    assert pet_status and pet_status["result"]["isError"] is False  # type: ignore[index]
    assert calls[-1] == ("pet_status", {})
    cue = await server.handle({
        "jsonrpc": "2.0", "id": 31, "method": "tools/call",
        "params": {"name": "huangshan_play_cue", "arguments": {"cue": "done"}},
    })
    assert cue and cue["result"]["isError"] is False  # type: ignore[index]
    assert calls[-1] == ("play_cue", {"cue": "done"})
    selected = await server.handle({
        "jsonrpc": "2.0", "id": 32, "method": "tools/call",
        "params": {"name": "huangshan_select_pet", "arguments": {"slug": "shinchan"}},
    })
    assert selected and selected["result"]["isError"] is False  # type: ignore[index]
    assert calls[-1] == ("select_pet", {"slug": "shinchan"})
    rejected = await server.handle({
        "jsonrpc": "2.0", "id": 4, "method": "tools/call",
        "params": {"name": "huangshan_power", "arguments": {}},
    })
    assert rejected and rejected["result"]["isError"] is True  # type: ignore[index]
    unknown = await server.handle({
        "jsonrpc": "2.0", "id": 5, "method": "tools/call",
        "params": {"name": "huangshan_gpio", "arguments": {}},
    })
    assert unknown and unknown["error"]["code"] == -32602  # type: ignore[index]
    assert (await server.handle([]))["error"]["code"] == -32600  # type: ignore[index]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Huangshan Codex Pet MCP stdio server")
    parser.add_argument("--socket", type=Path, default=DEFAULT_SOCKET)
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        asyncio.run(self_test_async())
        print("codex_pet_mcp self-test ok", file=sys.stderr)
        return 0
    try:
        asyncio.run(run_stdio(MCPServer(BridgeCaller(args.socket))))
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"codex_pet_mcp: {type(exc).__name__}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
