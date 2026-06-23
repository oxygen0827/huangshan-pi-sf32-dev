# Huangshan Runtime Bluetooth Transport

This note records the current Huangshan Runtime transport decision.

## Hardware Conclusion

The SF32LB52-MOD-1 material describes this module as a low-power Bluetooth MCU
module based on SF32LB525UC6. The documented built-in radio is dual-mode
Bluetooth 5.3 with BLE support.

The downloaded module datasheet does not document native Wi-Fi as a built-in
module feature. It exposes SD/SDIO, SPI, UART, USB, and other interfaces, so a
carrier board could add an external Wi-Fi device, but that would require a
carrier-board schematic or a Wi-Fi module part number.

For the current Huangshan board, treat App update transport as Bluetooth/BLE,
not ESP32-style Wi-Fi.

## Current Runtime Direction

The default Runtime transport is BLE GATT App install:

- The board advertises as `VibeBoard`.
- A phone or Mac scans for the board and initiates the connection.
- App packages are transferred over a custom BLE GATT command/status service.
- Installed apps still live under `/sdcard/apps/<app_id>`.
- The Runtime firmware is not reflashed for normal App updates.

This matches the product goal better than Bluetooth PAN: no hotspot password,
no phone tethering, and no board-initiated scan of the phone.

Serial install remains the development fallback and reliability baseline.

## Board API

BLE status:

```text
vb_runtime_ble_status
```

Expected healthy state:

```text
[vb_runtime][ble] api=vibeboard-huangshan-ble-install/v1 name=VibeBoard init=1 power=1 service=1 adv=1 conn=0 notify=0 mtu=23 ...
```

The BLE command characteristic accepts newline-terminated text commands:

```text
status
vb_runtime_status
vb_runtime_install_begin <appId>
vb_runtime_install_file <appId> <path> <offset> <hexChunk>
vb_runtime_install_end <appId>
vb_runtime_select <appId>
vb_runtime_reload
```

The status characteristic can be read or subscribed to for command results.

UUIDs as seen by CoreBluetooth/Bleak:

```text
service: 454d5452-0100-0000-5453-4e4954524256
command: 454d5452-0200-0000-5453-4e4954524256
status:  454d5452-0300-0000-5453-4e4954524256
```

The current Mac-side reference client is:

```bash
python3 -m venv .venv-ble
.venv-ble/bin/python -m pip install bleak
./scripts/runtime_install_ble.sh --scan-only
./scripts/runtime_install_ble.sh --status-only
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
```

It caches the first successful peripheral identifier/address at
`~/.vibeboard/huangshan_ble.json`, then tries that cached device before falling
back to a fresh scan for `VibeBoard`. The iPhone app should follow the same
model using CoreBluetooth's `retrievePeripherals(withIdentifiers:)` and then
scan only if retrieval/reconnect fails.

The iOS/CoreBluetooth reference package lives at:

```text
mobile/ios/VibeBoardBLE
```

It exposes `VibeBoardBLEClient.connect()`, `status()`, and `install(_:)`, plus
the shared `RuntimePackage` command builder used by phone-side App transfer.

A minimal iPhone app target that uses the same implementation lives at:

```text
mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj
```

It contains a SwiftUI screen with connect/status/install buttons and the
required Bluetooth usage descriptions. It auto-connects on launch/foreground,
tries the cached peripheral first, then scans for `VibeBoard` only if needed. It
can install the built-in `ios_demo` package or import a Runtime App folder from
the iOS Files picker and send it over BLE. It is intentionally Bluetooth-only:
it does not ask for hotspot, Wi-Fi, or PAN.

## Current Build Configuration

The Runtime now enables BLE without the classic Bluetooth PAN stack:

```text
CONFIG_RT_USING_BLUETOOTH=y
CONFIG_BLUETOOTH=y
CONFIG_BSP_BLE_SIBLES=y
CONFIG_BSP_BLE_NVDS_SYNC=y
CONFIG_BSP_BLE_CONNECTION_MANAGER=y
CONFIG_BT_CON_NUM_CUSTOMIZE=y
CONFIG_CFG_MAX_BT_ACL_NUM=2
```

The following are intentionally not enabled in the default Runtime:

```text
CONFIG_CFG_PAN
CONFIG_RT_USING_LWIP
CONFIG_BT_FINSH_PAN
CONFIG_BTS2_APP_MENU
CONFIG_CFG_AV
CONFIG_CFG_HID
```

PAN code remains in the source behind compile-time guards for future reference,
but the default firmware is BLE-only.

## Bring-Up Evidence

Verified on the Huangshan board connected as `/dev/cu.usbserial-13220`:

- `./scripts/build.sh` succeeds with the local SiFli SDK auto-discovered.
- `./scripts/flash.sh /dev/cu.usbserial-13220 --confirm-boot` succeeds.
- Boot log includes:
  - `[vb_runtime][ble] init name=VibeBoard`
  - `BLE ready!`
  - `[vb_runtime][ble] service ready`
  - `[vb_runtime][ble] adv started name=VibeBoard status=0`
- Boot log no longer includes:
  - `BTS2 Demo Main Menu`
  - `PAN enable`
  - `VibeBoard-PAN`
- `vb_runtime_ble_status` reports `init=1 power=1 service=1 adv=1`.
- Mac CoreBluetooth/Bleak scan finds `VibeBoard`.
- Mac CoreBluetooth/Bleak status connects by cached peripheral identifier and
  reads `ok status api=vibeboard-huangshan-ble-install/v1 active=clock_test`.
- BLE package install succeeds for `scripts/runtime_apps/status_test`:
  `installed status_test over BLE: 2 files, 30 commands, chunk=48 bytes`.
  The final BLE status reports `active=status_test`.
- `mobile/ios/VibeBoardBLE` builds and its package/command-generation tests pass
  with `swift test`, including directory import for `manifest.json`,
  `main.lua`, and whitelisted assets.
- `mobile/ios/VibeBoardPhone` builds with `xcodebuild` for the installed iOS
  simulator SDK using `CODE_SIGNING_ALLOWED=NO`.
- `./scripts/verify_phone_ble_ready.sh` passes: Swift package tests, iPhone app
  simulator build, and physical-board BLE status read all succeed.
- The iPhone 13 is visible to Xcode CoreDevice as
  `ED29D968-111A-51E0-8DD4-1F9ACE833506`, paired, and Developer Mode is
  enabled. Current local blocker for on-device install is Xcode account/profile
  setup: there is no installed iOS development provisioning profile and Xcode
  reports no account for team `XT73L75FTC`.

## iPhone Validation

iOS Settings does not reliably list arbitrary custom BLE GATT peripherals.
That is normal and does not mean the BLE advertisement is absent.

Use one of these for manual bring-up:

- LightBlue Explorer
- nRF Connect
- A future VibeCoding iOS app

Scan for `VibeBoard`, connect, then inspect the custom service that advertises
the VibeBoard Runtime install UUID. The phone app should remember the BLE
peripheral identifier after the first successful connection and reconnect to it
automatically later.

For the actual iPhone app, integrate `mobile/ios/VibeBoardBLE` and add the
required iOS Bluetooth usage description to the app target. That app can connect
on foreground entry, reuse the cached peripheral UUID first, and fall back to a
scan for `VibeBoard` only when reconnect fails.

The current demo app already integrates those pieces. To validate on iPhone:

1. Open `mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj` in Xcode.
2. Select the `VibeBoardPhone` target and choose your Apple development team if
   signing is not configured.
3. Choose the iPhone as the run destination and press Run.
4. Keep the board powered with the BLE Runtime flashed.
5. Wait for the App to auto-connect. The connection line should move through
   cached connect or scan states to `Connected`.
6. Tap `Read Runtime Status`; success should show
   `ok status api=vibeboard-huangshan-ble-install/v1 active=...`.
7. Tap `Install Demo App Over BLE`; success should switch the board to
   `ios_demo`.
8. To install a generated App package, save a folder containing
   `manifest.json`, `main.lua`, and optional `assets/`, `images/`, `fonts/`, or
   `lib/` resources into the iOS Files app, then tap
   `Import App Folder Over BLE` and select that folder.

Do not connect to a hotspot for this test. Only Bluetooth needs to be enabled.

Before running the iPhone app, the Mac-side readiness check is:

```bash
./scripts/verify_phone_ble_ready.sh
```

The physical-device signing/readiness check is:

```bash
./scripts/verify_phone_device_ready.sh
```

After signing is fixed, this command builds, installs, and launches the iPhone
app on the first detected iPhone:

```bash
./scripts/install_phone_app.sh
```

If it reports no account/profile, open Xcode > Settings > Accounts, sign in with
the Apple ID for team `XT73L75FTC`, then open
`mobile/ios/VibeBoardPhone/VibeBoardPhone.xcodeproj`, select the
`VibeBoardPhone` target, enable automatic signing, and let Xcode create the
development provisioning profile. Keep the iPhone unlocked and connected while
doing this.

## Product Boundary

App-package updates can change:

- UI layout and text.
- Game/application logic supported by the Runtime script subset.
- Images and other assets under the package whitelist.
- Which installed App is active.

Runtime firmware update is still required for:

- New BLE protocol features, bonding/security policy, or transfer reliability
  changes inside the installer.
- New LVGL bindings, sensors, storage primitives, or native modules.
- Full Lua VM integration.
- Any future Wi-Fi or TCP/IP stack support.

## Historical PAN Note

The SDK includes BT PAN examples and they were brought up earlier as a possible
network path. That path worked on the Mac side but did not match the desired
phone-first BLE pairing model. It also introduced classic Bluetooth profile
noise and hotspot/tethering requirements. The current product direction is
therefore BLE GATT App install, with PAN kept only as reference material.
