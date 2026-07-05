#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"

"$PYTHON" "$ROOT_DIR/scripts/runtime_full_reliability.py" "$@"
