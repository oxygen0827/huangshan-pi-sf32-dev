# Official Watch Examples Test Notes

Date: 2026-06-09

This note records the first pass over the LCKFB/SiFli official examples for the
Huangshan Pi board. Its purpose is to keep official example behavior separate
from our own Huangshan Pi apps.

## Source Boundary

Official watch example code, tracked in the local LCKFB example repository:

- `src/gui_apps/LC_Hello_World/`
- `src/gui_apps/rotation3d/`
- `src/gui_apps/clock/`
- `src/gui_apps/mem/`
- `src/gui_apps/utils/`
- `src/gui_apps/watch_demo.c`

Our application code or modified application surface:

- `src/gui_apps/Board_Diagnostics/`
- `src/gui_apps/Codex_Test/`
- `src/gui_apps/main/app_mainmenu.c` current `Huangshan Home` launcher

`src/gui_apps/mem/` is not an app page. It provides cache and animation memory
helpers used by the official graphics examples and transition animation code.

## Tested Built-In App IDs

These official watch example apps are registered through `BUILTIN_APP_EXPORT`
and can be launched through the app framework:

| App ID | Directory | Purpose |
| --- | --- | --- |
| `hello_world` | `src/gui_apps/LC_Hello_World/` | Minimal LVGL page and app lifecycle |
| `rotation3d` | `src/gui_apps/rotation3d/` | GPU/LVGL timer animation example |
| `clock` | `src/gui_apps/clock/` | Multi-watch-face clock example |

## Verification Commands

Build:

```bash
./scripts/build.sh
```

Result: exit code 0. The build target was `sf32lb52-lchspi-ulp_hcpu`.

Flash:

```bash
./scripts/flash.sh /dev/cu.usbserial-13220
```

Result: exit code 0 after running with serial-device permission.

Board launch loop:

```text
app_run hello_world
app_run Main
app_run rotation3d
app_run Main
app_run clock
app_run Main
```

The loop was run through the serial shell at 1000000 baud after an RTS reset.

## Board Evidence

Startup evidence:

- Bootloader printed `SFBL`.
- LCD driver found `co5300 id:331100h`.
- Touch driver found `ft6146`.
- LCD state reached `[INITIALIZED] -> [ON]`.
- Brightness was set to `100`.
- Launcher printed `[Huangshan_Home] start`.

`hello_world` evidence:

- `send msg[GUI_APP_MSG_RUN_APP] [hello_world]`
- `app[hello_world] do LOAD`
- `finding hello_world in builtin apps...`
- `page[hello_world][root] do ONSTART`
- `page[hello_world][root] do ONRESUME`
- Returned to `Main` and then `page[hello_world][root] do ONSTOP`.

`rotation3d` evidence:

- `send msg[GUI_APP_MSG_RUN_APP] [rotation3d]`
- `app[rotation3d] do LOAD`
- `finding rotation3d in builtin apps...`
- `page[rotation3d][root] do ONSTART`
- `last_dir=0 cur=1 -400,-560,303`
- `page[rotation3d][root] do ONRESUME`
- Returned to `Main` and then `app[rotation3d] do DESTROY`.

`clock` evidence:

- `send msg[GUI_APP_MSG_RUN_APP] [clock]`
- `app[clock] do LOAD`
- `finding clock in builtin apps...`
- `page[clock][root] do ONSTART`
- `service_reset_time:  122:6:1 - 9:0:22 - 5`
- `STATE_PAUSED: clock[simple] init`
- `STATE_ACTIVE: clock[rotate_b] init`
- `STATE_ACTIVE: clock[rotate_b] resume`
- Returned to `Main`, deinitialized active clock faces, and destroyed the app.

No reset, hard fault, LCD off message, or app lookup failure appeared in this
test pass.

## What We Learned

### App Registration Pattern

Each app uses the same framework pattern:

```c
#define APP_ID "example_id"

static int app_main(intent_t i)
{
    gui_app_regist_msg_handler(APP_ID, msg_handler);
    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(name), LV_EXT_IMG_GET(icon), APP_ID, app_main);
```

The shell or launcher starts an app with:

```c
gui_app_run("example_id");
```

The exported app ID is the real routing key. Directory names and display names
are secondary.

### Lifecycle Pattern

The app framework sends these page messages:

- `GUI_APP_MSG_ONSTART`: create root LVGL objects and initialize app state.
- `GUI_APP_MSG_ONRESUME`: start timers, animations, or active state.
- `GUI_APP_MSG_ONPAUSE`: stop timers and release temporary resources.
- `GUI_APP_MSG_ONSTOP`: delete LVGL objects and free persistent allocations.

The `rotation3d` example is the clearest timer rule: it creates LVGL timers in
`on_resume` and deletes them in `on_pause`.

The `clock` example is the clearest resource rule: it allocates the clock
manager on start, changes watch-face states on resume/pause, and frees the list
on stop.

### Memory And Animation

Transition animations allocate two buffers of 351000 bytes:

```text
app_anim_buf_alloc: ... index 0 size 351000
app_anim_buf_alloc: ... index 1 size 351000
```

They are freed after transition completion:

```text
app_anim_buf_free: ...
```

This means new apps should avoid large extra allocations during app transitions.
Start heavy timers or GPU work in `ONRESUME`, not before the page transition has
settled, unless there is a concrete reason.

### Hardware Boundary

The official examples do not directly reinitialize LCD or touch. They depend on
the board and framework startup sequence:

```text
LCD driver -> framebuffer -> touch -> LVGL -> app framework -> Main
```

This is the model our apps should follow. App code should not drive random GPIOs
unless the board mapping is confirmed. The Diagnostics black-screen fix came
from this rule: app-level control of `GPIO26/LED1` was removed.

## Current Issues Or Follow-Up

- The official `clock` example uses RTOS time and printed a year value of `122`
  from `struct tm`. That is normal C `tm_year` format, not a user-facing year.
- The serial test proves app lifecycle and no crash. Visual quality still needs
  human confirmation for animation smoothness and layout appearance.
- The broader LCKFB example repository also contains ADC, GPIO, UART, I2C
  charger, sensor, WS2812, and LVGL demo projects. Those are separate firmware
  projects, not built into this watch app.

## Independent Official Example Projects

These examples live under `/Users/hushaohong/vibe-coding/huangshan-pi-workspace/lckfb-hspi-ulp_example`
and each has its own `project/` directory. They are separate firmware images;
testing one replaces the firmware currently running on the board.

Build command used in each `project/` directory:

```bash
source /Users/hushaohong/vibe-coding/huangshan-pi-workspace/sifli-sdk/export.sh
scons --board=sf32lb52-lchspi-ulp -j8
```

Build results:

| Project | `main.bin` size | Build result | Board run result |
| --- | ---: | --- | --- |
| `adc/project` | 307004 bytes | Pass | Pass |
| `gpio/project` | 251608 bytes | Pass | Pass |
| `uart/project` | 304444 bytes | Pass | Not fully tested, requires external USB-TTL on UART2 |
| `I2C/charger/project` | 311092 bytes | Pass | Not flashed, writes charger current register |
| `RT-Device/sensor/project` | 325304 bytes | Pass | Pass |
| `ws2812/project` | 305960 bytes | Pass | Not fully tested, requires external WS2812 LED |
| `lvgl/lvgl_v8_demos/project` | 761400 bytes | Pass | Serial init pass, visual check needed |
| `lvgl/lvgl_v9_demos/project` | 882836 bytes | Pass | Serial/sysmon pass, visual check needed |

### ADC Project

Board result: pass.

Evidence:

- `Start adc demo!`
- Periodic `VBAT read value: ...`
- Periodic `ADC read value: ...`
- Repeated `ADC example end`

Observed values included VBAT raw values around `43104` to `43674` and PA34 ADC
values around `777` to `893` mV in this run. These numbers depend on power,
battery, and key state, so future tests should check that values are plausible
and changing, not fixed to these exact numbers.

Learning points:

- ADC1 is exposed as RT-Thread device `bat1`.
- The example uses channel 7 for VBAT and channel 6 for PA34.
- `rt_adc_read` and `rt_device_control(... RT_ADC_CMD_READ ...)` are both shown.
- PA34 is also involved in board key/reset behavior, so product apps should not
  casually repurpose it.

### GPIO Project

Board result: pass.

Evidence:

- `GPIO example`
- `PIN 20 state: 1`
- `PIN 20 state: 0`
- `GPIO example end`
- `Waiting`
- `KEY2 pressed`
- `KEY2 released`

Learning points:

- GPIO pin numbers map directly to PA numbers for this chip family.
- PA20 is used as a simple output example.
- KEY2 is PA43 and can be handled with `rt_pin_attach_irq`.
- The key interrupt callback should only do small work and log/read state.

### LVGL v8 Demos Project

Board result: serial initialization pass; visual check still needed.

Evidence:

- `Found lcd co5300 id:331100h`
- `touch screen found driver ..., ft6146`
- `[littlevgl2rtt] Welcome to the littlevgl2rtt lib.`
- `display on`
- No crash or reset in the capture window.

Learning points:

- This project does not use the watch app framework.
- It calls `littlevgl2rtt_init("lcd")`, starts a selected `lv_demo_main()`, and
  then runs the LVGL task loop directly.
- Demo choice is compile-time/menuconfig controlled, for example benchmark,
  widgets, keypad encoder, music, or stress.

### LVGL v9 Demos Project

Board result: serial/sysmon pass; visual check still needed.

Evidence:

- `Found lcd co5300 id:331100h`
- `touch screen found driver ..., ft6146`
- `[littlevgl2rtt] Welcome to the littlevgl2rtt lib.`
- `display on`
- Repeated `sysmon` output with FPS around `39` to `62` in the capture window.

Learning points:

- LVGL v9 uses the newer LVGL API surface, for example `lv_screen_active`.
- Sysmon output is useful as a quick performance signal.
- The v9 demo is a good reference for benchmark/performance reporting, but it is
  heavier than the watch app examples.

### RT-Device Sensor Project

Board result: pass.

Evidence:

- `Find i2c bus device i2c3`
- `LTR303_MEAS_RATE Reg[0] = 3`
- `light sensor init success`
- `MMC56x3 ID = 16`
- `mag sensor init success`
- `sensor.st.lsm6dsl sensor init success`
- `acce set odr 1660`
- `gyro set odr 1660`
- Sample data included `light: 12 lux`, `mag, x: -389, y: 138, z: -740`,
  `acce, x: 0, y: 20, z: -1009`, and `gyro, x: -1890, y: -4200, z: 3150`.
- `lsm6d step, step: 0` while the board was static.

Learning points:

- The example uses I2C3 on PA39 / PA40.
- LTR303, MMC56X3, and LSM6DSL all initialize and produce periodic readings.
- The step counter path is readable; a separate motion test is needed to verify
  step increments.

### Projects Not Fully Run In This Pass

`uart/project`:

- Builds successfully.
- Requires external USB-TTL connected to UART2.
- README maps UART2 to PA19 TX and PA18 RX.
- The board debug console is separate from UART2, so a full test needs two
  serial paths: debug console and UART2 adapter.

`I2C/charger/project`:

- Builds successfully.
- Uses `i2c2` and AW32001 charger address.
- The example scans I2C, reads chip ID, and repeatedly writes the charge current
  register.
- It was not flashed in this pass because changing charger current is a hardware
  side effect and should be done intentionally.

`ws2812/project`:

- Builds successfully.
- Requires an external WS2812 LED connection for a meaningful functional test.
- The serial log alone can show the color loop, but not whether the LED timing
  and wiring are correct.

## Board Restore

After testing independent examples, the board was flashed back to this
repository's firmware:

```bash
./scripts/flash.sh /dev/cu.usbserial-13220
```

Restore evidence:

- `Found lcd co5300 id:331100h`
- `touch screen found driver ..., ft6146`
- `display on`
- `[Huangshan_Home] start`

## Recommended Rule For Future AI Work

When adding a new Huangshan Pi app:

1. Copy the registration and lifecycle shape from `hello_world`.
2. Copy timer cleanup discipline from `rotation3d`.
3. Copy multi-module state management from `clock` only when the app really has
   multiple subviews or modes.
4. Keep board facts in BSP/docs/Diagnostics, not inside product apps.
5. Verify with build, flash, serial `app_run <id>`, and human screen check.
