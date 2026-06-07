# Huangshan Pi Bring-Up Notes

## Known Good Inputs

- SDK branch: `/Users/wq/sifli-sdk`, `release/v2.4`
- Example reference: `/Users/wq/lckfb-hspi-ulp_example`
- Board: `sf32lb52-lchspi-ulp`
- Serial port on this Mac: `/dev/cu.usbserial-110`
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

## Useful Commands

Build:

```bash
./scripts/build.sh
```

Flash:

```bash
./scripts/flash.sh /dev/cu.usbserial-110
```

Reset and monitor:

```bash
./scripts/monitor.sh /dev/cu.usbserial-110
```

## App Pattern

Each watch app lives under `src/gui_apps/<AppName>/` and has its own
`SConscript`. It is registered with:

```c
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(app_string_key), LV_EXT_IMG_GET(icon_name), APP_ID, app_main);
```

The launcher automatically scans `src/gui_apps` subdirectories that contain a
`SConscript`.
