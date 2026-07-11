# Huangshan Pi Bring-Up Notes

## Known Good Inputs

- Official SDK: https://gitee.com/SiFli/sifli-sdk, branch `main`, verified SDK 2.5.0 build `cbac8e56`
- SDK local path: `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk`
- LCKFB example repository: https://github.com/OpenSiFli/lckfb-hspi-ulp_example.git
- LCKFB example local path: `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/lckfb-hspi-ulp_example`
- Board: `sf32lb52-lchspi-ulp`
- Serial port on this Mac: `/dev/cu.usbserial-13220`
- Baud rate for logs: `1000000`

## What Was Verified

The official `watch` flow initializes:

```text
CO5300_ReadID 0x331100
Found lcd co5300 id:331100h
display on
[littlevgl2rtt] Welcome to the littlevgl2rtt lib.
touch screen found driver ... ft6146
```

The custom `Codex_Test` app was built, flashed, launched from the honeycomb
menu, and produced:

```text
send msg[GUI_APP_MSG_RUN_APP] [codex_test]
finding codex_test in builtin apps...
app[codex_test] do START
[Codex_Test] registered
[Codex_Test] start
[Codex_Test] resume
[Codex_Test] touch count=1
```

## SDK CO5300 Patch

The stock SDK driver can reject this panel or time out depending on the read ID
and TE behavior. The local SDK currently has these working changes:

```c
.syn_mode = HAL_LCDC_SYNC_DISABLE,
```

and:

```c
if ((data == LCD_ID) || (data == 0x1fff) || (data == 0x3fff))
{
    DEBUG_PRINTF("LCD module use CO5300 IC \n");
    return LCD_ID;
}
return data;
```

Without these changes, observed failures included:

```text
Try lcd co5300, read id:1fffh, expect:331100h
unknow lcd!
```

and:

```text
draw_core timeout
LCDC STATUS=1,TE=3
[INITIALIZED] -> [TIMEOUT]
```


## Jumper And Power-Domain Note

The LCKFB pinout sheet marks pins 5-6, 7-8, and 11-12 as jumper-cap shorts.
Treat these as required board power-domain jumpers unless the schematic for a
specific experiment says otherwise. Removing them can leave only part of the
board powered: the USB-UART or indicator LED may still light, while the SF32
MCU, display, SD card, or 3V3/VBAT domain is not properly powered. That state
can look like a dead board even when USB enumeration still works.

For normal Runtime development:

- Keep the required jumper caps installed before plugging in USB.
- If the green LED lights but the screen stays black and flashing cannot enter
  download mode, first inspect these jumper caps and the USB data cable.
- Without a multimeter, the safest recovery path is: unplug USB, restore jumper
  caps, use a known data cable, list ports with `./scripts/flash.sh --list-ports`,
  then flash again.

## Current Runtime UI Baseline

The current board UI no longer uses the old honeycomb launcher as the daily
Runtime entry. The `Main` screen scans `/sdcard/apps` and displays installed
Runtime apps as safe-area cards. Cards are scrollable when there are more apps
than fit on one screen, and tapping a card launches that app through
`VibeBoard_Runtime`. App install/delete/refresh management lives in the local
Web/iOS/desktop manager, not in a separate board-side App Manager page.

The display has rounded physical edges, so all important text and touch targets
must stay inside the safe area used by `HUANGSHAN_HOME_SAFE_*` or
`VB_SCREEN_SAFE_*` constants.

## Useful Commands

Build:

```bash
./scripts/build.sh
```

Flash:

```bash
./scripts/flash.sh /dev/cu.usbserial-13220
```

Reset and monitor:

```bash
./scripts/monitor.sh /dev/cu.usbserial-13220
```

## App Pattern

The currently copied LCKFB UI shell uses the watch launcher pattern. Each app
lives under `src/gui_apps/<AppName>/` and has its own
`SConscript`. It is registered with:

```c
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(app_string_key), LV_EXT_IMG_GET(icon_name), APP_ID, app_main);
```

The launcher automatically scans `src/gui_apps` subdirectories that contain a
`SConscript`.

This pattern is a proven starting point, not a product constraint. Future work
can keep this launcher, replace it with another LVGL shell, or build non-watch
board demos while preserving the same board target and SDK bring-up facts.
