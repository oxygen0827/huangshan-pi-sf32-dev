#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import os
import shutil
import sys
import time
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping, Protocol, Sequence
from urllib.parse import quote


CLIENT_NAME = "huangshan_codex_pet"
CLIENT_TITLE = "Huangshan Codex Pet"
CLIENT_VERSION = "0.1.0"


class CodexAppServerError(RuntimeError):
    pass


class CodexAppServerUnavailable(CodexAppServerError):
    pass


class CodexAppServerProtocolError(CodexAppServerError):
    pass


class CodexAppServerResponseError(CodexAppServerError):
    def __init__(self, method: str, error: object) -> None:
        super().__init__(f"Codex app-server {method} failed: {error}")
        self.method = method
        self.error = error


@dataclass(frozen=True)
class RateLimitWindow:
    used_percent: int
    window_minutes: int | None
    resets_at: int | None


@dataclass(frozen=True)
class RateLimitSnapshot:
    primary: RateLimitWindow | None
    secondary: RateLimitWindow | None
    limit_id: str | None
    plan_type: str | None
    fetched_at: int


@dataclass(frozen=True)
class UsageReading:
    source: str
    status: str
    snapshot: RateLimitSnapshot | None
    error: str | None


class UsageProvider(Protocol):
    async def fetch(self) -> UsageReading: ...


class RateLimitReader(Protocol):
    async def rate_limits(self) -> RateLimitSnapshot: ...


class AppServerUsageProvider:
    def __init__(self, client: RateLimitReader) -> None:
        self.client = client

    async def fetch(self) -> UsageReading:
        try:
            snapshot = await self.client.rate_limits()
        except CodexAppServerError as exc:
            return UsageReading("codex-app-server", "unavailable", None, public_error(exc))
        return UsageReading("codex-app-server", "live", snapshot, None)


def _mapping(value: object, label: str) -> Mapping[str, Any]:
    if not isinstance(value, dict):
        raise CodexAppServerProtocolError(f"{label} must be an object")
    return value


def _optional_int(value: object, label: str) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool) or not isinstance(value, int):
        raise CodexAppServerProtocolError(f"{label} must be an integer or null")
    return value


def parse_rate_limit_window(value: object, label: str) -> RateLimitWindow | None:
    if value is None:
        return None
    payload = _mapping(value, label)
    used = payload.get("usedPercent")
    if isinstance(used, bool) or not isinstance(used, int) or not 0 <= used <= 100:
        raise CodexAppServerProtocolError(f"{label}.usedPercent must be between 0 and 100")
    return RateLimitWindow(
        used_percent=used,
        window_minutes=_optional_int(payload.get("windowDurationMins"), f"{label}.windowDurationMins"),
        resets_at=_optional_int(payload.get("resetsAt"), f"{label}.resetsAt"),
    )


def parse_rate_limits(result: object, *, fetched_at: int | None = None) -> RateLimitSnapshot:
    response = _mapping(result, "account/rateLimits/read result")
    limits = _mapping(response.get("rateLimits"), "rateLimits")
    limit_id = limits.get("limitId")
    plan_type = limits.get("planType")
    if limit_id is not None and not isinstance(limit_id, str):
        raise CodexAppServerProtocolError("rateLimits.limitId must be a string or null")
    if plan_type is not None and not isinstance(plan_type, str):
        raise CodexAppServerProtocolError("rateLimits.planType must be a string or null")
    return RateLimitSnapshot(
        primary=parse_rate_limit_window(limits.get("primary"), "rateLimits.primary"),
        secondary=parse_rate_limit_window(limits.get("secondary"), "rateLimits.secondary"),
        limit_id=limit_id,
        plan_type=plan_type,
        fetched_at=int(time.time()) if fetched_at is None else fetched_at,
    )


def resolve_codex_bin(explicit: str | None = None) -> str:
    candidates = [
        explicit,
        os.environ.get("CODEX_BIN"),
        "/Applications/ChatGPT.app/Contents/Resources/codex",
        "/Applications/Codex.app/Contents/Resources/codex",
        shutil.which("codex"),
    ]
    for candidate in candidates:
        if candidate and Path(candidate).expanduser().is_file():
            return str(Path(candidate).expanduser())
    raise CodexAppServerUnavailable("Codex executable was not found; set CODEX_BIN or install Codex CLI")


def public_error(error: CodexAppServerError) -> str:
    if isinstance(error, CodexAppServerResponseError) and isinstance(error.error, dict):
        message = error.error.get("message")
        if isinstance(message, str) and message.strip():
            return message.strip()
    return str(error)


def codex_thread_url(thread_id: str) -> str:
    normalized = thread_id.strip()
    if not normalized or any(char in normalized for char in "\r\n"):
        raise ValueError("thread id must be non-empty and single-line")
    return "codex://threads/" + quote(normalized, safe="")


class CodexAppServerClient:
    def __init__(
        self,
        *,
        command: Sequence[str] | None = None,
        codex_bin: str | None = None,
        request_timeout: float = 15.0,
        experimental: bool = True,
    ) -> None:
        if request_timeout <= 0:
            raise ValueError("request_timeout must be positive")
        self._command = list(command) if command is not None else [resolve_codex_bin(codex_bin), "app-server", "--stdio"]
        if not self._command:
            raise ValueError("command must not be empty")
        self.request_timeout = request_timeout
        self.experimental = experimental
        self.process: asyncio.subprocess.Process | None = None
        self._next_id = 1
        self._pending: dict[int, tuple[str, asyncio.Future[object]]] = {}
        self._notifications: asyncio.Queue[dict[str, object]] = asyncio.Queue()
        self._server_requests: asyncio.Queue[dict[str, object]] = asyncio.Queue()
        self._reader_task: asyncio.Task[None] | None = None
        self._stderr_task: asyncio.Task[None] | None = None
        self._stderr_lines: deque[str] = deque(maxlen=20)
        self._closing = False

    async def __aenter__(self) -> "CodexAppServerClient":
        await self.start()
        return self

    async def __aexit__(self, exc_type: object, exc: object, traceback: object) -> None:
        await self.close()

    @property
    def stderr_summary(self) -> str:
        return " | ".join(self._stderr_lines)

    async def start(self) -> None:
        if self.process is not None:
            return
        try:
            self.process = await asyncio.create_subprocess_exec(
                *self._command,
                stdin=asyncio.subprocess.PIPE,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE,
            )
        except (FileNotFoundError, PermissionError, OSError) as exc:
            raise CodexAppServerUnavailable(f"could not start Codex app-server: {exc}") from exc
        self._reader_task = asyncio.create_task(self._read_stdout(), name="codex-appserver-stdout")
        self._stderr_task = asyncio.create_task(self._read_stderr(), name="codex-appserver-stderr")
        try:
            await self.request(
                "initialize",
                {
                    "clientInfo": {
                        "name": CLIENT_NAME,
                        "title": CLIENT_TITLE,
                        "version": CLIENT_VERSION,
                    },
                    "capabilities": {"experimentalApi": self.experimental},
                },
            )
            await self.notify("initialized", {})
        except Exception:
            await self.close()
            raise

    async def close(self) -> None:
        self._closing = True
        process = self.process
        self.process = None
        if process is not None and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                process.kill()
                await process.wait()
        current = asyncio.current_task()
        for task in (self._reader_task, self._stderr_task):
            if task is not None and task is not current and not task.done():
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task
        self._reader_task = None
        self._stderr_task = None
        self._fail_pending(CodexAppServerUnavailable("Codex app-server closed"))

    async def request(self, method: str, params: Mapping[str, object]) -> object:
        process = self.process
        if process is None or process.stdin is None:
            raise CodexAppServerUnavailable("Codex app-server is not running")
        request_id = self._next_id
        self._next_id += 1
        future: asyncio.Future[object] = asyncio.get_running_loop().create_future()
        self._pending[request_id] = (method, future)
        try:
            await self._write({"id": request_id, "method": method, "params": dict(params)})
            return await asyncio.wait_for(asyncio.shield(future), timeout=self.request_timeout)
        except asyncio.TimeoutError as exc:
            self._pending.pop(request_id, None)
            if not future.done():
                future.cancel()
            raise CodexAppServerUnavailable(
                f"Codex app-server {method} timed out after {self.request_timeout:.1f}s{self._diagnostic_suffix()}"
            ) from exc
        except BaseException:
            self._pending.pop(request_id, None)
            if not future.done():
                future.cancel()
            raise

    async def notify(self, method: str, params: Mapping[str, object]) -> None:
        await self._write({"method": method, "params": dict(params)})

    async def respond(self, request_id: object, result: object) -> None:
        await self._write({"id": request_id, "result": result})

    async def next_notification(self, *, timeout: float | None = None) -> dict[str, object]:
        return await self._queue_get(self._notifications, timeout)

    async def next_server_request(self, *, timeout: float | None = None) -> dict[str, object]:
        return await self._queue_get(self._server_requests, timeout)

    async def thread_start(
        self,
        cwd: Path,
        *,
        model: str | None = None,
        sandbox: str = "workspace-write",
        approval_policy: str = "on-request",
    ) -> str:
        resolved = cwd.expanduser().resolve()
        if not resolved.is_dir():
            raise ValueError(f"Codex workspace does not exist: {resolved}")
        params: dict[str, object] = {
            "cwd": str(resolved),
            "sandbox": sandbox,
            "approvalPolicy": approval_policy,
            "approvalsReviewer": "user",
        }
        if model:
            params["model"] = model
        result = _mapping(await self.request("thread/start", params), "thread/start result")
        return self._extract_id(result, "thread", "thread/start")

    async def thread_resume(self, thread_id: str) -> str:
        result = _mapping(
            await self.request("thread/resume", {"threadId": thread_id}),
            "thread/resume result",
        )
        return self._extract_id(result, "thread", "thread/resume")

    async def thread_archive(self, thread_id: str) -> None:
        await self.request("thread/archive", {"threadId": thread_id})

    async def turn_start(self, thread_id: str, text: str, *, cwd: Path | None = None) -> str:
        normalized = text.strip()
        if not normalized:
            raise ValueError("Codex prompt must not be empty")
        params: dict[str, object] = {
            "threadId": thread_id,
            "input": [{"type": "text", "text": normalized}],
        }
        if cwd is not None:
            resolved = cwd.expanduser().resolve()
            if not resolved.is_dir():
                raise ValueError(f"Codex workspace does not exist: {resolved}")
            params["cwd"] = str(resolved)
        result = _mapping(await self.request("turn/start", params), "turn/start result")
        return self._extract_id(result, "turn", "turn/start")

    async def account_read(self, *, refresh_token: bool = False) -> Mapping[str, Any]:
        return _mapping(
            await self.request("account/read", {"refreshToken": refresh_token}),
            "account/read result",
        )

    async def rate_limits(self) -> RateLimitSnapshot:
        return parse_rate_limits(await self.request("account/rateLimits/read", {}))

    async def _queue_get(
        self,
        queue: asyncio.Queue[dict[str, object]],
        timeout: float | None,
    ) -> dict[str, object]:
        if timeout is None:
            return await queue.get()
        try:
            return await asyncio.wait_for(queue.get(), timeout=timeout)
        except asyncio.TimeoutError as exc:
            raise CodexAppServerUnavailable("timed out waiting for Codex app-server event") from exc

    async def _write(self, message: Mapping[str, object]) -> None:
        process = self.process
        if process is None or process.stdin is None or process.returncode is not None:
            raise CodexAppServerUnavailable(f"Codex app-server is not writable{self._diagnostic_suffix()}")
        encoded = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode("utf-8") + b"\n"
        process.stdin.write(encoded)
        try:
            await process.stdin.drain()
        except (BrokenPipeError, ConnectionResetError) as exc:
            raise CodexAppServerUnavailable(f"Codex app-server connection closed{self._diagnostic_suffix()}") from exc

    async def _read_stdout(self) -> None:
        process = self.process
        if process is None or process.stdout is None:
            return
        try:
            while True:
                line = await process.stdout.readline()
                if not line:
                    break
                try:
                    message = json.loads(line)
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise CodexAppServerProtocolError(f"invalid JSON from Codex app-server: {exc}") from exc
                payload = _mapping(message, "Codex app-server message")
                method = payload.get("method")
                if "id" in payload and isinstance(method, str):
                    await self._server_requests.put(dict(payload))
                elif "id" in payload:
                    self._handle_response(payload)
                elif isinstance(method, str):
                    await self._notifications.put(dict(payload))
                else:
                    raise CodexAppServerProtocolError("Codex app-server message has neither id nor method")
        except asyncio.CancelledError:
            raise
        except Exception as exc:
            self._fail_pending(exc if isinstance(exc, CodexAppServerError) else CodexAppServerProtocolError(str(exc)))
        finally:
            if not self._closing:
                self._fail_pending(
                    CodexAppServerUnavailable(f"Codex app-server stdout closed{self._diagnostic_suffix()}")
                )

    async def _read_stderr(self) -> None:
        process = self.process
        if process is None or process.stderr is None:
            return
        while True:
            line = await process.stderr.readline()
            if not line:
                return
            text = line.decode("utf-8", "replace").strip()
            if text:
                self._stderr_lines.append(text)

    def _handle_response(self, payload: Mapping[str, object]) -> None:
        request_id = payload.get("id")
        if isinstance(request_id, bool) or not isinstance(request_id, int):
            return
        pending = self._pending.pop(request_id, None)
        if pending is None:
            return
        method, future = pending
        if future.done():
            return
        if "error" in payload:
            future.set_exception(CodexAppServerResponseError(method, payload["error"]))
        elif "result" in payload:
            future.set_result(payload["result"])
        else:
            future.set_exception(CodexAppServerProtocolError(f"Codex app-server {method} response has no result"))

    def _extract_id(self, result: Mapping[str, object], key: str, method: str) -> str:
        value = _mapping(result.get(key), f"{method} result.{key}").get("id")
        if not isinstance(value, str) or not value:
            raise CodexAppServerProtocolError(f"{method} result.{key}.id is missing")
        return value

    def _fail_pending(self, error: BaseException) -> None:
        pending = list(self._pending.values())
        self._pending.clear()
        for _, future in pending:
            if not future.done():
                future.set_exception(error)

    def _diagnostic_suffix(self) -> str:
        summary = self.stderr_summary
        return f"; stderr: {summary}" if summary else ""


def _fake_write(message: Mapping[str, object]) -> None:
    sys.stdout.write(json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def fake_server_main() -> int:
    for raw in sys.stdin:
        message = json.loads(raw)
        if "method" not in message:
            continue
        method = message["method"]
        request_id = message.get("id")
        if request_id is None:
            continue
        if method == "initialize":
            _fake_write({"id": request_id, "result": {"userAgent": "fake", "platformFamily": "unix", "platformOs": "macos"}})
        elif method == "thread/start":
            _fake_write({"id": request_id, "result": {"thread": {"id": "thr_fake"}}})
        elif method == "thread/resume":
            _fake_write({"id": request_id, "result": {"thread": {"id": message["params"]["threadId"]}}})
        elif method == "thread/archive":
            _fake_write({"id": request_id, "result": {}})
        elif method == "turn/start":
            _fake_write({"id": request_id, "result": {"turn": {"id": "turn_fake", "status": "inProgress"}}})
            _fake_write({"method": "turn/started", "params": {"threadId": "thr_fake", "turn": {"id": "turn_fake"}}})
            _fake_write(
                {
                    "id": 900,
                    "method": "item/tool/requestUserInput",
                    "params": {
                        "threadId": "thr_fake",
                        "turnId": "turn_fake",
                        "itemId": "item_fake",
                        "questions": [
                            {
                                "id": "api_key",
                                "header": "Credential",
                                "question": "Enter the API key on the computer",
                                "isSecret": True,
                                "options": None,
                            }
                        ],
                    },
                }
            )
            _fake_write({"method": "turn/completed", "params": {"threadId": "thr_fake", "turn": {"id": "turn_fake", "status": "completed"}}})
        elif method == "account/read":
            _fake_write({"id": request_id, "result": {"account": {"type": "chatgpt"}, "requiresOpenaiAuth": True}})
        elif method == "account/rateLimits/read":
            _fake_write(
                {
                    "id": request_id,
                    "result": {
                        "rateLimits": {
                            "limitId": "codex",
                            "planType": "plus",
                            "primary": {"usedPercent": 17, "windowDurationMins": 300, "resetsAt": 2000},
                            "secondary": {"usedPercent": 41, "windowDurationMins": 10080, "resetsAt": 3000},
                        },
                        "rateLimitsByLimitId": None,
                    },
                }
            )
        elif method == "test/noResponse":
            continue
        else:
            _fake_write({"id": request_id, "error": {"code": -32601, "message": "method not found"}})
    return 0


async def self_test_async() -> None:
    assert codex_thread_url("thr_123") == "codex://threads/thr_123"
    assert codex_thread_url("thread with space") == "codex://threads/thread%20with%20space"
    try:
        codex_thread_url("\n")
        raise AssertionError("empty thread id was accepted")
    except ValueError:
        pass

    parsed = parse_rate_limits(
        {
            "rateLimits": {
                "limitId": "codex",
                "planType": "plus",
                "primary": {"usedPercent": 1, "windowDurationMins": 300, "resetsAt": 10},
                "secondary": None,
            }
        },
        fetched_at=5,
    )
    assert parsed.primary == RateLimitWindow(1, 300, 10)
    assert parsed.secondary is None and parsed.fetched_at == 5
    try:
        parse_rate_limits({"rateLimits": {"primary": {"usedPercent": 101}}})
        raise AssertionError("invalid rate limit was accepted")
    except CodexAppServerProtocolError:
        pass

    command = [sys.executable, str(Path(__file__).resolve()), "--fake-server"]
    async with CodexAppServerClient(command=command, request_timeout=1.0) as client:
        thread_id = await client.thread_start(Path.cwd())
        assert thread_id == "thr_fake"
        assert await client.thread_resume(thread_id) == thread_id
        assert await client.turn_start(thread_id, "Run the offline bridge test") == "turn_fake"
        started = await client.next_notification(timeout=1.0)
        assert started["method"] == "turn/started"
        server_request = await client.next_server_request(timeout=1.0)
        assert server_request["method"] == "item/tool/requestUserInput"
        questions = _mapping(server_request["params"], "fake request params")["questions"]
        assert isinstance(questions, list) and questions[0]["isSecret"] is True
        await client.respond(server_request["id"], {"answers": {"api_key": {"answers": ["handled-on-computer"]}}})
        completed = await client.next_notification(timeout=1.0)
        assert completed["method"] == "turn/completed"
        account = await client.account_read()
        assert account["requiresOpenaiAuth"] is True
        limits = await client.rate_limits()
        assert limits.primary == RateLimitWindow(17, 300, 2000)
        assert limits.secondary == RateLimitWindow(41, 10080, 3000)
        reading = await AppServerUsageProvider(client).fetch()
        assert reading.status == "live" and reading.snapshot is not None and reading.error is None
        try:
            await client.request("missing/method", {})
            raise AssertionError("app-server response error was not raised")
        except CodexAppServerResponseError:
            pass
        await client.thread_archive(thread_id)

    async with CodexAppServerClient(command=command, request_timeout=1.0) as client:
        client.request_timeout = 0.05
        try:
            await client.request("test/noResponse", {})
            raise AssertionError("app-server timeout was not raised")
        except CodexAppServerUnavailable as exc:
            assert "timed out" in str(exc)

    class UnavailableUsageClient:
        async def rate_limits(self) -> RateLimitSnapshot:
            raise CodexAppServerResponseError(
                "account/rateLimits/read",
                {"code": -32600, "message": "chatgpt authentication required to read rate limits"},
            )

    unavailable = await AppServerUsageProvider(UnavailableUsageClient()).fetch()
    assert unavailable.status == "unavailable" and unavailable.snapshot is None
    assert unavailable.error == "chatgpt authentication required to read rate limits"


async def real_check(codex_bin: str | None) -> None:
    async with CodexAppServerClient(codex_bin=codex_bin) as client:
        account = await client.account_read()
        usage = await AppServerUsageProvider(client).fetch()
        limits = usage.snapshot
        payload = {
            "appServer": "ok",
            "accountPresent": account.get("account") is not None,
            "requiresOpenaiAuth": account.get("requiresOpenaiAuth"),
            "quotaStatus": usage.status,
            "quotaError": usage.error,
            "limitId": limits.limit_id if limits else None,
            "planType": limits.plan_type if limits else None,
            "primaryAvailable": limits is not None and limits.primary is not None,
            "secondaryAvailable": limits is not None and limits.secondary is not None,
        }
        print(json.dumps(payload, ensure_ascii=False, sort_keys=True))


async def real_thread_check(codex_bin: str | None, workspace: Path) -> None:
    async with CodexAppServerClient(codex_bin=codex_bin) as client:
        thread_id = await client.thread_start(workspace, sandbox="read-only")
        persisted = False
        try:
            try:
                resumed = await client.thread_resume(thread_id)
            except CodexAppServerResponseError as exc:
                if "no rollout found" not in public_error(exc):
                    raise
                print(
                    json.dumps(
                        {
                            "threadStart": "ok",
                            "threadPersisted": False,
                            "deepLinkReady": False,
                            "requirement": "the first turn must complete app-server acceptance before resume or deep link",
                        },
                        ensure_ascii=False,
                        sort_keys=True,
                    )
                )
                return
            if resumed != thread_id:
                raise CodexAppServerProtocolError("thread/resume returned a different thread id")
            persisted = True
            print(
                json.dumps(
                    {
                        "threadStart": "ok",
                        "threadResume": "ok",
                        "threadPersisted": True,
                        "deepLinkReady": True,
                        "deepLink": codex_thread_url(thread_id),
                    },
                    ensure_ascii=False,
                    sort_keys=True,
                )
            )
        finally:
            if persisted:
                await client.thread_archive(thread_id)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Codex app-server adapter for the Huangshan Codex Pet bridge")
    parser.add_argument("--codex-bin", help="Path to the Codex CLI executable")
    parser.add_argument("--check", action="store_true", help="Read account and rate-limit availability from the real app-server")
    parser.add_argument("--thread-check", action="store_true", help="Create, resume and archive an empty read-only Codex thread")
    parser.add_argument("--workspace", type=Path, default=Path.cwd(), help="Workspace used by --thread-check")
    parser.add_argument("--self-test", action="store_true", help="Run the offline fake-server contract test")
    parser.add_argument("--fake-server", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.fake_server:
        return fake_server_main()
    if args.self_test:
        asyncio.run(self_test_async())
        print("codex_pet_appserver self-test ok")
        return 0
    if args.check:
        asyncio.run(real_check(args.codex_bin))
        return 0
    if args.thread_check:
        asyncio.run(real_thread_check(args.codex_bin, args.workspace))
        return 0
    raise SystemExit("use --self-test or --check")


if __name__ == "__main__":
    raise SystemExit(main())
