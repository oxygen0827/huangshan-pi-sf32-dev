#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SDK_DIR="${SIFLI_SDK_PATH:-/Users/wq/sifli-sdk}"
BOARD="${BOARD:-sf32lb52-lchspi-ulp}"
JOBS="${JOBS:-8}"

cd "$ROOT_DIR/project"
source "$SDK_DIR/export.sh" >/tmp/huangshan-sifli-export.log
scons --board="$BOARD" -j"$JOBS"
