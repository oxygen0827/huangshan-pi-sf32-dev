# Board Diagnostics App Design

## Goal

Build the first useful Huangshan Pi board app on top of the existing verified
watch launcher flow.

The app should run on the `sf32lb52-lchspi-ulp` target and use the verified
CO5300 AMOLED + FT6146 touch + LVGL path. It should make board bring-up easier
by showing live display, touch, LVGL timer, and basic runtime status directly on
the 390x450 screen.

## Scope

Implement a new built-in app named:

```text
Board Diagnostics
```

The app lives under:

```text
src/gui_apps/Board_Diagnostics/
```

It is launched from the existing honeycomb main menu using the current
`BUILTIN_APP_EXPORT` pattern. The current `Main` launcher remains unchanged for
this slice.

## Non-Goals

- Do not replace the main launcher yet.
- Do not modify the external SiFli SDK.
- Do not add BLE, audio, storage, or sensor integrations in this slice.
- Do not depend on PC simulator-only behavior.

## Screen Model

The physical display is treated as:

```text
390 x 450
```

The implementation should use `LV_HOR_RES_MAX` and `LV_VER_RES_MAX` where
practical, while laying out for the verified portrait display.

The screen uses a compact diagnostic layout:

- Top area: title, target board label, uptime.
- Main metrics: touch count, last touch coordinate, LVGL tick count, frame or
  refresh indicator.
- Visual checks: red, green, blue, white color blocks.
- Bottom area: hint text and a back button.

## Behavior

On app start:

- Create a full-screen root object.
- Set a dark background to make AMOLED rendering and color blocks obvious.
- Start a 1-second LVGL timer.
- Log lifecycle events with `rt_kprintf`.

On timer tick:

- Increment a visible tick counter.
- Update uptime text.

On touch/click:

- Increment touch count.
- Record and display the latest touch coordinate if available.
- Print the touch count and coordinate to the serial log.

On back button:

- Run `gui_app_run("Main")`.

On stop:

- Delete the LVGL timer.
- Delete the root object.
- Clear stored pointers.

## App Registration

Add localized strings:

```text
board_diagnostics
```

English:

```text
Board Diagnostics
```

Chinese:

```text
板级诊断
```

Register the app with the existing built-in app export mechanism. Reuse an
existing icon for this slice unless a dedicated icon is already available in the
resource set.

## Implementation Boundaries

Keep diagnostics-specific state local to the app module. Avoid touching the
launcher, shared resource generation, or SDK board files unless the build proves
the existing app registration path requires a minimal resource/string change.

The app can reuse simple helper functions from `Codex_Test` only by copying the
small local UI helpers into the new module. Do not introduce a shared UI helper
abstraction in this slice unless two or more production apps need it.

## Verification

Required local verification:

```bash
./scripts/build.sh
```

Required board verification when the device is attached:

```bash
./scripts/flash.sh /dev/cu.usbserial-13220
./scripts/monitor.sh /dev/cu.usbserial-13220
```

Expected serial signals:

```text
[Board_Diagnostics] registered
[Board_Diagnostics] start
[Board_Diagnostics] resume
```

Touching the screen should print a touch log line and update the visible touch
counter.

## Follow-Up Path

After this app is verified on hardware:

1. Add external input and signal monitoring in the same app or a sibling
   `Signal Monitor` app.
2. Add more board demo apps as each hardware area is validated.
3. Replace the honeycomb launcher with a Huangshan Pi home screen only after
   there are several stable demo entries to organize.
