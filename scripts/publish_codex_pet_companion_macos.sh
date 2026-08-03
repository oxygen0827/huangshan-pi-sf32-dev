#!/bin/zsh
set -euo pipefail

ROOT="${0:A:h:h}"
VERSION="${VIBEBOARD_APP_VERSION:-}"
BUILD_NUMBER="${VIBEBOARD_BUILD_NUMBER:-}"
ARCH="${VIBEBOARD_RELEASE_ARCH:-$(uname -m)}"
GITHUB_REPOSITORY="${VIBEBOARD_GITHUB_REPOSITORY:-oxygen0827/huangshan-pi-sf32-dev}"
RELEASE_TAG="${VIBEBOARD_RELEASE_TAG:-v$VERSION}"
PUBLISH_GITHUB="${VIBEBOARD_PUBLISH_GITHUB_RELEASE:-0}"
SERVER="${VIBEBOARD_RELEASE_SERVER:-root@47.102.197.71}"
REMOTE_DIR="${VIBEBOARD_RELEASE_REMOTE_DIR:-/home/lincaigui/nginx/static/codex-pet/companion}"
REMOTE_SITE_DIR="${VIBEBOARD_SITE_REMOTE_DIR:-/home/lincaigui/nginx/static/pet}"
DMG="$ROOT/.local/dist/VibeBoard-Companion-$VERSION-macOS-$ARCH.dmg"
CHECKSUM="$DMG.sha256"
MANIFEST="$ROOT/.local/dist/update-manifest.json"
SITE="$ROOT/.local/dist/codex_pet_web.html"
DOWNLOAD_URL="${VIBEBOARD_RELEASE_DOWNLOAD_URL:-https://ldcx.tech/static/codex-pet/companion/VibeBoard-Companion-$VERSION-macOS-$ARCH.dmg}"
PYTHON="$ROOT/.venv/bin/python"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REMOTE_DMG="VibeBoard-Companion-$VERSION-macOS-$ARCH.dmg"

fail() {
  print -u2 "publish_codex_pet_companion_macos: $1"
  exit 1
}

[[ -x "$PYTHON" ]] || fail "missing project Python: $PYTHON"
[[ "$VERSION" == <->.<->.<-> ]] || fail "VIBEBOARD_APP_VERSION must be the exact release version"
[[ "$BUILD_NUMBER" == <-> ]] || fail "VIBEBOARD_BUILD_NUMBER must be the exact release build"
[[ "$ARCH" == "arm64" || "$ARCH" == "x86_64" ]] || fail "unsupported release architecture: $ARCH"
[[ "$PUBLISH_GITHUB" == 0 || "$PUBLISH_GITHUB" == 1 ]] || fail "VIBEBOARD_PUBLISH_GITHUB_RELEASE must be 0 or 1"
if [[ "$PUBLISH_GITHUB" == 1 ]]; then
  [[ "$RELEASE_TAG" == v[0-9]*.[0-9]*.[0-9]* ]] || fail "VIBEBOARD_RELEASE_TAG must be a v-prefixed semantic version tag"
fi
"$PYTHON" "$ROOT/scripts/check_macos_companion_release.py" \
  --dmg "$DMG" --checksum "$CHECKSUM" --manifest "$MANIFEST" \
  --site-html "$SITE" --expected-url "$DOWNLOAD_URL" \
  --expected-version "$VERSION" --expected-build "$BUILD_NUMBER" >/dev/null
/usr/bin/xcrun stapler validate "$DMG"
/usr/sbin/spctl --assess --type open --context context:primary-signature --verbose "$DMG"

if [[ "${1:-}" == "--dry-run" ]]; then
  print "validated release: $DMG"
  print "server update source: $SERVER:$REMOTE_DIR"
  if [[ "$PUBLISH_GITHUB" == 1 ]]; then
    print "GitHub mirror target: $GITHUB_REPOSITORY:$RELEASE_TAG"
  fi
  print "site target: $SERVER:$REMOTE_SITE_DIR/index.html"
  exit 0
fi

if [[ "$PUBLISH_GITHUB" == 1 ]]; then
  command -v gh >/dev/null || fail "GitHub CLI (gh) is required when GitHub mirroring is enabled"
  gh auth status >/dev/null 2>&1 || fail "gh is not authenticated for GitHub mirroring"
  if gh release view "$RELEASE_TAG" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
    gh release upload "$RELEASE_TAG" "$DMG" "$CHECKSUM" "$MANIFEST" --repo "$GITHUB_REPOSITORY" --clobber
  else
    gh release create "$RELEASE_TAG" "$DMG" "$CHECKSUM" "$MANIFEST" \
      --repo "$GITHUB_REPOSITORY" \
      --title "VibeBoard Companion $VERSION" \
      --notes "Mac Companion $VERSION (build $BUILD_NUMBER)"
  fi
fi

ssh "$SERVER" "mkdir -p '$REMOTE_DIR'"
ssh "$SERVER" "mkdir -p '$REMOTE_DIR/.release-$STAMP'"
scp "$DMG" "$CHECKSUM" "$MANIFEST" "$SERVER:$REMOTE_DIR/.release-$STAMP/"
ssh "$SERVER" "test \"\$(sha256sum '$REMOTE_DIR/.release-$STAMP/$REMOTE_DMG' | awk '{print \$1}')\" = \"\$(awk '{print \$1}' '$REMOTE_DIR/.release-$STAMP/$REMOTE_DMG.sha256')\""
ssh "$SERVER" "if test -f '$REMOTE_DIR/$REMOTE_DMG'; then cp -p '$REMOTE_DIR/$REMOTE_DMG' '$REMOTE_DIR/$REMOTE_DMG.backup-$STAMP'; fi"
ssh "$SERVER" "mv '$REMOTE_DIR/.release-$STAMP/$REMOTE_DMG' '$REMOTE_DIR/$REMOTE_DMG'"
ssh "$SERVER" "mv '$REMOTE_DIR/.release-$STAMP/$REMOTE_DMG.sha256' '$REMOTE_DIR/$REMOTE_DMG.sha256'"
ssh "$SERVER" "if test -f '$REMOTE_DIR/update-manifest.json'; then cp -p '$REMOTE_DIR/update-manifest.json' '$REMOTE_DIR/update-manifest.json.backup-$STAMP'; fi"
ssh "$SERVER" "mv '$REMOTE_DIR/.release-$STAMP/update-manifest.json' '$REMOTE_DIR/update-manifest.json'"
ssh "$SERVER" "rmdir '$REMOTE_DIR/.release-$STAMP'"

ssh "$SERVER" "mkdir -p '$REMOTE_SITE_DIR'"
ssh "$SERVER" "if test -f '$REMOTE_SITE_DIR/index.html'; then cp -p '$REMOTE_SITE_DIR/index.html' '$REMOTE_SITE_DIR/index.html.backup-$STAMP'; fi"
scp "$SITE" "$SERVER:$REMOTE_SITE_DIR/index.html.stage-$STAMP"
ssh "$SERVER" "mv '$REMOTE_SITE_DIR/index.html.stage-$STAMP' '$REMOTE_SITE_DIR/index.html'"
print "Published server Companion release $REMOTE_DMG and the matching Web page"
