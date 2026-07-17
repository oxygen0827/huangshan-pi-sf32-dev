#!/usr/bin/env python3
from __future__ import annotations

import argparse
import io
import math
import struct
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "scripts" / "runtime_apps" / "codex_pet" / "assets"
SAMPLE_RATE = 16_000
AMPLITUDE = 5_500

CUES: dict[str, tuple[tuple[float, int], ...]] = {
    "listening": ((0.075, 880),),
    "submitted": ((0.055, 660), (0.025, 0), (0.070, 880)),
    "needs_input": ((0.070, 740), (0.045, 0), (0.070, 740)),
    "done": ((0.060, 660), (0.025, 0), (0.085, 990)),
    "error": ((0.085, 520), (0.030, 0), (0.100, 330)),
}


def render_cue(parts: tuple[tuple[float, int], ...]) -> bytes:
    samples: list[int] = []
    for duration, frequency in parts:
        count = max(1, round(duration * SAMPLE_RATE))
        edge = min(count // 2, round(0.012 * SAMPLE_RATE))
        for index in range(count):
            if frequency == 0:
                value = 0.0
            else:
                envelope = min(1.0, index / max(1, edge), (count - index - 1) / max(1, edge))
                value = AMPLITUDE * max(0.0, envelope) * math.sin(2 * math.pi * frequency * index / SAMPLE_RATE)
            samples.append(round(value))
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(b"".join(struct.pack("<h", sample) for sample in samples))
    return output.getvalue()


def expected_files() -> dict[str, bytes]:
    return {f"{name}.wav": render_cue(parts) for name, parts in CUES.items()}


def generate(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for name, data in expected_files().items():
        (output / name).write_bytes(data)


def check(output: Path) -> None:
    for name, expected in expected_files().items():
        path = output / name
        if not path.is_file() or path.read_bytes() != expected:
            raise SystemExit(f"cue asset is missing or stale: {path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate deterministic Codex Pet PCM cue assets")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check(args.output)
        print("codex_pet cue assets ok")
    else:
        generate(args.output)
        print(f"generated {len(CUES)} Codex Pet cue assets in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
