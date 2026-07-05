#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

source "$ROOT_DIR/scripts/sifli-env.sh"
PYTHONDONTWRITEBYTECODE=1 python "$ROOT_DIR/scripts/voice_bridge_serial.py" "$@"
