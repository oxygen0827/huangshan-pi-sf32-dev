# Huangshan Pi AI UI Lab

This folder adapts the useful parts of `embedded-ui-ai-lab` for this
Huangshan Pi firmware repository.

The practical path for this board is:

```text
UI idea
  -> read lvgl-system-contract.md
  -> select shared Huangshan UI modules
  -> compose a built-in app or Runtime plan
  -> build and flash
  -> capture a photo/video or serial evidence
  -> review with visual-review-checklist.md
  -> iterate
```

## What Was Integrated

- A Huangshan-specific LVGL generation contract.
- A screenshot/photo review checklist for the 390x450 AMOLED + touch target.
- A route decision note explaining why LVGL stays the firmware UI path.
- A built-in `Huangshan UI Lab` app under `src/gui_apps/Huangshan_UI_Lab/`.
- A shared C library under `src/huangshan_ui/`, used by both built-in apps and
  Runtime Lua constructors.
- A module-driven AI plan example in `runtime-dashboard-plan.json`.

## Working Rule

AI-generated pages should not start by scattering colors, fonts, and absolute
coordinates through a page file. Start from the local theme/component layer,
extend it when a reusable pattern appears, then let screens compose those
components.

## Runtime Modules

The stable Runtime surface is `huangshan-ui/v1`:

- `vibe_ui_header(parent, title, subtitle)`
- `vibe_ui_metric(parent, label, value, x, y, status)`
- `vibe_ui_badge(parent, text, x, y, status)`
- `vibe_ui_progress(parent, label, value, x, y, status)`
- `vibe_ui_button(parent, label, x, y, role)`

AI callers should normally describe these through `ui.modules` and let
`scripts/runtime_app_plan_writer.py` place them. Direct calls remain available
for advanced Lua apps.
