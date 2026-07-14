#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENV_FILE="${VOICE_TERMINAL_ENV_FILE:-$ROOT_DIR/.voice_terminal.env}"
if [[ "${VOICE_TERMINAL_SKIP_ENV:-0}" != "1" && -f "$ENV_FILE" ]]; then
    set -a
    # shellcheck disable=SC1091
    . "$ENV_FILE"
    set +a
fi

DURATION_MS=1800
TURNS=0
MODE="interactive"
PROVIDER="${VOICE_LLM_PROVIDER:-openai}"
TRANSCRIBE_MODEL=""
REPLY_MODEL=""
LANGUAGE="${VOICE_TRANSCRIBE_LANGUAGE:-${OPENAI_TRANSCRIBE_LANGUAGE:-zh}}"
LOG_JSONL="$ROOT_DIR/captures/voice_terminal.jsonl"
PRINT_TRANSCRIPT=0
STATUS_ONLY=0
SELF_TEST=0
PREFLIGHT=0
YES_RECORD=0

zhipu_key_is_set() {
    [[ -n "${ZHIPU_API_KEY:-}" || -n "${ZHIPUAI_API_KEY:-}" || -n "${BIGMODEL_API_KEY:-}" ]]
}

api_key_is_set() {
    case "$PROVIDER" in
        openai)
            [[ -n "${OPENAI_API_KEY:-}" ]]
            ;;
        zhipu)
            zhipu_key_is_set
            ;;
        *)
            echo "unknown provider: $PROVIDER" >&2
            exit 2
            ;;
    esac
}

api_key_hint() {
    case "$PROVIDER" in
        openai)
            echo "OPENAI_API_KEY is required. Run: export OPENAI_API_KEY=..." >&2
            ;;
        zhipu)
            echo "ZHIPU_API_KEY is required. Run: export ZHIPU_API_KEY=..." >&2
            echo "Aliases also accepted: ZHIPUAI_API_KEY or BIGMODEL_API_KEY" >&2
            ;;
    esac
}

self_test_helper() {
    case "$PROVIDER" in
        openai)
            "$ROOT_DIR/scripts/voice_llm_openai.sh" --self-test
            ;;
        zhipu)
            "$ROOT_DIR/scripts/voice_llm_zhipu.sh" --self-test
            ;;
        *)
            echo "unknown provider: $PROVIDER" >&2
            exit 2
            ;;
    esac
}

set_provider_defaults() {
    case "$PROVIDER" in
        openai)
            TRANSCRIBE_MODEL="${TRANSCRIBE_MODEL:-${OPENAI_TRANSCRIBE_MODEL:-gpt-4o-mini-transcribe}}"
            REPLY_MODEL="${REPLY_MODEL:-${OPENAI_REPLY_MODEL:-gpt-4.1-mini}}"
            ;;
        zhipu)
            TRANSCRIBE_MODEL="${TRANSCRIBE_MODEL:-${ZHIPU_TRANSCRIBE_MODEL:-glm-asr-2512}}"
            REPLY_MODEL="${REPLY_MODEL:-${ZHIPU_REPLY_MODEL:-glm-4.5-flash}}"
            ;;
        *)
            echo "unknown provider: $PROVIDER" >&2
            exit 2
            ;;
    esac
}

usage() {
    cat <<'EOF'
Usage: ./scripts/voice_terminal.sh [options]

Open one terminal session, capture Huangshan Pi microphone audio over BLE,
process it with a model provider, and return the reply to the board info flow.

Options:
  --once                    Capture/process/reply once, then exit.
  --interactive             Keep a prompt open for repeated turns. Default.
  --turns N                 Interactive turn count; 0 means until q/Ctrl-C/EOF.
  --provider NAME           Model provider: openai or zhipu. Default: openai.
  --duration-ms N           Capture duration per turn. Default: 1800.
  --status-only             Check BLE voice bridge status without recording.
  --self-test               Run offline provider helper checks without recording.
  --preflight               Run BLE status + offline helper checks without recording.
  --yes-record              Confirm Huangshan Pi microphone capture without prompt.
  --print-transcript        Print transcript to stderr from the provider helper.
  --transcribe-model NAME   Override provider ASR model.
  --reply-model NAME        Override provider reply model.
  --language CODE           Default: zh.
  --log-jsonl PATH          Session evidence log. Default: captures/voice_terminal.jsonl.
  -h, --help                Show this help.

Recording modes require the selected provider API key:
  openai: OPENAI_API_KEY
  zhipu:  ZHIPU_API_KEY, ZHIPUAI_API_KEY, or BIGMODEL_API_KEY
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --once)
            MODE="once"
            shift
            ;;
        --interactive)
            MODE="interactive"
            shift
            ;;
        --turns)
            TURNS="${2:?--turns requires a value}"
            shift 2
            ;;
        --provider)
            PROVIDER="${2:?--provider requires a value}"
            shift 2
            ;;
        --duration-ms)
            DURATION_MS="${2:?--duration-ms requires a value}"
            shift 2
            ;;
        --status-only)
            STATUS_ONLY=1
            shift
            ;;
        --self-test)
            SELF_TEST=1
            shift
            ;;
        --preflight)
            PREFLIGHT=1
            shift
            ;;
        --yes-record)
            YES_RECORD=1
            shift
            ;;
        --print-transcript)
            PRINT_TRANSCRIPT=1
            shift
            ;;
        --transcribe-model)
            TRANSCRIBE_MODEL="${2:?--transcribe-model requires a value}"
            shift 2
            ;;
        --reply-model)
            REPLY_MODEL="${2:?--reply-model requires a value}"
            shift 2
            ;;
        --language)
            LANGUAGE="${2:?--language requires a value}"
            shift 2
            ;;
        --log-jsonl)
            LOG_JSONL="${2:?--log-jsonl requires a value}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ "$STATUS_ONLY" == "1" ]]; then
    exec "$ROOT_DIR/scripts/voice_bridge_ble.sh" --status-only
fi

set_provider_defaults

if [[ "$SELF_TEST" == "1" ]]; then
    self_test_helper
    exit 0
fi

if [[ "$PREFLIGHT" == "1" ]]; then
    echo "[1/3] BLE voice bridge status"
    "$ROOT_DIR/scripts/voice_bridge_ble.sh" --status-only
    echo
    echo "[2/3] $PROVIDER helper offline self-test"
    self_test_helper
    echo
    if api_key_is_set; then
        echo "[3/3] $PROVIDER API key is set"
    else
        echo "[3/3] $PROVIDER API key is not set; recording modes will ask you to export it first"
    fi
    exit 0
fi

if ! api_key_is_set; then
    api_key_hint
    exit 2
fi

if [[ "$YES_RECORD" != "1" ]]; then
    echo "This will capture audio from the Huangshan Pi microphone over BLE." >&2
    read -r -p "Type 'record' to continue: " CONFIRM
    if [[ "$CONFIRM" != "record" ]]; then
        echo "cancelled" >&2
        exit 130
    fi
fi

case "$PROVIDER" in
    openai)
        METADATA_SUFFIX="openai"
        HELPER="$ROOT_DIR/scripts/voice_llm_openai.sh"
        ;;
    zhipu)
        METADATA_SUFFIX="zhipu"
        HELPER="$ROOT_DIR/scripts/voice_llm_zhipu.sh"
        ;;
esac

REPLY_COMMAND="$HELPER --wav {wav} --metadata-json {wav}.$METADATA_SUFFIX.json --transcribe-model $TRANSCRIBE_MODEL --reply-model $REPLY_MODEL --language $LANGUAGE"
if [[ "$PRINT_TRANSCRIPT" == "1" ]]; then
    REPLY_COMMAND="$REPLY_COMMAND --print-transcript"
fi

args=(
    --duration-ms "$DURATION_MS"
    --log-jsonl "$LOG_JSONL"
    --reply-command "$REPLY_COMMAND"
)

if [[ "$MODE" == "interactive" ]]; then
    args+=(--interactive --turns "$TURNS")
fi

exec "$ROOT_DIR/scripts/voice_bridge_ble.sh" "${args[@]}"
