# Huangshan Pi SF32 Development Base

Independent development project for the LCKFB Huangshan Pi / 立创黄山派
SF32LB52x board.

The current verified runtime starts from the LCKFB `lvgl/watch` example because
that path proves the CO5300 AMOLED panel, FT6146 touch, launcher, resources, and
LVGL integration on this board. The repository is not limited to watch products:
the watch UI is one development outlet for this hardware, alongside other GUI,
sensor, audio, storage, USB, or board-control applications.

Upstream references:

- Official SiFli SDK: https://gitee.com/SiFli/sifli-sdk (`release/v2.4`)
  - Local path: `/Users/wq/sifli-sdk`
- LCKFB Huangshan Pi examples: https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
  - Local path: `/Users/wq/lckfb-hspi-ulp_example`

The board target is:

```text
sf32lb52-lchspi-ulp
```

## Verified Board

- Board: LCKFB Huangshan Pi / 立创黄山派
- MCU module: SF32LB52x-MOD-1-N16R8
- LCD: CO5300 AMOLED, 390x450, QADSPI
- Touch: FT6146
- Serial: CH340 USB UART
- macOS port used during bring-up: `/dev/cu.usbserial-110`

## Project Layout

```text
project/                 SCons project files
src/gui_apps/            GUI app modules registered into the current launcher
src/gui_apps/Codex_Test/ First verified custom app
src/resource/images/     Image assets converted by SiFli resource tools
src/resource/strings/    Multilingual string resources
scripts/                 Local build, flash, and monitor helpers
docs/                    Board and development notes
```

## Build

```bash
./scripts/build.sh
```

Equivalent manual command:

```bash
cd project
source /Users/wq/sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

## Flash

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

If no port is passed, the script uses `/dev/cu.usbserial-110`.

## Monitor And Reset

```bash
./scripts/monitor.sh /dev/cu.usbserial-110
```

The monitor toggles RTS to reset the board, then captures boot logs at `1000000`
baud.

## Current Demo App

`src/gui_apps/Codex_Test` is the first custom app in this project. It is
registered into the honeycomb launcher as `Codex测试 / Codex Test`.

It displays:

- Four color blocks for display validation
- `390x450` board label
- LVGL timer count
- Touch count
- A `Back` button that returns to `Main`

## Important SDK Patch

This board only worked reliably after patching the SDK CO5300 driver in:

```text
/Users/wq/sifli-sdk/customer/peripherals/co5300/co5300.c
```

Required behavior:

- Accept CO5300 read IDs `0x331100`, `0x1fff`, and `0x3fff`
- Disable LCDC TE sync for this panel path with `HAL_LCDC_SYNC_DISABLE`

Details are in `docs/board-bringup.md`.

## Development Rule

Treat this repository as the board-level application workspace. Keep official
SDK and LCKFB examples as upstream references, and put new product/application
code here unless a board-driver fix must be made in the SDK.

The full official SDK is not vendored into this repository. The verified LCKFB
`lvgl/watch` application structure is copied in as the current working base.
See `docs/upstream.md` for source repositories and dependency boundaries.
