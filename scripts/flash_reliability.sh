#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BOARD="${BOARD:-sf32lb52-lchspi-ulp}"
BUILD_DIR="$ROOT_DIR/project/build_${BOARD}_hcpu"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Missing $BUILD_DIR. Run ./scripts/build.sh first." >&2
    exit 1
fi

source "$ROOT_DIR/scripts/sifli-env.sh"
python "$ROOT_DIR/scripts/flash_reliability.py" "$@"
