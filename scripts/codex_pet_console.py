#!/usr/bin/env python3
"""Render the Bridge-owned Codex stream in the test backend terminal."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Mapping
from typing import TextIO


_APPROVAL_METHODS = {
    "item/commandExecution/requestApproval": "命令执行",
    "item/fileChange/requestApproval": "文件变更",
}


class CodexPetConsole:
    """Small local renderer; it never prints request payloads or secrets."""

    def __init__(self, stream: TextIO | None = None) -> None:
        self.stream = stream or sys.stdout
        self._message_items: set[str] = set()
        self._line_open = False

    def handle_notification(self, message: Mapping[str, object]) -> None:
        method = message.get("method")
        params = message.get("params")
        if not isinstance(method, str) or not isinstance(params, Mapping):
            return
        thread_id = params.get("threadId")
        short_thread = str(thread_id)[:8] if isinstance(thread_id, str) else "--------"
        if method == "thread/started":
            thread = params.get("thread")
            task_id = thread.get("id") if isinstance(thread, Mapping) else thread_id
            self._line(f"\n[Codex] 任务已连接: {str(task_id)[:8] if task_id else short_thread}")
        elif method == "turn/started":
            self._line(f"\n[Codex] 任务执行中 ({short_thread})")
        elif method == "item/agentMessage/delta":
            delta = params.get("delta")
            if isinstance(delta, str) and delta:
                self.stream.write(delta)
                self.stream.flush()
                self._line_open = True
                item_id = params.get("itemId")
                if isinstance(item_id, str):
                    self._message_items.add(item_id)
        elif method == "item/completed":
            item = params.get("item")
            if isinstance(item, Mapping) and item.get("type") == "agentMessage":
                item_id = item.get("id")
                if isinstance(item_id, str) and item_id not in self._message_items:
                    text = item.get("text")
                    if isinstance(text, str) and text:
                        self.stream.write(text)
                        self._line_open = True
                if isinstance(item_id, str):
                    self._message_items.discard(item_id)
                self._finish_line()
        elif method == "turn/completed":
            turn = params.get("turn")
            status = turn.get("status") if isinstance(turn, Mapping) else params.get("status")
            self._finish_line()
            self._line(f"[Codex] 任务完成: {status or 'completed'} ({short_thread})")
        elif method == "pet/approval/registered":
            self._line(f"[Codex] 板端审批已同步 ({short_thread})")
        elif method == "pet/approval/skipped":
            reason = params.get("reason")
            self._line(f"[Codex] 板端审批不可用: {reason or 'unsupported'} ({short_thread})")
        elif method == "pet/approval/error":
            self._line(f"[Codex] 板端审批发送失败 ({short_thread})")

    def handle_server_request(self, message: Mapping[str, object]) -> None:
        method = message.get("method")
        if not isinstance(method, str):
            return
        if method in _APPROVAL_METHODS:
            self._finish_line()
            self._line(f"[Codex] 需要审批: {_APPROVAL_METHODS[method]}。请在板子或 Codex 控制台处理。")
        elif method == "item/tool/requestUserInput":
            self._finish_line()
            params = message.get("params")
            questions = params.get("questions") if isinstance(params, Mapping) else None
            secret = isinstance(questions, list) and any(
                isinstance(question, Mapping) and question.get("isSecret") is True
                for question in questions
            )
            detail = "API Key/凭据" if secret else "手动输入"
            self._line(f"[Codex] 需要 {detail}。请在板子或 Codex 控制台处理。")
        elif method == "mcpServer/elicitation/request":
            self._finish_line()
            self._line("[Codex] MCP 需要手动输入。请在板子或 Codex 控制台处理。")

    def _line(self, text: str) -> None:
        self.stream.write(text + "\n")
        self.stream.flush()
        self._line_open = False

    def _finish_line(self) -> None:
        if self._line_open:
            self.stream.write("\n")
            self.stream.flush()
            self._line_open = False


def _self_test() -> None:
    import io

    stream = io.StringIO()
    console = CodexPetConsole(stream)
    console.handle_notification({"method": "turn/started", "params": {"threadId": "thread-1234"}})
    console.handle_notification({
        "method": "item/agentMessage/delta",
        "params": {"threadId": "thread-1234", "itemId": "item-1", "delta": "PING"},
    })
    console.handle_notification({
        "method": "item/completed",
        "params": {"threadId": "thread-1234", "item": {"type": "agentMessage", "id": "item-1", "text": "PING"}},
    })
    console.handle_server_request({"method": "item/commandExecution/requestApproval", "params": {}})
    console.handle_notification({
        "method": "pet/approval/registered",
        "params": {"threadId": "thread-1234"},
    })
    console.handle_server_request({
        "method": "item/tool/requestUserInput",
        "params": {"questions": [{"isSecret": True}]},
    })
    console.handle_notification({
        "method": "turn/completed",
        "params": {"threadId": "thread-1234", "turn": {"status": "completed"}},
    })
    output = stream.getvalue()
    assert "PING" in output
    assert "需要审批: 命令执行" in output
    assert "板端审批已同步" in output
    assert "需要 API Key/凭据" in output
    assert "任务完成: completed" in output
    print("codex_pet_console self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("use --self-test")
    _self_test()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
