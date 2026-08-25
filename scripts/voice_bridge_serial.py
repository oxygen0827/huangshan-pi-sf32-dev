#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from runtime_transport import RuntimeTransportError, SERIAL_MSH_VOICE_CHUNK_BYTES, SerialTransport, SerialTransportOptions
from voice_bridge_common import (
    DEFAULT_LOG_PATH,
    DEFAULT_OUT_DIR,
    append_jsonl,
    append_voice_reply_record,
    capture_timestamped_wav,
    reply_only_record,
)


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def print_progress(done: int, total: int) -> None:
    print(f"pulled {done}/{total} bytes", end="\r", flush=True)
    if done >= total:
        print()


def capture_process_reply_once(transport: SerialTransport, args: argparse.Namespace) -> tuple[Path, str]:
    started_at = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    pcm, info = transport.capture_voice(
        args.duration_ms,
        chunk_bytes=args.chunk_bytes,
        ready_timeout=args.ready_timeout,
        poll_interval=args.poll_interval,
        progress=print_progress,
    )
    wav_path = capture_timestamped_wav(args.out_dir, "vibeboard-voice-serial", pcm)
    print(f"saved wav: {wav_path} ({len(pcm)} bytes PCM, seq={info.get('seq', '?')})")

    reply = args.reply_text or f"Captured voice over serial: {len(pcm)} bytes"
    print(f"reply: {reply}")
    ack, reply_sequence = transport.send_voice_reply(reply)
    append_voice_reply_record(
        args.log_jsonl,
        transport="serial",
        started_at=started_at,
        wav_path=wav_path,
        pcm_bytes=len(pcm),
        info=info,
        reply_sequence=reply_sequence,
        reply=reply,
        ack=ack,
    )
    return wav_path, reply


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture Huangshan Pi microphone audio over serial, save WAV, and send a text reply back to the board.")
    parser.add_argument("port", help="Serial port, for example /dev/cu.usbserial-13220")
    parser.add_argument("--baud", type=int, default=1_000_000)
    parser.add_argument("--ready-timeout", type=float, default=24.0)
    parser.add_argument("--response-wait", type=float, default=0.15)
    parser.add_argument("--capture-wait", type=float, default=0.2, help="Kept for CLI compatibility; RuntimeTransport waits on explicit voice status.")
    parser.add_argument("--poll-interval", type=float, default=0.25)
    parser.add_argument("--duration-ms", type=int, default=1500, help="Capture duration, capped by firmware")
    parser.add_argument("--chunk-bytes", type=int, default=SERIAL_MSH_VOICE_CHUNK_BYTES, help="voice_read payload bytes per request; keep this small enough for serial MSH line limits")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--log-jsonl", type=Path, default=DEFAULT_LOG_PATH)
    parser.add_argument("--status-only", action="store_true", help="Only show board voice bridge status")
    parser.add_argument("--send-reply-only", action="store_true", help="Send --reply-text as pc.voice info flow without recording audio")
    parser.add_argument("--reply-text", default="电脑端串口语音回写验证 OK", help="Static text to send back to the board after capture")
    parser.add_argument("--no-echo", action="store_true")
    args = parser.parse_args()
    if args.status_only and args.send_reply_only:
        parser.error("use only one of --status-only and --send-reply-only")

    options = SerialTransportOptions(
        port=args.port,
        baud=args.baud,
        command_wait=args.response_wait,
        final_wait=max(args.response_wait, 0.15),
        ready_timeout=args.ready_timeout,
        echo=not args.no_echo,
    )
    try:
        with SerialTransport(options) as transport:
            if args.status_only:
                status, _ = transport.voice_status()
                print(status)
                return 0
            if args.send_reply_only:
                if not args.reply_text:
                    fail("--send-reply-only requires --reply-text")
                ack, reply_sequence = transport.send_voice_reply(args.reply_text)
                print(f"sent reply only: {args.reply_text}")
                append_jsonl(
                    args.log_jsonl,
                    reply_only_record(
                        transport="serial",
                        started_at=time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                        reply=args.reply_text,
                        reply_sequence=reply_sequence,
                        ack=ack,
                    ),
                )
                return 0
            capture_process_reply_once(transport, args)
            return 0
    except RuntimeTransportError as exc:
        fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
