# Firmware Release And Recovery

Normal pet deployment is a BLE Runtime App update. It never changes the
bootloader, flash table, or Runtime firmware. Firmware updates are a separate
USB-cabled recovery workflow until the dual-bank DFU bootloader has been
integrated and validated on production hardware.

## Release Contract

`scripts/firmware_release.py create` turns one tested build into a portable
release directory containing exactly the files listed in `sftool_param.json`:

- bootloader;
- Runtime firmware;
- flash table;
- a canonical `firmware-release.json` with the board, version, addresses,
  lengths, and SHA-256 of every image;
- an Ed25519 signature over that manifest.

The release verifier uses a pinned public key supplied by the release channel
or Companion build. It never trusts a public key included in the release being
verified. A release is rejected before it touches a board if its signature,
image hash, image length, flash address, or `sftool_param.json` layout differs.

Create a release only after build and real-board validation:

```sh
python scripts/firmware_release.py create \
  --build-dir project/build_sf32lb52-lchspi-ulp_hcpu \
  --output .local/firmware/v1.0.0 \
  --version 1.0.0 \
  --private-key /secure/release/firmware-private.pem \
  --public-key /secure/release/firmware-public.pem
```

The private key is release infrastructure, not a repository or Companion
asset. The corresponding public key must be pinned in the shipping Companion
before a release is offered to users.

## Update And Rollback

Verify first:

```sh
python scripts/firmware_release.py verify \
  --release .local/firmware/v1.0.0 \
  --public-key /secure/release/firmware-public.pem
```

With the board connected by a known data cable, perform a preflight:

```sh
python scripts/firmware_release.py apply \
  --release .local/firmware/v1.0.0 \
  --port /dev/cu.usbserial-XXXX \
  --public-key /secure/release/firmware-public.pem \
  --dry-run
```

Then run the same command with `--confirm UPDATE_FIRMWARE`. The tool delegates
to the existing retrying `flash.py` transport and requires a boot-log
confirmation. Only a successful, confirmed update is recorded as the local
last-good release.

Rollback is deliberately the same signed, verified operation with the previous
release directory as `--release`. This avoids a mutable "restore whatever was
on the board" image and guarantees that bootloader, flash table, and Runtime
come from one compatible release.

## Current Boundary

The Companion status API exposes `firmware.updateMode=verified_usb_recovery`
and `firmware.wirelessDfu=false`. Do not represent a BLE pet install as a
firmware update. The SiFli SDK dual-image DFU examples are not enabled by this
repository's current flash table; enabling them changes boot partitions and
requires factory-image migration, power-loss tests during both image swap
directions, and physical rollback verification. It remains a separate release
gate, not a hidden runtime switch.

## Companion Update Manager

The Mac Companion exposes the signed update path through `/v1/firmware/status`,
`/v1/firmware/available`, `/v1/firmware/update`, and `/v1/firmware/rollback`.
The manager downloads only HTTPS feed entries, enforces a pinned public key,
checks the archive digest, rejects archive traversal and symlink members before
extraction, then delegates to `firmware_release.py`. Initial feed/release URLs
and every redirect must be credential-free standard HTTPS URLs. Exactly one
update or rollback may own the flash operation at a time; a concurrent request
fails before it can race the board or overwrite `last-attempt.json`. A release is written to
`last-success.json` only after the board boot log and Runtime health check pass.
If health fails, the previous signed release is restored automatically. The UI
must keep update and rollback visibly separate from normal pet deployment.

## Ping-Pong DFU Migration Gate

`scripts/dual_bank_dfu.py` defines the proposed 4 MiB A/B geometry and signs a
layout package, but its `enabled` field is permanently `false` until the
bootloader and ptab are migrated. `self-test` covers contiguous slot geometry,
interrupted transfer preservation of the active image, and candidate hash
corruption. Do not flip this flag from the Companion or Runtime: enabling it
requires a factory-image migration, power-loss tests during both swap
directions, and a physical rollback on production hardware.

## One-Click Support Bundle

`POST /v1/support/bundle` creates a per-job ZIP containing redacted board,
Companion, firmware, recent-job, and bounded log-tail data. Credentials,
tokens, Basic/Bearer authorization values, cookies, private keys, passwords,
and hook commands are removed. The job inspection and download endpoints both
require a current Companion session token. The download endpoint resolves the
exact filename recorded by that job and rejects paths outside the support
directory, so concurrent diagnostic requests cannot return another user's
bundle.
