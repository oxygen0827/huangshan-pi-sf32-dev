#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
import wave
from pathlib import Path

from runtime_transport import parse_key_values


DEFAULT_LOG = Path("captures") / "voice_terminal.jsonl"


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def load_records(path: Path) -> list[dict]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        fail(f"log not found: {path}")
    records: list[dict] = []
    for index, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as exc:
            fail(f"invalid JSONL at {path}:{index}: {exc}")
        if not isinstance(record, dict):
            fail(f"invalid JSONL at {path}:{index}: record is not an object")
        records.append(record)
    return records


def latest_voice_reply(records: list[dict]) -> dict:
    for record in reversed(records):
        if record.get("event") == "voice_reply":
            return record
    fail("no voice_reply record found")


def verify_wav(record: dict) -> None:
    wav_value = record.get("wav")
    if not isinstance(wav_value, str) or not wav_value:
        fail("voice_reply record missing wav path")
    wav_path = Path(wav_value)
    if not wav_path.exists():
        fail(f"WAV file not found: {wav_path}")
    try:
        with wave.open(str(wav_path), "rb") as wav:
            frames = wav.getnframes()
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
    except wave.Error as exc:
        fail(f"invalid WAV file {wav_path}: {exc}")
    pcm_bytes = frames * channels * sample_width
    expected_pcm = int(record.get("pcm_bytes", 0))
    if expected_pcm <= 0:
        fail("voice_reply record has no positive pcm_bytes")
    if pcm_bytes != expected_pcm:
        fail(f"WAV PCM byte mismatch: log={expected_pcm}, wav={pcm_bytes}")
    if sample_rate != int(record.get("sample_rate", 0)):
        fail(f"WAV sample rate mismatch: log={record.get('sample_rate')}, wav={sample_rate}")
    if channels != int(record.get("channels", 0)):
        fail(f"WAV channel mismatch: log={record.get('channels')}, wav={channels}")
    if sample_width != int(record.get("sample_width_bytes", 0)):
        fail(f"WAV sample width mismatch: log={record.get('sample_width_bytes')}, wav={sample_width}")


def verify_record(record: dict) -> None:
    reply = record.get("reply")
    if not isinstance(reply, str) or not reply.strip():
        fail("voice_reply record has empty reply")
    ack = record.get("ack")
    if not isinstance(ack, str) or not ack.startswith("ok flow_send "):
        fail(f"voice_reply record ack does not prove board flow_send success: {ack!r}")
    reply_sequence = record.get("reply_sequence")
    if not reply_sequence:
        fail("voice_reply record missing reply_sequence")
    ack_values = parse_key_values(ack)
    if ack_values.get("channel") != "pc.voice":
        fail(f"voice_reply ack channel mismatch: {ack!r}")
    if str(reply_sequence) != ack_values.get("seq"):
        fail(f"voice_reply ack sequence mismatch: log={reply_sequence!r}, ack={ack!r}")
    expected_bytes = len(reply.encode("utf-8"))
    if str(expected_bytes) != ack_values.get("bytes"):
        fail(f"voice_reply ack byte count mismatch: expected={expected_bytes}, ack={ack!r}")
    model = record.get("model")
    if model is not None:
        if not isinstance(model, dict):
            fail("voice_reply model metadata is not an object")
        for key in ("provider", "transcribe_model", "reply_model", "transcript", "reply"):
            value = model.get(key)
            if not isinstance(value, str) or not value.strip():
                fail(f"voice_reply model metadata missing {key}")
        if model.get("reply") != reply:
            fail("voice_reply model reply does not match returned reply")
    verify_wav(record)


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify the latest VibeBoard terminal voice session evidence log.")
    parser.add_argument("--log-jsonl", type=Path, default=DEFAULT_LOG)
    parser.add_argument("--print-record", action="store_true")
    args = parser.parse_args()

    record = latest_voice_reply(load_records(args.log_jsonl))
    verify_record(record)
    print(
        "voice terminal evidence ok: "
        f"wav={record['wav']} pcm_bytes={record['pcm_bytes']} "
        f"reply_sequence={record['reply_sequence']}"
    )
    if args.print_record:
        print(json.dumps(record, ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
