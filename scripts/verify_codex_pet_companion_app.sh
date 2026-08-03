#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
APP="${1:-$ROOT/.local/VibeBoard Companion.app}"
APP="${APP:A}"
CONTENTS="$APP/Contents"
RESOURCES="$CONTENTS/Resources"
AGENT="$RESOURCES/Agent/VibeBoardCompanionAgent"
AGENT_ROOT="$RESOURCES/AgentRoot"
NODE="$RESOURCES/Tools/node"
SHARP="$RESOURCES/Tools/node_modules/sharp"

fail() {
  print -u2 "verify_codex_pet_companion_app: $1"
  exit 1
}

[[ -d "$APP" ]] || fail "app not found: $APP"
[[ -x "$CONTENTS/MacOS/VibeBoardCompanion" ]] || fail "native launcher is missing"
[[ -x "$AGENT" ]] || fail "bundled Agent is missing"
[[ -x "$RESOURCES/CodexPetDesktopApproval" ]] || fail "approval helper is missing"
[[ -x "$NODE" ]] || fail "bundled Node.js is missing"
[[ -d "$SHARP" ]] || fail "bundled Sharp is missing"

[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$CONTENTS/Info.plist")" == "dev.vibeboard.companion" ]] \
  || fail "unexpected bundle identifier"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleURLTypes:0:CFBundleURLSchemes:0' "$CONTENTS/Info.plist")" == "vibeboard" ]] \
  || fail "vibeboard:// URL handler is missing"
/usr/bin/codesign --verify --deep --strict "$APP"

export VIBEBOARD_COMPANION_ROOT="$AGENT_ROOT"
export VIBEBOARD_COMPANION_AGENT="$AGENT"
export NODE
export CODEX_PET_SHARP="$SHARP"

"$AGENT" --agent-self-test
"$AGENT" --firmware-health --self-test
"$AGENT" --self-test
"$NODE" "$AGENT_ROOT/scripts/hpet_crypto.js" --self-test
"$NODE" "$AGENT_ROOT/scripts/build_hpet_petdex.js" --self-test

print "VibeBoard Companion package verification ok: $APP"
