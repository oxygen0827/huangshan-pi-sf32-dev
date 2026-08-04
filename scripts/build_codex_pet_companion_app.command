#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
LOCAL="$ROOT/.local"
APP="$LOCAL/VibeBoard Companion.app"
DIST="$LOCAL/dist"
MODULE_CACHE="$LOCAL/swift-module-cache"
PYTHON="$ROOT/.venv/bin/python"
PYINSTALLER="$ROOT/.venv/bin/pyinstaller"
PYINSTALLER_CACHE="$LOCAL/pyinstaller-cache"
NODE_PROJECT="$ROOT/scripts/companion_node"
IDENTITY="${CODEX_PET_CODESIGN_IDENTITY:--}"
VERSION="${VIBEBOARD_APP_VERSION:-1.0.0}"
BUILD_NUMBER="${VIBEBOARD_BUILD_NUMBER:-1}"
PUBLIC_SITE_URL="${VIBEBOARD_PUBLIC_SITE_URL:-}"
UPDATE_MANIFEST_URL="${VIBEBOARD_UPDATE_MANIFEST_URL:-}"
RELEASE_DOWNLOAD_URL="${VIBEBOARD_RELEASE_DOWNLOAD_URL:-}"
FIRMWARE_MANIFEST_URL="${VIBEBOARD_FIRMWARE_MANIFEST_URL:-}"
FIRMWARE_PUBLIC_KEY_PATH="${VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH:-}"
SFTOOL_SOURCE="${VIBEBOARD_SFTOOL_PATH:-}"
NOTARY_PROFILE="${CODEX_PET_NOTARY_PROFILE:-}"
PRODUCT_ARCH="$(uname -m)"
DMG="$DIST/VibeBoard-Companion-$VERSION-macOS-$PRODUCT_ARCH.dmg"
STAGE="$(mktemp -d "$LOCAL/companion-build.XXXXXX")"

cleanup() {
  /bin/rm -rf "$STAGE"
}
trap cleanup EXIT

fail() {
  print -u2 "build_codex_pet_companion_app: $1"
  exit 1
}

[[ -x "$PYTHON" ]] || fail "missing $PYTHON; create the project .venv first"
[[ -x "$PYINSTALLER" ]] || fail "PyInstaller is missing; run .venv/bin/pip install pyinstaller"
command -v node >/dev/null || fail "Node.js is required to assemble the release bundle"
command -v npm >/dev/null || fail "npm is required to install the pinned Sharp dependency"
[[ -z "$PUBLIC_SITE_URL" || "$PUBLIC_SITE_URL" == https://* ]] || fail "VIBEBOARD_PUBLIC_SITE_URL must use https://"
[[ -z "$UPDATE_MANIFEST_URL" || "$UPDATE_MANIFEST_URL" == https://* ]] || fail "VIBEBOARD_UPDATE_MANIFEST_URL must use https://"
[[ -z "$RELEASE_DOWNLOAD_URL" || "$RELEASE_DOWNLOAD_URL" == https://* ]] || fail "VIBEBOARD_RELEASE_DOWNLOAD_URL must use https://"
[[ -z "$FIRMWARE_MANIFEST_URL" || "$FIRMWARE_MANIFEST_URL" == https://* ]] || fail "VIBEBOARD_FIRMWARE_MANIFEST_URL must use https://"

mkdir -p "$LOCAL" "$DIST" "$MODULE_CACHE" "$PYINSTALLER_CACHE"

SHARP_SOURCE="${VIBEBOARD_SHARP_PATH:-$NODE_PROJECT/node_modules/sharp}"
if [[ ! -d "$SHARP_SOURCE" ]]; then
  CHATGPT_SHARP="/Applications/ChatGPT.app/Contents/Resources/cua_node/lib/node_modules/sharp"
  if [[ "$IDENTITY" != "-" ]]; then
    fail "distribution builds require npm install --prefix scripts/companion_node --omit=dev"
  elif [[ -d "$CHATGPT_SHARP" ]]; then
    SHARP_SOURCE="$CHATGPT_SHARP"
    print -u2 'Using the local ChatGPT Sharp runtime for this development build.'
  else
    npm install --prefix "$NODE_PROJECT" --omit=dev
    SHARP_SOURCE="$NODE_PROJECT/node_modules/sharp"
  fi
fi
NODE_MODULES_SOURCE="${SHARP_SOURCE:h}"
for dependency in sharp @img detect-libc semver; do
  [[ -d "$NODE_MODULES_SOURCE/$dependency" ]] || fail "Sharp dependency $dependency is missing from $NODE_MODULES_SOURCE"
done

PYINSTALLER_CONFIG_DIR="$PYINSTALLER_CACHE" "$PYINSTALLER" \
  --noconfirm \
  --clean \
  --onedir \
  --name VibeBoardCompanionAgent \
  --paths "$ROOT/scripts" \
  --distpath "$STAGE/py-dist" \
  --workpath "$STAGE/py-build" \
  --specpath "$STAGE/spec" \
  --collect-submodules bleak \
  --hidden-import bleak.backends.corebluetooth.client \
  --hidden-import bleak.backends.corebluetooth.scanner \
  --hidden-import bleak.backends.corebluetooth.utils \
  --hidden-import CoreBluetooth \
  --hidden-import Foundation \
  --hidden-import objc \
  --hidden-import codex_pet_companion \
  --hidden-import codex_pet_monitor \
  --hidden-import codex_pet_usage \
  --hidden-import codex_pet_progress \
  --hidden-import codex_pet_mcp \
  --hidden-import firmware_release \
  --hidden-import flash \
  --hidden-import runtime_install_serial \
  "$ROOT/scripts/codex_pet_agent.py"

STAGED_APP="$STAGE/VibeBoard Companion.app"
CONTENTS="$STAGED_APP/Contents"
MACOS="$CONTENTS/MacOS"
RESOURCES="$CONTENTS/Resources"
mkdir -p "$MACOS" "$RESOURCES/AgentRoot/scripts/runtime_apps" "$RESOURCES/Tools/node_modules"
cp "$ROOT/scripts/VibeBoardCompanion-Info.plist" "$CONTENTS/Info.plist"

/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $BUILD_NUMBER" "$CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c "Set :VibeBoardPublicSiteURL $PUBLIC_SITE_URL" "$CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c "Set :VibeBoardUpdateManifestURL $UPDATE_MANIFEST_URL" "$CONTENTS/Info.plist"
/usr/libexec/PlistBuddy -c "Set :VibeBoardFirmwareManifestURL $FIRMWARE_MANIFEST_URL" "$CONTENTS/Info.plist"

CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
  /usr/bin/xcrun swiftc \
    -framework AppKit \
    -framework ServiceManagement \
    "$ROOT/scripts/codex_pet_companion_app.swift" \
    -o "$MACOS/VibeBoardCompanion"

CLANG_MODULE_CACHE_PATH="$MODULE_CACHE" SWIFT_MODULECACHE_PATH="$MODULE_CACHE" \
  /usr/bin/xcrun swiftc \
    "$ROOT/scripts/codex_pet_desktop_approval.swift" \
    -o "$RESOURCES/CodexPetDesktopApproval"

/usr/bin/ditto "$STAGE/py-dist/VibeBoardCompanionAgent" "$RESOURCES/Agent"
/usr/bin/ditto "$ROOT/scripts/runtime_apps/codex_pet" "$RESOURCES/AgentRoot/scripts/runtime_apps/codex_pet"
mkdir -p "$RESOURCES/AgentRoot/keys"
for resource in \
  codex_pet_hook.py \
  codex_pet_web.html \
  petdex_pets.json \
  petdex_state_contract.json \
  companion_state.py \
  hpet_crypto.js \
  build_hpet_petdex.js; do
  cp "$ROOT/scripts/$resource" "$RESOURCES/AgentRoot/scripts/$resource"
done

if [[ -n "$FIRMWARE_MANIFEST_URL" ]]; then
  [[ -n "$FIRMWARE_PUBLIC_KEY_PATH" && -f "$FIRMWARE_PUBLIC_KEY_PATH" ]] || fail "VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH is required when firmware updates are enabled"
  cp "$FIRMWARE_PUBLIC_KEY_PATH" "$RESOURCES/AgentRoot/keys/firmware-public.pem"
fi

if [[ -z "$SFTOOL_SOURCE" ]]; then
  SFTOOL_SOURCE="$(find "$HOME/.sifli/tools/sftool" -type f -name sftool -perm -111 2>/dev/null | sort | tail -n 1 || true)"
fi
if [[ -n "$SFTOOL_SOURCE" && -x "$SFTOOL_SOURCE" ]]; then
  mkdir -p "$RESOURCES/Tools"
  cp "$SFTOOL_SOURCE" "$RESOURCES/Tools/sftool"
fi

cp "$(command -v node)" "$RESOURCES/Tools/node"
for dependency in sharp @img detect-libc semver; do
  /usr/bin/ditto "$NODE_MODULES_SOURCE/$dependency" "$RESOURCES/Tools/node_modules/$dependency"
done

if [[ "$IDENTITY" == "-" ]]; then
  SIGN_ARGS=(--force --sign -)
else
  SIGN_ARGS=(--force --options runtime --timestamp --sign "$IDENTITY")
fi

while IFS= read -r candidate; do
  if /usr/bin/file "$candidate" | /usr/bin/grep -q 'Mach-O'; then
    /usr/bin/codesign "${SIGN_ARGS[@]}" "$candidate"
  fi
done < <(/usr/bin/find "$RESOURCES" -type f)
/usr/bin/codesign "${SIGN_ARGS[@]}" "$MACOS/VibeBoardCompanion"
/usr/bin/codesign "${SIGN_ARGS[@]}" "$STAGED_APP"
/usr/bin/codesign --verify --deep --strict "$STAGED_APP"

/bin/rm -rf "$APP"
/usr/bin/ditto "$STAGED_APP" "$APP"

DMG_ROOT="$STAGE/dmg"
mkdir -p "$DMG_ROOT"
/usr/bin/ditto "$APP" "$DMG_ROOT/VibeBoard Companion.app"
/bin/ln -s /Applications "$DMG_ROOT/Applications"
/bin/rm -f "$DMG"
/usr/bin/hdiutil create \
  -volname "VibeBoard Companion" \
  -srcfolder "$DMG_ROOT" \
  -format UDZO \
  -ov \
  "$DMG"

if [[ -n "$NOTARY_PROFILE" ]]; then
  [[ "$IDENTITY" != "-" ]] || fail "notarization requires CODEX_PET_CODESIGN_IDENTITY"
  /usr/bin/xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" --wait
  /usr/bin/xcrun stapler staple "$DMG"
  /usr/sbin/spctl --assess --type open --context context:primary-signature --verbose "$DMG"
fi

/usr/bin/shasum -a 256 "$DMG" > "$DMG.sha256"
if [[ -n "$RELEASE_DOWNLOAD_URL" ]]; then
  /usr/bin/printf '{\n  "version": "%s",\n  "build": "%s",\n  "downloadURL": "%s",\n  "sha256": "%s"\n}\n' \
    "$VERSION" "$BUILD_NUMBER" "$RELEASE_DOWNLOAD_URL" "$(/usr/bin/awk '{print $1}' "$DMG.sha256")" \
    > "$DIST/update-manifest.json"
fi

print "Built $APP"
print "Built $DMG"
if [[ "$IDENTITY" == "-" ]]; then
  print 'Ad-hoc signed development build. Set CODEX_PET_CODESIGN_IDENTITY and CODEX_PET_NOTARY_PROFILE for distribution.'
fi
