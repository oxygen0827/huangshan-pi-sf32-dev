# VibeBoardBLE iOS Reference Client

This Swift package is the phone-side reference implementation for Huangshan
Runtime BLE App install.

It does not use Wi-Fi, hotspot, Bluetooth PAN, or board-initiated phone scans.
The phone acts as the BLE central:

1. Retrieve the last connected peripheral from `UserDefaults`.
2. Reconnect to it with CoreBluetooth when possible.
3. If retrieval/reconnect fails, scan for the board advertising as `VibeBoard`.
4. Discover the Runtime install GATT service.
5. Send newline-terminated Runtime commands to the command characteristic.
6. Read or subscribe to the status characteristic for command results.

## UUIDs

```text
service: 454d5452-0100-0000-5453-4e4954524256
command: 454d5452-0200-0000-5453-4e4954524256
status:  454d5452-0300-0000-5453-4e4954524256
```

## Basic Usage

```swift
import VibeBoardBLE

let client = VibeBoardBLEClient()
try await client.connect()

let status = try await client.status()
print(status)

let sensors = try await client.sensors()
print(sensors.summary)

let package = try RuntimePackage(
    appId: "clock_test",
    files: [
        "manifest.json": Data(...),
        "main.lua": Data(...)
    ]
)
try await client.install(package)
```

The client stores the first successful peripheral UUID in:

```text
UserDefaults["VibeBoardBLE.lastPeripheralIdentifier"]
```

An app can call `connect()` on launch or when entering the foreground. The
client tries the cached peripheral before scanning, which is the intended
automatic reconnect behavior after the first successful pairing/connection.

## Demo App

A minimal iPhone app target is available at:

```text
../VibeBoardPhone/VibeBoardPhone.xcodeproj
```

Open it in Xcode, choose your iPhone as the run destination, set your Apple
development team on the `VibeBoardPhone` target if Xcode asks, then run. The
app auto-connects on launch and whenever it returns to the foreground. The first
screen also has manual actions:

- Connect / Auto Reconnect
- Read Runtime Status
- Read Built-in Sensors
- Install Demo App Over BLE
- Import App Folder Over BLE

This app only uses Bluetooth. It does not connect to the phone hotspot and does
not need a Wi-Fi password. Imported Runtime App folders must include
`main.lua` plus `manifest.json` or `app.info`; optional files must stay under
the same whitelist used by the board Runtime, such as `assets/`, `images/`,
`fonts/`, or `lib/`.

## Validation

Run package tests:

```bash
swift test
```

Run the full Mac-side readiness check:

```bash
../../scripts/verify_phone_ble_ready.sh
```

Or build the demo app for the installed simulator SDK:

```bash
cd ../VibeBoardPhone
xcodebuild -project VibeBoardPhone.xcodeproj \
  -target VibeBoardPhone \
  -configuration Debug \
  -sdk iphonesimulator26.5 \
  CODE_SIGNING_ALLOWED=NO \
  SYMROOT=.build \
  build
```

Current verified desktop equivalent:

```bash
./scripts/runtime_install_ble.sh --scan-only
./scripts/runtime_install_ble.sh --status-only
./scripts/runtime_install_ble.sh --sensors-only
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/status_test \
  --app-id status_test
```

The Mac client has verified the same BLE central flow against the real board:

- scan finds `VibeBoard`
- cached reconnect works
- command/status characteristic read/write works
- `status_test` installs over BLE and becomes active
- directory import builds the same install command stream from a local
  Runtime App folder

The demo iPhone app target now exists and includes Bluetooth permission strings.
Final hardware validation requires running it on the iPhone and pressing
`Connect / Auto Reconnect` next to the powered Huangshan board.
