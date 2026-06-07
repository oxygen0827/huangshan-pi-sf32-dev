#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="$(cd "$ROOT_DIR/.." && pwd)"
SDK_DIR="${SIFLI_SDK_PATH:-$WORKSPACE_DIR/sifli-sdk}"
BOARD="${BOARD:-sf32lb52-lchspi-ulp}"
JOBS="${JOBS:-8}"

cd "$ROOT_DIR/project"
source "$SDK_DIR/export.sh" >/tmp/huangshan-sifli-export.log
scons --board="$BOARD" -j"$JOBS"
