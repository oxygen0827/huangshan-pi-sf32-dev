#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${1:-${PORT:-/dev/cu.usbserial-110}}"
SECONDS_TO_CAPTURE="${SECONDS_TO_CAPTURE:-12}"

source "$ROOT_DIR/scripts/sifli-env.sh"
python -c '
import serial
import sys
import time

port = sys.argv[1]
seconds = float(sys.argv[2])

ser = serial.Serial(port, baudrate=1000000, timeout=0.05)
ser.rts = True
time.sleep(0.25)
ser.rts = False
time.sleep(0.25)
ser.timeout = 0.2

start = time.time()
total = b""
while time.time() - start < seconds:
    data = ser.read(4096)
    if data:
        total += data
        sys.stdout.buffer.write(data)
        sys.stdout.flush()
ser.close()

if not total:
    print("<no data>")
' "$PORT" "$SECONDS_TO_CAPTURE"
