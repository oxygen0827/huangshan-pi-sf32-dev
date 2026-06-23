#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

"$ROOT_DIR/scripts/runtime_install_ble.sh" \
  --connect-only \
  --no-cache \
  --scan-timeout "${SCAN_TIMEOUT:-8}" \
  --connect-timeout "${CONNECT_TIMEOUT:-8}" \
  --hold-seconds "${HOLD_SECONDS:-30}" \
  "$@"
