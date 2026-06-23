#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"

if [[ -x "$ROOT_DIR/.venv-ble/bin/python" ]]; then
    PYTHON="$ROOT_DIR/.venv-ble/bin/python"
fi

"$PYTHON" "$ROOT_DIR/scripts/runtime_install_ble.py" "$@"
