#!/bin/zsh
set -u
set -o pipefail

ROOT="${0:A:h:h}"
SOURCE="$ROOT/scripts/codex_pet_desktop_approval.swift"
HELPER="$ROOT/.local/CodexPetDesktopApproval"
MODULE_CACHE="$ROOT/.local/swift-module-cache"
LOG="$ROOT/.local/codex_pet_monitor.log"
BOARD="${CODEX_PET_BOARD:-}"

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
    printf '%s\n' '正在编译受限桌面审批 helper...'
    CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
        /usr/bin/xcrun swiftc "$SOURCE" -o "$HELPER" || exit 1
fi

printf '\033]0;%s\007' 'Codex Pet Monitor'
printf '\n'
printf '%s\n' '══  VibeBoard Codex Pet 监控  ═══════════════════════════════════════'
printf '%s\n' '服务启动后请用浏览器打开以下地址完成配对与宠物部署：'
printf '\n    http://127.0.0.1:8790/\n'
printf '%s\n' '板子开机后蓝牙会自动扫描 VibeBoard，连接可能需要 30～60 秒。'
printf '%s\n' '按 Ctrl-C 停止服务。'
printf '%s\n' '══════════════════════════════════════════════════════════════════════'
printf '\n'
cd "$ROOT" || exit 1
: > "$LOG"

bridge_args=(
    --mode monitor
    --workspace "$ROOT"
    --approval-helper "$HELPER"
)
if [[ -n "$BOARD" ]]; then
    bridge_args+=(--address "$BOARD")
fi
PYTHONUNBUFFERED=1 "$ROOT/.venv/bin/python" "$ROOT/scripts/codex_pet_bridge.py" \
    "${bridge_args[@]}" 2>&1 | tee "$LOG"
bridge_exit_code=${pipestatus[1]}

printf '\nCodex Pet monitor exited with code %d.\n' "$bridge_exit_code"
exit "$bridge_exit_code"
