#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import contextlib
import hashlib
import sys
import tempfile
from pathlib import Path
from typing import Awaitable, Callable, Protocol

from codex_pet_protocol import PetEnvelope
from voice_bridge_common import VoiceStreamCollector, capture_timestamped_wav, mulaw_to_pcm16, truncate_utf8, write_wav


TRANSCRIPT_CHANNEL = "pet.transcript"
ASR_ERROR_CHANNEL = "pet.asr.error"
TASK_ACK_CHANNEL = "pet.task.ack"
TASK_ERROR_CHANNEL = "pet.task.error"
MAX_BOARD_TEXT_BYTES = 192


class VoiceRuntimeCall(Protocol):
    async def __call__(self, method: str, *args: object, **kwargs: object) -> object: ...


class VoiceSubmit(Protocol):
    async def __call__(self, action_id: str, transcript: str, thread_id: str | None) -> PetEnvelope: ...


class VoiceStatePublish(Protocol):
    async def __call__(self, status: str, **kwargs: object) -> PetEnvelope: ...


class VoiceTranscriber(Protocol):
    async def __call__(self, pcm: bytes, sequence: int) -> str: ...


def safe_int(value: object, default: int = 0) -> int:
    try:
        return int(str(value))
    except (TypeError, ValueError):
        return default


def capture_action_id(sequence: int, stream_payload: bytes) -> str:
    digest = hashlib.sha256(stream_payload).hexdigest()[:20]
    return f"v:{sequence}:{digest}"


class GLMASRTranscriber:
    def __init__(
        self,
        *,
        model: str = "glm-asr-2512",
        prompt: str | None = None,
        save_dir: Path | None = None,
        mock_transcript: str | None = None,
    ) -> None:
        self.model = model
        self.prompt = prompt
        self.save_dir = save_dir
        self.mock_transcript = mock_transcript
        self.script = Path(__file__).resolve().parent / "voice_llm_zhipu.py"

    async def __call__(self, pcm: bytes, sequence: int) -> str:
        if self.mock_transcript is not None:
            return self.mock_transcript
        if self.save_dir is not None:
            wav_path = capture_timestamped_wav(self.save_dir, f"codex-pet-seq{sequence}", pcm)
            return await self._run(wav_path)
        with tempfile.TemporaryDirectory(prefix="codex-pet-asr-") as temporary:
            wav_path = Path(temporary) / "capture.wav"
            write_wav(wav_path, pcm)
            return await self._run(wav_path)

    async def _run(self, wav_path: Path) -> str:
        command = [
            sys.executable,
            str(self.script),
            "--wav",
            str(wav_path),
            "--transcribe-model",
            self.model,
            "--transcribe-only",
        ]
        if self.prompt:
            command.extend(["--prompt", self.prompt])
        process = await asyncio.create_subprocess_exec(
            *command,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        stdout, stderr = await process.communicate()
        if process.returncode != 0:
            details = stderr.decode("utf-8", "replace").strip().splitlines()
            raise RuntimeError(details[-1] if details else f"GLM-ASR exited with {process.returncode}")
        transcript = stdout.decode("utf-8", "replace").strip()
        if not transcript:
            raise RuntimeError("GLM-ASR returned no text")
        return transcript


async def open_codex_thread(url: str) -> None:
    if sys.platform != "darwin":
        return
    process = await asyncio.create_subprocess_exec(
        "open",
        url,
        stdout=asyncio.subprocess.DEVNULL,
        stderr=asyncio.subprocess.PIPE,
    )
    try:
        _, stderr = await asyncio.wait_for(process.communicate(), timeout=5.0)
    except asyncio.TimeoutError:
        process.kill()
        await process.wait()
        raise RuntimeError("opening the Codex task timed out")
    if process.returncode != 0:
        raise RuntimeError(stderr.decode("utf-8", "replace").strip() or "could not open the Codex task")


class CodexPetVoiceService:
    def __init__(
        self,
        *,
        runtime_call: VoiceRuntimeCall,
        submit: VoiceSubmit,
        publish_state: VoiceStatePublish,
        transcribe: VoiceTranscriber,
        initial_thread_id: str | None = None,
        open_thread: Callable[[str], Awaitable[None]] | None = open_codex_thread,
        poll_interval: float = 0.2,
    ) -> None:
        if poll_interval <= 0:
            raise ValueError("poll_interval must be positive")
        self.runtime_call = runtime_call
        self.submit = submit
        self.publish_state = publish_state
        self.transcribe = transcribe
        self.current_thread_id = initial_thread_id
        self.open_thread = open_thread
        self.poll_interval = poll_interval
        self.collector = VoiceStreamCollector()
        self.last_sequence = 0
        self._task: asyncio.Task[None] | None = None
        self._closed = False

    async def start(self, *, run_loop: bool = True) -> None:
        await self.runtime_call("start_voice_stream", self.collector.receive)
        status, info = await self._voice_status()
        if info.get("built") == "0":
            raise RuntimeError(f"board firmware has no microphone capture support: {status}")
        if info.get("ready") == "1" and str(info.get("context", "")).startswith("pet."):
            self.last_sequence = max(0, safe_int(info.get("seq")) - 1)
        else:
            self.last_sequence = safe_int(info.get("seq"))
        if run_loop:
            self._task = asyncio.create_task(self._loop(), name="codex-pet-voice")

    async def close(self) -> None:
        self._closed = True
        task = self._task
        self._task = None
        if task is not None:
            task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await task

    async def process_once(self) -> bool:
        _, info = await self._voice_status()
        sequence = safe_int(info.get("seq"))
        context = str(info.get("context", ""))
        if info.get("recording") == "1" or info.get("ready") != "1":
            return False
        if not context.startswith("pet.") or sequence <= 0 or sequence == self.last_sequence:
            return False
        total = safe_int(info.get("bytes"))
        if total <= 0:
            await self._fail_capture(sequence, "No audio captured")
            return True

        try:
            stream_payload = await self._read_capture(info, sequence, total)
            pcm = mulaw_to_pcm16(stream_payload) if info.get("codec") == "mulaw" else stream_payload
            await self.publish_state("transcribing", detail="GLM-ASR")
            transcript = truncate_utf8(await self.transcribe(pcm, sequence), MAX_BOARD_TEXT_BYTES)
            if not transcript:
                raise RuntimeError("No speech recognized")
            await self.runtime_call("flow_send", TRANSCRIPT_CHANNEL, sequence, transcript)
            continue_thread = self.current_thread_id if context == "pet.continue" else None
            action_id = capture_action_id(sequence, stream_payload)
            ack = await self.submit(action_id, transcript, continue_thread)
            ack_status = (ack.payload or {}).get("status")
            if ack_status not in {"accepted", "duplicate"}:
                error = str((ack.payload or {}).get("error", "submit_failed"))
                await self.runtime_call("flow_send", TASK_ERROR_CHANNEL, sequence, truncate_utf8(error, 96))
                return True
            if ack.task_id:
                self.current_thread_id = ack.task_id
            summary = truncate_utf8("Submitted: " + transcript, MAX_BOARD_TEXT_BYTES)
            await self.runtime_call("flow_send", TASK_ACK_CHANNEL, sequence, summary)
        except Exception:
            await self._fail_capture(sequence, "Voice task failed; check computer")
        finally:
            await self.runtime_call("voice_clear")
            self.last_sequence = sequence
        return True

    async def _read_capture(self, info: dict[str, str], sequence: int, total: int) -> bytes:
        if info.get("stream") == "1":
            return await self.collector.take(sequence, total)
        result = bytearray()
        offset = 0
        while offset < total:
            chunk = await self.runtime_call("voice_read", sequence, offset, min(200, total - offset))
            data = getattr(chunk, "data", None)
            if not isinstance(data, bytes) or not data:
                raise RuntimeError(f"voice_read returned no data at {offset}/{total}")
            result.extend(data)
            offset += len(data)
        return bytes(result)

    async def _voice_status(self) -> tuple[str, dict[str, str]]:
        result = await self.runtime_call("voice_status")
        if not isinstance(result, tuple) or len(result) != 2 or not isinstance(result[1], dict):
            raise RuntimeError("Runtime voice_status returned an invalid response")
        return result

    async def _fail_capture(self, sequence: int, message: str) -> None:
        public = truncate_utf8(message, 96) or "Voice task failed"
        with contextlib.suppress(Exception):
            await self.runtime_call("flow_send", ASR_ERROR_CHANNEL, sequence, public)
        with contextlib.suppress(Exception):
            await self.publish_state("blocked", subtype="submit", detail="voice task failed")

    async def _loop(self) -> None:
        while not self._closed:
            try:
                await self.process_once()
            except asyncio.CancelledError:
                raise
            except Exception:
                await asyncio.sleep(min(2.0, self.poll_interval * 4))
            else:
                await asyncio.sleep(self.poll_interval)


class _FakeRuntime:
    def __init__(self) -> None:
        self.callback: Callable[[bytes], None] | None = None
        self.info = {
            "built": "1",
            "recording": "0",
            "ready": "0",
            "seq": "0",
            "bytes": "0",
            "stream": "1",
            "codec": "pcm_s16le",
            "context": "",
        }
        self.frames: list[tuple[str, int, str]] = []
        self.clear_count = 0

    async def __call__(self, method: str, *args: object, **kwargs: object) -> object:
        del kwargs
        if method == "start_voice_stream":
            self.callback = args[0]
            return None
        if method == "voice_status":
            return "ok voice fake=1", dict(self.info)
        if method == "flow_send":
            self.frames.append((str(args[0]), int(args[1]), str(args[2])))
            return "ok flow_send"
        if method == "voice_clear":
            self.info["ready"] = "0"
            self.clear_count += 1
            return "ok voice_clear"
        raise AssertionError(f"unexpected fake Runtime call: {method}")

    def capture(self, sequence: int, payload: bytes, context: str) -> None:
        if self.callback is None:
            raise AssertionError("voice stream was not subscribed")
        self.info.update(
            {
                "ready": "1",
                "seq": str(sequence),
                "bytes": str(len(payload)),
                "context": context,
            }
        )
        self.callback(bytes([1]) + sequence.to_bytes(4, "little") + (0).to_bytes(4, "little") + payload)
        self.callback(bytes([2]) + sequence.to_bytes(4, "little") + len(payload).to_bytes(4, "little"))

    def cancel(self, sequence: int) -> None:
        if self.callback is None:
            raise AssertionError("voice stream was not subscribed")
        self.callback(bytes([3]) + sequence.to_bytes(4, "little") + (0).to_bytes(4, "little"))
        self.info.update({"ready": "0", "seq": str(sequence), "bytes": "0", "context": "pet.new"})


async def self_test_async() -> None:
    runtime = _FakeRuntime()
    submissions: list[tuple[str, str, str | None]] = []
    states: list[str] = []
    opened: list[str] = []

    async def submit(action_id: str, transcript: str, thread_id: str | None) -> PetEnvelope:
        submissions.append((action_id, transcript, thread_id))
        return PetEnvelope(
            "ack",
            len(submissions),
            f"ack:{len(submissions)}",
            1_900_000_000_000,
            task_id=thread_id or "thr_test",
            payload={"for": action_id, "status": "accepted"},
        )

    async def publish(status: str, **kwargs: object) -> PetEnvelope:
        del kwargs
        states.append(status)
        return PetEnvelope("state", len(states), f"state:{len(states)}", 1_900_000_000_000, payload={"status": status})

    async def transcribe(pcm: bytes, sequence: int) -> str:
        assert pcm and sequence > 0
        return "检查当前项目"

    async def opener(url: str) -> None:
        opened.append(url)

    service = CodexPetVoiceService(
        runtime_call=runtime,
        submit=submit,
        publish_state=publish,
        transcribe=transcribe,
        open_thread=opener,
    )
    await service.start(run_loop=False)
    runtime.capture(7, b"abcdef", "pet.new")
    assert await service.process_once()
    assert submissions[0][1:] == ("检查当前项目", None)
    assert submissions[0][0] == capture_action_id(7, b"abcdef")
    assert service.current_thread_id == "thr_test"
    assert [item[0] for item in runtime.frames[-2:]] == [TRANSCRIPT_CHANNEL, TASK_ACK_CHANNEL]
    assert opened == []
    assert states == ["transcribing"] and runtime.clear_count == 1

    runtime.capture(8, b"ghijkl", "pet.continue")
    assert await service.process_once()
    assert submissions[1][2] == "thr_test"
    runtime.cancel(9)
    assert not await service.process_once()
    assert len(submissions) == 2
    await service.close()

    failing_runtime = _FakeRuntime()

    async def fail_transcription(pcm: bytes, sequence: int) -> str:
        del pcm, sequence
        raise RuntimeError("secret provider diagnostic")

    failing_service = CodexPetVoiceService(
        runtime_call=failing_runtime,
        submit=submit,
        publish_state=publish,
        transcribe=fail_transcription,
        open_thread=opener,
    )
    await failing_service.start(run_loop=False)
    failing_runtime.capture(10, b"failure", "pet.new")
    assert await failing_service.process_once()
    assert len(submissions) == 2
    assert failing_runtime.frames[-1] == (
        ASR_ERROR_CHANNEL,
        10,
        "Voice task failed; check computer",
    )
    assert "secret" not in str(failing_runtime.frames)
    await failing_service.close()

    assert capture_action_id(7, b"abcdef") == capture_action_id(7, b"abcdef")
    assert capture_action_id(7, b"abcdef") != capture_action_id(8, b"abcdef")
    assert GLMASRTranscriber(mock_transcript="离线转录")


def main() -> int:
    parser = argparse.ArgumentParser(description="Codex Pet voice capture and GLM-ASR service")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if not args.self_test:
        parser.error("use --self-test")
    asyncio.run(self_test_async())
    print("codex_pet_voice self-test ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
