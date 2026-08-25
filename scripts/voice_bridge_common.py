#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import json
import shlex
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path
from typing import Any

DEFAULT_OUT_DIR = Path("captures")
DEFAULT_LOG_PATH = DEFAULT_OUT_DIR / "voice_terminal.jsonl"
SAMPLE_RATE = 16_000
CHANNELS = 1
SAMPLE_WIDTH_BYTES = 2


class VoiceStreamCollector:
    DATA = 1
    END = 2
    CANCEL = 3

    def __init__(self) -> None:
        self.buffers: dict[int, bytearray] = {}
        self.completed: dict[int, int] = {}
        self.errors: dict[int, str] = {}

    def receive(self, packet: bytes) -> None:
        if len(packet) < 9:
            return
        packet_type = packet[0]
        sequence = int.from_bytes(packet[1:5], "little")
        offset = int.from_bytes(packet[5:9], "little")
        buffer = self.buffers.setdefault(sequence, bytearray())
        if packet_type == self.DATA:
            payload = packet[9:]
            if offset == len(buffer):
                buffer.extend(payload)
            elif offset > len(buffer):
                self.errors[sequence] = f"voice stream gap at {len(buffer)}/{offset}"
        elif packet_type == self.END:
            self.completed[sequence] = offset
        elif packet_type == self.CANCEL:
            self.buffers.pop(sequence, None)
            self.completed.pop(sequence, None)
            self.errors.pop(sequence, None)

    async def take(self, sequence: int, expected_bytes: int, timeout: float = 3.0) -> bytes:
        deadline = time.monotonic() + timeout
        while sequence not in self.completed and time.monotonic() < deadline:
            await asyncio.sleep(0.01)
        error = self.errors.pop(sequence, None)
        buffer = bytes(self.buffers.pop(sequence, b""))
        total = self.completed.pop(sequence, None)
        if error:
            raise RuntimeError(error)
        if total is None:
            raise RuntimeError(f"voice stream end missing for seq={sequence}")
        if total != len(buffer) or expected_bytes != len(buffer):
            raise RuntimeError(
                f"voice stream length mismatch: board={expected_bytes} end={total} received={len(buffer)}"
            )
        return buffer


def truncate_utf8(text: str, max_bytes: int = 192) -> str:
    normalized = " ".join(text.strip().split())
    encoded = normalized.encode("utf-8")
    if len(encoded) <= max_bytes:
        return normalized
    return encoded[:max_bytes].decode("utf-8", "ignore").rstrip()


def mulaw_to_pcm16(payload: bytes) -> bytes:
    pcm = bytearray(len(payload) * 2)
    for index, encoded in enumerate(payload):
        value = (~encoded) & 0xFF
        magnitude = ((value & 0x0F) << 3) + 0x84
        magnitude <<= (value & 0x70) >> 4
        sample = 0x84 - magnitude if value & 0x80 else magnitude - 0x84
        struct.pack_into("<h", pcm, index * 2, max(-32768, min(32767, sample)))
    return bytes(pcm)


def write_wav(path: Path, pcm: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(CHANNELS)
        wav.setsampwidth(SAMPLE_WIDTH_BYTES)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm)


def capture_timestamped_wav(out_dir: Path, prefix: str, pcm: bytes) -> Path:
    timestamp = time.strftime("%Y%m%d-%H%M%S")
    wav_path = out_dir / f"{prefix}-{timestamp}.wav"
    write_wav(wav_path, pcm)
    return wav_path


def append_jsonl(path: Path | None, record: dict[str, Any]) -> None:
    if not path:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")


def load_sidecar_metadata(wav_path: Path) -> dict[str, Any] | None:
    sidecars = [
        Path(str(wav_path) + ".openai.json"),
        Path(str(wav_path) + ".zhipu.json"),
    ]
    for sidecar in sidecars:
        try:
            payload = json.loads(sidecar.read_text(encoding="utf-8"))
        except FileNotFoundError:
            continue
        except json.JSONDecodeError as exc:
            print(f"warning: invalid metadata sidecar {sidecar}: {exc}", file=sys.stderr)
            return None
        if not isinstance(payload, dict):
            print(f"warning: metadata sidecar {sidecar} is not a JSON object", file=sys.stderr)
            return None
        result = dict(payload)
        result["path"] = str(sidecar)
        return result
    return None


def run_reply_command(command_template: str, wav_path: Path, pcm_bytes: int) -> str:
    command = command_template.format(
        wav=shlex.quote(str(wav_path)),
        pcm_bytes=pcm_bytes,
        sample_rate=SAMPLE_RATE,
    )
    print(f"$ {command}")
    completed = subprocess.run(
        command,
        shell=True,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if completed.stderr:
        print(completed.stderr.rstrip(), file=sys.stderr)
    if completed.returncode != 0:
        raise RuntimeError(f"reply command failed with rc={completed.returncode}")
    reply = completed.stdout.strip()
    return reply or f"Captured {pcm_bytes} bytes from Huangshan Pi."


def build_reply_text(reply_text: str | None, reply_command: str | None, wav_path: Path, pcm_bytes: int) -> str:
    if reply_text:
        return reply_text
    if reply_command:
        return run_reply_command(reply_command, wav_path, pcm_bytes)
    return f"Captured voice: {pcm_bytes} bytes, {SAMPLE_RATE} Hz PCM."


def voice_reply_record(
    *,
    transport: str,
    started_at: str,
    wav_path: Path,
    pcm_bytes: int,
    info: dict[str, Any],
    reply_sequence: int,
    reply: str,
    ack: str,
    model_metadata: dict[str, Any] | None = None,
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "event": "voice_reply",
        "started_at": started_at,
        "transport": transport,
        "wav": str(wav_path),
        "pcm_bytes": pcm_bytes,
        "sample_rate": SAMPLE_RATE,
        "channels": CHANNELS,
        "sample_width_bytes": SAMPLE_WIDTH_BYTES,
        "voice_seq": info.get("seq"),
        "reply_sequence": reply_sequence,
        "reply": reply,
        "ack": ack,
    }
    if model_metadata:
        record["model"] = model_metadata
    return record


def reply_only_record(*, transport: str, started_at: str, reply: str, reply_sequence: int, ack: str) -> dict[str, Any]:
    return {
        "event": "reply_only",
        "started_at": started_at,
        "transport": transport,
        "reply": reply,
        "reply_sequence": reply_sequence,
        "ack": ack,
    }


def append_voice_reply_record(path: Path | None, **kwargs: Any) -> dict[str, Any]:
    record = voice_reply_record(**kwargs)
    append_jsonl(path, record)
    return record


def run_self_test() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        wav_path = capture_timestamped_wav(root, "sample", b"\0\0" * 80)
        with wave.open(str(wav_path), "rb") as wav:
            assert wav.getnchannels() == CHANNELS
            assert wav.getsampwidth() == SAMPLE_WIDTH_BYTES
            assert wav.getframerate() == SAMPLE_RATE
            assert wav.getnframes() == 80
        record = voice_reply_record(
            transport="test",
            started_at="2026-01-01T00:00:00+0000",
            wav_path=wav_path,
            pcm_bytes=160,
            info={"seq": "7"},
            reply_sequence=9,
            reply="ok",
            ack="ok flow_send channel=pc.voice seq=9 bytes=2 total=1",
        )
        log_path = root / "voice.jsonl"
        append_jsonl(log_path, record)
        loaded = json.loads(log_path.read_text(encoding="utf-8"))
        assert loaded["wav"] == str(wav_path)
        assert loaded["transport"] == "test"
        assert build_reply_text("static", None, wav_path, 160) == "static"
        assert truncate_utf8("  你好\n 世界  ", 192) == "你好 世界"
        collector = VoiceStreamCollector()
        collector.receive(bytes([1]) + (7).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"abc")
        collector.receive(bytes([1]) + (7).to_bytes(4, "little") + (3).to_bytes(4, "little") + b"def")
        collector.receive(bytes([2]) + (7).to_bytes(4, "little") + (6).to_bytes(4, "little"))
        assert asyncio.run(collector.take(7, 6)) == b"abcdef"
        collector.receive(bytes([1]) + (8).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"cancel")
        collector.receive(bytes([3]) + (8).to_bytes(4, "little") + (6).to_bytes(4, "little"))
        assert 8 not in collector.buffers
        assert mulaw_to_pcm16(bytes([0xFF, 0x7F])) == b"\x00\x00\x00\x00"
    print("voice_bridge_common self-test ok")


def main() -> int:
    parser = argparse.ArgumentParser(description="Shared helpers for VibeBoard voice bridge scripts.")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    parser.error("use --self-test")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
