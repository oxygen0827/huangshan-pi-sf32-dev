#!/bin/zsh

set -u
set -o pipefail

ROOT="${0:A:h:h}"
STAMP="$(date +%Y%m%d_%H%M%S)"
OUTPUT="${CODEX_PET_SOAK_OUTPUT:-$HOME/.vibeboard/codex_pet_soak_${STAMP}.jsonl}"

clear
printf '\033]0;%s\007' 'Codex Pet 24h Soak'
printf '%s\n' 'Huangshan Codex Pet 24-hour stability test'
printf 'JSONL: %s\n' "$OUTPUT"
printf '%s\n' 'Keep this terminal and the Codex Pet Desktop Monitor terminal open.'
printf '%s\n' 'Required: normal Mac sleep/wake, one board reset, and one Bridge restart.'
printf '%s\n\n' 'Do not start another BLE client. Press Ctrl-C only to abort the test.'

cd "$ROOT" || exit 1
mkdir -p "${OUTPUT:h}"

PYTHONPATH="$ROOT/scripts" PYTHONUNBUFFERED=1 \
  "$ROOT/.venv/bin/python" "$ROOT/scripts/codex_pet_soak.py" \
  --duration-hours 24 \
  --sample-seconds 5 \
  --exercise \
  --exercise-seconds 600 \
  --require-sleep-gap \
  --minimum-reconnects 2 \
  --minimum-exercises 100 \
  --output "$OUTPUT"
exit_code=$?

printf '\nCodex Pet soak exited with code %d.\n' "$exit_code"
printf 'Result: %s\n' "$OUTPUT"
exit "$exit_code"
