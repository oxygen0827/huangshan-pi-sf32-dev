# Huangshan Peer Link v1

Peer Link lets two Huangshan Pi boards running the same Runtime maintain one
bidirectional BLE connection. The lower stable Bluetooth address initiates the
connection; the other board remains the peripheral. Applications do not need to
know which role was selected.

`CONFIG_NVDS_AUTO_UPDATE_MAC_ADDRESS=y` is required. Huangshan development
boards otherwise retain the SDK placeholder `12:34:56:78:AB:CD`; two boards
with that same address cannot discover or elect each other. The enabled SDK
path copies each board's factory MAC into NVDS and preserves it across boots.

## GATT service

```text
service 454d5452-0100-0000-5245-455054524256
RX      454d5452-0200-0000-5245-455054524256  write/write-without-response
TX      454d5452-0300-0000-5245-455054524256  read/notify
```

The 12-byte little-endian frame header is:

```text
version:u8 type:u8 messageId:u32 total:u16 offset:u16 crc16:u16
```

Types are `PAIR=1`, `TEXT=2`, `ACK=3`, and `PING=4`. Payloads are limited to
192 UTF-8 bytes. Frames are split against the negotiated ATT MTU and still work
with the default MTU 23. A complete text is persisted before ACK. Pending
outgoing messages are retried after reconnect, while received message IDs are
deduplicated.

Pair identity and the 20-entry message ring are stored under:

```text
/sdcard/apps/.peer/pair.bin
/sdcard/apps/.peer/messages.bin
```

`peer_pair forget` removes both files and resets pairing, history, unread, and
pending-delivery state.

Pair confirmation prevents accidental association with another nearby board,
but v1 does not enable BLE bonding or encryption. Do not send sensitive data.

## Runtime commands

```text
peer_status
peer_pair start
peer_pair confirm
peer_pair cancel
peer_pair forget
peer_send <hexUtf8>
peer_send_fill <length> <hexByte>
peer_messages_page <offset> <limit>
```

`peer_send_fill` is an automation helper for 1-192 byte repeated printable-ASCII
payloads. It avoids the upstream FinSH 8-bit line-position limit when exercising
the 192-byte protocol boundary; applications should use `vibe_peer_send()`.

The same commands are accepted by the existing Runtime BLE command
characteristic. `json_read peer <offset> <maxBytes>` returns long status JSON in
the same way as other Runtime capabilities.

## Pager app and verification

Install `scripts/runtime_apps/pager` on both boards and open it on both screens.
The app exposes pair confirmation, recent messages, a push-to-talk message
button, transcript review, delivery state, and RGB receive feedback. While
recording, sliding upward into the visible cancel target discards the capture
without calling ASR. Pager has no close button; navigation back uses the system
left-edge swipe. Leaving the app cancels any active recording, disables the Peer
radio state machine, and restores normal Runtime BLE behavior.

## Pager push-to-talk bridge

The board records 16 kHz mono audio for as long as the Pager talk button is
held. It encodes samples as G.711 mu-law and streams them over a dedicated BLE
Notify characteristic while recording. The computer bridge restores 16-bit
PCM as packets arrive, so releasing the button no longer starts a slow BLE
hex-pull. It then calls Zhipu `glm-asr-2512` and returns at most 192 UTF-8 bytes
on the `pager.compose` Flow channel. Pager shows the transcript for explicit
Send, Retry, or Cancel; ASR never sends a peer message on its own.

Keep the Zhipu API key on the computer. It is neither copied into firmware nor
written to the SD card:

```bash
export ZHIPU_API_KEY=...
./scripts/pager_voice_bridge.sh --list-devices

# One bridge per board. Use a different CoreBluetooth UUID and cache file.
./scripts/pager_voice_bridge.sh \
  --address BOARD_A_COREBLUETOOTH_UUID \
  --cache ~/.vibeboard/pager-a.json

./scripts/pager_voice_bridge.sh \
  --address BOARD_B_COREBLUETOOTH_UUID \
  --cache ~/.vibeboard/pager-b.json
```

To keep both board bridges in one Terminal window, use the supervisor:

```bash
./scripts/pager_voice_bridges.sh \
  BOARD_A_COREBLUETOOTH_UUID \
  BOARD_B_COREBLUETOOTH_UUID
```

While the supervisor is active, the BLE App Store refuses Runtime BLE commands
so its browser polling cannot overwrite voice bridge responses. Close the voice
bridge terminal before using the BLE App Store; the serial App Store is not
affected.

Start both bridges before pressing either board's talk button. For an end-to-end
hardware check that does not call the paid ASR API, use `--mock-transcript`:

```bash
./scripts/pager_voice_bridge.sh \
  --address BOARD_A_COREBLUETOOTH_UUID \
  --cache ~/.vibeboard/pager-a.json \
  --mock-transcript "语音链路测试"
```

To measure the streaming path without pressing the screen or calling ASR:

```bash
./scripts/pager_voice_bridge.sh \
  --address BOARD_A_COREBLUETOOTH_UUID \
  --cache ~/.vibeboard/pager-a.json \
  --stream-test-seconds 20
```

The result must report `dropped=0`. `stream_bytes` is the mu-law payload and
`pcm_bytes` is the restored 16-bit PCM size.

The bridge reconnects with capped exponential backoff, saves WAV evidence under
`captures/pager/`, and logs transcript/Flow ACK metadata without API keys. The
board waits up to 120 seconds for `pager.compose`; `pager.asr.error` and local
timeouts return the talk button to a retryable state.
If the bridge was started after a capture was already completed, add
`--process-existing`; the default startup behavior clears stale audio.

Offline protocol checks:

```bash
python3 scripts/peer_protocol_test.py
python3 scripts/runtime_package.py --package-dir scripts/runtime_apps/pager
./scripts/pager_voice_bridge.sh --self-test
```

Two-board serial regression:

```bash
python3 scripts/peer_reliability.py \
  --port-a /dev/cu.usbserial-A \
  --port-b /dev/cu.usbserial-B \
  --pair --messages 100
```

Boundary-size and UTF-8 regression:

```bash
python3 scripts/peer_reliability.py \
  --port-a /dev/cu.usbserial-A \
  --port-b /dev/cu.usbserial-B \
  --pair --messages 0 --boundary
```

After Pager is installed on both boards, omit `--pair` to reset both UART
bridges, launch Pager, restore the saved pairing, reconnect, and exchange using
the existing identity:

```bash
python3 scripts/peer_reliability.py \
  --port-a /dev/cu.usbserial-A \
  --port-b /dev/cu.usbserial-B \
  --messages 2
```

For the long-run gate, use `--messages 500` and retain the UART output from both
boards. The test waits for receiver sequence advancement and sender ACK before
moving to the next message.
