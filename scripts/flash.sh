#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SDK_DIR="${SIFLI_SDK_PATH:-/Users/wq/sifli-sdk}"
BOARD="${BOARD:-sf32lb52-lchspi-ulp}"
PORT="${1:-${PORT:-/dev/cu.usbserial-110}}"
BUILD_DIR="$ROOT_DIR/project/build_${BOARD}_hcpu"

if [[ ! -x "$BUILD_DIR/uart_download.sh" ]]; then
    echo "Missing $BUILD_DIR/uart_download.sh. Run ./scripts/build.sh first." >&2
    exit 1
fi

cd "$BUILD_DIR"
source "$SDK_DIR/export.sh" >/tmp/huangshan-sifli-export.log
./uart_download.sh "$PORT"
