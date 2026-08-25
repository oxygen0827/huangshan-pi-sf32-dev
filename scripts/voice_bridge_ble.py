#!/usr/bin/env python3
from __future__ import annotations

import argparse
import asyncio
import sys
import time
from pathlib import Path

from runtime_transport import BLETransport, BLETransportOptions, DEFAULT_BLE_CACHE_PATH, DEFAULT_DEVICE_NAME, RuntimeTransportError, VOICE_CHUNK_BYTES
from voice_bridge_common import (
    DEFAULT_LOG_PATH,
    DEFAULT_OUT_DIR,
    SAMPLE_RATE,
    append_jsonl,
    append_voice_reply_record,
    build_reply_text,
    capture_timestamped_wav,
    load_sidecar_metadata,
    reply_only_record,
)


def fail(message: str, code: int = 1) -> None:
    print(message, file=sys.stderr)
    raise SystemExit(code)


def print_progress(done: int, total: int) -> None:
    print(f"pulled {done}/{total} bytes", end="\r", flush=True)
    if done >= total:
        print()


async def capture_process_reply_once(transport: BLETransport, args: argparse.Namespace) -> tuple[Path, str]:
    started_at = time.strftime("%Y-%m-%dT%H:%M:%S%z")
    pcm, info = await transport.capture_voice(
        args.duration_ms,
        chunk_bytes=args.chunk_bytes,
        ready_timeout=args.ready_timeout,
        poll_interval=args.poll_interval,
        progress=print_progress,
    )
    wav_path = capture_timestamped_wav(args.out_dir, "vibeboard-voice", pcm)
    print(f"saved wav: {wav_path} ({len(pcm)} bytes PCM, seq={info.get('seq', '?')})")

    reply = build_reply_text(args.reply_text, args.reply_command, wav_path, len(pcm))
    print(f"reply: {reply}")
    ack, reply_sequence = await transport.send_voice_reply(reply)
    append_voice_reply_record(
        args.log_jsonl,
        transport="ble",
        started_at=started_at,
        wav_path=wav_path,
        pcm_bytes=len(pcm),
        info=info,
        reply_sequence=reply_sequence,
        reply=reply,
        ack=ack,
        model_metadata=load_sidecar_metadata(wav_path),
    )
    return wav_path, reply


async def prompt_for_turn(turn: int) -> str:
    return await asyncio.to_thread(input, f"turn {turn}> press Enter to record, q to quit: ")


async def run_interactive_session(transport: BLETransport, args: argparse.Namespace) -> int:
    print("interactive voice bridge ready")
    print("press Enter for each capture; type q, quit, or exit to stop")
    turn = 1
    while args.turns <= 0 or turn <= args.turns:
        try:
            command = (await prompt_for_turn(turn)).strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if command in ("q", "quit", "exit"):
            break
        await capture_process_reply_once(transport, args)
        turn += 1
    if args.settle_seconds > 0:
        await asyncio.sleep(args.settle_seconds)
    return 0


async def run_bridge(args: argparse.Namespace) -> int:
    options = BLETransportOptions(
        name=args.name,
        cache=args.cache,
        no_cache=args.no_cache,
        scan_timeout=args.scan_timeout,
        connect_timeout=args.connect_timeout,
        connect_settle=args.connect_settle,
        service_timeout=args.service_timeout,
        response_wait=args.response_wait,
        final_wait=max(args.settle_seconds, args.response_wait),
        disconnect_pause=args.disconnect_pause,
        echo=not args.no_echo,
    )
    try:
        async with BLETransport(options) as transport:
            if args.status_only:
                status, _ = await transport.voice_status()
                print(status)
                return 0

            if args.send_reply_only:
                if not args.reply_text:
                    fail("--send-reply-only requires --reply-text")
                ack, reply_sequence = await transport.send_voice_reply(args.reply_text)
                print(f"sent reply only: {args.reply_text}")
                append_jsonl(
                    args.log_jsonl,
                    reply_only_record(
                        transport="ble",
                        started_at=time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                        reply=args.reply_text,
                        reply_sequence=reply_sequence,
                        ack=ack,
                    ),
                )
                if args.settle_seconds > 0:
                    await asyncio.sleep(args.settle_seconds)
                return 0

            if args.interactive:
                return await run_interactive_session(transport, args)

            await capture_process_reply_once(transport, args)
            if args.settle_seconds > 0:
                await asyncio.sleep(args.settle_seconds)
            return 0
    except (RuntimeTransportError, RuntimeError) as exc:
        fail(str(exc))


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Capture Huangshan Pi microphone audio over BLE, process it on the computer, and return text to the board."
    )
    parser.add_argument("--name", default=DEFAULT_DEVICE_NAME, help="BLE local name to scan for")
    parser.add_argument("--cache", type=Path, default=DEFAULT_BLE_CACHE_PATH, help="Peripheral cache path for reconnects")
    parser.add_argument("--no-cache", action="store_true", help="Ignore cached peripheral address")
    parser.add_argument("--scan-timeout", type=float, default=12.0)
    parser.add_argument("--connect-timeout", type=float, default=12.0)
    parser.add_argument("--connect-settle", type=float, default=0.35, help="Seconds to wait after connect before GATT commands")
    parser.add_argument("--service-timeout", type=float, default=6.0, help="Seconds to wait for GATT service discovery")
    parser.add_argument("--response-wait", type=float, default=0.15)
    parser.add_argument("--settle-seconds", type=float, default=0.8, help="Keep the BLE link open briefly after the final write")
    parser.add_argument("--disconnect-pause", type=float, default=0.8, help="Pause after stopping notifications before disconnect")
    parser.add_argument("--capture-wait", type=float, default=0.2, help="Kept for CLI compatibility; RuntimeTransport waits on explicit voice status.")
    parser.add_argument("--poll-interval", type=float, default=0.25, help="Seconds between voice_status polls while recording")
    parser.add_argument("--ready-timeout", type=float, default=8.0, help="Maximum seconds to wait for a capture to finish")
    parser.add_argument("--duration-ms", type=int, default=1500, help="Capture duration, capped by firmware")
    parser.add_argument("--chunk-bytes", type=int, default=VOICE_CHUNK_BYTES, help="BLE voice_read payload bytes per request")
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--log-jsonl", type=Path, default=DEFAULT_LOG_PATH, help=f"Append session records to a JSONL log. Default: {DEFAULT_LOG_PATH}")
    parser.add_argument("--status-only", action="store_true", help="Only show board voice bridge status")
    parser.add_argument("--send-reply-only", action="store_true", help="Send --reply-text as pc.voice info flow without recording audio")
    parser.add_argument("--interactive", action="store_true", help="Keep one BLE terminal session open for repeated capture/reply turns")
    parser.add_argument("--turns", type=int, default=0, help="Interactive turns to run; 0 means until q/Ctrl-C/EOF")
    parser.add_argument("--reply-text", help="Static text to send back to the board after capture")
    parser.add_argument(
        "--reply-command",
        help="Shell command that processes the WAV and prints reply text. Placeholders: {wav}, {pcm_bytes}, {sample_rate}",
    )
    parser.add_argument("--no-echo", action="store_true")
    args = parser.parse_args()
    return asyncio.run(run_bridge(args))


if __name__ == "__main__":
    raise SystemExit(main())
