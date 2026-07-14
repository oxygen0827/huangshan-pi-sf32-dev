#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 BOARD_A_COREBLUETOOTH_UUID BOARD_B_COREBLUETOOTH_UUID" >&2
    exit 2
fi

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CACHE_DIR="${HOME}/.vibeboard"
ACTIVE_FILE="$CACHE_DIR/pager-voice-active"
mkdir -p "$CACHE_DIR"

cleanup() {
    kill "${PID_A:-}" "${PID_B:-}" 2>/dev/null || true
    rm -f "$ACTIVE_FILE"
}
trap cleanup EXIT INT TERM
touch "$ACTIVE_FILE"

"$ROOT_DIR/scripts/pager_voice_bridge.sh" \
    --address "$1" \
    --cache "$CACHE_DIR/pager-a.json" \
    --no-echo &
PID_A=$!

"$ROOT_DIR/scripts/pager_voice_bridge.sh" \
    --address "$2" \
    --cache "$CACHE_DIR/pager-b.json" \
    --no-echo &
PID_B=$!

echo "Pager voice bridges running: A=$PID_A B=$PID_B"
wait "$PID_A"
