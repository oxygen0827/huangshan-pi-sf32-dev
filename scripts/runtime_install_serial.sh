#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

source "$ROOT_DIR/scripts/sifli-env.sh"
python "$ROOT_DIR/scripts/runtime_install_serial.py" "$@"
