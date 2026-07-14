#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import os
import struct
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path

from runtime_transport import (
    BLETransport,
    BLETransportOptions,
    DEFAULT_DEVICE_NAME,
    RuntimeTransportError,
    VOICE_CHUNK_BYTES,
    ensure_bleak,
    save_ble_cache,
)
from voice_bridge_common import append_jsonl, capture_timestamped_wav
from voice_llm_zhipu import API_KEY_ENV_NAMES


ROOT_DIR = Path(__file__).resolve().parent.parent
DEFAULT_CACHE = Path.home() / ".vibeboard" / "pager_voice.json"
DEFAULT_OUT_DIR = ROOT_DIR / "captures" / "pager"
DEFAULT_LOG = DEFAULT_OUT_DIR / "pager_voice.jsonl"
ASR_CHANNEL = "pager.compose"
ASR_ERROR_CHANNEL = "pager.asr.error"
MAX_TRANSCRIPT_BYTES = 192
HOLD_UNTIL_RELEASE_MS = 0xFFFFFFFF


@dataclass
class MonitorState:
    last_sequence: int = 0
    initialized: bool = False


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


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def safe_int(value: str | None, default: int = 0) -> int:
    try:
        return int(value or "")
    except ValueError:
        return default


def truncate_utf8(text: str, max_bytes: int = MAX_TRANSCRIPT_BYTES) -> str:
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


def api_key_is_set() -> bool:
    return any(os.environ.get(name, "").strip() for name in API_KEY_ENV_NAMES)


async def zhipu_transcribe(wav_path: Path, args: argparse.Namespace) -> str:
    if args.mock_transcript is not None:
        return args.mock_transcript
    command = [
        sys.executable,
        str(ROOT_DIR / "scripts" / "voice_llm_zhipu.py"),
        "--wav",
        str(wav_path),
        "--transcribe-model",
        args.model,
        "--transcribe-only",
    ]
    if args.prompt:
        command.extend(["--prompt", args.prompt])
    process = await asyncio.create_subprocess_exec(
        *command,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await process.communicate()
    if process.returncode != 0:
        detail = stderr.decode("utf-8", "replace").strip().splitlines()
        raise RuntimeError(detail[-1] if detail else f"ASR exited with {process.returncode}")
    transcript = stdout.decode("utf-8", "replace").strip()
    if not transcript:
        raise RuntimeError("ASR returned no text")
    return transcript


async def pull_ready_pcm(transport: BLETransport, sequence: int, total: int) -> bytes:
    pcm = bytearray()
    offset = 0
    while offset < total:
        chunk = await transport.voice_read(
            sequence,
            offset,
            min(VOICE_CHUNK_BYTES, total - offset),
        )
        if not chunk.data:
            raise RuntimeError(f"voice_read returned no data at {offset}/{total}")
        pcm.extend(chunk.data)
        offset += len(chunk.data)
    return bytes(pcm)


async def send_error(transport: BLETransport, sequence: int, message: str) -> str:
    payload = truncate_utf8(message, 96) or "ASR failed"
    return await transport.flow_send(ASR_ERROR_CHANNEL, sequence, payload)


async def process_ready_capture(
    transport: BLETransport,
    collector: VoiceStreamCollector,
    info: dict[str, str],
    args: argparse.Namespace,
) -> None:
    sequence = safe_int(info.get("seq"))
    total = safe_int(info.get("bytes"))
    started_at = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    if sequence <= 0 or total <= 0:
        raise RuntimeError(f"invalid ready capture: seq={sequence} bytes={total}")

    pull_started = time.monotonic()
    if info.get("stream") == "1":
        stream_payload = await collector.take(sequence, total)
        pcm = mulaw_to_pcm16(stream_payload) if info.get("codec") == "mulaw" else stream_payload
        transfer_mode = "notify"
    else:
        pcm = await pull_ready_pcm(transport, sequence, total)
        stream_payload = pcm
        transfer_mode = "legacy-pull"
    pull_seconds = time.monotonic() - pull_started
    prefix = f"pager-{args.cache.stem}-seq{sequence}"
    wav_path = capture_timestamped_wav(args.out_dir, prefix, pcm)
    print(f"voice seq={sequence}: saved {wav_path} ({len(pcm)} PCM bytes)")

    asr_started = time.monotonic()
    transcript = truncate_utf8(await zhipu_transcribe(wav_path, args))
    asr_seconds = time.monotonic() - asr_started
    if not transcript:
        raise RuntimeError("ASR returned an empty transcript")
    ack = await transport.flow_send(ASR_CHANNEL, sequence, transcript)
    append_jsonl(
        args.log_jsonl,
        {
            "event": "pager_asr",
            "started_at": started_at,
            "transport": "ble",
            "voice_seq": sequence,
            "pcm_bytes": len(pcm),
            "stream_bytes": len(stream_payload),
            "codec": info.get("codec", "pcm_s16le"),
            "transfer_mode": transfer_mode,
            "pull_seconds": round(pull_seconds, 3),
            "asr_seconds": round(asr_seconds, 3),
            "wav": str(wav_path),
            "model": args.model if args.mock_transcript is None else "mock",
            "transcript": transcript,
            "ack": ack,
        },
    )
    print(
        f"voice seq={sequence}: transcript returned to Pager in "
        f"{pull_seconds + asr_seconds:.2f}s "
        f"(transfer={pull_seconds:.2f}s, ASR={asr_seconds:.2f}s): {transcript}"
    )


async def monitor_connection(
    transport: BLETransport,
    state: MonitorState,
    args: argparse.Namespace,
) -> bool:
    collector = VoiceStreamCollector()
    await transport.start_voice_stream(collector.receive)
    status, info = await transport.voice_status()
    print(f"connected: {transport.connection_label}")
    print(status)
    if info.get("built") == "0":
        raise RuntimeError("board firmware has no microphone capture support")

    if not state.initialized:
        if args.process_existing and info.get("ready") == "1":
            state.last_sequence = max(0, safe_int(info.get("seq")) - 1)
        else:
            await transport.voice_clear()
            _, info = await transport.voice_status()
            state.last_sequence = safe_int(info.get("seq"))
        state.initialized = True
        print("Pager voice bridge ready; hold the talk button on this board.")

    while True:
        status, info = await transport.voice_status()
        sequence = safe_int(info.get("seq"))
        if info.get("recording") == "1":
            await asyncio.sleep(args.poll_interval)
            continue
        if info.get("ready") == "1" and sequence != state.last_sequence:
            failure: str | None = None
            try:
                await process_ready_capture(transport, collector, info, args)
            except Exception as exc:
                message = f"ASR failed: {exc}"
                failure = message
                print(message, file=sys.stderr)
                try:
                    await send_error(transport, sequence, message)
                except Exception as send_exc:
                    print(f"could not return ASR error to Pager: {send_exc}", file=sys.stderr)
            finally:
                await transport.voice_clear()
                state.last_sequence = sequence
            if args.once:
                if failure:
                    raise RuntimeError(failure)
                return True
        elif info.get("recording") == "0" and info.get("err") not in (None, "0", "1"):
            if sequence != state.last_sequence:
                await send_error(transport, sequence, f"Microphone capture failed ({info.get('err')})")
                await transport.voice_clear()
                state.last_sequence = sequence
                if args.once:
                    return True
        await asyncio.sleep(args.poll_interval)


def make_options(args: argparse.Namespace) -> BLETransportOptions:
    return BLETransportOptions(
        name=args.name,
        address=args.address,
        cache=args.cache,
        no_cache=False,
        scan_timeout=args.scan_timeout,
        connect_timeout=args.connect_timeout,
        connect_settle=0.35,
        service_timeout=6.0,
        response_wait=0.03,
        final_wait=0.8,
        disconnect_pause=0.4,
        write_with_response=False,
        subscribe_status_notifications=False,
        echo=not args.no_echo,
    )


async def run_bridge(args: argparse.Namespace) -> int:
    if args.address:
        save_ble_cache(args.cache, {"name": args.name, "address": args.address})
    state = MonitorState()
    delay = 1.0
    while True:
        try:
            async with BLETransport(make_options(args)) as transport:
                if args.stream_test_seconds:
                    collector = VoiceStreamCollector()
                    await transport.start_voice_stream(collector.receive)
                    ble_status = await transport.command("ble_status")
                    print(ble_status)
                    _, before = await transport.voice_status()
                    expected_sequence = safe_int(before.get("seq")) + 1
                    await transport.voice_start(HOLD_UNTIL_RELEASE_MS, expected_sequence)
                    started = time.monotonic()
                    await asyncio.sleep(args.stream_test_seconds)
                    if args.stream_test_cancel:
                        await transport.voice_clear()
                        await asyncio.sleep(0.1)
                        _, info = await transport.voice_status(expected_sequence)
                        if expected_sequence in collector.buffers:
                            raise RuntimeError("cancelled voice stream was not discarded by the bridge")
                        if info.get("ready") != "0" or info.get("recording") != "0":
                            raise RuntimeError(f"cancelled voice state was not cleared: {info}")
                        print(
                            f"stream cancel test ok: seq={expected_sequence} "
                            f"seconds={time.monotonic() - started:.2f}"
                        )
                        return 0
                    await transport.voice_stop()
                    deadline = time.monotonic() + 5.0
                    info: dict[str, str] = {}
                    while time.monotonic() < deadline:
                        _, info = await transport.voice_status(expected_sequence)
                        if info.get("ready") == "1" and info.get("recording") == "0":
                            break
                        await asyncio.sleep(0.05)
                    stream_payload = await collector.take(expected_sequence, safe_int(info.get("bytes")))
                    pcm = mulaw_to_pcm16(stream_payload) if info.get("codec") == "mulaw" else stream_payload
                    elapsed = time.monotonic() - started
                    print(
                        f"stream test ok: seq={expected_sequence} seconds={elapsed:.2f} "
                        f"stream_bytes={len(stream_payload)} pcm_bytes={len(pcm)} "
                        f"codec={info.get('codec', '?')} dropped={info.get('dropped', '?')} "
                        f"packets={info.get('packets', '?')}"
                    )
                    await transport.voice_clear()
                    return 0
                if args.status_only:
                    status, _ = await transport.voice_status()
                    print(status)
                    return 0
                completed = await monitor_connection(transport, state, args)
                if completed:
                    return 0
        except Exception as exc:
            if args.once or args.status_only:
                fail(str(exc))
            print(f"bridge disconnected: {exc}; retrying in {delay:.0f}s", file=sys.stderr)
            await asyncio.sleep(delay)
            delay = min(delay * 2, 30.0)
        else:
            delay = 1.0


async def list_devices(args: argparse.Namespace) -> int:
    _, scanner = ensure_bleak()
    discovered = await scanner.discover(timeout=args.scan_timeout, return_adv=True)
    matches = []
    for device, advertisement in discovered.values():
        name = device.name or advertisement.local_name or ""
        if name == args.name:
            matches.append((device.address, name, advertisement.rssi))
    if not matches:
        fail(f"no BLE devices named {args.name!r} found")
    for address, name, rssi in sorted(matches):
        print(f"{address}\t{name}\trssi={rssi}")
    return 0


def self_test() -> int:
    assert truncate_utf8("  你好\n 世界  ") == "你好 世界"
    text = "你" * 100
    truncated = truncate_utf8(text)
    assert len(truncated.encode("utf-8")) <= MAX_TRANSCRIPT_BYTES
    assert truncated.encode("utf-8").decode("utf-8") == truncated
    collector = VoiceStreamCollector()
    collector.receive(bytes([1]) + (7).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"abc")
    collector.receive(bytes([1]) + (7).to_bytes(4, "little") + (3).to_bytes(4, "little") + b"def")
    collector.receive(bytes([2]) + (7).to_bytes(4, "little") + (6).to_bytes(4, "little"))
    assert asyncio.run(collector.take(7, 6)) == b"abcdef"
    collector.receive(bytes([1]) + (8).to_bytes(4, "little") + (0).to_bytes(4, "little") + b"cancel")
    collector.receive(bytes([3]) + (8).to_bytes(4, "little") + (6).to_bytes(4, "little"))
    assert 8 not in collector.buffers
    silence = mulaw_to_pcm16(bytes([0xFF, 0x7F]))
    assert silence == b"\x00\x00\x00\x00"
    with tempfile.TemporaryDirectory(prefix="pager_voice_bridge_") as tmp:
        wav = capture_timestamped_wav(Path(tmp), "test", b"\0\0" * 160)
        assert wav.exists() and wav.stat().st_size > 44
    print("pager_voice_bridge self-test ok")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Listen for Pager push-to-talk captures, call Zhipu GLM-ASR-2512, and return editable text."
    )
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME)
    parser.add_argument("--address", help="CoreBluetooth peripheral UUID; saved to --cache")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=12.0)
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument("--model", default="glm-asr-2512")
    parser.add_argument("--prompt", default="黄山派双板 Pager 的简短中文消息")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--log-jsonl", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--once", action="store_true", help="Exit after one capture")
    parser.add_argument("--status-only", action="store_true")
    parser.add_argument(
        "--stream-test-seconds",
        type=float,
        default=0.0,
        help="Capture a BLE voice stream for this many seconds without calling ASR",
    )
    parser.add_argument(
        "--stream-test-cancel",
        action="store_true",
        help="Cancel --stream-test-seconds instead of completing the capture",
    )
    parser.add_argument("--list-devices", action="store_true", help="List matching BLE UUIDs without connecting")
    parser.add_argument("--process-existing", action="store_true", help="Process a ready capture instead of clearing it on startup")
    parser.add_argument("--mock-transcript", help="Skip the API and return this text; useful for hardware tests")
    parser.add_argument("--no-echo", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.self_test:
        return self_test()
    if args.list_devices:
        return asyncio.run(list_devices(args))
    if (
        args.mock_transcript is None
        and not args.status_only
        and not args.stream_test_seconds
        and not api_key_is_set()
    ):
        names = " or ".join(API_KEY_ENV_NAMES)
        fail(f"{names} is required; keep it on this computer, not in board firmware", 2)
    return asyncio.run(run_bridge(args))


if __name__ == "__main__":
    raise SystemExit(main())
