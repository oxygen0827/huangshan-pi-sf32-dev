#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [[ -f "$ROOT_DIR/.voice_terminal.env" ]]; then
    set -a
    # shellcheck disable=SC1091
    . "$ROOT_DIR/.voice_terminal.env"
    set +a
fi

PYTHON="${PYTHON:-python3}"
if [[ -x "$ROOT_DIR/.venv-ble/bin/python" ]]; then
    PYTHON="$ROOT_DIR/.venv-ble/bin/python"
fi

exec "$PYTHON" "$ROOT_DIR/scripts/pager_voice_bridge.py" "$@"
