#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export VOICE_TERMINAL_SKIP_ENV=1

assert_contains() {
    local haystack="$1"
    local needle="$2"
    local label="$3"
    if [[ "$haystack" != *"$needle"* ]]; then
        echo "FAIL: $label" >&2
        echo "expected to find: $needle" >&2
        echo "$haystack" >&2
        exit 1
    fi
}

run_expect() {
    local expected_rc="$1"
    shift
    set +e
    local output
    output="$("$@" 2>&1)"
    local rc=$?
    set -e
    if [[ "$rc" != "$expected_rc" ]]; then
        echo "FAIL: expected rc=$expected_rc, got rc=$rc for: $*" >&2
        echo "$output" >&2
        exit 1
    fi
    printf '%s' "$output"
}

help_output="$(run_expect 0 "$ROOT_DIR/scripts/voice_terminal.sh" --help)"
assert_contains "$help_output" "--preflight" "help includes preflight"
assert_contains "$help_output" "--yes-record" "help includes yes-record"
assert_contains "$help_output" "--log-jsonl" "help includes log-jsonl"
assert_contains "$help_output" "--provider" "help includes provider"
assert_contains "$help_output" "zhipu" "help includes zhipu provider"

self_test_output="$(run_expect 0 "$ROOT_DIR/scripts/voice_terminal.sh" --self-test)"
assert_contains "$self_test_output" "voice_llm_openai self-test ok" "terminal self-test delegates to helper"

zhipu_self_test_output="$(run_expect 0 "$ROOT_DIR/scripts/voice_terminal.sh" --provider zhipu --self-test)"
assert_contains "$zhipu_self_test_output" "voice_llm_zhipu self-test ok" "terminal self-test delegates to zhipu helper"

unset OPENAI_API_KEY
no_key_output="$(run_expect 2 "$ROOT_DIR/scripts/voice_terminal.sh" --once --duration-ms 500)"
assert_contains "$no_key_output" "OPENAI_API_KEY is required" "recording path requires API key before BLE"

unset ZHIPU_API_KEY ZHIPUAI_API_KEY BIGMODEL_API_KEY
zhipu_no_key_output="$(run_expect 2 "$ROOT_DIR/scripts/voice_terminal.sh" --provider zhipu --once --duration-ms 500)"
assert_contains "$zhipu_no_key_output" "ZHIPU_API_KEY is required" "zhipu recording path requires API key before BLE"

set +e
cancel_output="$(printf 'no\n' | OPENAI_API_KEY=dummy "$ROOT_DIR/scripts/voice_terminal.sh" --once --duration-ms 500 2>&1)"
cancel_rc=$?
set -e
if [[ "$cancel_rc" != "130" ]]; then
    echo "FAIL: expected rc=130, got rc=$cancel_rc for cancelled recording prompt" >&2
    echo "$cancel_output" >&2
    exit 1
fi
assert_contains "$cancel_output" "This will capture audio from the Huangshan Pi microphone" "recording prompt explains capture"
assert_contains "$cancel_output" "cancelled" "recording prompt can cancel before BLE"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT
wav_path="$tmpdir/sample.wav"
log_path="$tmpdir/voice.jsonl"
"${PYTHON:-python3}" - "$wav_path" "$log_path" <<'PY'
import json
import sys
import wave
from pathlib import Path

wav_path = Path(sys.argv[1])
log_path = Path(sys.argv[2])
with wave.open(str(wav_path), "wb") as wav:
    wav.setnchannels(1)
    wav.setsampwidth(2)
    wav.setframerate(16000)
    wav.writeframes(b"\x00\x00" * 80)
record = {
    "event": "voice_reply",
    "wav": str(wav_path),
    "pcm_bytes": 160,
    "sample_rate": 16000,
    "channels": 1,
    "sample_width_bytes": 2,
    "reply_sequence": 123,
    "reply": "ok",
    "ack": "ok flow_send channel=pc.voice seq=123 bytes=2 total=1",
    "model": {
        "provider": "openai",
        "transcribe_model": "gpt-4o-mini-transcribe",
        "reply_model": "gpt-4.1-mini",
        "transcript": "你好",
        "reply": "ok",
    },
}
log_path.write_text(json.dumps(record, ensure_ascii=False) + "\n", encoding="utf-8")
PY
verify_output="$(run_expect 0 "$ROOT_DIR/scripts/voice_terminal_verify.sh" --log-jsonl "$log_path")"
assert_contains "$verify_output" "voice terminal evidence ok" "verifier accepts valid evidence"

missing_output="$(run_expect 1 "$ROOT_DIR/scripts/voice_terminal_verify.sh" --log-jsonl "$tmpdir/missing.jsonl")"
assert_contains "$missing_output" "log not found" "verifier reports missing evidence log"

echo "voice_terminal self-test ok"
