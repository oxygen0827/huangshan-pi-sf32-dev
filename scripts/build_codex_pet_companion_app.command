#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
APP="$ROOT/.local/VibeBoard Companion.app"
CONTENTS="$APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
MODULE_CACHE="$ROOT/.local/swift-module-cache"
IDENTITY="${CODEX_PET_CODESIGN_IDENTITY:--}"

mkdir -p "$MACOS" "$RESOURCES" "$MODULE_CACHE"
cp "$ROOT/scripts/VibeBoardCompanion-Info.plist" "$CONTENTS/Info.plist"

CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
  /usr/bin/xcrun swiftc \
    -framework AppKit \
    "$ROOT/scripts/codex_pet_companion_app.swift" \
    -o "$MACOS/VibeBoardCompanion"

CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
  /usr/bin/xcrun swiftc \
    "$ROOT/scripts/codex_pet_desktop_approval.swift" \
    -o "$RESOURCES/CodexPetDesktopApproval"

/usr/bin/codesign --force --deep --options runtime --sign "$IDENTITY" "$APP"
/usr/bin/codesign --verify --deep --strict "$APP"
printf 'Built %s\n' "$APP"
if [[ "$IDENTITY" == "-" ]]; then
  printf '%s\n' 'Ad-hoc signed development build. Set CODEX_PET_CODESIGN_IDENTITY for distribution signing.'
fi
