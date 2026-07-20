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

The board's Micro SD slot is SPI1, not SDIO: PA24/PA25 carry data, PA28 is clock,
PA29 is chip select, and PA27 is card detect. The N16R8 module's SDIO/MPI2 pins
are occupied by its NOR flash. Runtime therefore enables `RT_USING_SPI_MSD`,
mounts `sd0` at `/sdcard`, supports exFAT, and caps the card at 6 MHz after
initialization for reliable runtime reads while the display is refreshing.

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
The local Web UI follows the same rule: the browser talks to a localhost bridge,
and that bridge uses a RuntimeTransport adapter for either BLE or serial. The
board itself does not expose an ESP32-style HTTP server or stable LAN IP.

## Board API

BLE status:

```text
vb_runtime_ble_status
```

Expected healthy state:

```text
[vb_runtime][ble] api=vibeboard-huangshan-ble-install/v1 name=VibeBoard init=1 power=1 service=1 adv=1 conn=0 notify=0 mtu=23 ...
```

The BLE command characteristic accepts newline-terminated text commands.
The same command names are mirrored by serial/MSH where practical, so desktop
bridges and phone apps should treat serial and BLE as transport adapters over
the same Runtime protocol:

```text
capabilities
json_read <kind> <offset> <maxBytes>
status
app
apps
launch <appId>
stop
delete <appId>
power
display [brightness0To100]
display_brightness <brightness0To100>
gpio
touch
rgb [off|red|green|blue|yellow|cyan|magenta|white|RRGGBB]
flow_status
flow_send <channel> <seq> <hexUtf8Payload>
flow_clear
voice
voice_status
voice_start <durationMs>
voice_read <offset> <maxBytes>
voice_clear
sensors
vb_runtime_install_begin <appId>
vb_runtime_install_file <appId> <path> <offset> <hexChunk>
vb_runtime_install_abort <appId>
vb_runtime_install_end <appId>
vb_runtime_select <appId>
vb_runtime_reload
```

`json_read` currently supports `capabilities`, `sensors`, `power`, `display`,
`gpio`, `touch`, `rgb`, `voice`, `app`, and `apps`. Short aliases such as
`app_status`, `app_list`, and the `vb_runtime_*` command names are accepted for
compatibility, but new bridge clients should use the compact names above.

The status characteristic can be read or subscribed to for command results.
The `capabilities` command returns compact JSON under
`vibeboard-huangshan-capabilities/v1`; phone and AI clients should use it as
the first handshake before assuming that manifest/Lua, sensors, voice, info
flow, power, RGB, or future hardware APIs are available. `power` returns the
`vibeboard-huangshan-power/v1` JSON snapshot for the read-only battery voltage
and AW32001 charger status API; charger register writes/control remain
intentionally unavailable. `rgb` returns or sets the `vibeboard-huangshan-rgb/v1`
session state for the single onboard `rgbled` device. The color resets to `off`
after a board reboot; apps that need a persistent visual state should call
`vibe_rgb(...)` from their active `main.lua`.


Install commands acknowledge with per-command status lines such as
`ok install_begin app=<appId> rc=0`,
`ok install_file app=<appId> path=<path> offset=<offset> rc=0`, and
`ok install_end app=<appId> active=<appId> rc=0`.
Clients should wait for the matching ack instead of assuming the most recent
status value belongs to the latest command.

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
./scripts/runtime_install_ble.sh --capabilities-only
./scripts/runtime_install_ble.sh --power-only
./scripts/runtime_install_ble.sh --rgb-color 3366ff
./scripts/runtime_install_ble.sh --rgb-only
./scripts/runtime_install_ble.sh \
  --package-dir scripts/runtime_apps/clock_test \
  --app-id clock_test
```

It caches the first successful peripheral identifier/address at
`~/.vibeboard/huangshan_ble.json`, then tries that cached device before falling
back to a fresh scan for `VibeBoard`. App listing uses `apps_page <offset> <limit>`
and combines pages on the host; the BLE adapter currently uses a 2-app page to
avoid long status JSON truncation, while the serial adapter keeps a 5-app page.
If the board reports advertising but the Mac cannot find `VibeBoard` after a
failed connection, use the serial diagnostic command `vb_runtime_ble_restart` to
force a BLE advertising stop/start. The iPhone app should follow the same cached
reconnect model using CoreBluetooth's `retrievePeripherals(withIdentifiers:)` and
then scan only if retrieval/reconnect fails.

The localhost App Store bridge now uses `scripts/runtime_transport.py` for the
same protocol surface. It exposes local HTTP endpoints for transport status,
Runtime capabilities handshake, Runtime App listing, launch, stop, delete, and
install; every board-facing operation is serialized through one transport lock so
browser polling cannot race with install, capabilities, or App Manager commands. The bridge intentionally avoids periodic status/app-list polling after initial page load; launch/stop/install actions update the browser from cache first, because extra serial reads during LVGL screen transitions can make the watch UI flicker or bounce back to Home. The desktop serial/BLE install scripts also
route their standard query, App Manager, `install_abort`, flow, voice
capture/reply, flow persistence, normal install paths, and staged install fault
injection through that adapter. Script-local direct serial/BLE code is intentionally
limited to bottom-layer exceptions: BLE scan/connect-hold, RTS reset inside
cold-recovery tests, flashing, and monitor tools. Those paths discover, reset,
or observe the board; they are not normal App management APIs.
Run `./scripts/app_store_mac.sh --transport ble`
for normal phone-like BLE transport. Use `--ble-name VibeBoard` to select a
non-default BLE local name and `--ble-no-cache` to force a fresh scan when the
cached peripheral is stale. Use `./scripts/app_store_mac.sh --transport serial
--serial-port /dev/cu.usbserial-13220` when using the CH340/UART fallback.

The iOS/CoreBluetooth reference package lives at:

```text
mobile/ios/VibeBoardBLE
```

It exposes `VibeBoardBLEClient.connect()`, `status()`, `capabilities()`,
`power()`, `rgb(color:)`, `sendInfoFlow(...)`, `voiceStatus()`,
`voiceStart()`, `voiceRead(offset:maxBytes:expectedSequence:)`, `voiceClear()`,
`captureVoice(...)`, `sendVoiceReply(...)`, `install(_:)`, and `abortInstall(_:)`, plus the shared
`RuntimePackage` command builder used by phone-side App transfer. Failed phone-side installs now attempt the same staging cleanup as the desktop serial/BLE adapter before surfacing the original error.

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


## Board Home And Web Manager

The board-side UI is intentionally simple: the `Main` home screen scans
`/sdcard/apps`, creates safe-area cards for installed compatible Runtime apps,
and launches an app by writing `/sdcard/apps/.active` before entering
`VibeBoard_Runtime`. The old engineering-style board App Manager page is no
longer the daily entry point.

The localhost Web App Store still contains a section named board App Manager,
but that is a host-side management surface. It can refresh the app list, launch,
stop, and delete installed apps through serial/BLE Runtime commands. It should
not be interpreted as a separate page on the watch display.

When using the serial transport, avoid background polling while an app is
launching. The bridge keeps a cached app list and only refreshes on initial load
or manual refresh. This prevents the browser from sending status/app-list reads
while LVGL is switching screens, which previously caused visible flicker and
sometimes returned the board to Home shortly after launching an app.

## Current Build Configuration

The Runtime now enables BLE without the classic Bluetooth PAN stack:

```text
CONFIG_RT_USING_BLUETOOTH=y
CONFIG_BLUETOOTH=y
CONFIG_BSP_BLE_SIBLES=y
CONFIG_BSP_BLE_NVDS_SYNC=y
CONFIG_BSP_BLE_CONNECTION_MANAGER=y
CONFIG_BLE_GAP_CENTRAL=y
CONFIG_BLE_GATT_CLIENT=y
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
- `vb_runtime_ble_status` reports `init=1 power=1 service=1 adv=1`, and
  `vb_runtime_ble_restart` can force a diagnostic advertising stop/start.
- Mac CoreBluetooth/Bleak scan finds `VibeBoard`.
- BLE App Manager listing succeeds through 2-app `apps_page` reads and combines
  the complete installed App list on the host. The same list backs the local
  Web App Manager and the board home-screen cards.
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
7. Tap `Read Capabilities`, `Read Power`, or `Read RGB` to confirm the Runtime
   hardware APIs are reachable from iPhone.
8. Tap `Capture` in the Voice section to pull a short microphone clip over BLE,
   then tap `Send Voice Reply` to write text back to the board flow label.
9. Tap `Install Demo App Over BLE`; success should switch the board to
   `ios_demo`.
10. To install a generated App package, save a folder containing
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

Huangshan Runtime packages target the `huangshan` profile. Package manifests may
declare `runtimeProfile`, `targetProfile`, or `target`, but those values must
resolve to Huangshan/SF32. Top-level `capabilities`, `requires`, and
`permissions` are also checked by the host packager. ESP32-era or board-native
network capabilities such as `wifi`, `http`, `network`, `ntp`, `board_ip`,
`native`, `camera`, `gamepad`, and `i2s` are intentionally rejected for normal
Huangshan App packages. Cloud, weather, and AI data should be fetched by the
phone or desktop bridge and injected through Runtime APIs such as `flow_send` or
manifest data like `weather.current`; they are not a promise that the board owns
a Wi-Fi/TCP stack. Host-side Python tools share `scripts/runtime_transport.py` for
normal App management, install, flow, and voice capture/reply paths, with serial
and BLE adapters underneath. The product firmware does not export board-native
HTTP App OTA; that path is hidden behind explicit experimental compile flags.

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
therefore BLE GATT App install. PAN and HTTP App OTA are not part of the default
product firmware; they only compile/export when explicit experimental macros
(such as `VB_RUNTIME_ENABLE_BT_PAN` and `VB_RUNTIME_ENABLE_HTTP_APP_OTA`) are
provided.
