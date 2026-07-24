# BLE Bulk Protocol v2

BLE bulk v2 accelerates Runtime package installation while preserving the
existing staging, manifest validation, rollback, and encrypted-link behavior.
It reuses the existing Runtime command characteristic, so the GATT database and
cached characteristic handles do not change.

## Negotiation and fallback

The board advertises `bulk=2` in the normal `ok status` response. The Python
transport uses v2 only when that capability is present and the BLE backend
reports enough Write Without Response capacity for a 24-byte header plus at
least 16 payload bytes. Otherwise it automatically uses the v1 hex command
protocol.

## File transfer

The host starts the existing transaction, then starts each file with:

```text
vb_runtime_install_begin <app>
vb_runtime_install_bulk <app> <path> <size> <transfer_id>
```

`transfer_id` is a non-zero random uint32. A zero-length file completes during
the bulk command and has no data frames.

Data frames are little endian and are sent to the existing command
characteristic with Write Without Response:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | Magic `VBK2` |
| 4 | 1 | Version `2` |
| 5 | 1 | Flags; bit 0 requests an ACK |
| 6 | 2 | Payload length, maximum 220 bytes |
| 8 | 4 | Transfer ID |
| 12 | 4 | Sequence number |
| 16 | 4 | File offset |
| 20 | 4 | Payload CRC32 |
| 24 | N | Raw payload |

The stable physical-board baseline sends one frame per window and requests one
ACK for every frame. Write Without Response only confirms that CoreBluetooth
accepted a frame; the Runtime ACK confirms that the board mailbox and SD worker
consumed it. The board responds with its next expected sequence and byte offset:

```text
ok install_bulk_data id=<id> seq=<frame> next=<sequence> offset=<bytes> rc=0
```

On a CRC, sequence, offset, or session mismatch, the ACK-requesting frame
returns `err install_bulk_data` with the same cumulative `next` and `offset`.
The host retries the frame. Already accepted frames are idempotent, so a lost
ACK cannot duplicate file data. If the link fails, the host reconnects,
reissues the same `vb_runtime_install_bulk` command, and resends the unconfirmed
frame. A duplicate bulk begin with the same app, path, size, and transfer ID
returns success at the board's current offset, including after the complete
file ACK was lost. A duplicate `vb_runtime_install_begin` for the same active
staging transaction is also a no-op; a different app remains a conflict.

The host does not reconnect at file boundaries. Reconnects are recovery for an
observed error, not routine flow control. A prior per-file reconnect strategy
amplified CCCD subscription races and was removed after the real notification
failure was fixed.

## Status notification reliability

The SiFli notification API can temporarily return zero when its finite TX packet
pool is empty. The board checks that return value and retries up to 20 times at
5 ms intervals; exhausting the budget emits `status notify dropped` to serial.
Each notification uses a local snapshot so another callback cannot overwrite a
queued install ACK.

The status CCCD callback only records and logs the subscription state. It does
not replace the shared response with `ok notify=1` or send an unsolicited
notification, because that response previously raced with the first Runtime
command after reconnect. The host still drains the legacy marker for backward
compatibility with older firmware.

## Storage and commit behavior

The GATT callback only copies a frame into the BLE worker mailbox. The worker
validates frames and coalesces payloads into a dynamically allocated 4 KiB
buffer before writing to the SD card. The final file buffer is flushed and
`fsync` completes before the final data ACK.

`vb_runtime_install_end` is rejected while a bulk file is incomplete. A normal
commit still validates every staged file's declared size and SHA-256 digest,
then performs the existing staging/backup rename transaction. Abort and staging
cleanup release the bulk buffer and remove partial files.
