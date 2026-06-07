# Huangshan Watch App

Independent LVGL watch application project for the LCKFB Huangshan Pi board.

This project is based on the verified `lvgl/watch` flow from:

- Official SiFli SDK: `/Users/wq/sifli-sdk`
- LCKFB examples: `/Users/wq/lckfb-hspi-ulp_example`

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
src/gui_apps/            GUI app modules registered into the watch launcher
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

Keep official SDK and LCKFB examples as references. Put new application code in
this project unless a board-driver fix must be made in the SDK.
