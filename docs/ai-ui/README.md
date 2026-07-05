# Huangshan Pi AI UI Lab

This folder adapts the useful parts of `embedded-ui-ai-lab` for this
Huangshan Pi firmware repository.

The practical path for this board is:

```text
UI idea
  -> read lvgl-system-contract.md
  -> build or extend reusable LVGL components
  -> compose a built-in app or Runtime manifest
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

## Working Rule

AI-generated pages should not start by scattering colors, fonts, and absolute
coordinates through a page file. Start from the local theme/component layer,
extend it when a reusable pattern appears, then let screens compose those
components.
