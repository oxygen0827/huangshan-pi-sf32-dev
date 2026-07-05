# UI Route Decision

The `embedded-ui-ai-lab` project explores LVGL, Qt/QML, and Flutter routes.
For this firmware repository, only the LVGL route belongs in the board firmware
itself.

## Chosen Firmware Route: LVGL

Use LVGL for:

- Built-in board apps under `src/gui_apps/`.
- The launcher and board diagnostics.
- Runtime-rendered package UI.
- Sensor, BLE, storage, and board-control screens.

Why:

- The Huangshan Pi runs RT-Thread on SF32LB52 rather than embedded Linux.
- The display, touch, resource system, and flash flow are already verified
  through the SiFli watch/LVGL example.
- LVGL keeps RAM, flash, redraw, and touch behavior close to the hardware.

## Supporting Routes

Qt/QML can be useful later for desktop companion tools, manufacturing tools, or
an embedded Linux sibling product. It should not be linked into this MCU
firmware.

Flutter can be useful for phone or desktop companion apps. The current iOS BLE
package under `mobile/ios/` is the natural place to study that direction, not
the firmware app tree.

Web frontend references can still teach dashboard density, forms, and component
systems, but visual ideas must be translated into LVGL objects and MCU budgets.

## Repository Practice

- Keep route notes in `docs/ai-ui/`.
- Keep board-verified UI code in `src/gui_apps/` or `src/gui_apps/VibeBoard_Runtime/`.
- Keep phone/desktop companion code outside the firmware app tree.
- Record source repository licenses before reusing third-party code directly.
