#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BLE_PKG_DIR="$ROOT_DIR/mobile/ios/VibeBoardBLE"
PHONE_APP_DIR="$ROOT_DIR/mobile/ios/VibeBoardPhone"

cd "$BLE_PKG_DIR"
CLANG_MODULE_CACHE_PATH="$PWD/.build/module-cache" \
SWIFTPM_HOME="$PWD/.build/swiftpm-cache" \
swift test --scratch-path .build/scratch

IOS_SIM_SDK="$(xcodebuild -showsdks | awk '/iphonesimulator/ {print $NF}' | tail -n 1)"
if [[ -z "$IOS_SIM_SDK" ]]; then
    echo "No iOS simulator SDK found. Install an iOS simulator SDK in Xcode." >&2
    exit 1
fi

cd "$PHONE_APP_DIR"
xcodebuild \
    -project VibeBoardPhone.xcodeproj \
    -target VibeBoardPhone \
    -configuration Debug \
    -sdk "$IOS_SIM_SDK" \
    CODE_SIGNING_ALLOWED=NO \
    SYMROOT=.build \
    build

cd "$ROOT_DIR"
"$ROOT_DIR/scripts/runtime_install_ble.sh" \
    --status-only \
    --scan-timeout 4 \
    --connect-timeout 8 \
    --no-echo
