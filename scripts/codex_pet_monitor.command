#!/bin/zsh
set -u
set -o pipefail

ROOT="${0:A:h:h}"
SOURCE="$ROOT/scripts/codex_pet_desktop_approval.swift"
HELPER="$ROOT/.local/CodexPetDesktopApproval"
MODULE_CACHE="$ROOT/.local/swift-module-cache"
LOG="${TMPDIR:-/tmp}/huangshan_codex_pet_monitor.log"
BOARD="${CODEX_PET_BOARD:-83ECC050-7656-62E0-746A-7B5F0DDBA396}"

if [[ "${1:-}" == "--self-test" ]]; then
    mkdir -p "$MODULE_CACHE"
    CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
        /usr/bin/xcrun swiftc "$SOURCE" -o "${TMPDIR:-/tmp}/CodexPetDesktopApproval.selftest" || exit 1
    "${TMPDIR:-/tmp}/CodexPetDesktopApproval.selftest" --self-test || exit 1
    printf '%s\n' 'codex_pet_monitor launcher self-test ok'
    exit 0
fi

mkdir -p "$ROOT/.local"
mkdir -p "$MODULE_CACHE"
if [[ ! -x "$HELPER" || "$SOURCE" -nt "$HELPER" ]]; then
    printf '%s\n' 'Building the restricted Codex Desktop approval helper...'
    CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
        /usr/bin/xcrun swiftc "$SOURCE" -o "$HELPER" || exit 1
fi

clear
printf '\033]0;%s\007' 'Codex Pet Desktop Monitor'
printf '%s\n' 'Huangshan Codex Pet desktop monitor'
printf '%s\n' 'Voice input is disabled; existing Codex Desktop tasks are watched through Hooks.'
printf '%s\n' 'Keep this terminal open. Press Ctrl-C to stop.'
printf '%s\n\n' 'Accessibility is requested only when desktop approval is enabled.'
cd "$ROOT" || exit 1
: > "$LOG"

PYTHONUNBUFFERED=1 "$ROOT/.venv/bin/python" "$ROOT/scripts/codex_pet_bridge.py" \
    --mode monitor \
    --workspace "$ROOT" \
    --address "$BOARD" \
    --approval-helper "$HELPER" \
    2>&1 | tee "$LOG"
bridge_exit_code=${pipestatus[1]}

printf '\nCodex Pet monitor exited with code %d.\n' "$bridge_exit_code"
exit "$bridge_exit_code"
