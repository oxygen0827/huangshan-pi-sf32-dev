#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
import wave
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Mapping


TTS_SAMPLE_RATES = {16_000, 24_000}
TTS_MAX_SECONDS = 10.0
TTS_MAX_WAV_BYTES = 512 * 1024

KWS_MAX_FALSE_ACCEPTS_PER_HOUR = 0.2
KWS_MAX_FALSE_REJECT_RATE = 0.08
KWS_MAX_P95_LATENCY_MS = 700
KWS_MAX_CURRENT_DELTA_MA = 10.0
KWS_MAX_PEAK_RAM_BYTES = 128 * 1024
KWS_MIN_TEST_HOURS = 8.0
KWS_MIN_UTTERANCES = 200
AEC_MIN_ERLE_DB = 15.0
AEC_MIN_BARGE_IN_RECALL = 0.90


class AudioPolicyError(ValueError):
    pass


@dataclass(frozen=True)
class TTSWavInfo:
    sha256: str
    bytes: int
    sample_rate: int
    frames: int
    duration_seconds: float
    channels: int
    sample_width: int


@dataclass(frozen=True)
class WakeWordMetrics:
    test_hours: float
    utterances: int
    false_accepts_per_hour: float
    false_reject_rate: float
    p95_latency_ms: int
    average_current_delta_ma: float
    peak_ram_bytes: int
    aec_erle_db: float
    barge_in_recall: float
    privacy_indicator: bool


@dataclass(frozen=True)
class WakeWordGate:
    passed: bool
    failures: tuple[str, ...]


def inspect_tts_wav(data: bytes) -> TTSWavInfo:
    if not data or len(data) > TTS_MAX_WAV_BYTES:
        raise AudioPolicyError(f"TTS WAV must be 1..{TTS_MAX_WAV_BYTES} bytes")
    try:
        with wave.open(io.BytesIO(data), "rb") as wav:
            channels = wav.getnchannels()
            sample_width = wav.getsampwidth()
            sample_rate = wav.getframerate()
            frames = wav.getnframes()
            compression = wav.getcomptype()
    except (EOFError, wave.Error) as exc:
        raise AudioPolicyError("TTS output is not a valid WAV file") from exc
    if compression != "NONE":
        raise AudioPolicyError("TTS WAV must use uncompressed PCM")
    if channels != 1 or sample_width != 2:
        raise AudioPolicyError("TTS WAV must be mono 16-bit PCM")
    if sample_rate not in TTS_SAMPLE_RATES:
        raise AudioPolicyError("TTS WAV sample rate must be 16000 or 24000 Hz")
    if frames <= 0:
        raise AudioPolicyError("TTS WAV contains no samples")
    duration = frames / sample_rate
    if duration > TTS_MAX_SECONDS:
        raise AudioPolicyError(f"TTS WAV exceeds {TTS_MAX_SECONDS:.0f} seconds")
    return TTSWavInfo(
        sha256=hashlib.sha256(data).hexdigest(),
        bytes=len(data),
        sample_rate=sample_rate,
        frames=frames,
        duration_seconds=round(duration, 4),
        channels=channels,
        sample_width=sample_width,
    )


def wake_word_gate(metrics: WakeWordMetrics) -> WakeWordGate:
    failures: list[str] = []
    checks = (
        (metrics.test_hours >= KWS_MIN_TEST_HOURS, "test_hours"),
        (metrics.utterances >= KWS_MIN_UTTERANCES, "utterances"),
        (0 <= metrics.false_accepts_per_hour <= KWS_MAX_FALSE_ACCEPTS_PER_HOUR, "false_accepts_per_hour"),
        (0 <= metrics.false_reject_rate <= KWS_MAX_FALSE_REJECT_RATE, "false_reject_rate"),
        (0 <= metrics.p95_latency_ms <= KWS_MAX_P95_LATENCY_MS, "p95_latency_ms"),
        (metrics.average_current_delta_ma <= KWS_MAX_CURRENT_DELTA_MA, "average_current_delta_ma"),
        (0 <= metrics.peak_ram_bytes <= KWS_MAX_PEAK_RAM_BYTES, "peak_ram_bytes"),
        (metrics.aec_erle_db >= AEC_MIN_ERLE_DB, "aec_erle_db"),
        (AEC_MIN_BARGE_IN_RECALL <= metrics.barge_in_recall <= 1, "barge_in_recall"),
        (metrics.privacy_indicator, "privacy_indicator"),
    )
    failures.extend(label for passed, label in checks if not passed)
    return WakeWordGate(not failures, tuple(failures))


def metrics_from_mapping(value: Mapping[str, object]) -> WakeWordMetrics:
    try:
        return WakeWordMetrics(
            test_hours=float(value["test_hours"]),
            utterances=int(value["utterances"]),
            false_accepts_per_hour=float(value["false_accepts_per_hour"]),
            false_reject_rate=float(value["false_reject_rate"]),
            p95_latency_ms=int(value["p95_latency_ms"]),
            average_current_delta_ma=float(value["average_current_delta_ma"]),
            peak_ram_bytes=int(value["peak_ram_bytes"]),
            aec_erle_db=float(value["aec_erle_db"]),
            barge_in_recall=float(value["barge_in_recall"]),
            privacy_indicator=value["privacy_indicator"] is True,
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise AudioPolicyError("wake-word metrics are incomplete or invalid") from exc


def self_test() -> None:
    from generate_codex_pet_cues import expected_files

    cue = expected_files()["done.wav"]
    info = inspect_tts_wav(cue)
    assert info.sample_rate == 16_000 and 0 < info.duration_seconds < 1
    try:
        inspect_tts_wav(b"not wav")
        raise AssertionError("invalid WAV was accepted")
    except AudioPolicyError:
        pass
    good = WakeWordMetrics(8, 200, 0.2, 0.08, 700, 10, 128 * 1024, 15, 0.9, True)
    assert wake_word_gate(good).passed
    bad = WakeWordMetrics(1, 10, 2, 0.5, 1500, 20, 256 * 1024, 5, 0.2, False)
    result = wake_word_gate(bad)
    assert not result.passed and len(result.failures) == 10


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate host-generated Codex Pet audio and KWS metrics")
    parser.add_argument("--validate-wav", type=Path)
    parser.add_argument("--evaluate-kws", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    selected = sum(bool(value) for value in (args.validate_wav, args.evaluate_kws, args.self_test))
    if selected != 1:
        parser.error("choose exactly one of --validate-wav, --evaluate-kws, or --self-test")
    if args.self_test:
        self_test()
        print("codex_pet_audio self-test ok")
        return 0
    if args.validate_wav:
        print(json.dumps(asdict(inspect_tts_wav(args.validate_wav.read_bytes())), sort_keys=True))
        return 0
    assert args.evaluate_kws is not None
    value = json.loads(args.evaluate_kws.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise AudioPolicyError("wake-word metrics must be a JSON object")
    result = wake_word_gate(metrics_from_mapping(value))
    print(json.dumps(asdict(result), sort_keys=True))
    return 0 if result.passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AudioPolicyError, OSError, json.JSONDecodeError) as exc:
        print(f"codex_pet_audio: {exc}", file=sys.stderr)
        raise SystemExit(1)
