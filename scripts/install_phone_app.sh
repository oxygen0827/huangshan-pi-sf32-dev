#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/mobile/ios/VibeBoardPhone"
DEVICE_ID="${1:-}"
TEAM_ID="${DEVELOPMENT_TEAM:-}"
BUNDLE_ID="${PRODUCT_BUNDLE_IDENTIFIER:-com.vibeboard.huangshan.phone}"
OVERRIDE_BUNDLE_ID="${PRODUCT_BUNDLE_IDENTIFIER:-}"

pick_iphone_id() {
    local json_file
    json_file="$(mktemp)"
    xcrun devicectl list devices --json-output "$json_file" >/dev/null
    python3 - "$json_file" <<'PY'
import json
import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)

devices = data.get("result", {}).get("devices", [])
for device in devices:
    hardware = device.get("hardwareProperties", {})
    if hardware.get("deviceType") == "iPhone":
        print(device.get("identifier", ""))
        break
PY
    rm -f "$json_file"
}

if [[ -z "$DEVICE_ID" ]]; then
    DEVICE_ID="$(pick_iphone_id)"
fi

if [[ -z "$DEVICE_ID" ]]; then
    echo "[phone-install] No iPhone device found by Xcode." >&2
    exit 1
fi

cd "$APP_DIR"
BUILD_OVERRIDES=(CODE_SIGNING_ALLOWED=YES SYMROOT=.build)
if [[ -n "$TEAM_ID" ]]; then
    BUILD_OVERRIDES+=(DEVELOPMENT_TEAM="$TEAM_ID")
fi
if [[ -n "$OVERRIDE_BUNDLE_ID" ]]; then
    BUILD_OVERRIDES+=(PRODUCT_BUNDLE_IDENTIFIER="$OVERRIDE_BUNDLE_ID")
fi

echo "[phone-install] Building signed app for $DEVICE_ID"
xcodebuild \
    -project VibeBoardPhone.xcodeproj \
    -target VibeBoardPhone \
    -configuration Debug \
    -sdk iphoneos \
    "${BUILD_OVERRIDES[@]}" \
    -allowProvisioningUpdates \
    -allowProvisioningDeviceRegistration \
    build

APP_PATH="$(find "$APP_DIR/.build" -path '*/Debug-iphoneos/VibeBoardPhone.app' -type d | head -n 1)"
if [[ -z "$APP_PATH" ]]; then
    echo "[phone-install] Built app bundle not found under $APP_DIR/.build" >&2
    exit 1
fi

echo "[phone-install] Installing $APP_PATH"
xcrun devicectl device install app --device "$DEVICE_ID" "$APP_PATH"

echo "[phone-install] Launching $BUNDLE_ID"
xcrun devicectl device process launch --device "$DEVICE_ID" --terminate-existing "$BUNDLE_ID"
