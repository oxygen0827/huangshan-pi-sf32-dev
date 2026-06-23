#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_DIR="$ROOT_DIR/mobile/ios/VibeBoardPhone"
DEVICE_ID="${1:-}"
TEAM_ID="${DEVELOPMENT_TEAM:-}"
BUNDLE_ID="${PRODUCT_BUNDLE_IDENTIFIER:-}"

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

echo "[phone-device] Xcode devices"
xcrun devicectl list devices

if [[ -z "$DEVICE_ID" ]]; then
    DEVICE_ID="$(pick_iphone_id)"
fi

if [[ -z "$DEVICE_ID" ]]; then
    echo "[phone-device] No iPhone device found by Xcode." >&2
    echo "[phone-device] Connect the iPhone, unlock it, trust this Mac, then rerun." >&2
    exit 1
fi

echo "[phone-device] Inspecting device $DEVICE_ID"
xcrun devicectl device info details --device "$DEVICE_ID"

echo "[phone-device] Code signing identities"
security find-identity -v -p codesigning

PROFILE_DIR="$HOME/Library/MobileDevice/Provisioning Profiles"
PROFILE_COUNT=0
if [[ -d "$PROFILE_DIR" ]]; then
    PROFILE_COUNT="$(find "$PROFILE_DIR" -name '*.mobileprovision' -maxdepth 1 -type f | wc -l | tr -d ' ')"
fi
echo "[phone-device] Installed provisioning profiles: $PROFILE_COUNT"

BUILD_OVERRIDES=(CODE_SIGNING_ALLOWED=YES SYMROOT=.build)
if [[ -n "$TEAM_ID" ]]; then
    BUILD_OVERRIDES+=(DEVELOPMENT_TEAM="$TEAM_ID")
fi
if [[ -n "$BUNDLE_ID" ]]; then
    BUILD_OVERRIDES+=(PRODUCT_BUNDLE_IDENTIFIER="$BUNDLE_ID")
fi

TEAM_LABEL="${TEAM_ID:-Xcode project setting}"
BUNDLE_LABEL="${BUNDLE_ID:-Xcode project setting}"

echo "[phone-device] Building signed iphoneos app for team $TEAM_LABEL bundle $BUNDLE_LABEL"
cd "$APP_DIR"
set +e
xcodebuild \
    -project VibeBoardPhone.xcodeproj \
    -target VibeBoardPhone \
    -configuration Debug \
    -sdk iphoneos \
    "${BUILD_OVERRIDES[@]}" \
    -allowProvisioningUpdates \
    -allowProvisioningDeviceRegistration \
    build
RESULT=$?
set -e

if [[ "$RESULT" -ne 0 ]]; then
    cat >&2 <<'EOF'
[phone-device] Signed iphoneos build failed.

Most common fixes:
1. Open Xcode > Settings > Accounts and sign in with the Apple ID for the selected team.
2. Open mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj.
3. Select target VibeBoardPhone > Signing & Capabilities.
4. Enable Automatically manage signing and select your team.
5. Keep the iPhone unlocked, trust this Mac, and enable Developer Mode on the iPhone.
6. If Xcode says the device is unavailable, reconnect USB or restart the iPhone/Mac pairing tunnel.

After Xcode creates the development provisioning profile, rerun this script.
EOF
    exit "$RESULT"
fi

echo "[phone-device] Signed iphoneos build succeeded."
