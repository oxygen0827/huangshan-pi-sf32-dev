# Huangshan Pi LVGL AI System Contract

Use this contract when asking AI to generate or modify LVGL UI code for this
repository.

## Target

- Board: LCKFB Huangshan Pi / `sf32lb52-lchspi-ulp`.
- Display: CO5300 AMOLED, 390x450 portrait.
- Touch: FT6146.
- GUI stack: SiFli SDK `main` / verified SDK 2.5.0, RT-Thread, LVGL 8.
- Color/resource assumptions: RGB565 target hardware, constrained MCU memory,
  existing SiFli resource system.

## Code Rules

- Generate C code for LVGL 8 APIs. Do not use C++.
- Keep page lifecycle compatible with `gui_app_fwk`:
  `GUI_APP_MSG_ONSTART`, `ONRESUME`, `ONPAUSE`, `ONSTOP`.
- Register built-in apps through the existing `BUILTIN_APP_EXPORT` pattern.
- Use `LV_HOR_RES_MAX` and `LV_VER_RES_MAX` where practical, while designing
  for 390x450 portrait.
- Prefer reusable component helpers and a theme file over one-off styling.
- Reuse `src/huangshan_ui/` first. Use an app-specific prefix only for a
  component that is genuinely local to one app.
- Separate page creation from dynamic data updates.
- Delete timers and root objects in `ONSTOP`.
- Use `rt_kprintf` for lifecycle and important interaction evidence.

## Layout Rules

- Touch targets should be at least 44x44 px.
- Use LVGL flex/grid when it keeps layout simpler.
- Absolute positioning is acceptable for tightly bounded 390x450 board demos,
  but keep values centralized and readable.
- Reserve the current rounded-screen safe margins: use the project safe-area constants instead of placing critical text/buttons in the four corners.
- Keep text short enough for English and Chinese labels.
- Do not add a tiny top-right Home button as the only navigation path; K1 is the primary return-home affordance, and left-edge swipe can be a secondary gesture.
- Lists and app grids must scroll when content exceeds one screen; do not hard-code an eight-item limit.

## Visual Rules

- Use dark AMOLED-friendly backgrounds with high-contrast text.
- Keep color semantics stable:
  - normal/ready: cyan or green
  - warning: amber
  - danger/error: red
  - secondary text: muted blue-gray
- Avoid blur, glass effects, large transparent layers, heavy shadows, and large
  unbudgeted bitmap assets.
- Limit animation to small, purposeful changes unless performance is measured.

## Runtime App Rules

For `VibeBoard_Runtime` packages:

- Treat `manifest.json` plus `uiKit: huangshan-ui/v1` as the stable UI contract.
- Prefer `ui.modules` in the App Plan Writer for headers, metrics, badges,
  progress and buttons. Its layout budget protects the 390x450 safe area.
- Runtime Lua may hold at most 96 tracked LVGL handles and at most 8
  `vibe_ui_*` high-level components per app.
- The Runtime uses a full, memory/instruction-limited Lua VM. Lua language
  features are available, but board access still goes through the documented
  LVGL and `vibe_*` APIs.
- Prefer existing capability bindings before asking for firmware changes.
- Firmware changes are still required for new LVGL/widget bindings, networking
  primitives, button actions, or hardware APIs.

## Review Loop

Build and inspect on target whenever possible:

```bash
./scripts/build.sh
./scripts/flash.sh --list-ports
./scripts/flash.sh /dev/cu.usbserial-13220
./scripts/monitor.sh /dev/cu.usbserial-13220
```

Then review a photo/video of the screen with `visual-review-checklist.md`.
