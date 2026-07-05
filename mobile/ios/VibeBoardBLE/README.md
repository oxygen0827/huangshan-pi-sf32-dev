# VibeBoardBLE iOS Reference Client

This Swift package is the phone-side reference implementation for Huangshan
Runtime BLE App install, display/power/sensor APIs, info flow, and voice bridge.

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

let touch = try await client.touch()
print(touch.summary)

let gpio = try await client.gpio()
print(gpio.summary)

let display = try await client.display(brightness: 70)
print(display.summary)

let voice = try await client.voice()
print(voice.summary)

let bridgeStatus = try await client.voiceStatus()
print(bridgeStatus.summary)

let capture = try await client.captureVoice(durationMs: 1500)
print("pcm bytes: \(capture.pcm.count)")

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

The same client also exposes `capabilities()`, `sensors()`, `power()`,
`display(brightness:)`, `touch()`, `gpio()`, `rgb(color:)`, `appStatus()`,
`apps()`, `launchApp(...)`, `stopApp()`, `deleteApp(...)`,
`sendInfoFlow(...)`, `voice()`, `voiceStatus()`, `voiceStart()`,
`voiceRead(offset:maxBytes:expectedSequence:)`, `voiceClear()`,
`captureVoice(...)`, and `sendVoiceReply(...)`, so the iPhone side can reuse
the same BLE text protocol that `scripts/runtime_transport.py` also validates
for the Mac BLE/serial bridge. Flow and voice ACK matchers require the expected
`ok`/`err` status prefix plus sequence/byte fields, matching the Python adapter's
anti-stale-status checks. `VibeBoardBLEClient` now explicitly conforms to
`VibeBoardRuntimeTransport`; the Demo app depends on that transport interface,
with CoreBluetooth as the concrete adapter.

For non-paged runtime JSON reads, the client first tries the normal status reply
path and then automatically falls back to `json_read <kind> <offset> <maxBytes>`
chunk pulls when BLE notifications do not contain the full JSON payload. App
listing is deliberately different: `apps()` reads contiguous `apps_page` results
with a 2-app page and combines them locally, so page offset/limit semantics are
not lost through the generic `json_read` fallback. This keeps the iOS client
aligned with the current board Runtime and Mac BLE tooling.

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
- Read Runtime Status / Capabilities / Sensors / Power / Display / Touch / GPIO / RGB
- Send or clear retained info flow text
- Read, launch, stop, and delete Runtime Apps through App Manager commands
- Read voice status, capture a short BLE microphone clip, clear it, and send a text reply back to the board
- Install Demo App Over BLE
- Import App Folder Over BLE

This app only uses Bluetooth. It does not connect to the phone hotspot and does
not need a Wi-Fi password. Imported Runtime App folders must include
`main.lua` plus `manifest.json` or `app.info`; optional files must stay under
the same whitelist used by the board Runtime, such as `assets/`, `images/`,
`fonts/`, or `lib/`. If a manifest declares `runtimeProfile`, `targetProfile`,
`target`, `capabilities`, `requires`, or `permissions`, the iOS importer uses
the same Huangshan profile boundary as the Python packager and rejects ESP32 or
board-native network capabilities such as `wifi`, `http`, `network`, `camera`,
`gamepad`, and `i2s`.

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
./scripts/runtime_install_ble.sh --capabilities-only
./scripts/runtime_install_ble.sh --sensors-only
./scripts/runtime_install_ble.sh --power-only
./scripts/runtime_install_ble.sh --display-only
./scripts/runtime_install_ble.sh --display-brightness 70
./scripts/runtime_install_ble.sh --gpio-only
./scripts/runtime_install_ble.sh --touch-only
./scripts/runtime_install_ble.sh --voice-only
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/status_test \
  --app-id status_test
./scripts/runtime_reliability_ble.sh \
  --runs 1 \
  --verify-capabilities \
  --verify-display \
  --verify-gpio \
  --verify-voice-status \
  --apps gpio_keys_stage voice_stage
```

The Mac client has verified the same BLE central flow against the real board:

- scan finds `VibeBoard`
- cached reconnect works
- command/status characteristic read/write works
- long `capabilities` JSON now completes over BLE via `json_read` fallback
- `display` runtime JSON/brightness set completes over BLE and reports LCD size/state
- `gpio` runtime JSON also completes over BLE and matches the board whitelist API
- `voice` runtime JSON completes over BLE without starting microphone capture
- Runtime packages can declare `voice.start` / `voice.clear` actions for controlled board-side capture requests
- `status_test` installs over BLE and becomes active
- failed install sessions use `abortInstall(_:)` to clear board staging before reporting the original error
- directory import builds the same install command stream from a local
  Runtime App folder
- `swift test` covers capability, display, touch, GPIO, App Manager JSON/action, voice JSON/status/action, Huangshan profile package validation, and package-decoding paths on the iOS side

The demo iPhone app target now exists and includes Bluetooth permission strings.
Final hardware validation requires running it on the iPhone and pressing
`Connect / Auto Reconnect` next to the powered Huangshan board.
