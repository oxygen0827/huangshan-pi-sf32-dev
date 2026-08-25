#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-}"

if [[ -z "$PORT" ]]; then
    echo "usage: bash scripts/provision_codex_pet_board.sh /dev/cu.usbserial-XXXX" >&2
    exit 2
fi

echo "[1/3] Flashing bootloader, Runtime, LCPU and flash table"
TMPDIR="${TMPDIR:-/tmp}" "$ROOT_DIR/scripts/flash.sh" "$PORT" \
    --attempts 3 --retry-delay 2 --pre-reset

echo "[2/3] Installing the Codex Pet Runtime app from the local package"
TMPDIR="${TMPDIR:-/tmp}" "$ROOT_DIR/scripts/runtime_install_serial.sh" "$PORT" \
    --package-dir "$ROOT_DIR/scripts/runtime_apps/codex_pet" \
    --app-id codex_pet --binary-install --final-wait 3 --ready-timeout 90 --no-echo

echo "[3/3] Verifying Codex Pet readiness and advancing UI ticks"
TMPDIR="${TMPDIR:-/tmp}" "$ROOT_DIR/scripts/runtime_install_serial.sh" "$PORT" \
    --codex-pet-only --final-wait 1 --ready-timeout 90 --no-echo

echo "Codex Pet board provisioning completed."
