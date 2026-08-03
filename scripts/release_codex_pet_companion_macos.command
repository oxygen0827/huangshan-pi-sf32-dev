#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
VERSION="${VIBEBOARD_APP_VERSION:-}"
BUILD_NUMBER="${VIBEBOARD_BUILD_NUMBER:-}"
ARCH="${VIBEBOARD_RELEASE_ARCH:-$(uname -m)}"
PUBLIC_SITE_URL="${VIBEBOARD_PUBLIC_SITE_URL:-https://ldcx.tech/pet/}"
DOWNLOAD_ROOT="${VIBEBOARD_DOWNLOAD_ROOT:-https://ldcx.tech/static/codex-pet/companion}"
DOWNLOAD_URL="$DOWNLOAD_ROOT/VibeBoard-Companion-$VERSION-macOS-$ARCH.dmg"
UPDATE_URL="${VIBEBOARD_UPDATE_MANIFEST_URL:-$DOWNLOAD_ROOT/update-manifest.json}"
FIRMWARE_URL="${VIBEBOARD_FIRMWARE_MANIFEST_URL:-https://ldcx.tech/static/codex-pet/firmware/releases.json}"
IDENTITY="${CODEX_PET_CODESIGN_IDENTITY:-}"
NOTARY_PROFILE="${CODEX_PET_NOTARY_PROFILE:-}"
PUBLIC_KEY="$ROOT/codex-pet-companion/keys/firmware-public.pem"
PYTHON="$ROOT/.venv/bin/python"

fail() {
  print -u2 "release_codex_pet_companion_macos: $1"
  exit 1
}

[[ -x "$PYTHON" ]] || fail "missing project Python: $PYTHON"
[[ "$VERSION" == <->.<->.<-> ]] || fail "VIBEBOARD_APP_VERSION must be an explicit numeric semantic version"
[[ "$BUILD_NUMBER" == <-> ]] || fail "VIBEBOARD_BUILD_NUMBER must be an explicit integer"
[[ "$ARCH" == "arm64" || "$ARCH" == "x86_64" ]] || fail "unsupported release architecture: $ARCH"
[[ -n "$IDENTITY" && "$IDENTITY" == Developer\ ID\ Application:* ]] || fail "CODEX_PET_CODESIGN_IDENTITY must be a Developer ID Application identity"
[[ -n "$NOTARY_PROFILE" ]] || fail "CODEX_PET_NOTARY_PROFILE is required"
security find-identity -v -p codesigning | grep -F '"'"$IDENTITY"'"' >/dev/null || fail "signing identity is not available in the keychain"
[[ -d "$ROOT/scripts/companion_node/node_modules/sharp" ]] || fail "install the pinned project Sharp dependency before a distribution build"
[[ -f "$PUBLIC_KEY" ]] || fail "firmware public key is missing"

if [[ -z "${VIBEBOARD_SFTOOL_PATH:-}" ]]; then
  VIBEBOARD_SFTOOL_PATH="$(find "$HOME/.sifli/tools/sftool" -type f -name sftool -perm -111 2>/dev/null | sort | tail -n 1 || true)"
fi
[[ -x "${VIBEBOARD_SFTOOL_PATH:-}" ]] || fail "VIBEBOARD_SFTOOL_PATH must point to the bundled sftool executable"

env \
  VIBEBOARD_APP_VERSION="$VERSION" \
  VIBEBOARD_BUILD_NUMBER="$BUILD_NUMBER" \
  VIBEBOARD_PUBLIC_SITE_URL="$PUBLIC_SITE_URL" \
  VIBEBOARD_UPDATE_MANIFEST_URL="$UPDATE_URL" \
  VIBEBOARD_RELEASE_DOWNLOAD_URL="$DOWNLOAD_URL" \
  VIBEBOARD_FIRMWARE_MANIFEST_URL="$FIRMWARE_URL" \
  VIBEBOARD_FIRMWARE_PUBLIC_KEY_PATH="$PUBLIC_KEY" \
  VIBEBOARD_SFTOOL_PATH="$VIBEBOARD_SFTOOL_PATH" \
  CODEX_PET_CODESIGN_IDENTITY="$IDENTITY" \
  CODEX_PET_NOTARY_PROFILE="$NOTARY_PROFILE" \
  "$ROOT/scripts/build_codex_pet_companion_app.command"

APP="$ROOT/.local/VibeBoard Companion.app"
DMG="$ROOT/.local/dist/VibeBoard-Companion-$VERSION-macOS-$ARCH.dmg"
CHECKSUM="$DMG.sha256"
MANIFEST="$ROOT/.local/dist/update-manifest.json"
RELEASE_SITE="$ROOT/.local/dist/codex_pet_web.html"
"$PYTHON" "$ROOT/scripts/prepare_macos_companion_site.py" \
  --source "$ROOT/scripts/codex_pet_web.html" \
  --destination "$RELEASE_SITE" \
  --download-url "$DOWNLOAD_URL"
"$ROOT/scripts/verify_codex_pet_companion_app.sh" "$APP"
"$PYTHON" "$ROOT/scripts/check_macos_companion_release.py" \
  --dmg "$DMG" \
  --checksum "$CHECKSUM" \
  --manifest "$MANIFEST" \
  --site-html "$RELEASE_SITE" \
  --expected-url "$DOWNLOAD_URL" \
  --expected-version "$VERSION" \
  --expected-build "$BUILD_NUMBER"
/usr/bin/xcrun stapler validate "$DMG"
/usr/sbin/spctl --assess --type open --context context:primary-signature --verbose "$DMG"
print "Production Mac Companion release ready: $DMG"
